const static char * _serial_c_Id = "$Id: serial.c,v 1.2 2001/08/15 16:17:12 alan Exp $";

/*
 * Linux-HA serial heartbeat code
 *
 * The basic facilities for round-robin (ring) heartbeats are
 * contained within.
 *
 *
 * Copyright (C) 1999, 2000 Alan Robertson <alanr@unix.sh>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <portability.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <heartbeat.h>
#include <HBcomm.h>

#define PIL_PLUGINTYPE		HB_COMM_TYPE
#define PIL_PLUGINTYPE_S	HB_COMM_TYPE_S
#define PIL_PLUGIN		serial
#define PIL_PLUGIN_S		"serial"
#include <pils/plugin.h>


struct serial_private {
        char *			ttyname;
        int			ttyfd;		/* For direct TTY i/o */ 
        struct hb_media*	next;
};

static int serial_baud = 0;

/* Used to maintain a list of our serial ports in the ring */
static struct hb_media*		lastserialport;

static struct hb_media*	serial_new(const char * value);
static struct ha_msg*	serial_read(struct hb_media *mp);
static char *		ttygets(char * inbuf, int length
,				struct serial_private *tty);
static int		serial_write(struct hb_media*mp, struct ha_msg *msg);
static int		serial_open(struct hb_media* mp);
static int		ttysetup(int fd);
static int		opentty(char * serial_device);
static int		serial_close(struct hb_media* mp);
static int		serial_init(void);

static void		serial_localdie(void);

static int		serial_mtype(char **buffer);
static int		serial_descr(char **buffer);
static int		serial_isping(void);

/*
 * serialclosepi is called as part of unloading the serial HBcomm plugin.
 * If there was any global data allocated, or file descriptors opened, etc.
 * which is associated with the plugin, and not a single interface
 * in particular, here's our chance to clean it up.
 */

static void
serialclosepi(PILPlugin*pi)
{
	serial_localdie();
}


/*
 * serialcloseintf called as part of shutting down the serial HBcomm interface.
 * If there was any global data allocated, or file descriptors opened, etc.
 * which is associated with the serial implementation, here's our chance
 * to clean it up.
 */
static PIL_rc
serialcloseintf(PILInterface* pi, void* pd)
{
	return PIL_OK;
}

static struct hb_media_fns serialOps ={
	serial_new,	/* Create single object function */
	NULL,		/* whole-line parse function */
	serial_open,
	serial_close,
	serial_read,
	serial_write,
	serial_mtype,
	serial_descr,
	serial_isping,
};

PIL_PLUGIN_BOILERPLATE("1.0", Debug, serialclosepi);
static const PILPluginImports*  PluginImports;
static PILPlugin*               OurPlugin;
static PILInterface*		OurInterface;
static struct hb_media_imports*	OurImports;
static void*			interfprivate;

PIL_rc
PIL_PLUGIN_INIT(PILPlugin*us, const PILPluginImports* imports);

PIL_rc
PIL_PLUGIN_INIT(PILPlugin*us, const PILPluginImports* imports)
{
	/* Force the compiler to do a little type checking */
	(void)(PILPluginInitFun)PIL_PLUGIN_INIT;

	PluginImports = imports;
	OurPlugin = us;

	/* Register ourself as a plugin */
	imports->register_plugin(us, &OurPIExports);  

	serial_init();
	/*  Register our interface implementation */
 	return imports->register_interface(us, PIL_PLUGINTYPE_S
	,	PIL_PLUGIN_S
	,	&serialOps
	,	serialcloseintf		/*close */
	,	&OurInterface
	,	(void*)&OurImports
	,	interfprivate); 
}

#define		IsTTYOBJECT(mp)	((mp) && ((mp)->vf == (void*)&serial_media_fns))
//#define		TTYASSERT(mp)	ASSERT(IsTTYOBJECT(mp))
#define		TTYASSERT(mp)
#define		RTS_WARNTIME	3600

static int
serial_mtype (char **buffer) { 
	
	*buffer = ha_malloc((strlen("serial") * sizeof(char)) + 1);

	strcpy(*buffer, "serial");

	return strlen("serial");
}

static int
serial_descr (char **buffer) { 

	const char *str = "serial ring";	

	*buffer = ha_malloc((strlen(str) * sizeof(char)) + 1);

	strcpy(*buffer, str);

	return strlen(str);
}

static int
serial_isping (void) {
	return 0;
}

/* Initialize global serial data structures */
static int
serial_init (void)
{
	(void)_serial_c_Id;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;	/* ditto */
	lastserialport = NULL;
	/* This eventually ought be done through the configuration API */
	if (serial_baud <= 0) {
		const char *	chbaud;
		if ((chbaud  = OurImports->ParamValue("baud")) != NULL) {
			serial_baud = OurImports->StrToBaud(chbaud);
		}
	}
	if (serial_baud <= 0) {
		serial_baud = DEFAULTBAUD;
	}
	return(HA_OK);
}

/* Process a serial port declaration */
static struct hb_media *
serial_new (const char * port)
{
	char	msg[MAXLINE];
	struct	stat	sbuf;
	struct hb_media * ret;


	/* Let's see if this looks like it might be a serial port... */
	if (*port != '/') {
		sprintf(msg, "Serial port not full pathname [%s] in config file"
		,	port);
		ha_error(msg);
		return(NULL);
	}

	if (stat(port, &sbuf) < 0) {
		sprintf(msg, "Nonexistent serial port [%s] in config file"
		,	port);
		ha_perror(msg);
		return(NULL);
	}
	if (!S_ISCHR(sbuf.st_mode)) {
		sprintf(msg, "Serial port [%s] not a char device in config file"
		,	port);
		ha_error(msg);
		return(NULL);
	}

	ret = MALLOCT(struct hb_media);
	if (ret != NULL) {
		struct serial_private * sp;
		sp = MALLOCT(struct serial_private);
		if (sp != NULL)  {
			/*
			 * This implies we have to process the "new"
			 * for this object in the parent process of us all...
			 * otherwise we can't do this linking stuff...
			 */
			sp->next = lastserialport;
			lastserialport=ret;
			sp->ttyname = (char *)ha_malloc(strlen(port)+1);
			strcpy(sp->ttyname, port);
			ret->name = sp->ttyname;
			ret->pd = sp;
		}else{
			ha_free(ret);
			ret = NULL;
			ha_error("Out of memory (private serial data)");
		}
	}else{
		ha_error("Out of memory (serial data)");
	}
	return(ret);
}

static int
serial_open (struct hb_media* mp)
{
	struct serial_private*	sp;
	char			msg[MAXLINE];

	TTYASSERT(mp);
	sp = (struct serial_private*)mp->pd;
	if (OurImports->devlock(sp->ttyname) < 0) {
		snprintf(msg, MAXLINE, "cannot lock line %s", sp->ttyname);
		ha_error(msg);
		return(HA_FAIL);
	}
	if ((sp->ttyfd = opentty(sp->ttyname)) < 0) {
		return(HA_FAIL);
	}
	ha_log(LOG_NOTICE, "Starting serial heartbeat on tty %s", sp->ttyname);
	return(HA_OK);
}

static int
serial_close (struct hb_media* mp)
{
	struct serial_private*	sp;
	int rc;

	TTYASSERT(mp);
	sp = (struct serial_private*)mp->pd;
	rc = close(sp->ttyfd) < 0 ? HA_FAIL : HA_OK;
	OurImports->devunlock(sp->ttyname);
	return rc;
}

/* Set up a serial line the way we want it be done */
static int
ttysetup(int fd)
{
	struct TERMIOS	ti;

	if (GETATTR(fd, &ti) < 0) {
		ha_perror("cannot get tty attributes");
		return(HA_FAIL);
	}

#ifndef IUCLC
#	define IUCLC	0	/* Ignore it if not supported */
#endif
#ifndef CBAUD
#	define CBAUD	0
#endif

	ti.c_iflag &= ~(BRKINT|IGNBRK|IUCLC|IXANY|IXOFF|IXON|ICRNL|PARMRK);
	/* Unsure if I want PARMRK or not...  It may not matter much */
	ti.c_iflag |=  (INPCK|ISTRIP|IGNCR);

	ti.c_oflag &= ~(OPOST);
	ti.c_cflag &= ~(CBAUD|CSIZE|PARENB);

/*
 * Make a silly Linux/Gcc -Wtraditional warning go away
 * This is not my fault, you understand...                       ;-)
 * Suggestions on how to better work around it would be welcome.
 */
#if CRTSCTS == 020000000000
#	undef CRTSCTS
#	define CRTSCTS 020000000000U
#endif

	ti.c_cflag |=  (serial_baud|(unsigned)CS8|(unsigned)CREAD|(unsigned)CLOCAL|(unsigned)CRTSCTS);

	ti.c_lflag &= ~(ICANON|ECHO|ISIG);
#ifdef HAVE_TERMIOS_C_LINE
	ti.c_line = 0;
#endif
	ti.c_cc[VMIN] = 1;
	ti.c_cc[VTIME] = 1;
	if (SETATTR(fd, &ti) < 0) {
		ha_perror("cannot set tty attributes");
		return(HA_FAIL);
	}
	/* For good measure */
	FLUSH(fd);
	return(HA_OK);
}

/* Open a tty and set it's line parameters */
static int
opentty(char * serial_device)
{
	int	fd;

	if ((fd=open(serial_device, O_RDWR)) < 0 ) {
		char msg[128];
		sprintf(msg, "cannot open %s", serial_device);
		ha_perror(msg);
		return(fd);
	}
	if (!ttysetup(fd)) {
		close(fd);
		return(-1);
	}
	if (fcntl(fd, F_SETFD, FD_CLOEXEC)) {
		ha_perror("Error setting the close-on-exec flag");
	}
	return(fd);
}

static struct hb_media* ourmedia = NULL;

static void
serial_localdie(void)
{
	int	ourtty;
	if (!ourmedia || !ourmedia->pd) {
		return;
	}
	ourtty = ((struct serial_private*)(ourmedia->pd))->ttyfd;
	if (ourtty >= 0) {
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "serial_localdie: Flushing tty");
		}
		tcflush(ourtty, TCIOFLUSH);
	}
}

/* This function does all the writing to our tty ports */
static int
serial_write (struct hb_media*mp, struct ha_msg*m)
{
	char *		str;

	int		wrc;
	int		size;
	int		ourtty;
	static TIME_T	last_norts;

	TTYASSERT(mp);

	ourmedia = mp;	/* Only used for the "localdie" function */
	ourtty = ((struct serial_private*)(mp->pd))->ttyfd;
	if ((str=msg2string(m)) == NULL) {
		ha_error("Cannot convert message to tty string");
		return(HA_FAIL);
	}
	size = strlen(str);
	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "Sending pkt to %s [%d bytes]"
		,	mp->name, size);
	}
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, str);
	}
	alarm(2);
	wrc = write(ourtty, str, size);
	alarm(0);
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, "write returned %d", wrc);
	}

	if (wrc < 0) {
		if (errno == EINTR) {
			TIME_T	now = time(NULL);
			tcflush(ourtty, TCIOFLUSH);
			if ((now - last_norts) > RTS_WARNTIME) {
				last_norts = now;
				ha_log(LOG_ERR
				,	"TTY write timeout on [%s]"
				" (no connection?)", mp->name);
			}
		}else{
			ha_perror("TTY write failure on [%s]", mp->name);
		}
	}
	ha_free(str);
	return(HA_OK);
}

/* This function does all the reading from our tty ports */
static struct ha_msg *
serial_read (struct hb_media*mp)
{
	char buf[MAXLINE];
	struct hb_media*	sp;
	struct serial_private*	thissp;
	struct ha_msg*		ret;
	char *			newmsg = NULL;
	int			newmsglen = 0;
	int			startlen;
	const char *		start = MSG_START;
	const char *		end = MSG_START;
	int			endlen;
	struct serial_private*	spp;

	TTYASSERT(mp);
	thissp = (struct serial_private*)mp->pd;

	if ((ret = ha_msg_new(0)) == NULL) {
		ha_error("Cannot get new message");
		return(NULL);
	}
	startlen = strlen(start);
	if (start[startlen-1] == '\n') {
		--startlen;
	}
	endlen = strlen(end);
	if (end[endlen-1] == '\n') {
		--endlen;
	}
	/* Skip until we find a MSG_START (hopefully we skip nothing) */
	while (ttygets(buf, MAXLINE, thissp) != NULL
	&&	strncmp(buf, start, startlen) != 0) {
		/* Nothing */
	}
	/* Add Name=value pairs until we reach MSG_END or EOF */
	while (ttygets(buf, MAXLINE, thissp) != NULL
	&&	strncmp(buf, MSG_END, endlen) != 0) {

		/* Add the "name=value" string on this line to the message */
		if (ha_msg_add_nv(ret, buf) != HA_OK) {
			ha_msg_del(ret);
			return(NULL);
		}
	}
	/* Should this message should continue around the ring? */

	if (!isauthentic(ret) || !should_ring_copy_msg(ret)) {
		/* Avoid infinite loops... Ignore this message */
		return(ret);
	}

	/* Forward message to other port in ring (if any) */
	for (sp=lastserialport; sp; sp=spp->next) {
		TTYASSERT(sp);
		spp = (struct serial_private*)sp->pd;
		if (sp == mp) {
			/* That's us! */
			continue;
		}

		/* Modify message, decrementing TTL (and reauthenticate it) */
		if (newmsglen) {
			const char *		ttl_s;
			int			ttl;
			char			nttl[8];

			/* Decrement TTL in the message before forwarding */
			if ((ttl_s = ha_msg_value(ret, F_TTL)) == NULL) {
				return(ret);
			}
			ttl = atoi(ttl_s);
			sprintf(nttl, "%d", ttl-1);
			ha_msg_mod(ret, F_TTL, nttl);

			/* Re-authenticate message */
			add_msg_auth(ret);

			if ((newmsg = msg2string(ret)) == NULL) {
				ha_error("Cannot convert serial msg to string");
				continue;
			}
			newmsglen = strlen(newmsg);
		}
		/*
		 * This will eventually have to be changed
		 * if/when we change from FIFOs to more general IPC
		 */
		if (newmsglen) {
			/*
			 * I suppose it just becomes an IPC abstraction
			 * and we issue a "msgput" or some such on it...
			 */
			write(sp->wpipe[P_WRITEFD], newmsg, newmsglen);
		}
	}

	if (newmsglen) {
		ha_free(newmsg);
	}
	return(ret);
}


/* Gets function for our tty */
static char *
ttygets(char * inbuf, int length, struct serial_private *tty)
{
	char *	cp;
	char *	end = inbuf + length;
	int	rc;
	int	fd = tty->ttyfd;

	for(cp=inbuf; cp < end; ++cp) {
		errno = 0;
		/* One read per char -- yecch  (but it's easy) */
		rc = read(fd, cp, 1);
		if (rc != 1) {
			char	msg[MAXLINE];
			sprintf(msg, "EOF in ttygets [%s]", tty->ttyname);
			ha_perror(msg);
			return(NULL);
		}
		if (*cp == '\r' || *cp == '\n') {
			break;
		}
	}
	*cp = '\0';
	return(inbuf);
}
/*
 * $Log: serial.c,v $
 * Revision 1.2  2001/08/15 16:17:12  alan
 * Fixed the code so that serial comm plugins build/load/work.
 *
 * Revision 1.1  2001/08/10 17:16:44  alan
 * New code for the new plugin loading system.
 *
 * Revision 1.27  2001/06/08 04:57:48  alan
 * Changed "config.h" to <portability.h>
 *
 * Revision 1.26  2001/05/31 15:51:08  alan
 * Put in more fixes to get module loading (closer to) working...
 *
 * Revision 1.25  2001/05/26 17:38:01  mmoerz
 * *.cvsignore: added automake generated files that were formerly located in
 * 	     config/
 * * Makefile.am: removed ac_aux_dir stuff (for libtool) and added libltdl
 * * configure.in: removed ac_aux_dir stuff (for libtool) and added libltdl as
 * 		a convenience library
 * * bootstrap: added libtools libltdl support
 * * heartbeat/Makefile.am: added some headerfile to noinst_HEADERS
 * * heartbeat/heartbeat.c: changed dlopen, dlclose to lt_dlopen, lt_dlclose
 * * heartbeat/crc.c: changed to libltdl, exports functions via EXPORT()
 * * heartbeat/mcast.c: changed to libltdl, exports functions via EXPORT()
 * * heartbeat/md5.c: changed to libltdl, exports functions via EXPORT()
 * * heartbeat/ping.c: changed to libltdl, exports functions via EXPORT()
 * * heartbeat/serial.c: changed to libltdl, exports functions via EXPORT()
 * * heartbeat/sha1.c: changed to libltdl, exports functions via EXPORT()
 * * heartbeat/udp.c: changed to libltdl, exports functions via EXPORT()
 * * heartbeat/hb_module.h: added EXPORT() Macro, changed to libtools function
 * 			pointer
 * * heartbeat/module.c: converted to libtool (dlopen/dlclose -> lt_dlopen/...)
 * 		      exchanged scandir with opendir, readdir. enhanced
 * 		      autoloading code so that only .la modules get loaded.
 *
 * Revision 1.24  2001/05/12 06:05:23  alan
 * Put in the latest portability fixes (aka autoconf fixes)
 *
 * Revision 1.23  2001/05/11 06:20:26  alan
 * Fixed CFLAGS so we load modules from the right diurectory.
 * Fixed minor static symbol problems.
 * Fixed a bug which kept early error messages from coming out.
 *
 * Revision 1.22  2001/05/10 22:36:37  alan
 * Deleted Makefiles from CVS and made all the warnings go away.
 *
 * Revision 1.21  2000/12/12 23:23:47  alan
 * Changed the type of times from time_t to TIME_T (unsigned long).
 * Added BuildPreReq: lynx
 * Made things a little more OpenBSD compatible.
 *
 * Revision 1.20  2000/12/04 22:16:33  alan
 * Simplfied a BSD compatibility fix.
 *
 * Revision 1.19  2000/12/04 20:33:17  alan
 * OpenBSD fixes from Frank DENIS aka Jedi/Sector One <j@c9x.org>
 *
 * Revision 1.18  2000/09/01 21:10:46  marcelo
 * Added dynamic module support
 *
 * Revision 1.17  2000/08/13 04:36:16  alan
 * Added code to make ping heartbeats work...
 * It looks like they do, too ;-)
 *
 * Revision 1.16  2000/08/04 03:45:56  alan
 * Moved locking code into lock.c, so it could be used by both heartbeat and
 * the client code.  Also restructured it slightly...
 *
 * Revision 1.15  2000/07/26 05:17:19  alan
 * Added GPL license statements to all the code.
 *
 * Revision 1.14  2000/05/17 13:39:55  alan
 * Added the close-on-exec flag to sockets and tty fds that we open.
 * Thanks to Christoph Jäger for noticing the problem.
 *
 * Revision 1.13  2000/04/27 13:24:34  alan
 * Added comments about lock file fix. Minor corresponding code changes.
 *
 * Revision 1.12  2000/04/11 22:12:22  horms
 * Now cleans locks on serial devices from dead processes succesfully
 *
 * Revision 1.11  2000/02/23 18:44:53  alan
 * Put in a bug fix from Cliff Liang <lqm@readworld.com> to fix the tty
 * locking code.  The parameters to sscanf were mixed up.
 *
 * Revision 1.10  1999/11/15 05:31:43  alan
 * More tweaks for CTS/RTS flow control.
 *
 * Revision 1.9  1999/11/14 08:23:44  alan
 * Fixed bug in serial code where turning on flow control caused
 * heartbeat to hang.  Also now detect hangs and shutdown automatically.
 *
 * Revision 1.8  1999/11/11 04:58:04  alan
 * Fixed a problem in the Makefile which caused resources to not be
 * taken over when we start up.
 * Added RTSCTS to the serial port.
 * Added lots of error checking to the resource takeover code.
 *
 * Revision 1.7  1999/11/07 20:57:21  alan
 * Put in Matt Soffen's latest FreeBSD patch...
 *
 * Revision 1.6  1999/10/25 15:35:03  alan
 * Added code to move a little ways along the path to having error recovery
 * in the heartbeat protocol.
 * Changed the code for serial.c and ppp-udp.c so that they reauthenticate
 * packets they change the ttl on (before forwarding them).
 *
 * Revision 1.5  1999/10/10 20:12:54  alanr
 * New malloc/free (untested)
 *
 * Revision 1.4  1999/10/05 06:17:30  alanr
 * Fixed various uninitialized variables
 *
 * Revision 1.3  1999/09/30 15:55:12  alanr
 *
 * Added Matt Soffen's fix to change devname to serial_device for some kind
 * of FreeBSD compatibility.
 *
 * Revision 1.2  1999/09/26 14:01:18  alanr
 * Added Mijta's code for authentication and Guenther Thomsen's code for serial locking and syslog reform
 *
 * Revision 1.1.1.1  1999/09/23 15:31:24  alanr
 * High-Availability Linux
 *
 * Revision 1.11  1999/09/18 02:56:36  alanr
 * Put in Matt Soffen's portability changes...
 *
 * Revision 1.10  1999/09/16 05:50:20  alanr
 * Getting ready for 0.4.3...
 *
 * Revision 1.9  1999/08/17 03:49:26  alanr
 * added log entry to bottom of file.
 *
 */
