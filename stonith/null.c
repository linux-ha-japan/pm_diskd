/*
 * Stonith module for NULL Stonith device
 *
 * Copyright (c) 2000 Alan Robertson <alanr@unix.sh>
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

#define	DEVICE	"NULL STONITH device"
#define WHITESPACE	" \t\n\r\f"

/*
 *	Null STONITH device.  We are very agreeable, but don't do much :-)
 */

struct NullDevice {
	const char *	NULLid;
	char **		hostlist;
	int		hostcount;
};

static const char * NULLid = "NullDevice-Stonith";
static const char * NOTnullID = "Hey, dummy this has been destroyed (NullDev)";

#define	ISNULLDEV(i)	(((i)!= NULL && (i)->pinfo != NULL)	\
	&& ((struct NullDevice *)(i->pinfo))->NULLid == NULLid)


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


#define N_(text)	(text)
#define _(text)		dgettext(ST_TEXTDOMAIN, text)

const char *
	st_getinfo(Stonith * s, int InfoType);
char **	st_hostlist(Stonith  *);
void	st_freehostlist(char **);
int	st_status(Stonith * );
int	st_reset(Stonith * s, int request, const char * host);
void	st_destroy(Stonith *);
int		WordCount(const char * s);
void *	st_new(void);
int st_setconffile(Stonith* s, const char * configname);
int st_setconfinfo(Stonith* s, const char * info);


int
st_status(Stonith  *s)
{

	if (!ISNULLDEV(s)) {
		syslog(LOG_ERR, "invalid argument to NULL_status");
		return(S_OOPS);
	}
	return S_OK;
}


/*
 *	Return the list of hosts configured for this NULL device
 */

char **
st_hostlist(Stonith  *s)
{
	int		numnames = 0;
	char **		ret = NULL;
	struct NullDevice*	nd;
	int		j;

	if (!ISNULLDEV(s)) {
		syslog(LOG_ERR, "invalid argument to NULL_list_hosts");
		return(NULL);
	}
	nd = (struct NullDevice*) s->pinfo;
	if (nd->hostcount < 0) {
		syslog(LOG_ERR
		,	"unconfigured stonith object in NULL_list_hosts");
		return(NULL);
	}
	numnames = nd->hostcount;

	ret = (char **)MALLOC(numnames*sizeof(char*));
	if (ret == NULL) {
		syslog(LOG_ERR, "out of memory");
		return ret;
	}

	memset(ret, 0, numnames*sizeof(char*));

	for (j=0; j < numnames-1; ++j) {
		ret[j] = MALLOC(strlen(nd->hostlist[j])+1);
		if (ret[j] == NULL) {
			st_freehostlist(ret);
			ret = NULL;
			return ret;
		}
		strcpy(ret[j], nd->hostlist[j]);
	}
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
	hlist = NULL;
}


int
WordCount(const char * s)
{
	int	wc = 0;
	if (!s) {
		return wc;
	}
	do {
		s += strspn(s, WHITESPACE);
		if (*s)  {
			++wc;
			s += strcspn(s, WHITESPACE);
		}
	}while (*s);

	return(wc);
}

/*
 *	Parse the config information, and stash it away...
 */

static int
NULL_parse_config_info(struct NullDevice* nd, const char * info)
{
	char **			ret;
	int			wc;
	int			numnames;
	const char *		s = info;
	int			j;

	if (nd->hostcount >= 0) {
		return(S_OOPS);
	}

	wc = WordCount(info);
	numnames = wc + 1;

	ret = (char **)MALLOC(numnames*sizeof(char*));
	if (ret == NULL) {
		syslog(LOG_ERR, "out of memory");
		return S_OOPS;
	}

	memset(ret, 0, numnames*sizeof(char*));

	for (j=0; j < wc; ++j) {
		s += strspn(s, WHITESPACE);
		if (*s)  {
			const char *	start = s;
			s += strcspn(s, WHITESPACE);
			ret[j] = MALLOC((1+(s-start))*sizeof(char));
			if (ret[j] == NULL) {
				st_freehostlist(ret);
				ret = NULL;
				return S_OOPS;
			}
			strncpy(ret[j], start, (s-start));
		}
	}
	nd->hostlist = ret;
	nd->hostcount = numnames;
	return(S_OK);
}


/*
 *	Pretend to reset the given host on this Stonith device.
 *	(we don't even error check the "request" type)
 */
int
st_reset(Stonith * s, int request, const char * host)
{

	if (!ISNULLDEV(s)) {
		syslog(LOG_ERR, "invalid argument to %s", __FUNCTION__);
		return(S_OOPS);
	}
	syslog(LOG_INFO, _("Host %s null-reset."), host);
	return S_OK;
}

/*
 *	Parse the information in the given configuration file,
 *	and stash it away...
 */
int
st_setconffile(Stonith* s, const char * configname)
{
	FILE *	cfgfile;

	char	NULLline[256];

	struct NullDevice*	nd;

	if (!ISNULLDEV(s)) {
		syslog(LOG_ERR, "invalid argument to NULL_set_configfile");
		return(S_OOPS);
	}
	nd = (struct NullDevice*) s->pinfo;

	if ((cfgfile = fopen(configname, "r")) == NULL)  {
		syslog(LOG_ERR, "Cannot open %s", configname);
		return(S_BADCONFIG);
	}
	while (fgets(NULLline, sizeof(NULLline), cfgfile) != NULL){
		if (*NULLline == '#' || *NULLline == '\n' || *NULLline == EOS) {
			continue;
		}
		return(NULL_parse_config_info(nd, NULLline));
	}
	return(S_BADCONFIG);
}

/*
 *	Parse the config information in the given string, and stash it away...
 */
int
st_setconfinfo(Stonith* s, const char * info)
{
	struct NullDevice* nd;

	if (!ISNULLDEV(s)) {
		syslog(LOG_ERR, "%s: invalid argument", __FUNCTION__);
		return(S_OOPS);
	}
	nd = (struct NullDevice *)s->pinfo;

	return(NULL_parse_config_info(nd, info));
}

const char *
st_getinfo(Stonith * s, int reqtype)
{
	struct NullDevice* nd;
	char *		ret;

	if (!ISNULLDEV(s)) {
		syslog(LOG_ERR, "NULL_idinfo: invalid argument");
		return NULL;
	}
	/*
	 *	We look in the ST_TEXTDOMAIN catalog for our messages
	 */
	nd = (struct NullDevice *)s->pinfo;

	switch (reqtype) {
		case ST_DEVICEID:
			ret = _("null STONITH device");
			break;

		case ST_CONF_INFO_SYNTAX:
			ret = _("hostname ...\n"
			"host names are white-space delimited.");
			break;

		case ST_CONF_FILE_SYNTAX:
			ret = _("IP-address login password\n"
			"host names are white-space delimited.  "
			"All host names must be on one line.  "
			"Blank lines and lines beginning with # are ignored");
			break;

		default:
			ret = NULL;
			break;
	}
	return ret;
}

/*
 *	NULL Stonith destructor...
 */
void
st_destroy(Stonith *s)
{
	struct NullDevice* nd;

	if (!ISNULLDEV(s)) {
		syslog(LOG_ERR, "%s: invalid argument", __FUNCTION__);
		return;
	}
	nd = (struct NullDevice *)s->pinfo;

	nd->NULLid = NOTnullID;
	if (nd->hostlist) {
		st_freehostlist(nd->hostlist);
		nd->hostlist = NULL;
	}
	nd->hostcount = -1;
	FREE(nd);
	s->pinfo = NULL;
	FREE(s);
	s = NULL;
}

/* Create a new Null Stonith device.  Too bad this function can't be static */
void *
st_new(void)
{
	struct NullDevice*	nd = MALLOCT(struct NullDevice);

	if (nd == NULL) {
		syslog(LOG_ERR, "out of memory");
		return(NULL);
	}
	memset(nd, 0, sizeof(*nd));
	nd->NULLid = NULLid;
	nd->hostlist = NULL;
	nd->hostcount = -1;
	return((void *)nd);
}
