/*
 *	Stonith module for WTI Network Power Switch Devices (NPS-xxx)
 *
 *  Copyright (c) 2001 Mission Critical Linux, Inc.
 *                     mike ledoux <mwl@mclinux.com>
 *
 *  Based strongly on original code from baytech.c
 *	  Copyright (c) 2000 Alan Robertson <alanr@unix.sh>
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

#include "expect.h"
#include "stonith.h"

#define	DEVICE	"WTI Network Power Switch"

#define N_(text)	(text)
#define _(text)		dgettext(ST_TEXTDOMAIN, text)

/*
 *	I have a NPS-110.  This code has been tested with this switch.
 */

struct WTINPS {
	const char *	NPSid;
	char *		idinfo;
	char *		unitid;
	pid_t		pid;
	int		rdfd;
	int		wrfd;
	int		config;
	char *		device;
	char *		passwd;
};

static const char * NPSid = "WTINPS-Stonith";
static const char * NOTnpsid = "Hey, dummy this has been destroyed (WTINPS)";

#define	ISWTINPS(i)	(((i)!= NULL && (i)->pinfo != NULL)	\
	&& ((struct WTINPS *)(i->pinfo))->NPSid == NPSid)

#define	ISCONFIGED(i)	(ISWTINPS(i) && ((struct WTINPS *)(i->pinfo))->config)

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
 *	Different expect strings that we get from the WTI
 *	Network Power Switch
 */

#define WTINPSSTR	"Network Power Switch"

static struct Etoken EscapeChar[] =	{ {"Escape character is '^]'.", 0, 0}
					,	{NULL,0,0}};
static struct Etoken password[] =	{ {"Password:", 0, 0} ,{NULL,0,0}};
static struct Etoken Prompt[] =	{ {"NPS>", 0, 0} ,{NULL,0,0}};
static struct Etoken LoginOK[] =	{ {WTINPSSTR, 0, 0}
                    , {"Invalid password", 1, 0} ,{NULL,0,0}};
static struct Etoken Separator[] =	{ {"-----", 0, 0} ,{NULL,0,0}};
/* Accept either a CR/NL or an NL/CR */
static struct Etoken CRNL[] =		{ {"\n\r",0,0},{"\r\n",0,0},{NULL,0,0}};

/* We may get a notice about rebooting, or a request for confirmation */
static struct Etoken Processing[] =	{ {"rocessing - please wait", 0, 0}
				,	{"(Y/N):", 1, 0}
				,	{NULL,0,0}};

static int	NPSLookFor(struct WTINPS* nps, struct Etoken * tlist, int timeout);
static int	NPS_connect_device(struct WTINPS * nps);
static int	NPSLogin(struct WTINPS * nps);
static int	NPSNametoOutlet(struct WTINPS*, const char * name, char **outlets);
static int	NPSReset(struct WTINPS*, char * outlets, const char * rebootid);
static int	NPSScanLine(struct WTINPS* nps, int timeout, char * buf, int max);
static int	NPSLogout(struct WTINPS * nps);
static void	NPSkillcomm(struct WTINPS * nps);

int  st_setconffile(Stonith *, const char * cfgname);
int	st_setconfinfo(Stonith *, const char * info);
static int	NPS_parse_config_info(struct WTINPS* nps, const char * info);
const char *
		st_getinfo(Stonith * s, int InfoType);

char **	st_hostlist(Stonith  *);
void	st_freehostlist(char **);
int	st_status(Stonith * );
int	st_reset(Stonith * s, int request, const char * host);
#if defined(ST_POWERON) && defined(ST_POWEROFF)
static int	NPS_onoff(struct WTINPS*, char * outlets, const char * unitid
,		int request);
#endif
void	st_destroy(Stonith *);
void *	st_new(void);

/*
 *	We do these things a lot.  Here are a few shorthand macros.
 */

#define	SEND(s)	(write(nps->wrfd, (s), strlen(s)))

#define	EXPECT(p,t)	{						\
			if (NPSLookFor(nps, p, t) < 0)			\
				return(errno == ETIME			\
			?	S_TIMEOUT : S_OOPS);			\
			}

#define	NULLEXPECT(p,t)	{						\
				if (NPSLookFor(nps, p, t) < 0)		\
					return(NULL);			\
			}

#define	SNARF(s, to)	{						\
				if (NPSScanLine(nps,to,(s),sizeof(s))	\
				!=	S_OK)				\
					return(S_OOPS);			\
			}

#define	NULLSNARF(s, to)	{					\
				if (NPSScanLine(nps,to,(s),sizeof(s))	\
				!=	S_OK)				\
					return(NULL);			\
				}

/* Look for any of the given patterns.  We don't care which */

static int
NPSLookFor(struct WTINPS* nps, struct Etoken * tlist, int timeout)
{
	int	rc;
	if ((rc = ExpectToken(nps->rdfd, tlist, timeout, NULL, 0)) < 0) {
		syslog(LOG_ERR, _("Did not find string: '%s' from" DEVICE ".")
		,	tlist[0].string);
		NPSkillcomm(nps);
		return(-1);
	}
	return(rc);
}

/* Read and return the rest of the line */

static int
NPSScanLine(struct WTINPS* nps, int timeout, char * buf, int max)
{
	if (ExpectToken(nps->rdfd, CRNL, timeout, buf, max) < 0) {
		syslog(LOG_ERR, ("Could not read line from " DEVICE "."));
		NPSkillcomm(nps);
		nps->pid = -1;
		return(S_OOPS);
	}
	return(S_OK);
}

/* Login to the WTI Network Power Switch (NPS) */

static int
NPSLogin(struct WTINPS * nps)
{
	char		IDinfo[128];
	char *		idptr = IDinfo;


	EXPECT(EscapeChar, 10);
	/* Look for the unit type info */
	if (ExpectToken(nps->rdfd, password, 2, IDinfo
	,	sizeof(IDinfo)) < 0) {
		syslog(LOG_ERR, _("No initial response from " DEVICE "."));
		NPSkillcomm(nps);
		return(errno == ETIME ? S_TIMEOUT : S_OOPS);
	}
	idptr += strspn(idptr, WHITESPACE);
	/*
	 * We should be looking at something like this:
     *	Enter Password: 
	 */

	SEND(nps->passwd);
	SEND("\r");

	/* Expect "Network Power Switch vX.YY" */

	switch (NPSLookFor(nps, LoginOK, 5)) {

		case 0:	/* Good! */
			break;

		case 1:	/* Uh-oh - bad password */
			syslog(LOG_ERR, _("Invalid password for " DEVICE "."));
			return(S_ACCESS);

		default:
			NPSkillcomm(nps);
			return(errno == ETIME ? S_TIMEOUT : S_OOPS);
	}

	return(S_OK);
}

/* Log out of the WTI NPS */

static int
NPSLogout(struct WTINPS* nps)
{
	int	rc;

	/* Make sure we're in the right menu... */
	SEND("\033\033\033\033");

	/* Expect "NPS>" */
	rc = NPSLookFor(nps, Prompt, 5);

	/* "/x" is Logout, "/x,y" auto-confirms */
	SEND("/x,y\r");

	close(nps->wrfd);
	close(nps->rdfd);
	nps->wrfd = nps->rdfd = -1;
	NPSkillcomm(nps);
	return(rc >= 0 ? S_OK : (errno == ETIME ? S_TIMEOUT : S_OOPS));
}
static void
NPSkillcomm(struct WTINPS* nps)
{
	if (nps->pid > 0) {
		kill(nps->pid, SIGKILL);
		(void)waitpid(nps->pid, NULL, 0);
		nps->pid = -1;
	}
}

/* Reset (power-cycle) the given outlets */
static int
NPSReset(struct WTINPS* nps, char * outlets, const char * rebootid)
{
	char		unum[32];


	SEND("\033\033\033\033\r");

	/* Make sure we're in the top level menu */

	/* Expect "NPS>" */
	EXPECT(Prompt, 5);

	/* Send REBOOT command for given outlet */
	snprintf(unum, sizeof(unum), "/BOOT %s,y\r", outlets);
	SEND(unum);

	/* Expect "Processing "... or "(Y/N)" (if confirmation turned on) */

	retry:
	switch (NPSLookFor(nps, Processing, 5)) {
		case 0: /* Got "Processing" Do nothing */
			break;

		case 1: /* Got that annoying command confirmation :-( */
			SEND("Y\r");
			goto retry;

		default: 
			return(errno == ETIME ? S_RESETFAIL : S_OOPS);
	}
	syslog(LOG_INFO, _("Host %s being rebooted."), rebootid);

	/* Expect "NPS>" */
	if (NPSLookFor(nps, Prompt, 10) < 0) {
		return(errno == ETIME ? S_RESETFAIL : S_OOPS);
	}

	/* All Right!  Power is back on.  Life is Good! */

	syslog(LOG_INFO, _("Power restored to host %s."), rebootid);

	return(S_OK);
}

#if defined(ST_POWERON) && defined(ST_POWEROFF)
static int
NPS_onoff(struct WTINPS* nps, char *outlets, const char * unitid, int req)
{
	char		unum[32];

	const char *	onoff = (req == ST_POWERON ? "/On" : "/Off");
	int	rc;


	if (NPS_connect_device(nps) != S_OK) {
		return(S_OOPS);
	}

	if ((rc = NPSLogin(nps) != S_OK)) {
		syslog(LOG_ERR, _("Cannot log into " DEVICE "."));
		return(rc);
	}
	SEND("\033\033\033\033\r");

	/* Make sure we're in the top level menu */

	/* Expect "NPS>" */
	EXPECT(Prompt, 5);

	/* Send ON/OFF command for given outlet */
	snprintf(unum, sizeof(unum), "%s %s,y\r", onoff, outlets);
	SEND(unum);

	/* Expect "Processing"... or "(Y/N)" (if confirmation turned on) */

	if (NPSLookFor(nps, Processing, 5) == 1) {
		/* They've turned on that annoying command confirmation :-( */
		SEND("Y\r");
	}
	
	EXPECT(Prompt, 10);

	/* All Right!  Command done. Life is Good! */
	syslog(LOG_NOTICE, _("Power to NPS outlet(s) %s turned %s."), outlets, onoff);
	/* Pop back to main menu */
	SEND("\033\033\033\033\r");
	return(S_OK);
}
#endif /* defined(ST_POWERON) && defined(ST_POWEROFF) */

/*
 *	Map the given host name into an (AC) Outlet number on the power strip
 */

static int
NPSNametoOutlet(struct WTINPS* nps, const char * name, char **outlets)
{
	char	NameMapping[128];
	int	sockno;
	char	sockname[32];
	int times = 0;
	char	buf[32];
	int left = 17;
	int ret = -1;

	
	if (*outlets != NULL) {
		free(*outlets);
		*outlets = NULL;
	}
	
	if ((*outlets = (char *)MALLOC(left*sizeof(char))) == NULL) {
		syslog(LOG_ERR, "out of memory");
		return(-1);
	}
	strncpy(*outlets, "", left);
	left = left - 1;	/* ensure terminating '\0' */

	/* Verify that we're in the top-level menu */
	SEND("\033\033\033\033\r");

	/* Expect "NPS>" */
	EXPECT(Prompt, 5);

	/* The status command output contains mapping of hosts to outlets */
	SEND("/s\r");

	/* Expect: "-----" so we can skip over it... */
	EXPECT(Separator, 5);
	EXPECT(CRNL, 5);

	/* Looks Good!  Parse the status output */

	do {
		times++;
		NameMapping[0] = EOS;
		SNARF(NameMapping, 5);
		if (sscanf(NameMapping
		,	"%d | %16c",&sockno, sockname) == 2) {

			char *	last = sockname+16;
			*last = EOS;
			--last;

			/* Strip off trailing blanks */
			for(; last > sockname; --last) {
				if (*last == ' ') {
					*last = EOS;
				}else{
					break;
				}
			}
			if (strcmp(name, sockname) == 0) {
				ret = sockno;
				sprintf(buf, "%d+", sockno);
				strncat(*outlets, buf, left);
				left = left - 2;
			}
		}
	} while (strlen(NameMapping) > 2 && times < 8 && left > 0);

	/* Pop back out to the top level menu */
	SEND("\033\033\033\033\r");
	return(ret);
}

int
st_status(Stonith  *s)
{
	struct WTINPS*	nps;
	int	rc;

	if (!ISWTINPS(s)) {
		syslog(LOG_ERR, "invalid argument to NPS_status");
		return(S_OOPS);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in NPS_status");
		return(S_OOPS);
	}
	nps = (struct WTINPS*) s->pinfo;
	if (NPS_connect_device(nps) != S_OK) {
		return(S_OOPS);
	}

	if ((rc = NPSLogin(nps) != S_OK)) {
		syslog(LOG_ERR, _("Cannot log into " DEVICE "."));
		return(rc);
	}

	/* Verify that we're in the top-level menu */
	SEND("\033\033\033\033\r");

	/* Expect "NPS>" */
	EXPECT(Prompt, 5);

	return(NPSLogout(nps));
}

/*
 *	Return the list of hosts (outlet names) for the devices on this NPS unit
 */

char **
st_hostlist(Stonith  *s)
{
	char		NameMapping[128];
	char*		NameList[64];
	unsigned int		numnames = 0;
	char **		ret = NULL;
	struct WTINPS*	nps;

	if (!ISWTINPS(s)) {
		syslog(LOG_ERR, "invalid argument to NPS_list_hosts");
		return(NULL);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in NPS_list_hosts");
		return(NULL);
	}
	nps = (struct WTINPS*) s->pinfo;

	if (NPS_connect_device(nps) != S_OK) {
		return(NULL);
	}

	if (NPSLogin(nps) != S_OK) {
		syslog(LOG_ERR, _("Cannot log into " DEVICE "."));
		return(NULL);
	}

	/* Verify that we're in the top-level menu */
	SEND("\033\033\033\033\r");

	/* Expect "NPS>" */
	NULLEXPECT(Prompt, 5);

	/* The status command output contains mapping of hosts to outlets */
	SEND("/s\r");

	/* Expect: "-----" so we can skip over it... */
	NULLEXPECT(Separator, 5);
	NULLEXPECT(CRNL, 5);

	/* Looks Good!  Parse the status output */

	do {
		int	sockno;
		char	sockname[64];
		NameMapping[0] = EOS;
		NULLSNARF(NameMapping, 5);
		if (sscanf(NameMapping
		,	"%d | %16c",&sockno, sockname) == 2) {

			char *	last = sockname+16;
			char *	nm;
			*last = EOS;
			--last;

			/* Strip off trailing blanks */
			for(; last > sockname; --last) {
				if (*last == ' ') {
					*last = EOS;
				}else{
					break;
				}
			}
			if (numnames >= DIMOF(NameList)-1) {
				break;
			}
			if ((nm = (char*)MALLOC(strlen(sockname)+1)) == NULL) {
				syslog(LOG_ERR, "out of memory");
				return(NULL);
			}
			strcpy(nm, sockname);
			NameList[numnames] = nm;
			++numnames;
			NameList[numnames] = NULL;
		}
	} while (strlen(NameMapping) > 2);

	/* Pop back out to the top level menu */
	SEND("\033\033\033\033\r");
	if (numnames >= 1) {
		ret = (char **)MALLOC(numnames*sizeof(char*));
		if (ret == NULL) {
			syslog(LOG_ERR, "out of memory");
		}else{
			memcpy(ret, NameList, numnames*sizeof(char*));
		}
	}
	(void)NPSLogout(nps);
	return(ret);
}

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
 *	Parse the given configuration information, and stash it away...
 */

static int
NPS_parse_config_info(struct WTINPS* nps, const char * info)
{
	static char dev[1024];
	static char passwd[1024];

	if (nps->config) {
		return(S_OOPS);
	}


	if (sscanf(info, "%s %[^\n\r\t]", dev, passwd) == 2
	&&	strlen(passwd) > 1) {

		if ((nps->device = (char *)MALLOC(strlen(dev)+1)) == NULL) {
			syslog(LOG_ERR, "out of memory");
			return(S_OOPS);
		}
		if ((nps->passwd = (char *)MALLOC(strlen(passwd)+1)) == NULL) {
			free(nps->device);
			nps->device=NULL;
			syslog(LOG_ERR, "out of memory");
			return(S_OOPS);
		}
		strcpy(nps->device, dev);
		strcpy(nps->passwd, passwd);
		nps->config = 1;
		return(S_OK);
	}
	return(S_BADCONFIG);
}

/*
 *	Connect to the given NPS device.  We should add serial support here
 *	eventually...
 */
static int
NPS_connect_device(struct WTINPS * nps)
{
	char	TelnetCommand[256];

	snprintf(TelnetCommand, sizeof(TelnetCommand)
	,	"exec telnet %s 2>/dev/null", nps->device);

	nps->pid=StartProcess(TelnetCommand, &nps->rdfd, &nps->wrfd);
	if (nps->pid <= 0) {
		return(S_OOPS);
	}
	return(S_OK);
}

/*
 *	Reset the given host on this Stonith device.  
 */
int
st_reset(Stonith * s, int request, const char * host)
{
	int	rc = 0;
	int	lorc = 0;
	struct WTINPS*	nps;

	if (!ISWTINPS(s)) {
		syslog(LOG_ERR, "invalid argument to NPS_reset_host");
		return(S_OOPS);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in NPS_reset_host");
		return(S_OOPS);
	}
	nps = (struct WTINPS*) s->pinfo;

	if ((rc = NPS_connect_device(nps)) != S_OK) {
		return(rc);
	}

	if ((rc = NPSLogin(nps)) != S_OK) {
		syslog(LOG_ERR, _("Cannot log into " DEVICE "."));
	}else{
		char *outlets;
		int noutlet;
		noutlet = NPSNametoOutlet(nps, host, &outlets);

		if (noutlet < 1) {
			syslog(LOG_WARNING, _("%s %s "
			"doesn't control host [%s]."), nps->idinfo
			,	nps->unitid, host);
			NPSkillcomm(nps);
			return(S_BADHOST);
		}
		switch(request) {

#if defined(ST_POWERON) && defined(ST_POWEROFF)
		case ST_POWERON:
		case ST_POWEROFF:
			rc = NPS_onoff(nps, outlets, host, request);
			break;
#endif
		case ST_GENERIC_RESET:
			rc = NPSReset(nps, outlets, host);
			break;
		default:
			rc = S_INVAL;
			break;
		}
	}

	lorc = NPSLogout(nps);
	NPSkillcomm(nps);

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

	char	WTINPSid[256];

	struct WTINPS*	nps;

	if (!ISWTINPS(s)) {
		syslog(LOG_ERR, "invalid argument to NPS_set_configfile");
		return(S_OOPS);
	}
	nps = (struct WTINPS*) s->pinfo;

	if ((cfgfile = fopen(configname, "r")) == NULL)  {
		syslog(LOG_ERR, _("Cannot open %s"), configname);
		return(S_BADCONFIG);
	}
	while (fgets(WTINPSid, sizeof(WTINPSid), cfgfile) != NULL){
		if (*WTINPSid == '#' || *WTINPSid == '\n' || *WTINPSid == EOS) {
			continue;
		}
		return(NPS_parse_config_info(nps, WTINPSid));
	}
	return(S_BADCONFIG);
}

/*
 *	Parse the config information in the given string, and stash it away...
 */
int
st_setconfinfo(Stonith* s, const char * info)
{
	struct WTINPS* nps;

	if (!ISWTINPS(s)) {
		syslog(LOG_ERR, "NPS_provide_config_info: invalid argument");
		return(S_OOPS);
	}
	nps = (struct WTINPS *)s->pinfo;

	return(NPS_parse_config_info(nps, info));
}
const char *
st_getinfo(Stonith * s, int reqtype)
{
	struct WTINPS* nps;
	char *		ret;

	if (!ISWTINPS(s)) {
		syslog(LOG_ERR, "NPS_idinfo: invalid argument");
		return NULL;
	}
	/*
	 *	We look in the ST_TEXTDOMAIN catalog for our messages
	 */
	nps = (struct WTINPS *)s->pinfo;

	switch (reqtype) {
		case ST_DEVICEID:
			ret = nps->idinfo;
			break;

		case ST_CONF_INFO_SYNTAX:
			ret = _("IP-address password\n"
			"The IP-address and password are white-space delimited.");
			break;

		case ST_CONF_FILE_SYNTAX:
			ret = _("IP-address password\n"
			"The IP-address and password are white-space delimited.  "
			"All three items must be on one line.  "
			"Blank lines and lines beginning with # are ignored");
			break;

		default:
			ret = NULL;
			break;
	}
	return ret;
}

/*
 *	Baytech Stonith destructor...
 */
void
st_destroy(Stonith *s)
{
	struct WTINPS* nps;

	if (!ISWTINPS(s)) {
		syslog(LOG_ERR, "wtinps_del: invalid argument");
		return;
	}
	nps = (struct WTINPS *)s->pinfo;

	nps->NPSid = NOTnpsid;
	NPSkillcomm(nps);
	if (nps->rdfd >= 0) {
		nps->rdfd = -1;
		close(nps->rdfd);
	}
	if (nps->wrfd >= 0) {
		close(nps->wrfd);
		nps->wrfd = -1;
	}
	if (nps->device != NULL) {
		FREE(nps->device);
		nps->device = NULL;
	}
	if (nps->passwd != NULL) {
		FREE(nps->passwd);
		nps->passwd = NULL;
	}
	if (nps->idinfo != NULL) {
		FREE(nps->idinfo);
		nps->idinfo = NULL;
	}
	if (nps->unitid != NULL) {
		FREE(nps->unitid);
		nps->unitid = NULL;
	}
}

/* Create a new BayTech Stonith device. */

void *
st_new(void)
{
	struct WTINPS*	nps = MALLOCT(struct WTINPS);

	if (nps == NULL) {
		syslog(LOG_ERR, "out of memory");
		return(NULL);
	}
	memset(nps, 0, sizeof(*nps));
	nps->NPSid = NPSid;
	nps->pid = -1;
	nps->rdfd = -1;
	nps->wrfd = -1;
	nps->config = 0;
	nps->device = NULL;
	nps->passwd = NULL;
	nps->idinfo = NULL;
	nps->unitid = NULL;
	REPLSTR(nps->idinfo, DEVICE);
	REPLSTR(nps->unitid, "unknown");

	return((void *)nps);
}
