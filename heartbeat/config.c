const static char * _heartbeat_c_Id = "$Id: config.c,v 1.61 2002/03/28 02:42:58 alan Exp $";
/*
 * Parse various heartbeat configuration files...
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 *	portions (c) 1999,2000 Mitja Sarp
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pwd.h>
#include <netdb.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include <heartbeat.h>
#include <ha_msg.h>
#include <pils/plugin.h>
#include <HBcomm.h>
#include <hb_module.h>

#define	DIRTYALIASKLUDGE

extern const char *			cmdname;
extern int				parse_only;
extern struct hb_media*			sysmedia[MAXMEDIA];
extern struct sys_config *		config;
extern struct node_info *		curnode;
extern int				verbose;
extern volatile struct pstat_shm *	procinfo;
extern volatile struct process_info *	curproc;
extern char *				watchdogdev;
extern int				nummedia;
extern int                              nice_failback;
extern clock_t				hb_warn_ticks;
extern PILPluginUniv*			PluginLoadingSystem;
extern GHashTable*			CommFunctions;

int	islegaldirective(const char *directive);
int     parse_config(const char * cfgfile, char *nodename);
int	parse_ha_resources(const char * cfgfile);
static clock_t get_ticks(const char * input);
void	dump_config(void);
int	add_option(const char *	option, const char * value);
int	add_node(const char * value, int nodetype);
int   	parse_authfile(void);
int	init_config(const char * cfgfile);
int     set_logfile(const char * value);
int     set_dbgfile(const char * value);
int	StringToBaud(const char * baudstr);

int	baudrate;
int	serial_baud;

int	num_hb_media_types;

struct hb_media_fns**	hbmedia_types;


#ifdef IRIX
	void setenv(const char *name, const char * value, int);
#endif


/*
 *	Read in and validate the configuration file.
 *	Act accordingly.
 */

int
init_config(const char * cfgfile)
{
	struct utsname	u;
	int	errcount = 0;

	/* This may be dumb.  I'll decide later */
	(void)_heartbeat_c_Id;	/* Make warning go away */
	(void)_heartbeat_h_Id;	/* ditto */
	(void)_ha_msg_h_Id;	/* ditto */
/*
 *	'Twould be good to move this to a shared memory segment
 *	Then we could share this information with others
 */
	config = (struct sys_config *)ha_calloc(1
	,	sizeof(struct sys_config));
	if (config == NULL) {
		ha_log(LOG_ERR, "Heartbeat not started: "
			"Out of memory during configuration");
		return(HA_FAIL);
	}
	config->format_vers = 100;
	config->heartbeat_interval = 2;
	config->deadtime_interval = 5;
	config->initial_deadtime = 30;
	config->hopfudge = 1;
	config->log_facility = -1;
	config->client_children = g_hash_table_new(g_direct_hash
	,	g_direct_equal);
	config->client_list = NULL;

	uname(&u);
	curnode = NULL;

	if (!parse_config(cfgfile, u.nodename)) {
		ha_log(LOG_ERR, "Heartbeat not started: configuration error.");
		return(HA_FAIL);
	}
	if (parse_authfile() != HA_OK) {
		ha_log(LOG_ERR, "Authentication configuration error.");
		return(HA_FAIL);
	}
	if (config->log_facility >= 0) {
		openlog(cmdname, LOG_CONS | LOG_PID, config->log_facility);
	}

	if (nummedia < 1) {
		ha_log(LOG_ERR, "No heartbeat ports defined");
		++errcount;
	}

	if (config->warntime_interval <= 0) {
		config->warntime_interval
		=	(config->deadtime_interval * CLK_TCK*3)/4;
	}
	
#if !defined(MITJA)
	/* We should probably complain if there aren't at least two... */
	if (config->nodecount < 1) {
		ha_log(LOG_ERR, "no nodes defined");
		++errcount;
	}
	if (config->authmethod == NULL) {
		ha_log(LOG_ERR, "No authentication specified.");
		++errcount;
	}
#endif
	if ((curnode = lookup_node(u.nodename)) == NULL) {
#if defined(MITJA)
		ha_log(LOG_NOTICE, "%s", msg);
		add_node(u.nodename, NORMALNODE);
		curnode = lookup_node(u.nodename);
		ha_log(LOG_NOTICE, "Current node [%s] added to configuration"
		,	u.nodename);
#else
		ha_log(LOG_ERR, "Current node [%s] not in configuration!"
		,	u.nodename);
		++errcount;
#endif
	}
	setenv(CURHOSTENV, u.nodename, 1);
	if (config->deadtime_interval <= 2 * config->heartbeat_interval) {
		ha_log(LOG_ERR
		,	"Dead time [%d] is too small compared to keeplive [%d]"
		,	config->deadtime_interval, config->heartbeat_interval);
		++errcount;
	}
	if (config->initial_deadtime < 2 * config->deadtime_interval) {
		ha_log(LOG_ERR
		,	"Initial dead time [%d] is too small compared to"
	        " deadtime [%d]"
		,	config->initial_deadtime, config->deadtime_interval);
		++errcount;
	}

	if (*(config->logfile) == EOS) {
                 if (config->log_facility > 0) {
                        /* 
                         * Set to DEVNULL in case a stray script outputs logs
                         */
                        strncpy(config->logfile, DEVNULL
			, 	sizeof(config->logfile));
                        config->use_logfile=0;
                  }else{
		        set_logfile(DEFAULTLOG);
                        config->use_logfile=1;
		        if (!parse_only) {
			        ha_log(LOG_INFO
			        ,	"Neither logfile nor "
				        "logfacility found.");
			        ha_log(LOG_INFO, "Logging defaulting to " 
                                                DEFAULTLOG);
		        }
                }
	}
	if (*(config->dbgfile) == EOS) {
	        if (config->log_facility > 0) {
		        /* 
		        * Set to DEVNULL in case a stray script outputs errors
		        */
		        strncpy(config->dbgfile, DEVNULL
			,	sizeof(config->dbgfile));
                        config->use_dbgfile=0;
	        }else{
		        set_dbgfile(DEFAULTDEBUG);
	        }
        }
	if (!RestartRequested && errcount == 0 && !parse_only) {
		ha_log(LOG_INFO, "**************************");
		ha_log(LOG_INFO, "Configuration validated."
		" Starting heartbeat %s", VERSION);
		if (nice_failback) {
			ha_log(LOG_INFO, "nice_failback is in effect.");
		}
	}
	
	return(errcount ? HA_FAIL : HA_OK);
}


#define	KEY_HOST	"node"
#define KEY_HOPS	"hopfudge"
#define KEY_KEEPALIVE	"keepalive"
#define KEY_DEADTIME	"deadtime"
#define KEY_WARNTIME	"warntime"
#define KEY_INITDEAD	"initdead"
#define KEY_WATCHDOG	"watchdog"
#define	KEY_BAUDRATE	"baud"
#define	KEY_UDPPORT	"udpport"
#define	KEY_FACILITY	"logfacility"
#define	KEY_LOGFILE	"logfile"
#define	KEY_DBGFILE	"debugfile"
#define KEY_FAILBACK	"nice_failback"
#define KEY_STONITH	"stonith"
#define KEY_STONITHHOST "stonith_host"
#define KEY_CLIENT_CHILD "respawn"

int add_normal_node(const char *);
int set_hopfudge(const char *);
int set_keepalive(const char *);
int set_deadtime_interval(const char *);
int set_initial_deadtime(const char *);
int set_watchdogdev(const char *);
int set_baudrate(const char *);
int set_udpport(const char *);
int set_facility(const char *);
int set_logfile(const char *);
int set_dbgfile(const char *);
int set_nice_failback(const char *);
int set_warntime_interval(const char *);
int set_stonith_info(const char *);
int set_stonith_host_info(const char *);
int add_client_child(const char *);

struct directive {
	const char * name;
	int (*add_func) (const char *);
}Directives[] =
{	{KEY_HOST,	add_normal_node}
,	{KEY_HOPS,	set_hopfudge}
,	{KEY_KEEPALIVE,	set_keepalive}
,	{KEY_DEADTIME,	set_deadtime_interval}
,	{KEY_INITDEAD,	set_initial_deadtime}
,	{KEY_WARNTIME,	set_warntime_interval}
,	{KEY_WATCHDOG,	set_watchdogdev}
,	{KEY_BAUDRATE,	set_baudrate}
,	{KEY_UDPPORT,	set_udpport}
,	{KEY_FACILITY,  set_facility}
,	{KEY_LOGFILE,   set_logfile}
,	{KEY_DBGFILE,   set_dbgfile}
,	{KEY_FAILBACK,  set_nice_failback}
};

static const struct WholeLineDirective {
	const char * type;
	int (*parse) (const char *line);
}WLdirectives[] =
{
	{KEY_STONITH,  	   set_stonith_info},
	{KEY_STONITHHOST,  set_stonith_host_info}
,	{KEY_CLIENT_CHILD,  add_client_child}
};

/*
 *	Parse the configuration file and stash away the data
 */
int
parse_config(const char * cfgfile, char *nodename)
{
	FILE	*	f;
	char		buf[MAXLINE];
	char *		cp;
	char		directive[MAXLINE];
	int		dirlength;
	char		option[MAXLINE];
	int		optionlength;
	int		errcount = 0;
	int		j;
	int		i;
	clock_t		cticks;
	struct stat	sbuf;
	struct tms	proforma_tms;

	if ((f = fopen(cfgfile, "r")) == NULL) {
		ha_log(LOG_ERR, "Cannot open config file [%s]", cfgfile);
		return(HA_FAIL);
	}

	fstat(fileno(f), &sbuf);
	config->cfg_time = sbuf.st_mtime;

	/* It's ugly, but effective  */

	while (fgets(buf, MAXLINE, f) != NULL) {
		char *  bp = buf; 
		int	IsOptionDirective=1;
		struct hb_media_fns*	funs = NULL;

		/* Skip over white space */
		bp += strspn(bp, WHITESPACE);

		/* Zap comments on the line */
		if ((cp = strchr(bp, COMMENTCHAR)) != NULL)  {
			*cp = EOS;
		}
		
		/* Strip '\n' and '\r' chars */
		if ((cp = strpbrk(bp, CRLF)) != NULL) {
			*cp = EOS;
		}

		/* Ignore blank (and comment) lines */
		if (*bp == EOS) {
			continue;
		}

		/* Now we expect a directive name */

		dirlength = strcspn(bp, WHITESPACE);
		strncpy(directive, bp, dirlength);
		directive[dirlength] = EOS;
#ifdef DIRTYALIASKLUDGE
		if (strcmp(directive, "udp") == 0) {
			ha_log(LOG_WARNING
			,	"WARNING: directive 'udp' replaced by 'bcast'");
			strncpy(directive, "bcast", sizeof("bcast"));
		}
#endif
		if (!islegaldirective(directive)) {
			ha_log(LOG_ERR, "Illegal directive [%s] in %s"
			,	directive, cfgfile);
			++errcount;
			continue;
		}

		bp += dirlength;

		/* Skip over Delimiters */
		bp += strspn(bp, DELIMS);

		/* Load the medium plugin if its not already loaded... */
		if ((funs=g_hash_table_lookup(CommFunctions, directive))
		==	NULL) {
			if (PILPluginExists(PluginLoadingSystem
			,	HB_COMM_TYPE_S, directive) == PIL_OK) {
				PIL_rc rc;
				if ((rc = PILLoadPlugin(PluginLoadingSystem
				,	HB_COMM_TYPE_S, directive, NULL))
				!=	PIL_OK) {
					ha_log(LOG_ERR, "Cannot load comm"
					" plugin %s [%s]", directive
					,	PIL_strerror(rc));
					continue;
				}
				
				funs=g_hash_table_lookup(CommFunctions
				,	directive);
			}
		}else{
			PILIncrIFRefCount(PluginLoadingSystem
			,	HB_COMM_TYPE_S, directive, +1);
		}


		/* Check first for whole line media-type  directives */
		if (funs && funs->parse)  {
			int num_save = nummedia;
			char *		pname = strdup(bp);
			IsOptionDirective=0;
			if (funs->parse(bp) != HA_OK) {
				PILIncrIFRefCount(PluginLoadingSystem
				,	HB_COMM_TYPE_S, directive, -1);
				errcount++;
				free(pname); pname=NULL;
				*bp = EOS;	/* Stop parsing now */
				continue;
			}
			sysmedia[num_save]->vf = funs;
			sysmedia[num_save]->name = pname;
			funs->mtype(&sysmedia[num_save]->type);
			funs->descr(&sysmedia[num_save]->description);
			g_assert(sysmedia[num_save]->type);
			g_assert(sysmedia[num_save]->description);

			*bp = EOS;
		}

		/* Check for "parse" type (whole line) directives */

		for (j=0; j < DIMOF(WLdirectives); ++j) {
			if (WLdirectives[j].parse == NULL)  {
				continue;
			}
			if (strcmp(directive, WLdirectives[j].type) == 0) {
				IsOptionDirective=0;
				if (WLdirectives[j].parse(bp) != HA_OK) {
					errcount++;
				}
				*bp = EOS;
			}
		}
		/* Now Check for  the options-list stuff */
		while (IsOptionDirective && *bp != EOS) {
			optionlength = strcspn(bp, DELIMS);
			strncpy(option, bp, optionlength);
			option[optionlength] = EOS;
			bp += optionlength;
			if (add_option(directive, option) != HA_OK) {
				errcount++;
			}

			/* Skip over Delimiters */
			bp += strspn(bp, DELIMS);
		}
	}

	cticks = times(&proforma_tms);

	for (i=0; i < config->nodecount; ++i) {
		if (config->nodes[i].nodetype == PINGNODE) {
			config->nodes[i].nlinks = 1;
			for (j=0; j < nummedia; j++) {
				struct link *lnk = &config->nodes[i].links[0];
				if (!sysmedia[j]->vf->isping()
				||	strcmp(config->nodes[i].nodename
				,	sysmedia[j]->name) != 0) {
					continue;
				}
				lnk->name = config->nodes[i].nodename;
				lnk->lastupdate = cticks;
				strncpy(lnk->status, DEADSTATUS
				,	sizeof(lnk->status));
				lnk[1].name = NULL;
				break;
			}
			continue;
		}
		config->nodes[i].nlinks = 0;
		for (j=0; j < nummedia; j++) {
			int nc = config->nodes[i].nlinks;
			struct link *lnk = &config->nodes[i].links[nc];
			if (sysmedia[j]->vf->isping()) {
				continue;
			}
			lnk->name = sysmedia[j]->name;
			lnk->lastupdate = cticks;
			strncpy(lnk->status, DEADSTATUS, sizeof(lnk->status));
			lnk[1].name = NULL;
			++config->nodes[i].nlinks;
		}
	}

	j++;

	fclose(f);
	return(errcount ? HA_FAIL : HA_OK);
}

/*
 *	Dump the configuration file - as a configuration file :-)
 *
 *	This does not include every directive at this point.
 *	At this point, we don't dump ppp-udp lines correctly.
 */
void
dump_config(void)
{
	int	j;
	struct node_info *	hip;
	struct utsname	u;
	const char *	last_media = NULL;

	uname(&u);

	printf("#\n#	Local HA configuration (on %s)\n#\n"
	,	u.nodename);

	for(j=0; j < nummedia; ++j) {
			puts("\n");
		g_assert(sysmedia[j]->type);
		g_assert(sysmedia[j]->description);
		printf("# %s heartbeat channel -------------\n"
		,	sysmedia[j]->description);
		printf(" %s", sysmedia[j]->type);
		last_media = sysmedia[j]->type;
		printf(" %s", sysmedia[j]->name);
	}
	printf("\n#---------------------------------------------------\n");

	printf("#\n#	Global HA configuration and status\n#\n");
	printf("#\n%s	%d\t# hops allowed above #nodes (below)\n"
	,	KEY_HOPS, config->hopfudge);
	printf("%s	%d\t# (heartbeat interval in seconds)\n"
	,	KEY_KEEPALIVE, config->heartbeat_interval);
	printf("%s	%d\t# (seconds to \"node dead\")\n#\n"
	,	KEY_DEADTIME, config->deadtime_interval);
	if (watchdogdev) {
		printf("%s	%s\t# (watchdog device name)\n#\n"
		,	KEY_WATCHDOG, watchdogdev);
	}else{
		printf("#%s	%s\t# (NO watchdog device specified)\n#\n"
		,	KEY_WATCHDOG, "/dev/watchdog");
	}

	printf("#\n");

	for (j=0; j < config->nodecount; ++j) {
		hip = &config->nodes[j];
		printf("%s %s\t#\t current status: %s\n"
		,	KEY_HOST
		,	hip->nodename
		,	hip->status);
	}
	printf("#---------------------------------------------------\n");
}

/*
 *	Check the /etc/ha.d/haresources file
 *
 *	All we check for now is the set of node names.
 *
 *	It would be good to check the resource names, too...
 */
int
parse_ha_resources(const char * cfgfile)
{
	char		buf[MAXLINE];
	struct stat	sbuf;
	int		rc = HA_OK;
	FILE *		f;

	if ((f = fopen(cfgfile, "r")) == NULL) {
		ha_log(LOG_ERR, "Cannot open config file [%s]", cfgfile);
		return(HA_FAIL);
	}

	fstat(fileno(f), &sbuf);
	config->rsc_time = sbuf.st_mtime;
	
	while (fgets(buf, MAXLINE-1, f) != NULL) {
		char *	bp = buf;
		char *	endp;
		char	token[MAXLINE];

		/* Skip over white space */
		bp += strspn(bp, WHITESPACE);

		if (*bp == COMMENTCHAR) {
			continue;
		}
		
		if (*bp == EOS) {
			continue;
		}
		endp = bp + strcspn(bp, WHITESPACE);
		strncpy(token, bp, endp - bp);
		token[endp-bp] = EOS;
		if (lookup_node(token) == NULL) {
			ha_log(LOG_ERR, "Bad nodename in %s [%s]", cfgfile
			,	token);
			rc = HA_FAIL;
			break;
		}

		/*
		 * FIXME: Really ought to deal with very long lines
		 * correctly.
		 */
		while (buf[strlen(buf)-2]=='\\') {
			if (fgets(buf, MAXLINE-1, f)==NULL)
				break;
		}
	}
	fclose(f);
	return(rc);
}

/*
 *	Is this a legal directive name?
 */
int
islegaldirective(const char *directive)
{
	int	j;

	/*
	 * We have four kinds of directives to deal with:
	 *
	 *	1) Builtin directives which are keyword value value value...
	 *		"Directives[]"
	 *	2) Builtin directives which are one per line...
	 *		WLdirectives[]
	 *	3) media declarations which are media value value value
	 *		These are dynamically loaded plugins...
	 *		of type HBcomm
	 *	4) media declarations which are media rest-of-line
	 *		These are dynamically loaded plugins...
	 *		of type HBcomm
	 *
	 */
	for (j=0; j < DIMOF(Directives); ++j) {
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG
			,	"Comparing directive [%s] against [%s]"
			,	 directive, Directives[j].name);
		}

		if (strcmp(directive, Directives[j].name) == 0) {
			return(HA_OK);
		}
	}
	for (j=0; j < DIMOF(WLdirectives); ++j) {
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG
			, "Comparing directive [%s] against WLdirective[%s]"
			,	 directive, WLdirectives[j].type);
		}
		if (strcmp(directive, WLdirectives[j].type) == 0) {
			return(HA_OK);
		}
	}
	if (PILPluginExists(PluginLoadingSystem,  HB_COMM_TYPE_S, directive)
	== PIL_OK){
		return HA_OK;
	}
	return(HA_FAIL);
}

/*
 *	Add the given option/value pair to the configuration
 */
int
add_option(const char *	option, const char * value)
{
	int	j;
	struct hb_media_fns*	funs = NULL;

	if (ANYDEBUG)  {
		ha_log(LOG_DEBUG, "add_option(%s,%s)", option, value);
	}

	for (j=0; j < DIMOF(Directives); ++j) {
		if (strcmp(option, Directives[j].name) == 0) {
			int rc;
			rc = ((*Directives[j].add_func)(value));
			if (rc == HA_OK) {
				SetParameterValue(option, value);
			}
			return rc;
		}
	}

	if ((funs=g_hash_table_lookup(CommFunctions, option)) != NULL
		&&	funs->new != NULL) {
		struct hb_media* mp = funs->new(value);
		char*		type;
		char*		descr;

		funs->descr(&descr);
		funs->mtype(&type);

		sysmedia[nummedia] = mp;
		if (mp == NULL) {
			ha_log(LOG_ERR, "Illegal %s [%s] in config file [%s]"
			,	type, descr, value);
			PILIncrIFRefCount(PluginLoadingSystem
			,	HB_COMM_TYPE_S, option, -1);
			ha_free(descr); descr = NULL;
			ha_free(type);  type = NULL;
			return(HA_FAIL);
		}else{
			mp->type = type;
			mp->description = descr;
			g_assert(mp->type);
			g_assert(mp->description);
			g_assert(mp->type[0] != '(');
			g_assert(mp->description[0] != '(');
			mp->vf = funs;
			mp->name = strdup(value);
			++nummedia;
			PILIncrIFRefCount(PluginLoadingSystem
			,	HB_COMM_TYPE_S, option, +1);
			return(HA_OK);
		}
		g_assert(sysmedia[nummedia-1]->type);
		g_assert(sysmedia[nummedia-1]->description);
	}
	ha_log(LOG_ERR, "Illegal configuration directive [%s]", option);
	return(HA_FAIL);
}

/*
 * For reliability reasons, we should probably require nodename
 * to be in /etc/hosts, so we don't lose our mind if (when) DNS goes out...
 * This would also give us an additional sanity check for the config file.
 *
 * This is only the administrative interface, whose IP address never moves
 * around.
 */

/* Process a node declaration */
int
add_node(const char * value, int nodetype)
{
	struct node_info *	hip;
	struct tms		proforma_tms;
	if (config->nodecount >= MAXNODE) {
		return(HA_FAIL);
	}
	hip = &config->nodes[config->nodecount];
	memset(hip, 0, sizeof(*hip));
	++config->nodecount;
	strncpy(hip->status, INITSTATUS, sizeof(hip->status));
	strncpy(hip->nodename, value, sizeof(hip->nodename));
	hip->rmt_lastupdate = 0L;
	hip->anypacketsyet  = 0;
	hip->local_lastupdate = times(&proforma_tms);
	hip->track.nmissing = 0;
	hip->track.last_seq = NOSEQUENCE;
	hip->nodetype = nodetype;
	return(HA_OK);
}

/* Process a node declaration */
int
add_normal_node(const char * value)
{
	return add_node(value, NORMALNODE);
}



/* Set the hopfudge variable */
int
set_hopfudge(const char * value)
{
	config->hopfudge = atoi(value);

	if (config->hopfudge >= 0 && config->hopfudge < 256) {
		return(HA_OK);
	}
	return(HA_FAIL);
}

/* Set the keepalive time */
int
set_keepalive(const char * value)
{
	config->heartbeat_interval = atoi(value);

	if (config->heartbeat_interval > 0) {
		return(HA_OK);
	}
	return(HA_FAIL);

}

/* Set the dead timeout */
int
set_deadtime_interval(const char * value)
{
	config->deadtime_interval = atoi(value);
	if (config->deadtime_interval >= 0) {
		return(HA_OK);
	}
	return(HA_FAIL);
}

/* Set the initial dead timeout */
int
set_initial_deadtime(const char * value)
{
	config->initial_deadtime = atoi(value);
	if (config->initial_deadtime >= 0) {
		return(HA_OK);
	}
	return(HA_FAIL);
}

/* Set the watchdog device */
int
set_watchdogdev(const char * value)
{

	if (watchdogdev != NULL) {
		fprintf(stderr, "%s: Watchdog device multiply specified.\n"
		,	cmdname);
		return(HA_FAIL);
	}
	if ((watchdogdev = (char *)ha_malloc(strlen(value)+1)) == NULL) {
		fprintf(stderr, "%s: Out of memory for watchdog device\n"
		,	cmdname);
		return(HA_FAIL);
	}
	strcpy(watchdogdev, value);
	return(HA_OK);
}

int
StringToBaud(const char * baudstr)
{
	int	baud;

	baud = atoi(baudstr);
	switch(baud)  {
		case 9600:	return  B9600;
		case 19200:	return  B19200;
#ifdef B38400
		case 38400:	return  B38400;
#endif
#ifdef B57600
		case 57600:	return  B57600;
#endif
#ifdef B115200
		case 115200:	return  B115200;
#endif
#ifdef B230400
		case 230400:	return  B230400;
#endif
#ifdef B460800
		case 460800:	return  B460800;
#endif
		default:	return 0;
	}
}

int
set_baudrate(const char * value)
{
	static int	baudset = 0;
	if (baudset) {
		fprintf(stderr, "%s: Baudrate multiply specified.\n"
		,	cmdname);
		return(HA_FAIL);
	}
	++baudset;
	serial_baud = StringToBaud(value);
	if (serial_baud <= 0) {
		fprintf(stderr, "%s: invalid baudrate [%s] specified.\n"
		,	cmdname, value);
		return(HA_FAIL);
	}
	return(HA_OK);
}

int
set_udpport(const char * value)
{
	int		port = atoi(value);
	struct servent*	service;

	if (port <= 0) {
		fprintf(stderr, "%s: invalid port [%s] specified.\n"
		,	cmdname, value);
		return(HA_FAIL);
	}

	/* Make sure this port isn't reserved for something else */
	if ((service=getservbyport(htons(port), "udp")) != NULL) {
		if (strcmp(service->s_name, HA_SERVICENAME) != 0) {
			ha_log(LOG_WARNING
			,	"%s: udp port %s reserved for service \"%s\"."
			,	cmdname, value, service->s_name);
		}
	}
	endservent();

	SetParameterValue("udpport", value);
	return(HA_OK);
}

struct _syslog_code {
        const char    *c_name;
        int     c_val;
};


struct _syslog_code facilitynames[] =
{
#ifdef LOG_AUTH
	{ "auth", LOG_AUTH },
	{ "security", LOG_AUTH },           /* DEPRECATED */
#endif
#ifdef LOG_AUTHPRIV
	{ "authpriv", LOG_AUTHPRIV },
#endif
#ifdef LOG_CRON
	{ "cron", LOG_CRON },
#endif
#ifdef LOG_DAEMON
	{ "daemon", LOG_DAEMON },
#endif
#ifdef LOG_FTP
	{ "ftp", LOG_FTP },
#endif
#ifdef LOG_KERN
	{ "kern", LOG_KERN },
#endif
#ifdef LOG_LPR
	{ "lpr", LOG_LPR },
#endif
#ifdef LOG_MAIL
	{ "mail", LOG_MAIL },
#endif

/*	{ "mark", INTERNAL_MARK },           * INTERNAL */

#ifdef LOG_NEWS
	{ "news", LOG_NEWS },
#endif
#ifdef LOG_SYSLOG
	{ "syslog", LOG_SYSLOG },
#endif
#ifdef LOG_USER
	{ "user", LOG_USER },
#endif
#ifdef LOG_UUCP
	{ "uucp", LOG_UUCP },
#endif
#ifdef LOG_LOCAL0
	{ "local0", LOG_LOCAL0 },
#endif
#ifdef LOG_LOCAL1
	{ "local1", LOG_LOCAL1 },
#endif
#ifdef LOG_LOCAL2
	{ "local2", LOG_LOCAL2 },
#endif
#ifdef LOG_LOCAL3
	{ "local3", LOG_LOCAL3 },
#endif
#ifdef LOG_LOCAL4
	{ "local4", LOG_LOCAL4 },
#endif
#ifdef LOG_LOCAL5
	{ "local5", LOG_LOCAL5 },
#endif
#ifdef LOG_LOCAL6
	{ "local6", LOG_LOCAL6 },
#endif
#ifdef LOG_LOCAL7
	{ "local7", LOG_LOCAL7 },
#endif
	{ NULL, -1 }
};

/* set syslog facility config variable */
int
set_facility(const char * value)
{
	int		i;

	for(i = 0; facilitynames[i].c_name != NULL; ++i) {
		if(strcmp(value, facilitynames[i].c_name) == 0) {
			config->log_facility = facilitynames[i].c_val;
			strncpy(config->facilityname, value
			,	sizeof(config->facilityname)-1);
			config->facilityname[sizeof(config->facilityname)-1]
			=	EOS;
			return(HA_OK);
		}
	}
	return(HA_FAIL);
}

/* set syslog facility config variable */
int
set_dbgfile(const char * value)
{
	strncpy(config->dbgfile, value, PATH_MAX);
	config->use_dbgfile=1;
	return(HA_OK);
}

/* set syslog facility config variable */
int
set_logfile(const char * value)
{
	strncpy(config->logfile, value, PATH_MAX);
	config->use_logfile=1;
	return(HA_OK);
}

/* sets nice_failback behavior on/off */
int
set_nice_failback(const char * value)
{
        if(!strcasecmp(value, "on")) {
                nice_failback = 1;
        } else {
                nice_failback = 0;
        }

        return(HA_OK);
}

#define	NUMCHARS	"0123456789."

static clock_t
get_ticks(const char * input)
{
	const char *	cp = input;
	const char *	units;
	int		multiplier = CLK_TCK;
	int		divisor = 1;
	clock_t		ret;
	double		dret;

	cp += strspn(cp, WHITESPACE);
	units = cp + strspn(cp, NUMCHARS);
	units += strspn(units, WHITESPACE);

	if (strncasecmp(units, "ms", 2) == 0
	||	strncasecmp(units, "msec", 4) == 0) {
		multiplier = CLK_TCK;
		divisor = 1000;
	}
	dret = atof(cp);
	dret *= (double)multiplier;
	dret /= (double)divisor;
	dret += 0.5;
	ret = (clock_t)dret;
	return(ret);
}

/* Set warntime interval */
int
set_warntime_interval(const char * value)
{
	clock_t	warntime;
	warntime = get_ticks(value);

	if (warntime <= 0) {
		fprintf(stderr, "Warn time [%s] is invalid.\n", value);
		return(HA_FAIL);
	}
	config->warntime_interval = warntime;
	return(HA_OK);
}

/*
 * Set Stonith information
 * 
 * Expect a line that looks like:
 * stonith <type> <configfile>
 *
 */
int
set_stonith_info(const char * value)
{
	const char *	vp = value;
	const char *	evp;
	Stonith *	s;
	char		StonithType [MAXLINE];
	char		StonithFile [MAXLINE];
	int		tlen;
	int		rc;

	vp += strspn(vp, WHITESPACE);
	tlen = strcspn(vp, WHITESPACE);

	evp = vp + tlen;

	if (tlen < 1) {
		ha_log(LOG_ERR, "No Stonith type given");
		return(HA_FAIL);
	}
	if (tlen >= sizeof(StonithType)) {
		ha_log(LOG_ERR, "Stonith type too long");
		return(HA_FAIL);
	}

	strncpy(StonithType, vp, tlen);
	StonithType[tlen] = EOS;

	if ((s = stonith_new(StonithType)) == NULL) {
		ha_log(LOG_ERR, "Invalid Stonith type [%s]", StonithType);
		return(HA_FAIL);
	}

	vp = evp + strspn(evp, WHITESPACE);
	sscanf(vp, "%[^\r\n]",  StonithFile);

	switch ((rc=s->s_ops->set_config_file(s, StonithFile))) {
		case S_OK:
			/* This will have to change to a list !!! */
			config->stonith = s;
			s->s_ops->status(s);
			return(HA_OK);

		case S_BADCONFIG:
			ha_log(LOG_ERR, "Invalid Stonith config file [%s]"
			,	StonithFile);
			break;
		

		default:
			ha_log(LOG_ERR, "Unknown Stonith config error [%s] [%d]"
			,	StonithFile, rc);
			break;
	}
	return(HA_FAIL);
}


/*
 * Set Stonith information
 * 
 * Expect a line that looks like:
 * stonith_host <hostname> <type> <params...>
 *
 */
int
set_stonith_host_info(const char * value)
{
	const char *	vp = value; /* points to the current token */
	const char *	evp;        /* points to the next token */
	Stonith *	s;
	char		StonithType [MAXLINE];
	char		StonithHost [HOSTLENG];
	int		tlen;
	int		rc;
	struct utsname	u;
	
	vp += strspn(vp, WHITESPACE);
	tlen = strcspn(vp, WHITESPACE);

	/* Save the pointer just past the hostname field */
	evp = vp + tlen;

	/* Grab the hostname */
	if (tlen < 1) {
		ha_log(LOG_ERR, "No Stonith hostname argument given");
		return(HA_FAIL);
	}	
	if (tlen >= sizeof(StonithHost)) {
		ha_log(LOG_ERR, "Stonith hostname too long");
		return(HA_FAIL);
	}
	strncpy(StonithHost, vp, tlen);
	StonithHost[tlen] = EOS;

	/* Verify that this host is valid to create this stonith
	   object.  Expect the hostname listed to match this host or
	   '*'
	*/
	if (uname(&u) < 0) {
		ha_perror("uname failure parsing stonith_host");
		return HA_FAIL;
	}
	
	if (strcmp ("*", StonithHost) != 0 
	    && strcmp (u.nodename,StonithHost)) {
		/* This directive is not valid for this host */
		return HA_OK;
	}
	
	/* Grab the next field */
	vp = evp + strspn(evp, WHITESPACE);
	tlen = strcspn(vp, WHITESPACE);
	
	/* Save the pointer just past the stonith type field */
	evp = vp + tlen;
	
	/* Grab the stonith type */
	if (tlen < 1) {
		ha_log(LOG_ERR, "No Stonith type given");
		return(HA_FAIL);
	}
	if (tlen >= sizeof(StonithType)) {
		ha_log(LOG_ERR, "Stonith type too long");
		return(HA_FAIL);
	}
	
	strncpy(StonithType, vp, tlen);
	StonithType[tlen] = EOS;

	if ((s = stonith_new(StonithType)) == NULL) {
		ha_log(LOG_ERR, "Invalid Stonith type [%s]", StonithType);
		return(HA_FAIL);
	}

	/* Grab the parameters list */
	vp = evp;
	vp += strspn(vp, WHITESPACE);
	
        /* NOTE: this might contain a newline character */
	/* I'd strip it off, but vp is a const char *   */

	switch ((rc=s->s_ops->set_config_info(s, vp))) {
		case S_OK:
			/* This will have to change to a list !!! */
			config->stonith = s;
			s->s_ops->status(s);
			return(HA_OK);

		case S_BADCONFIG:
			ha_log(LOG_ERR, "Invalid Stonith configuration parameter [%s]"
			,	evp);
			break;
		

		default:
			ha_log(LOG_ERR
			,	"Unknown Stonith config error parsing [%s] [%d]"
			,	evp, rc);
			break;
	}
	return(HA_FAIL);
}
int
add_client_child(const char * directive)
{
	struct client_child*	child;
	const char *		uidp;
	const char *		cmdp;
	char			chuid[64];
	int			uidlen;
	int			cmdlen;
	char*			command;
	struct passwd*		pw;

	ha_log(LOG_INFO, "respawn directive: %s", directive);

	/* Skip over initial white space, so we can get the uid */
	uidp = directive;
	uidp += strspn(uidp, WHITESPACE);
	uidlen = strcspn(uidp, WHITESPACE);

	cmdp = uidp + uidlen+1;

	/* Skip over white space, find the command */
	cmdp += strspn(cmdp, WHITESPACE);
	cmdlen = strcspn(cmdp, WHITESPACE);
	
	if (uidlen >= sizeof(chuid)) {
		ha_log(LOG_ERR
		,	"UID specified for client child is too long");
		return HA_FAIL;
	}
	memcpy(chuid, uidp, uidlen);
	chuid[uidlen] = EOS;

	if ((pw = getpwnam(chuid)) == NULL) {
		ha_log(LOG_ERR
		,	"Invalid uid [%s] specified for client child"
		,	chuid);
		return HA_FAIL;
	}

	if (*cmdp != '/') {
		ha_log(LOG_ERR
		,	"Client child command [%s] is not full pathname"
		,	cmdp);
		return HA_FAIL;
	}

	command = ha_malloc(cmdlen+1);
	if (command == NULL) {
		ha_log(LOG_ERR, "Out of memory in add_client_child");
		return HA_FAIL;
	}
	memcpy(command, cmdp, cmdlen);
	command[cmdlen] = EOS;

	if (access(command, X_OK|F_OK) < 0) {
		ha_log(LOG_ERR
		,	"Client child command [%s] is not executable"
		,	command);
		ha_free(command); command=NULL;
		return HA_FAIL;
	}

 	child = MALLOCT(struct client_child);
	if (child == NULL) {
		ha_log(LOG_ERR, "Out of memory in add_client_child");
		ha_free(command); command=NULL;
		return HA_FAIL;
	}
	memset(child, 0, sizeof(*child));
	child->respawn = 1;
	child->u_runas = pw->pw_uid;
	child->g_runas = pw->pw_gid;
	child->command = command;
	config->client_list = g_list_append(config->client_list, child);

	return HA_OK;
}
/*
 * $Log: config.c,v $
 * Revision 1.61  2002/03/28 02:42:58  alan
 * Fixed a bug where having an error on a media directive which was not on the
 * first occurance on the line made us unload the module prematurely, and then
 * we weren't able to proceed because we had mistakenly unloaded the module,
 * but thought it was still loaded.  Instant core dump.
 * Thanks to Sean Reifschneider for pointing this out.
 *
 * Revision 1.60  2002/03/25 23:54:52  alan
 * Put in fixes to two bugs noted by Gamid Isayev of netzilla networks.
 * One of the two fixes is also due to him.
 *
 * Revision 1.59  2002/03/25 23:19:36  horms
 * Patches from Gamid Isayev to
 *
 * 1) Avoid shadowing the udpport global, hence this
 *    configuration option should now work.
 *
 * 2) Call getservbyname with a vaild protocol (udp), this should
 *    also allow the udpport configuration option to work.
 *
 * --
 * Horms
 *
 * Revision 1.58  2002/03/19 13:43:43  alan
 * Just added a note about very long lines in haresources not being
 * handled correctly.
 *
 * Revision 1.57  2002/02/11 22:31:34  alan
 * Added a new option ('l') to make heartbeat run at low priority.
 * Added support for a new capability - to start and stop client
 * 	processes together with heartbeat itself.
 *
 * Revision 1.56  2002/02/10 23:09:25  alan
 * Added a little initial code to support starting client
 * programs when we start, and shutting them down when we stop.
 *
 * Revision 1.55  2002/02/06 21:22:40  alan
 * Fixed a bug concerning comparing a string to "udp".  Forgot the == 0.
 *
 * Revision 1.54  2002/01/25 05:32:01  alan
 * Put in a warning when 'udp' is encountered.
 *
 * Revision 1.53  2002/01/25 05:29:46  alan
 * Put in a dirty alias kludge to make the software recognize udp as well as bcast.
 *
 * Revision 1.52  2001/10/09 01:19:08  alan
 * Put in fix from Andreas Piesk for detecting / dealing with blank/comment
 * lines properly in config.c
 *
 * Revision 1.51  2001/10/04 21:14:30  alan
 * Patch from Reza Arbab <arbab@austin.ibm.com> to make it compile correctly
 * on AIX.
 *
 * Revision 1.50  2001/10/03 21:28:35  alan
 * Put in Andreas Piesk's fix to the funny question mark at the end of the line
 * on ps output when you have a multicast comm link.
 *
 * Revision 1.49  2001/10/03 05:28:01  alan
 * Fixed a kind of big oops regarding parsing directives...
 *
 * Revision 1.48  2001/10/03 05:22:19  alan
 * Added code to save configuration parameters so we can pass them to the various communication plugins...
 *
 * Revision 1.47  2001/09/29 19:08:24  alan
 * Wonderful security and error correction patch from Emily Ratliff
 * 	<ratliff@austin.ibm.com>
 * Fixes code to have strncpy() calls instead of strcpy calls.
 * Also fixes the number of arguments to several functions which were wrong.
 * Many thanks to Emily.
 *
 * Revision 1.46  2001/08/15 17:58:13  alan
 * Hopefully fixed the -v flag...
 * Soon to remove bag from head?
 *
 * Revision 1.45  2001/08/15 16:56:47  alan
 * Put in the code to allow serial port comm plugins to work...
 *
 * Revision 1.44  2001/08/10 17:35:37  alan
 * Removed some files for comm plugins
 * Moved the rest of the software over to use the new plugin system for comm
 * plugins.
 *
 * Revision 1.43  2001/07/18 21:13:43  alan
 * Put in Emily Ratliff's patch for checking for out of memory in config.c
 *
 */
