/*
 *	Stonith module for APC Master Switch (AP9211)
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

#define	DEVICE	"APC MasterSwitch"

#define N_(text)	(text)
#define _(text)		dgettext(ST_TEXTDOMAIN, text)

/*
 *	I have an AP9211.  This code has been tested with this switch.
 */

struct APCMS {
	const char *	MSid;
	char *		idinfo;
	char *		unitid;
	pid_t		pid;
	int		rdfd;
	int		wrfd;
	int		config;
	char *		device;
	char *		user;
	char *		passwd;
};

static const char * MSid = "APCMS-Stonith";
static const char * NOTmsid = "Hey dummy, this has been destroyed (APCMS)";

#define	ISAPCMS(i)	(((i)!= NULL && (i)->pinfo != NULL)	\
	&& ((struct APCMS *)(i->pinfo))->MSid == MSid)

#define	ISCONFIGED(i)	(ISAPCMS(i) && ((struct APCMS *)(i->pinfo))->config)

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
 *	Different expect strings that we get from the APC MasterSwitch
 */

#define APCMSSTR	"American Power Conversion"

static struct Etoken EscapeChar[] =	{ {"Escape character is '^]'.", 0, 0}
					,	{NULL,0,0}};
static struct Etoken login[] = 		{ {"User Name :", 0, 0}, {NULL,0,0}};
static struct Etoken password[] =	{ {"Password  :", 0, 0} ,{NULL,0,0}};
static struct Etoken Prompt[] =	{ {">", 0, 0} ,{NULL,0,0}};
static struct Etoken LoginOK[] =	{ {APCMSSTR, 0, 0}
                    , {"User Name :", 1, 0} ,{NULL,0,0}};
static struct Etoken Separator[] =	{ {"-----", 0, 0} ,{NULL,0,0}};
/* Accept either a CR/NL or an NL/CR */
static struct Etoken CRNL[] =		{ {"\n\r",0,0},{"\r\n",0,0},{NULL,0,0}};

/* We may get a notice about rebooting, or a request for confirmation */
static struct Etoken Processing[] =	{ {"Press <ENTER> to continue", 0, 0}
				,	{"Enter 'YES' to continue", 1, 0}
				,	{NULL,0,0}};

static int	MSLookFor(struct APCMS* ms, struct Etoken * tlist, int timeout);
static int	MS_connect_device(struct APCMS * ms);
static int	MSLogin(struct APCMS * ms);
static int	MSNametoOutlet(struct APCMS*, const char * name, char **outlets);
static int	MSReset(struct APCMS*, char * outlets, const char * rebootid);
static int	MSScanLine(struct APCMS* ms, int timeout, char * buf, int max);
static int	MSLogout(struct APCMS * ms);
static void	MSkillcomm(struct APCMS * ms);

int  st_setconffile(Stonith *, const char * cfgname);
int	st_setconfinfo(Stonith *, const char * info);
static int	MS_parse_config_info(struct APCMS* ms, const char * info);
const char *
		st_getinfo(Stonith * s, int InfoType);

char **	st_hostlist(Stonith  *);
void	st_freehostlist(char **);
int	st_status(Stonith * );
int	st_reset(Stonith * s, int request, const char * host);
#if defined(ST_POWERON) && defined(ST_POWEROFF)
static int	MS_onoff(struct APCMS*, char * outlets, const char * unitid
,		int request);
#endif
void	st_destroy(Stonith *);
void *	st_new(void);

/*
 *	We do these things a lot.  Here are a few shorthand macros.
 */

#define	SEND(s)	(write(ms->wrfd, (s), strlen(s)))

#define	EXPECT(p,t)	{						\
			if (MSLookFor(ms, p, t) < 0)			\
				return(errno == ETIME			\
			?	S_TIMEOUT : S_OOPS);			\
			}

#define	NULLEXPECT(p,t)	{						\
				if (MSLookFor(ms, p, t) < 0)		\
					return(NULL);			\
			}

#define	SNARF(s, to)	{						\
				if (MSScanLine(ms,to,(s),sizeof(s))	\
				!=	S_OK)				\
					return(S_OOPS);			\
			}

#define	NULLSNARF(s, to)	{					\
				if (MSScanLine(ms,to,(s),sizeof(s))	\
				!=	S_OK)				\
					return(NULL);			\
				}

/* Look for any of the given patterns.  We don't care which */

static int
MSLookFor(struct APCMS* ms, struct Etoken * tlist, int timeout)
{
	int	rc;
	if ((rc = ExpectToken(ms->rdfd, tlist, timeout, NULL, 0)) < 0) {
		syslog(LOG_ERR, _("Did not find string: '%s' from" DEVICE ".")
		,	tlist[0].string);
		MSkillcomm(ms);
		return(-1);
	}
	return(rc);
}

/* Read and return the rest of the line */

static int
MSScanLine(struct APCMS* ms, int timeout, char * buf, int max)
{
	if (ExpectToken(ms->rdfd, CRNL, timeout, buf, max) < 0) {
		syslog(LOG_ERR, ("Could not read line from " DEVICE "."));
		MSkillcomm(ms);
		ms->pid = -1;
		return(S_OOPS);
	}
	return(S_OK);
}

/* Login to the APC Master Switch */

static int
MSLogin(struct APCMS * ms)
{
	char		IDinfo[128];
	char *		idptr = IDinfo;


	EXPECT(EscapeChar, 10);
	/* Look for the unit type info */
	if (ExpectToken(ms->rdfd, password, 2, IDinfo
	,	sizeof(IDinfo)) < 0) {
		syslog(LOG_ERR, _("No initial response from " DEVICE "."));
		MSkillcomm(ms);
		return(errno == ETIME ? S_TIMEOUT : S_OOPS);
	}
	idptr += strspn(idptr, WHITESPACE);
	/*
	 * We should be looking at something like this:
     *	User Name :
	 */

	EXPECT(login, 2);
	SEND(ms->user);
	SEND("\r");

	/* Expect "Password  :" */
	EXPECT(password, 5);
	SEND(ms->passwd);
	SEND("\r");

	/* Expect "American Power Conversion" */

	switch (MSLookFor(ms, LoginOK, 5)) {

		case 0:	/* Good! */
			break;

		case 1:	/* Uh-oh - bad password */
			syslog(LOG_ERR, _("Invalid password for " DEVICE "."));
			return(S_ACCESS);

		default:
			MSkillcomm(ms);
			return(errno == ETIME ? S_TIMEOUT : S_OOPS);
	}

	return(S_OK);
}

/* Log out of the APC Master Switch */

static int
MSLogout(struct APCMS* ms)
{
	int	rc;

	/* Make sure we're in the right menu... */
	SEND("\033\033\033\033\033\033\033");

	/* Expect ">" */
	rc = MSLookFor(ms, Prompt, 5);

	/* "4" is logout */
	SEND("4\r");

	close(ms->wrfd);
	close(ms->rdfd);
	ms->wrfd = ms->rdfd = -1;
	MSkillcomm(ms);
	return(rc >= 0 ? S_OK : (errno == ETIME ? S_TIMEOUT : S_OOPS));
}
static void
MSkillcomm(struct APCMS* ms)
{
	if (ms->pid > 0) {
		kill(ms->pid, SIGKILL);
		(void)waitpid(ms->pid, NULL, 0);
		ms->pid = -1;
	}
}

/* Reset (power-cycle) the given outlets */
static int
MSReset(struct APCMS* ms, char * outlets, const char * rebootid)
{
	char		unum[32];


	/* Make sure we're in the top level menu */
	SEND("\033\033\033\033\033\033\033\r");

	/* Expect ">" */
	EXPECT(Prompt, 5);

	/* Request menu 1 (Device Control) */
	SEND("1\r");

	/* Select requested outlet */
	snprintf(unum, sizeof(unum), "%s\r", outlets);
	SEND(unum);

	/* Select menu 1 (Control Outlet) */
	SEND("1\r");

	/* Select menu 3 (Immediate Reboot) */
	SEND("3\r");

	/* Expect "Press <ENTER> " or "Enter 'YES'" (if confirmation turned on) */
	retry:
	switch (MSLookFor(ms, Processing, 5)) {
		case 0: /* Got "Press <ENTER>" Do so */
			SEND("\r");
			break;

		case 1: /* Got that annoying command confirmation :-( */
			SEND("YES\r");
			goto retry;

		default: 
			return(errno == ETIME ? S_RESETFAIL : S_OOPS);
	}
	syslog(LOG_INFO, _("Host %s being rebooted."), rebootid);

	/* Expect ">" */
	if (MSLookFor(ms, Prompt, 10) < 0) {
		return(errno == ETIME ? S_RESETFAIL : S_OOPS);
	}

	/* All Right!  Power is back on.  Life is Good! */

	syslog(LOG_INFO, _("Power restored to host %s."), rebootid);

	/* Return to top level menu */
	SEND("\033\033\033\033\033\r");

	return(S_OK);
}

#if defined(ST_POWERON) && defined(ST_POWEROFF)
static int
MS_onoff(struct APCMS* ms, char *outlets, const char * unitid, int req)
{
	char		unum[32];

	const char *	onoff = (req == ST_POWERON ? "1\r" : "2\r");
	int	rc;


	if (MS_connect_device(ms) != S_OK) {
		return(S_OOPS);
	}

	if ((rc = MSLogin(ms) != S_OK)) {
		syslog(LOG_ERR, _("Cannot log into " DEVICE "."));
		return(rc);
	}
	
	/* Make sure we're in the top level menu */
	SEND("\033\033\033\033\033\033\033\033\r");

	/* Expect ">" */
	EXPECT(Prompt, 5);

	/* Request menu 1 (Device Control) */
	SEND("1\r");

	/* Select requested outlet */
	snprintf(unum, sizeof(unum), "%s\r", outlets);
	SEND(unum);

	/* Select menu 1 (Control Outlet) */
	SEND("1\r");

	/* Send ON/OFF command for given outlet */
	SEND(onoff);

	/* Expect "Press <ENTER> " or "Enter 'YES'" (if confirmation turned on) */
	retry:
	switch (MSLookFor(ms, Processing, 5)) {
		case 0: /* Got "Press <ENTER>" Do so */
			SEND("\r");
			break;

		case 1: /* Got that annoying command confirmation :-( */
			SEND("YES\r");
			goto retry;

		default: 
			return(errno == ETIME ? S_RESETFAIL : S_OOPS);
	}
	
	EXPECT(Prompt, 10);

	/* All Right!  Command done. Life is Good! */
	syslog(LOG_NOTICE, _("Power to MS outlet(s) %s turned %s."), outlets, onoff);
	/* Pop back to main menu */
	SEND("\033\033\033\033\033\033\033\r");
	return(S_OK);
}
#endif /* defined(ST_POWERON) && defined(ST_POWEROFF) */

/*
 *	Map the given host name into an (AC) Outlet number on the power strip
 */

static int
MSNametoOutlet(struct APCMS* ms, const char * name, char **outlets)
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
	SEND("\033\033\033\033\033\033\033\r");

	/* Expect ">" */
	EXPECT(Prompt, 5);
	
	/* Request menu 1 (Device Control) */
	SEND("1\r");

	/* Expect: "-----" so we can skip over it... */
	EXPECT(Separator, 5);
	EXPECT(CRNL, 5);
	EXPECT(CRNL, 5);

	/* Looks Good!  Parse the status output */

	do {
		times++;
		NameMapping[0] = EOS;
		SNARF(NameMapping, 5);
		if (sscanf(NameMapping
		,	"%d- %23c",&sockno, sockname) == 2) {

			char *	last = sockname+23;
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
				sprintf(buf, "%d ", sockno);
				strncat(*outlets, buf, left);
				left = left - 2;
			}
		}
	} while (strlen(NameMapping) > 2 && times < 8 && left > 0);

	/* Pop back out to the top level menu */
	SEND("\033\033\033\033\033\033\r");
	return(ret);
}

int
st_status(Stonith  *s)
{
	struct APCMS*	ms;
	int	rc;

	if (!ISAPCMS(s)) {
		syslog(LOG_ERR, "invalid argument to MS_status");
		return(S_OOPS);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in MS_status");
		return(S_OOPS);
	}
	ms = (struct APCMS*) s->pinfo;
	if (MS_connect_device(ms) != S_OK) {
		return(S_OOPS);
	}

	if ((rc = MSLogin(ms) != S_OK)) {
		syslog(LOG_ERR, _("Cannot log into " DEVICE "."));
		return(rc);
	}

	/* Verify that we're in the top-level menu */
	SEND("\033\033\033\033\033\033\033\033\r");

	/* Expect ">" */
	EXPECT(Prompt, 5);

	return(MSLogout(ms));
}

/*
 *	Return the list of hosts (outlet names) for the devices on this MS unit
 */

char **
st_hostlist(Stonith  *s)
{
	char		NameMapping[128];
	char*		NameList[64];
	unsigned int	numnames = 0;
	char **		ret = NULL;
	struct APCMS*	ms;

	if (!ISAPCMS(s)) {
		syslog(LOG_ERR, "invalid argument to MS_list_hosts");
		return(NULL);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in MS_list_hosts");
		return(NULL);
	}
	ms = (struct APCMS*) s->pinfo;

	if (MS_connect_device(ms) != S_OK) {
		return(NULL);
	}

	if (MSLogin(ms) != S_OK) {
		syslog(LOG_ERR, _("Cannot log into " DEVICE "."));
		return(NULL);
	}

	/* Verify that we're in the top-level menu */
	SEND("\033\033\033\033\033\033\033\r");

	/* Expect ">" */
	NULLEXPECT(Prompt, 5);

	/* Request menu 1 (Device Control) */
	SEND("1\r");

	/* Expect: "-----" so we can skip over it... */
	NULLEXPECT(Separator, 5);
	NULLEXPECT(CRNL, 5);
	NULLEXPECT(CRNL, 5);

	/* Looks Good!  Parse the status output */
	do {
		int	sockno;
		char	sockname[64];
		NameMapping[0] = EOS;
		NULLSNARF(NameMapping, 5);
		if (sscanf(NameMapping
		,	"%d- %23c",&sockno, sockname) == 2) {

			char *	last = sockname+23;
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
	SEND("\033\033\033\033\033\033\033\r");
	if (numnames >= 1) {
		ret = (char **)MALLOC((numnames+1)*sizeof(char*));
		if (ret == NULL) {
			syslog(LOG_ERR, "out of memory");
		}else{
			memcpy(ret, NameList, (numnames+1)*sizeof(char*));
		}
	}
	(void)MSLogout(ms);
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
MS_parse_config_info(struct APCMS* ms, const char * info)
{
	static char dev[1024];
	static char user[1024];
	static char passwd[1024];

	if (ms->config) {
		return(S_OOPS);
	}


	if (sscanf(info, "%s %[^\n\r\t]", dev, passwd) == 2
	&&	strlen(passwd) > 1) {

		if ((ms->device = (char *)MALLOC(strlen(dev)+1)) == NULL) {
			syslog(LOG_ERR, "out of memory");
			return(S_OOPS);
		}
		if ((ms->user = (char *)MALLOC(strlen(user)+1)) == NULL) {
			free(ms->device);
			ms->device=NULL;
			syslog(LOG_ERR, "out of memory");
			return(S_OOPS);
		}
		if ((ms->passwd = (char *)MALLOC(strlen(passwd)+1)) == NULL) {
			free(ms->device);
			ms->device=NULL;
			syslog(LOG_ERR, "out of memory");
			return(S_OOPS);
		}
		strcpy(ms->device, dev);
		strcpy(ms->user, user);
		strcpy(ms->passwd, passwd);
		ms->config = 1;
		return(S_OK);
	}
	return(S_BADCONFIG);
}

/*
 *	Connect to the given MS device.  We should add serial support here
 *	eventually...
 */
static int
MS_connect_device(struct APCMS * ms)
{
	char	TelnetCommand[256];

	snprintf(TelnetCommand, sizeof(TelnetCommand)
	,	"exec telnet %s 2>/dev/null", ms->device);

	ms->pid=StartProcess(TelnetCommand, &ms->rdfd, &ms->wrfd);
	if (ms->pid <= 0) {
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
	struct APCMS*	ms;

	if (!ISAPCMS(s)) {
		syslog(LOG_ERR, "invalid argument to MS_reset_host");
		return(S_OOPS);
	}
	if (!ISCONFIGED(s)) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in MS_reset_host");
		return(S_OOPS);
	}
	ms = (struct APCMS*) s->pinfo;

	if ((rc = MS_connect_device(ms)) != S_OK) {
		return(rc);
	}

	if ((rc = MSLogin(ms)) != S_OK) {
		syslog(LOG_ERR, _("Cannot log into " DEVICE "."));
	}else{
		char *outlets;
		int noutlet;
		noutlet = MSNametoOutlet(ms, host, &outlets);

		if (noutlet < 1) {
			syslog(LOG_WARNING, _("%s %s "
			"doesn't control host [%s]."), ms->idinfo
			,	ms->unitid, host);
			MSkillcomm(ms);
			return(S_BADHOST);
		}
		switch(request) {

#if defined(ST_POWERON) && defined(ST_POWEROFF)
		case ST_POWERON:
		case ST_POWEROFF:
			rc = MS_onoff(ms, outlets, host, request);
			break;
#endif
		case ST_GENERIC_RESET:
			rc = MSReset(ms, outlets, host);
			break;
		default:
			rc = S_INVAL;
			break;
		}
	}

	lorc = MSLogout(ms);
	MSkillcomm(ms);

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

	char	APCMSid[256];

	struct APCMS*	ms;

	if (!ISAPCMS(s)) {
		syslog(LOG_ERR, "invalid argument to MS_set_configfile");
		return(S_OOPS);
	}
	ms = (struct APCMS*) s->pinfo;

	if ((cfgfile = fopen(configname, "r")) == NULL)  {
		syslog(LOG_ERR, _("Cannot open %s"), configname);
		return(S_BADCONFIG);
	}
	while (fgets(APCMSid, sizeof(APCMSid), cfgfile) != NULL){
		if (*APCMSid == '#' || *APCMSid == '\n' || *APCMSid == EOS) {
			continue;
		}
		return(MS_parse_config_info(ms, APCMSid));
	}
	return(S_BADCONFIG);
}

/*
 *	Parse the config information in the given string, and stash it away...
 */
int
st_setconfinfo(Stonith* s, const char * info)
{
	struct APCMS* ms;

	if (!ISAPCMS(s)) {
		syslog(LOG_ERR, "MS_provide_config_info: invalid argument");
		return(S_OOPS);
	}
	ms = (struct APCMS *)s->pinfo;

	return(MS_parse_config_info(ms, info));
}
const char *
st_getinfo(Stonith * s, int reqtype)
{
	struct APCMS* ms;
	char *		ret;

	if (!ISAPCMS(s)) {
		syslog(LOG_ERR, "MS_idinfo: invalid argument");
		return NULL;
	}
	/*
	 *	We look in the ST_TEXTDOMAIN catalog for our messages
	 */
	ms = (struct APCMS *)s->pinfo;

	switch (reqtype) {
		case ST_DEVICEID:
			ret = ms->idinfo;
			break;

		case ST_CONF_INFO_SYNTAX:
			ret = _("IP-address login password\n"
			"The IP-address, login and password are white-space delimited.");
			break;

		case ST_CONF_FILE_SYNTAX:
			ret = _("IP-address login password\n"
			"The IP-address, login and password are white-space delimited.  "
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
	struct APCMS* ms;

	if (!ISAPCMS(s)) {
		syslog(LOG_ERR, "apcms_del: invalid argument");
		return;
	}
	ms = (struct APCMS *)s->pinfo;

	ms->MSid = NOTmsid;
	MSkillcomm(ms);
	if (ms->rdfd >= 0) {
		ms->rdfd = -1;
		close(ms->rdfd);
	}
	if (ms->wrfd >= 0) {
		close(ms->wrfd);
		ms->wrfd = -1;
	}
	if (ms->device != NULL) {
		FREE(ms->device);
		ms->device = NULL;
	}
	if (ms->user != NULL) {
		FREE(ms->user);
		ms->user = NULL;
	}
	if (ms->passwd != NULL) {
		FREE(ms->passwd);
		ms->passwd = NULL;
	}
	if (ms->idinfo != NULL) {
		FREE(ms->idinfo);
		ms->idinfo = NULL;
	}
	if (ms->unitid != NULL) {
		FREE(ms->unitid);
		ms->unitid = NULL;
	}
}

/* Create a new BayTech Stonith device. */

void *
st_new(void)
{
	struct APCMS*	ms = MALLOCT(struct APCMS);

	if (ms == NULL) {
		syslog(LOG_ERR, "out of memory");
		return(NULL);
	}
	memset(ms, 0, sizeof(*ms));
	ms->MSid = MSid;
	ms->pid = -1;
	ms->rdfd = -1;
	ms->wrfd = -1;
	ms->config = 0;
	ms->user = NULL;
	ms->device = NULL;
	ms->passwd = NULL;
	ms->idinfo = NULL;
	ms->unitid = NULL;
	REPLSTR(ms->idinfo, DEVICE);
	REPLSTR(ms->unitid, "unknown");

	return((void *)ms);
}
