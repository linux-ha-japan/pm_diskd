/*
 *	Stonith module for WTI Remote Power Controllers (RPS-10M device)
 *
 *      Original code from baytech.c by
 *	Copyright (c) 2000 Alan Robertson <alanr@unix.sh>
 *
 *      Modifications for WTI RPS10
 *	Copyright (c) 2000 Computer Generation Incorporated
 *               Eric Z. Ayers <eric.ayers@compgen.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <libintl.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stonith/expect.h>
#include <stonith/stonith.h>

#define	DEVICE	"WTI RPS10 Power Switch"

#define N_(text)	(text)
#define _(text)		dgettext(ST_TEXTDOMAIN, text)

/*
 *	This was written for a Western Telematic Inc. (WTI) 
 *      Remote Power Switch - RPS-10M. 
 *
 *      It has a DB9 serial port, a Rotary Address Switch,
 *      and a pair of RJ-11 jacks for linking multiple switches 
 *      together.  The 'M' unit is a master unit which can control 
 *      up to 9 additional slave units. (the master unit also has an
 *      A/C outlet, so you can control up to 10 devices)
 *
 *      There are a set of dip switches. The default shipping configuration
 *      is with all dip switches down. I highly recommend that you flip
 *      switch #3 up, so that when the device is plugged in, the power 
 *      to the unit comes on.
 *
 *      The serial interface is fixed at 9600 BPS (well, you *CAN* 
 *        select 2400 BPS with a dip switch, but why?) 8-N-1
 *
 *      The ASCII command string is:
 *
 *      ^B^X^X^B^X^Xac^M
 *      
 *      ^B^X^X^B^X^X  "fixed password" prefix (CTRL-B CTRL-X ... )
 *      ^M            the carriage return character
 *     
 *      a = 0-9  Indicates the address of the module to receive the command
 *      a = *    Sends the command to all modules
 *
 *      c = 0    Switch the AC outlet OFF
 *               Returns:
 *                         Plug 0 Off
 *                         Complete
 *
 *      c = 1    Switch the AC outlet ON
 *               Returns:
 *                        Plug 0 On
 *                        Complete
 *
 *      c = T    Toggle AC OFF (delay) then back ON
 *               Returns:
 *                         Plug 0 Off
 *                         Plug 0 On
 *                         Complete
 *
 *      c = ?    Read and display status of the selected module
 *               Returns:
 *                        Plug 0 On   # or Plug 0 Off
 *                        Complete
 *
 *   e.g. ^B^X^X^B^X^X0T^M toggles the power on plug 0 OFF and then ON
 * 
 *   21 September 2000
 *   Eric Z. Ayers
 *   Computer Generation, Inc.
 */

struct cntrlr_str {
  int outletnum;		/* value 0-9 */
  int configured;	/* 1 == this outlet (plug) is configured */
  char * node;          /* name of the node attached to this outlet */
};

struct WTI_RPS10 {
  const char *	WTIid;

  char *	idinfo;  /* ??? What's this for Alan ??? */
  char *	unitid;  /* ??? What's this for Alan ??? */

  int		fd;      /* FD open to the serial port */

  int		config;  /* 0 if not configured, 
                            1 if configured with st_setconffile() 
                                   or st_setconfinfo()
                          */
  char *	device;  /* Serial device name to use to communicate 
                            to this RPS10
			  */

#define WTI_NUM_CONTROLLERS	10
  struct cntrlr_str 
                controllers[WTI_NUM_CONTROLLERS];
  		/* one master switch can address 10 controllers */

};

/* This string is used to identify this type of object in the config file */
static const char * WTIid = "WTI_RPS10";
static const char * NOTwtiid = "OBJECT DESTROYED: (WTI RPS-10)";

/* WTIpassword - The fixed string ^B^X^X^B^X^X */
static const char WTIpassword[7] = {2,24,24,2,24,24,0}; 

#ifndef DEBUG
#define DEBUG 0
#endif
static int gbl_debug = DEBUG;

#define	ISWTIRPS10(i)	(((i)!= NULL && (i)->pinfo != NULL)	\
	&& ((struct WTI_RPS10 *)(i->pinfo))->WTIid == WTIid)

#define	ISCONFIGED(i)	(ISWTIRPS10(i) && ((struct WTI_RPS10 *)(i->pinfo))->config)

#ifndef MALLOC
#	define	MALLOC	malloc
#endif
#ifndef FREE
#	define	FREE	free
#endif
#ifndef MALLOCT
#	define     MALLOCT(t)      ((t *)(MALLOC(sizeof(t)))) 
#endif

#define DIMOF(a)	(sizeof(a)/sizeof(a[0]))
#define WHITESPACE	" \t\n\r\f"

#define	REPLSTR(s,v)	{					\
			if ((s) != NULL) {			\
				FREE(s);			\
				(s)=NULL;			\
			}					\
			(s) = MALLOC(strlen(v)+1);		\
			if ((s) == NULL) {			\
				syslog(LOG_ERR, _("out of memory"));\
			}else{					\
				strcpy((s),(v));		\
			}					\
			}

/*
 *	Different expect strings that we get from the WTI_RPS10
 *	Remote Power Controllers...
 */

static struct Etoken WTItokReady[] =	{ {"RPS-10 Ready", 0, 0}, {NULL,0,0}};
static struct Etoken WTItokComplete[] =	{ {"Complete", 0, 0} ,{NULL,0,0}};
static struct Etoken WTItokPlug[] =	{ {"Plug", 0, 0}, {NULL,0,0}};
static struct Etoken WTItokOutlet[] =	{ {"0", 0, 0}, 
					  {"1", 0, 0}, 
					  {"2", 0, 0}, 
					  {"3", 0, 0}, 
					  {"4", 0, 0}, 
					  {"5", 0, 0}, 
					  {"6", 0, 0}, 
					  {"7", 0, 0}, 
					  {"8", 0, 0}, 
					  {"9", 0, 0}, 
					  {NULL,0,0}};

static struct Etoken WTItokOn[] =	{ {"On", 0, 0}, {NULL,0,0}};
static struct Etoken WTItokOff[] =	{ {"Off", 0, 0}, {NULL,0,0}};

/* Accept either a CR/NL or an NL/CR */
static struct Etoken WTItokCRNL[] =	{ {"\n\r",0,0},{"\r\n",0,0},{NULL,0,0}};

static int	RPSConnect(struct WTI_RPS10 * ctx);
static int	RPSDisconnect(struct WTI_RPS10 * ctx);

static int	RPSReset(struct WTI_RPS10*, int unitnum, const char * rebootid);
#if defined(ST_POWERON) 
static int	RPSOn(struct WTI_RPS10*, int unitnum, const char * rebootid);
#endif
#if defined(ST_POWEROFF) 
static int	RPSOff(struct WTI_RPS10*, int unitnum, const char * rebootid);
#endif
static int	RPSNametoOutlet ( struct WTI_RPS10 * ctx, const char * host );

int  	st_setconffile(Stonith *, const char * cfgname);
int	st_setconfinfo(Stonith *, const char * info);
static int RPS_parse_config_info(struct WTI_RPS10* ctx, const char * info);
const char *
		st_getinfo(Stonith * s, int InfoType);

char **	st_hostlist(Stonith  *);
void	st_freehostlist(char **);
int	st_status(Stonith * );
int	st_reset(Stonith * s, int request, const char * host);
void	st_destroy(Stonith *);
void *	st_new(void);

/*
 *	We do these things a lot.  Here are a few shorthand macros.
 */

#define	SEND(outlet, cmd, timeout)		{                       \
	int return_val = RPSSendCommand(ctx, outlet, cmd, timeout);     \
	if (return_val != S_OK)  return return_val;                     \
}

#define	EXPECT(p,t)	{						\
			if (RPSLookFor(ctx, p, t) < 0)			\
				return(errno == ETIMEDOUT		\
			?	S_TIMEOUT : S_OOPS);			\
			}
#ifdef WEDONTUSETHESE

#define	NULLEXPECT(p,t)	{						\
				if (RPSLookFor(ctx, p, t) < 0)		\
					return(NULL);			\
			}

#define	SNARF(s, to)	{						\
				if (RPSScanLine(ctx,to,(s),sizeof(s))	\
				!=	S_OK)				\
					return(S_OOPS);			\
			}

#define	NULLSNARF(s, to)	{					\
				if (RPSScanLine(ctx,to,(s),sizeof(s))	\
				!=	S_OK)				\
					return(NULL);			\
				}

#endif

/* Look for any of the given patterns.  We don't care which */

static int
RPSLookFor(struct WTI_RPS10* ctx, struct Etoken * tlist, int timeout)
{
	int	rc;
	if ((rc = ExpectToken(ctx->fd, tlist, timeout, NULL, 0)) < 0) {
		syslog(LOG_ERR, _("Did not find string: '%s' from" DEVICE ".")
		,	tlist[0].string);
		RPSDisconnect(ctx);
		return(-1);
	}
	return(rc);
}


/*
 * RPSSendCommand - send a command to the specified outlet
 */
static int
RPSSendCommand (struct WTI_RPS10 *ctx, int outlet, char command, int timeout)
{
	char            writebuf[10]; /* all commands are 9 chars long! */
	int		return_val;  /* system call result */
	fd_set          rfds, wfds, xfds;
				     /*  list of FDs for select() */
	struct timeval 	tv;	     /*  */

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&xfds);

	snprintf (writebuf, sizeof(writebuf), "%s%d%c\r",
		  WTIpassword, outlet, command);

	if (gbl_debug) printf ("Sending %s\n", writebuf);

	/* Make sure the serial port won't block on us. use select()  */
	FD_SET(ctx->fd, &wfds);
	FD_SET(ctx->fd, &xfds);
	
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	
	return_val = select(ctx->fd+1, NULL, &wfds,&xfds, &tv);
	if (return_val == 0) {
		/* timeout waiting on serial port */
		syslog (LOG_ERR, "%s: Timeout writing to %s",
			WTIid, ctx->device);
		return S_TIMEOUT;
	} else if ((return_val == -1) || FD_ISSET(ctx->fd, &xfds)) {
		/* an error occured */
		syslog (LOG_ERR, "%s: Error before writing to %s: %s",
			WTIid, ctx->device, strerror(errno));		
		return S_OOPS;
	}

	/* send the command */
	if (write(ctx->fd, writebuf, strlen(writebuf)) != strlen(writebuf)) {
		syslog (LOG_ERR, "%s: Error writing to  %s : %s",
			WTIid, ctx->device, strerror(errno));
		return S_OOPS;
	}

	/* suceeded! */
	return S_OK;

}  /* end RPSSendCommand() */

/* 
 * RPSReset - Reset (power-cycle) the given outlet number 
 */
static int
RPSReset(struct WTI_RPS10* ctx, int unitnum, const char * rebootid)
{

	if (gbl_debug) printf ("Calling RPSReset (%s)\n", WTIid);
	
	if (ctx->fd < 0) {
		syslog(LOG_ERR, "%s: device %s is not open!", WTIid, 
		       ctx->device);
		return S_OOPS;
	}

	/* send the "toggle power" command */
	SEND(unitnum, 'T', 10);

	/* Expect "Plug 0 Off" */
	EXPECT(WTItokPlug, 5);
	if (gbl_debug)	printf ("Got Plug\n");
	EXPECT(WTItokOutlet, 2);
	if (gbl_debug) printf ("Got Outlet #\n");
	EXPECT(WTItokOff, 2);
	if (gbl_debug) printf ("Got Off\n");	
	EXPECT(WTItokCRNL, 2);
	syslog(LOG_INFO, _("Host %s being rebooted."), rebootid);
	
	/* Expect "Plug 0 On"  will take 5 or 10 secs depending on dip switch*/
	EXPECT(WTItokPlug, 12);
	if (gbl_debug) printf ("Got Plug\n");	
	EXPECT(WTItokOutlet, 2);
	if (gbl_debug) printf ("Got Outlet #\n");	
	EXPECT(WTItokOn, 2);
	if (gbl_debug) printf ("Got Off\n");
	EXPECT(WTItokCRNL, 2);

	/* Expect "Complete" */
	EXPECT(WTItokComplete, 5);
	if (gbl_debug) printf ("Got Complete\n");
	EXPECT(WTItokCRNL, 2);
	if (gbl_debug) printf ("Got NL\n");
	
	return(S_OK);

} /* end RPSReset() */


#if defined(ST_POWERON) 
/* 
 * RPSOn - Turn OFF the given outlet number 
 */
static int
RPSOn(struct WTI_RPS10* ctx, int unitnum, const char * host)
{

	if (ctx->fd < 0) {
		syslog(LOG_ERR, "%s: device %s is not open!", WTIid, 
		       ctx->device);
		return S_OOPS;
	}

	/* send the "On" command */
	SEND(unitnum, '1', 10);

	/* Expect "Plug 0 On" */
	EXPECT(WTItokPlug, 5);
	EXPECT(WTItokOutlet, 2);
	EXPECT(WTItokOn, 2);
	EXPECT(WTItokCRNL, 2);
	syslog(LOG_INFO, _("Host %s being turned on."), host);
	
	/* Expect "Complete" */
	EXPECT(WTItokComplete, 5);
	EXPECT(WTItokCRNL, 2);

	return(S_OK);

} /* end RPSOn() */
#endif


#if defined(ST_POWEROFF) 
/* 
 * RPSOff - Turn Off the given outlet number 
 */
static int
RPSOff(struct WTI_RPS10* ctx, int unitnum, const char * host)
{

	if (ctx->fd < 0) {
		syslog(LOG_ERR, "%s: device %s is not open!", WTIid, 
		       ctx->device);
		return S_OOPS;
	}

	/* send the "Off" command */
	SEND(unitnum, '0', 10);

	/* Expect "Plug 0 Off" */
	EXPECT(WTItokPlug, 5);
	EXPECT(WTItokOutlet, 2);
	EXPECT(WTItokOff, 2);
	EXPECT(WTItokCRNL, 2);
	syslog(LOG_INFO, _("Host %s being turned on."), host);
	
	/* Expect "Complete" */
	EXPECT(WTItokComplete, 5);
	EXPECT(WTItokCRNL, 2);

	return(S_OK);

} /* end RPSOff() */
#endif


/*
 * st_status - API entry point to probe the status of the stonith device 
 *           (basically just "is it reachable and functional?", not the
 *            status of the individual outlets)
 * 
 * Returns:
 *    S_OOPS - some error occured
 *    S_OK   - if the stonith device is reachable and online.
 */
int
st_status(Stonith  *s)
{
	struct WTI_RPS10*	ctx;
	
	if (gbl_debug) printf ("Calling st_status (%s)\n", WTIid);
	
	if (!ISWTIRPS10(s)) {
		syslog(LOG_ERR, "invalid argument to RPS_status");
		return(S_OOPS);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in RPS_status");
		return(S_OOPS);
	}
	ctx = (struct WTI_RPS10*) s->pinfo;
	if (RPSConnect(ctx) != S_OK) {
		return(S_OOPS);
	}

	/* The "connect" really does enough work to see if the 
	   controller is alive...  It verifies that it is returning 
	   RPS-10 Ready 
	*/

	return(RPSDisconnect(ctx));
}

/*
 * st_hostlist - API entry point to return the list of hosts 
 *                 for the devices on this WTI_RPS10 unit
 * 
 *               This type of device is configured from the config file,
 *                 so we don't actually have to connect to figure this
 *                 out, just peruse the 'ctx' structure.
 * Returns:
 *     NULL on error
 *     a malloced array, terminated with a NULL,
 *         of null-terminated malloc'ed strings.
 */
char **
st_hostlist(Stonith  *s)
{
	char **		ret = NULL;	/* list to return */
	int 		i, numnames = 0;
	struct WTI_RPS10*	ctx;

	if (gbl_debug) printf ("Calling st_hostlist (%s)\n", WTIid);
	
	if (!ISWTIRPS10(s)) {
		syslog(LOG_ERR, "invalid argument to RPS_list_hosts");
		return(NULL);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in RPS_list_hosts");
		return(NULL);
	}

	ctx = (struct WTI_RPS10*) s->pinfo;

	/* check to see how many outlets are configured */
	for (i=0;i<WTI_NUM_CONTROLLERS;i++) {
		if (ctx->controllers[i].configured)
			numnames++;
	}

	if (numnames >= 1) {
		ret = (char **)MALLOC((numnames+1)*sizeof(char*));
		if (ret == NULL) {
			syslog(LOG_ERR, "out of memory");
		} else {
			ret[numnames]=NULL; /* null terminate the array */
			for (i=0;i<WTI_NUM_CONTROLLERS;i++) {
				if (ctx->controllers[i].configured) {
					numnames--;
					ret[numnames] = 
						strdup(ctx->controllers[i].node);
				} /* end if this outlet is configured */
			} /* end for each possible outlet */
		} /* end if malloc() suceeded */
	} /* end if any outlets are configured */

	return(ret);
} /* end si_hostlist() */

/*
 * st_freehostlist - free the result from st_hostlist() 
 */
void
st_freehostlist (char ** hlist)
{
	char **	hl = hlist;
	if (hl == NULL) {
		return;
	}
	while (*hl) {
		FREE(*hl);
		*hl = NULL;
		++hl;
	}
	FREE(hlist);
}


/*
 *	Parse the given configuration information, and stash
 *      it away...
 *
 *         The format of <info> for this module is:
 *            <serial device> <remotenode> <outlet> [<remotenode> <outlet>] ...
 *
 *      e.g. A machine named 'nodea' can kill a machine named 'nodeb' through
 *           a device attached to serial port /dev/ttyS0.
 *           A machine named 'nodeb' can kill machines 'nodea' and 'nodec'
 *           through a device attached to serial port /dev/ttyS1 (outlets 0 
 *             and 1 respectively)
 *
 *      <assuming this is the heartbeat configuration syntax:>
 * 
 *      stonith nodea rps10 /dev/ttyS0 nodeb 0 
 *      stonith nodeb rps10 /dev/ttyS0 nodea 0 nodec 1
 *
 *      Another possible configuration is for 2 stonith devices
 *         accessible through 2 different serial ports on nodeb:
 *
 *      stonith nodeb rps10 /dev/ttyS0 nodea 0 
 *      stonith nodeb rps10 /dev/ttyS1 nodec 0
 */

/*
 * 	OOPS!
 *
 * 	Most of the large block of comments above is incorrect as far as this
 * 	module is concerned.  It is somewhat applicable to the heartbeat code,
 * 	but not to this Stonith module.
 *
 * 	The format of parameter string for this module is:
 *            <serial device> <remotenode> <outlet> [<remotenode> <outlet>] ...
 */

static int
RPS_parse_config_info(struct WTI_RPS10* ctx, const char * info)
{
	char *copy;
	char *token;
	char *outlet, *node;
	int  anyconfigured = 0;


	if (ctx->config) {
		/* The module is already configured. */
		return(S_OOPS);
	}

	/* strtok() is nice to use to parse a string with 
	   (other than it isn't threadsafe), but it is destructive, so
	   we're going to alloc our own private little copy for the
	   duration of this function.
	*/

	copy = strdup(info);
	if (!copy) {
		syslog(LOG_ERR, "out of memory");
		return S_OOPS;
	}

	/* Grab the serial device */
	token = strtok (copy, " \t");

	if (!token) {
		syslog(LOG_ERR, "%s: Can't find serial device on config line '%s'",
		       WTIid, info);
		goto token_error;		
	}

	ctx->device = strdup(token);
	if (!ctx->device) {
		syslog(LOG_ERR, "out of memory");
		goto token_error;
	}

	/* Loop through the rest of the command line which should consist of */
	/* <nodename> <outlet> pairs */
	while ((node = strtok (NULL, " \t"))
	       && (outlet = strtok (NULL, " \t\n"))) {
		int outletnum;

		/* validate the outlet token */
		if ((sscanf (outlet, "%d", &outletnum) != 1)
		    || outletnum < 0 
		    || outletnum >= WTI_NUM_CONTROLLERS ) {
			syslog(LOG_ERR
			, "%s: the outlet number %s must be a number between"
			" 0 and 9",
			       WTIid, outlet);
			goto token_error;
		}
		
		ctx->controllers[outletnum].configured = 1;
		ctx->controllers[outletnum].node = strdup(node);
		ctx->controllers[outletnum].outletnum = outletnum;
		++anyconfigured;
	} 

	/* free our private copy of the string we've been destructively 
	 * parsing with strtok()
	 */
	free(copy);
	ctx->config = 1;
	return (anyconfigured ? S_OK : S_BADCONFIG);

token_error:
	free(copy);
	return(S_BADCONFIG);
}


/* 
 * dtrtoggle - toggle DTR on the serial port
 * 
 * snarfed from minicom, sysdep1.c, a well known POSIX trick.
 *
 */
static void dtrtoggle(int fd) {
    struct termios tty, old;
    int sec = 2;
    
    if (gbl_debug) printf ("Calling dtrtoggle (%s)\n", WTIid);
    
    tcgetattr(fd, &tty);
    tcgetattr(fd, &old);
    cfsetospeed(&tty, B0);
    cfsetispeed(&tty, B0);
    tcsetattr(fd, TCSANOW, &tty);
    if (sec>0) {
      sleep(sec);
      tcsetattr(fd, TCSANOW, &old);
    }
    
    if (gbl_debug) printf ("dtrtoggle Complete (%s)\n", WTIid);
}

/*
 * RPSConnect -
 *
 * Connect to the given WTI_RPS10 device.  
 * Side Effects
 *    DTR on the serial port is toggled
 *    ctx->fd now contains a valid file descriptor to the serial port
 *    ??? LOCK THE SERIAL PORT ???
 *  
 * Returns 
 *    S_OK on success
 *    S_OOPS on error
 *    S_TIMEOUT if the device did not respond
 *
 */
static int
RPSConnect(struct WTI_RPS10 * ctx)
{
  	  
	/* Open the serial port if it isn't already open */
	if (ctx->fd < 0) {
		struct termios tio;

		ctx->fd = open (ctx->device, O_RDWR);
		if (ctx->fd <0) {
			syslog (LOG_ERR, "%s: Can't open %s : %s",
				WTIid, ctx->device, strerror(errno));
			return S_OOPS;
		}

		/* set the baudrate to 9600 8 - N - 1 */
		memset (&tio, 0, sizeof(tio));

		/* ??? ALAN - the -tradtitional flag on gcc causes the 
		   CRTSCTS constant to generate a warning, and warnings 
                   are treated as errors, so I can't set this flag! - EZA ???
		   
                   Hmmm. now that I look at the documentation, RTS
		   is just wired high on this device! we don't need it.
		*/
		/* tio.c_cflag = B9600 | CS8 | CLOCAL | CREAD | CRTSCTS ;*/
		tio.c_cflag = B9600 | CS8 | CLOCAL | CREAD ;
		tio.c_lflag = ICANON;

		if (tcsetattr (ctx->fd, TCSANOW, &tio) < 0) {
			syslog (LOG_ERR, "%s: Can't set attributes %s : %s",
				WTIid, ctx->device, strerror(errno));
			close (ctx->fd);
			ctx->fd=-1;
			return S_OOPS;
		}
		/* flush all data to and fro the serial port before we start */
		if (tcflush (ctx->fd, TCIOFLUSH) < 0) {
			syslog (LOG_ERR, "%s: Can't flush %s : %s",
				WTIid, ctx->device, strerror(errno));
			close (ctx->fd);
			ctx->fd=-1;
			return S_OOPS;		
		}
		
	}

	/* Toggle DTR - this 'resets' the controller serial port interface 
           In minicom, try CTRL-A H to hangup and you can see this behavior.
         */
	dtrtoggle(ctx->fd);

	/* Wait for the switch to respond with "RPS-10 Ready".  
	   Emperically, this usually takes 5-10 seconds... 
	*/
	if (gbl_debug) printf ("Waiting for READY\n");
	EXPECT(WTItokReady, 12);
	if (gbl_debug) printf ("Got READY\n");
	EXPECT(WTItokCRNL, 2);
	if (gbl_debug) printf ("Got NL\n");
	   
  return(S_OK);
}

static int
RPSDisconnect(struct WTI_RPS10 * ctx)
{

  if (ctx->fd >= 0) {
    /* Flush the serial port, we don't care what happens to the characters
       and failing to do this can cause close to hang.
    */
    tcflush(ctx->fd, TCIOFLUSH);
    close (ctx->fd);
  }
  ctx->fd = -1;

  return S_OK;
} 

/*
 * RPSNametoOutlet - Map a hostname to an outlet number on this stonith device.
 *
 * Returns:
 *     0-9 on success ( the outlet number on the RPS10 )
 *     -1 on failure (host not found in the config file)
 * 
 */
static int
RPSNametoOutlet ( struct WTI_RPS10 * ctx, const char * host )
{
	int i=0;

	/* scan the controllers[] array to see if this host is there */
	for (i=0;i<WTI_NUM_CONTROLLERS;i++) {
		/* return the outlet number */
		if (ctx->controllers[i].configured
		    && ctx->controllers[i].node 
		    && !strcmp(host, ctx->controllers[i].node)) {
			/* found it! */
			return ctx->controllers[i].outletnum;
		}
	}

	return -1;
}


/*
 *	st_reset - API call to Reset (reboot) the given host on 
 *          this Stonith device.  This involves toggling the power off 
 *          and then on again, OR just calling the builtin reset command
 *          on the stonith device.
 */
int
st_reset(Stonith * s, int request, const char * host)
{
	int	rc = S_OK;
	int	lorc = S_OK;
	int outletnum = -1;
	struct WTI_RPS10*	ctx;
	
	if (gbl_debug) printf ("Calling st_reset (%s)\n", WTIid);
	
	if (!ISWTIRPS10(s)) {
		syslog(LOG_ERR, "invalid argument to RPS_reset_host");
		return(S_OOPS);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in RPS_reset_host");
		return(S_OOPS);
	}
	ctx = (struct WTI_RPS10*) s->pinfo;

	if ((rc = RPSConnect(ctx)) != S_OK) {
		return(rc);
	}

	outletnum = RPSNametoOutlet(ctx, host);

	if (outletnum < 0) {
		syslog(LOG_WARNING, _("%s %s "
				      "doesn't control host [%s]."), 
		       ctx->idinfo, ctx->unitid, host);
		RPSDisconnect(ctx);
		return(S_BADHOST);
	}

	switch(request) {

#if defined(ST_POWERON) 
		case ST_POWERON:
			rc = RPSOn(ctx, outletnum, host);
			break;
#endif
#if defined(ST_POWEROFF)
		case ST_POWEROFF:
			rc = RPSOff(ctx, outletnum, host);
			break;
#endif
	case ST_GENERIC_RESET:
		rc = RPSReset(ctx, outletnum, host);
		break;
	default:
		rc = S_INVAL;
		break;
	}

	lorc = RPSDisconnect(ctx);

	return(rc != S_OK ? rc : lorc);
}

/*
 *	Parse the information in the given configuration file,
 *	and stash it away...
 */
int
st_setconffile(Stonith* s, const char * configname)
{
	FILE *	cfgfile;

	char	RPSid[256];

	struct WTI_RPS10*	ctx;

	if (!ISWTIRPS10(s)) {
		syslog(LOG_ERR, "invalid argument to RPS_set_configfile");
		return(S_OOPS);
	}
	ctx = (struct WTI_RPS10*) s->pinfo;

	if ((cfgfile = fopen(configname, "r")) == NULL)  {
		syslog(LOG_ERR, _("Cannot open %s"), configname);
		return(S_BADCONFIG);
	}

	while (fgets(RPSid, sizeof(RPSid), cfgfile) != NULL){

		switch (RPSid[0]){
			case '\0': case '\n': case '\r': case '#':
			continue;
		}

		/* We can really only handle one line. Wimpy. */
		return RPS_parse_config_info(ctx, RPSid);
	}
	return(S_BADCONFIG);
}

/*
 *	st_setconfinfo - API entry point to process one line of config info 
 *       for this particular device.
 *
 *      Parse the config information in the given string, and stash it away...
 *
 */
int
st_setconfinfo(Stonith* s, const char * info)
{
	struct WTI_RPS10* ctx;

	if (!ISWTIRPS10(s)) {
		syslog(LOG_ERR, "RPS_provide_config_info: invalid argument");
		return(S_OOPS);
	}
	ctx = (struct WTI_RPS10 *)s->pinfo;

	return(RPS_parse_config_info(ctx, info));
}

/*
 * st_getinfo - API entry point to retrieve something from the handle
 */
const char *
st_getinfo(Stonith * s, int reqtype)
{
	struct WTI_RPS10* ctx;
	const char *	ret;

	if (!ISWTIRPS10(s)) {
		syslog(LOG_ERR, "RPS_idinfo: invalid argument");
		return NULL;
	}
	/*
	 *	We look in the ST_TEXTDOMAIN catalog for our messages
	 */
	ctx = (struct WTI_RPS10 *)s->pinfo;

	switch (reqtype) {
		case ST_DEVICEID:
			ret = ctx->idinfo;
			break;

		case ST_CONF_INFO_SYNTAX:
			ret = _("<serial_device> <node> <outlet> "
			"[ <node> <outlet> [...] ]\n"
			"All tokens are white-space delimited.\n");
			break;

		case ST_CONF_FILE_SYNTAX:
			ret = _("<serial_device> <node> <outlet> "
			"[ <node> <outlet> [...] ]\n"
			"All tokens are white-space delimited.\n"
			"Blank lines and lines beginning with # are ignored");
			break;

		default:
			ret = NULL;
			break;
	}
	return ret;
}

/*
 * st_destroy - API entry point to destroy a WTI_RPS10 Stonith object.
 */
void
st_destroy(Stonith *s)
{
	struct WTI_RPS10* ctx;

	if (!ISWTIRPS10(s)) {
		syslog(LOG_ERR, "wti_rps10_del: invalid argument");
		return;
	}
	ctx = (struct WTI_RPS10 *)s->pinfo;

	ctx->WTIid = NOTwtiid;

	/*  close the fd if open and set ctx->fd to invalid */
	RPSDisconnect(ctx);
	
	if (ctx->device != NULL) {
		FREE(ctx->device);
		ctx->device = NULL;
	}
	if (ctx->idinfo != NULL) {
		FREE(ctx->idinfo);
		ctx->idinfo = NULL;
	}
	if (ctx->unitid != NULL) {
		FREE(ctx->unitid);
		ctx->unitid = NULL;
	}
}

/* 
 * st_new - API entry point called to create a new WTI_RPS10 Stonith device
 *          object. 
 */
void *
st_new(void)
{
	struct WTI_RPS10*	ctx = MALLOCT(struct WTI_RPS10);

	if (ctx == NULL) {
		syslog(LOG_ERR, "out of memory");
		return(NULL);
	}
	memset(ctx, 0, sizeof(*ctx));
	ctx->WTIid = WTIid;
	ctx->fd = -1;
	ctx->config = 0;
	ctx->device = NULL;
	ctx->idinfo = NULL;
	ctx->unitid = NULL;
	REPLSTR(ctx->idinfo, DEVICE);
	REPLSTR(ctx->unitid, "unknown");

	return((void *)ctx);
}
