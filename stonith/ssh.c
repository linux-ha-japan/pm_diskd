/*
 * Stonith module for SSH Stonith device
 *
 * Copyright (c) 2001 SuSE Linux AG
 *
 * Authors: Joachim Gleissner <jg@suse.de>, Lars Marowsky-Brée <lmb@suse.de>
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

#include "stonith.h"
#include "expect.h"

#define	DEVICE	"SSH STONITH device"
#define WHITESPACE	" \t\n\r\f"
/* uncomment this if you have an ssh that can do what it claims
#define SSH_COMMAND "ssh -q -x -o PasswordAuthentication=no StrictHostKeyChecking=no" 
*/
/* use this if you have the (broken) OpenSSH 2.1.1 */
#define SSH_COMMAND "ssh -q -x -n -l root"

/* We need to do a real hard reboot without syncing anything to simulate a
 * power cut. 
 * We have to do it in the background, otherwise this command will not
 * return.
 */
#define REBOOT_COMMAND "nohup sh -c '(sleep 2; nohup /sbin/reboot -nf) >/dev/null 2>&1' &"

/*
 *    SSH STONITH device
 *
 * I used the null device as template, so I guess there is missing
 * some functionality.
 *
 */

struct sshDevice {
  const char *	sshid;
  char **		hostlist;
  int		hostcount;
};

static const char * sshid = "SSHDevice-Stonith";
static const char * NOTsshID = "SSH device has been destroyed";

#define	ISSSHDEV(i)	(((i)!= NULL && (i)->pinfo != NULL)	\
	&& ((struct sshDevice *)(i->pinfo))->sshid == sshid)


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
  if (!ISSSHDEV(s)) {
    syslog(LOG_ERR, "invalid argument to SSH_status");
    return(S_OOPS);
  }

  return S_OK;
}


/*
 *	Return the list of hosts configured for this SSH device
 */

char **
st_hostlist(Stonith  *s)
{
  int		numnames = 0;
  char **		ret = NULL;
  struct sshDevice*	sd;
  int		j;

  if (!ISSSHDEV(s)) {
    syslog(LOG_ERR, "invalid argument to SSH_list_hosts");
    return(NULL);
  }
  sd = (struct sshDevice*) s->pinfo;
  if (sd->hostcount < 0) {
    syslog(LOG_ERR
	   ,	"unconfigured stonith object in SSH_list_hosts");
    return(NULL);
  }
  numnames = sd->hostcount;

  ret = (char **)MALLOC(numnames*sizeof(char*));
  if (ret == NULL) {
    syslog(LOG_ERR, "out of memory");
    return ret;
  }

  memset(ret, 0, numnames*sizeof(char*));

  for (j=0; j < numnames-1; ++j) {
    ret[j] = MALLOC(strlen(sd->hostlist[j])+1);
    if (ret[j] == NULL) {
      st_freehostlist(ret);
      ret = NULL;
      return ret;
    }
    strcpy(ret[j], sd->hostlist[j]);
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
ssh_parse_config_info(struct sshDevice* sd, const char * info)
{
  char **			ret;
  int			wc;
  int			numnames;
  const char *		s = info;
  int			j;

  if (sd->hostcount >= 0) {
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
  sd->hostlist = ret;
  sd->hostcount = numnames;
  return(S_OK);
}


/*
 *	Reset the given host on this Stonith device.
 */
int
st_reset(Stonith * s, int request, const char * host)
{
  char cmd[4096];

  if (!ISSSHDEV(s)) {
    syslog(LOG_ERR, "invalid argument to %s", __FUNCTION__);
    return(S_OOPS);
  }
  syslog(LOG_INFO, _("Host %s ssh-reset initiating"), host);

  snprintf(cmd, 4096, "%s \"%s\" \"%s\"", SSH_COMMAND, host, REBOOT_COMMAND);
  
  if (system(cmd) == 0) 
    return S_OK;
  else {
    syslog(LOG_ERR, "command %s failed", cmd);
    return(S_RESETFAIL);
  }
}

/*
 *	Parse the information in the given configuration file,
 *	and stash it away...
 */
int
st_setconffile(Stonith* s, const char * configname)
{
  FILE *	cfgfile;
  char	line[256];
  struct sshDevice*	sd;

  if (!ISSSHDEV(s)) {
    syslog(LOG_ERR, "invalid argument to SSH_set_configfile");
    return(S_OOPS);
  }
  sd = (struct sshDevice*) s->pinfo;

  if ((cfgfile = fopen(configname, "r")) == NULL)  {
    syslog(LOG_ERR, "Cannot open %s", configname);
    return(S_BADCONFIG);
  }
  while (fgets(line, sizeof(line), cfgfile) != NULL){
    if (*line == '#' || *line == '\n' || *line == EOS) {
      continue;
    }
    return(ssh_parse_config_info(sd, line));
  }
  return(S_BADCONFIG);
}

/*
 *	Parse the config information in the given string, and stash it away...
 */
int
st_setconfinfo(Stonith* s, const char * info)
{
  struct sshDevice* sd;

  if (!ISSSHDEV(s)) {
    syslog(LOG_ERR, "%s: invalid argument", __FUNCTION__);
    return(S_OOPS);
  }
  sd = (struct sshDevice *)s->pinfo;

  return(ssh_parse_config_info(sd, info));
}

const char *
st_getinfo(Stonith * s, int reqtype)
{
  struct sshDevice* sd;
  char *		ret;

  if (!ISSSHDEV(s)) {
    syslog(LOG_ERR, "SSH_idinfo: invalid argument");
    return NULL;
  }
  /*
   *	We look in the ST_TEXTDOMAIN catalog for our messages
   */
  sd = (struct sshDevice *)s->pinfo;

  switch (reqtype) {
  case ST_DEVICEID:
    ret = _("ssh STONITH device");
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
 *	SSH Stonith destructor...
 */
void
st_destroy(Stonith *s)
{
  struct sshDevice* sd;

  if (!ISSSHDEV(s)) {
    syslog(LOG_ERR, "%s: invalid argument", __FUNCTION__);
    return;
  }
  sd = (struct sshDevice *)s->pinfo;

  sd->sshid = NOTsshID;
  if (sd->hostlist) {
    st_freehostlist(sd->hostlist);
    sd->hostlist = NULL;
  }
  sd->hostcount = -1;
  FREE(sd);
  s->pinfo = NULL;
  FREE(s);
  s = NULL;
}

/* Create a new ssh Stonith device */
void *
st_new(void)
{
  struct sshDevice*	sd = MALLOCT(struct sshDevice);

  if (sd == NULL) {
    syslog(LOG_ERR, "out of memory");
    return(NULL);
  }
  memset(sd, 0, sizeof(*sd));
  sd->sshid = sshid;
  sd->hostlist = NULL;
  sd->hostcount = -1;
  return((void *)sd);
}
