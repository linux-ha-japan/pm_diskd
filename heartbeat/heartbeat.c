const static char * _heartbeat_c_Id = "$Id: heartbeat.c,v 1.4 1999/09/27 04:14:42 alanr Exp $";
/*
 *	Near term needs:
 *	- Logging of up/down status changes to a file... (or somewhere)
 *	- restart message forces heartbeat daemon to restart
 *		(should this be a "cluster-wide" message?
 *		 -- needs "graceful shutdown" mechanism.  Maybe this can just
 *		    be "/etc/rc.d/init.d/ha restart" ?)
 */

/*
 *	Linux-HA heartbeat code
 *
 *	The basic facilities for round-robin (ring) and IP heartbeats are
 *	contained within.
 *
 *	There is a master configuration file which we open to tell us
 *	what to do.
 *
 *	It has lines like this in it:
 *
 *	serial	/dev/cua0, /dev/cua1
 *	udp	eth0
 *
 *	node	amykathy, kathyamy
 *	node	dralan
 *	keepalive 2
 *	deadtime  10
 *	hopfudge 2
 *	baud 19200
 *	udpport 1001
 *
 *	"Serial" lines tell us about our heartbeat configuration.
 *	If there is more than one serial port configured, we are in a "ring"
 *	configuration, every message not originated on our node is echoed
 *	to the other serial port(s)
 *
 *	"Node" lines tell us about the cluster configuration.
 *	We had better find our uname -n nodename here, or we won't start up.
 *
 *	We ought to complain if we find extra nodes in the stream that aren't in
 *	the master configuration file.
 *
 *	keepalive lines specify the keepalive interval
 *	deadtime lines specify how long we wait before declaring
 *		a node dead
 *	hopfudge says how much larger than #nodes we allow hopcount
 *		to grow before dropping the message
 *
 *	I need to separate things into a "global" configuration file,
 *	and a "local" configuration file, so I can double check
 *	the global against the cluster when changing configurations.
 *	Things like serial port assignments may be node-specific...
 *
 *	Oh well, that will have to wait until later...
 */

/*
 *	Here's our process structure:
 *
 *
 *		Control process - starts off children and reads a fifo
 *			for commands to send to the cluster.  These
 *			commands are sent to write pipes, and status pipe
 *
 *		Status process - reads the status pipe
 *			and forks off child processes to perform actions
 *			associated with config status changes
 *			It also sends out the periodic keepalive messages.
 *
 *		hb channel read processes - each reads a hb channel, and
 *			copies messages to the status pipe.  The tty
 *			version of this cross-echos to the other ttys
 *			in the ring (ring passthrough)
 *
 *		hb channel write processes - one per hb channel, each reads its
 *			own pipe and send the result to its medium
 *
 *	The result of all this hoorah is that we have the following procs:
 *
 *	One Control process
 *	One Master Status process
 *		"n" hb channel read processes
 *		"n" hb channel write processes
 *
 *	For the usual 2 ttys in a ring configuration, this is 6 processes
 *
 *	For a system using only UDP for heartbeats this is 4 processes.
 *
 *	For a system using 2 ttys and UDP, this is 8 processes.
 *
 *	If every second, each node writes out 50 chars of status,
 *	and we have 16 nodes, and the data rate would be about 800 chars/sec.
 *	This would require about 8000 bps.
 *	This seems awfully close to 9600.  Better run faster than that
 *	for such a cluster...
 *
 *
 *	Process/Pipe configuration:
 *
 *	Control process:
 *		Reads a control fifo for input
 *		Writes master status pipe
 *		Writes heartbeat channel pipes
 *
 *	Master Status process:
 *		Reads master status pipe
 *		forks children, etc. to deal with status changes
 *
 *	heartbeat read processes:
 *		Reads hb channel (tty, UDP, etc)
 *		copying to master status pipe
 *		For ttys ONLY:
 *			copying to tty write pipes (incrementing hop count and
 *				filtering out "ring wraparounds")
 *	Wish List:
 *
 *	Splitting global from local configuration information
 *
 *	Nearest Neighbor heartbeating (? maybe?)
 *		This should replace the current policy of full-ring heartbeats
 *		In this policy, each machine only heartbeats to it's nearest
 *		neighbors.  The nearest neighbors only forward on status CHANGES
 *		to their neighbors.  This means that the total ring traffic
 *		in the non-error case is reduced to the same as a 3-node
 *		cluster.  This is a huge improvement.  It probably means that
 *		19200 is fast enough for almost any size network.
 *		Non-heartbeat admin traffic is forwarded to all members of the
 *		ring as it was before.
 *
 *	IrDA heartbeats
 *		This is a near-exact replacement for ethernet with lower
 *		bandwidth, low costs and fewer points of failure.
 *		The role of an ethernet hub is replaced by a mirror, which
 *		is less likely to fail.  But if it does, it might mean
 *		seven years of bad luck :-)
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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
#include <netdb.h>

#include <heartbeat.h>
#include <ha_msg.h>

#define OPTARGS		"vd"


int		verbose = 0;

const char *	cmdname = "heartbeat";
const char **	Argv = NULL;
int		Argc = -1;
int		debug = 0;
int		childpid = -1;
char *		watchdogdev = NULL;
int		watchdogfd = -1;
void		(*localdie)(void);


struct hb_media*	sysmedia[MAXMEDIA];
int			nummedia = 0;
int			status_pipe[2];	/* The Master status pipe */

const char *ha_log_priority[8] = {
	"emerg",
	"alert",
	"crit",
	"error",
	"warn",
	"notice",
	"info",
	"debug"
};

struct _syslog_code {
        const char    *c_name;
        int     c_val;
};


struct _syslog_code facilitynames[] =
{
	{ "auth", LOG_AUTH },
	{ "authpriv", LOG_AUTHPRIV },
	{ "cron", LOG_CRON },
	{ "daemon", LOG_DAEMON },
	{ "ftp", LOG_FTP },
	{ "kern", LOG_KERN },
	{ "lpr", LOG_LPR },
	{ "mail", LOG_MAIL },
/*	{ "mark", INTERNAL_MARK },           * INTERNAL */
	{ "news", LOG_NEWS },
	{ "security", LOG_AUTH },           /* DEPRECATED */
	{ "syslog", LOG_SYSLOG },
	{ "user", LOG_USER },
	{ "uucp", LOG_UUCP },
	{ "local0", LOG_LOCAL0 },
	{ "local1", LOG_LOCAL1 },
	{ "local2", LOG_LOCAL2 },
	{ "local3", LOG_LOCAL3 },
	{ "local4", LOG_LOCAL4 },
	{ "local5", LOG_LOCAL5 },
	{ "local6", LOG_LOCAL6 },
	{ "local7", LOG_LOCAL7 },
	{ NULL, -1 }
};

struct sys_config *	config;
struct node_info *	curnode = NULL;

volatile struct pstat_shm *	procinfo = NULL;
volatile struct process_info *	curproc = NULL;

int	setline(int fd);
void	cleanexit(int rc);
void	debug_sig(int sig);
void	signal_all(int sig);
void	parent_debug_sig(int sig);
int	islegaldirective(const char *directive);
int	parse_config(const char * cfgfile);
int	parse_ha_resources(const char * cfgfile);
void	dump_config(void);
char *	ha_timestamp(void);
int	add_option(const char *	option, const char * value);
int	add_node(const char * value);
int	set_hopfudge(const char * value);
int	set_keepalive(const char * value);
int	set_deadtime_interval(const char * value);
int	set_watchdogdev(const char * value);
int	set_baudrate(const char * value);
int	set_udpport(const char * value);
int	set_facility(const char * value);
int	set_logfile(const char * value);
int	set_dbgfile(const char * value);
int   	set_auth(const char * value);
void	init_watchdog(void);
void	tickle_watchdog(void);
void	usage(void);
int	init_config(const char * cfgfile);
void	init_procinfo(void);
int	initialize_heartbeat(void);
void	init_status_alarm(void);
void	ding(int sig);
void	dump_msg(const struct ha_msg *msg);
void	check_node_timeouts(void);
void	mark_node_dead(struct node_info* hip);
void	notify_world(struct ha_msg * msg, const char * ostatus);
struct node_info *	lookup_node(const char *);
void	make_daemon(void);
void	heartbeat_monitor(struct ha_msg * msg);
void	init_monitor(void);
int	should_drop_message(struct node_info* node, const struct ha_msg* msg);
void	add2_xmit_hist (struct msg_xmit_hist * hist, struct ha_msg* msg
,		unsigned long seq);
void	init_xmit_hist (struct msg_xmit_hist * hist);

/* The biggies */
void control_process(FILE * f);
void read_child(struct hb_media* mp);
void write_child(struct hb_media* mp);
void master_status_process(void);		/* The real biggie */

#ifdef IRIX
	void setenv(const char *name, const char * value, int);
#endif

pid_t	processes[MAXPROCS];
int	num_procs = 0;

#define	ADDPROC(pid)	{if (pid > 0 && pid != -1) {processes[num_procs] = (pid); ++num_procs;};}

/*
 *	Read in and validate the configuration file.
 *	Act accordingly.
 */

int
init_config(const char * cfgfile)
{
	struct utsname	u;
	int	errcount = 0;
	char	msg[MAXLINE];

	/* This may be dumb.  I'll decide later */
	(void)_heartbeat_c_Id;	/* Make warning go away */
	(void)_heartbeat_h_Id;	/* ditto */
	(void)_ha_msg_h_Id;	/* ditto */
/*
 *	'Twould be good to move this to a shared memory segment
 *	Then we could share this information with others
 */
	config = (struct sys_config *)calloc(1, sizeof(struct sys_config));
	config->format_vers = 100;
	config->heartbeat_interval = 2;
	config->deadtime_interval = 5;
	config->hopfudge = 1;
	config->log_facility = -1;

	uname(&u);
	curnode = NULL;

	if (!parse_config(cfgfile)) {
		ha_error("Heartbeat not started: configuration error.");
		return(HA_FAIL);
	}
	if (config->log_facility < 0 && *(config->logfile) == 0) {
		strcpy(config->logfile, DEFAULTLOG);
		strcpy(config->dbgfile, DEFAULTDEBUG);
		ha_log(LOG_INFO, "Neither logfile nor logfacility found.");
		ha_log(LOG_INFO, "Defaulting to " DEFAULTLOG);
	}
	if (config->log_facility >= 0) {
		openlog(cmdname, LOG_CONS | LOG_PID, config->log_facility);
	}

	if (nummedia < 1) {
		ha_error("No heartbeat ports defined");
		++errcount;
	}
	/* We should probably complain if there aren't at least two... */
	if (config->nodecount < 1) {
		ha_error("no nodes defined");
		++errcount;
	}
	if (config->authmethod == NULL) {
		ha_error("No authentication specified.");
		++errcount;
	}
	if ((curnode = lookup_node(u.nodename)) == NULL) {
		sprintf(msg, "Current node [%s] not in configuration"
		,	u.nodename);
#if defined(MITJA)
		ha_log(msg);
		add_node(from);
		curnode = lookup_node(u.nodename);
#else
		ha_error(msg);
		++errcount;
#endif
	}
	setenv(CURHOSTENV, u.nodename, 1);
	if (config->deadtime_interval <= 2 * config->heartbeat_interval) {
		sprintf(msg
		,	"Dead time [%d] is too small compared to keeplive [%d]"
		,	config->deadtime_interval, config->heartbeat_interval);
		ha_error(msg);
		++errcount;
	}
	return(errcount ? HA_FAIL : HA_OK);
}

void
init_procinfo()
{
	int	ipcid;
	char *	shm;
	if ((ipcid = shmget(IPC_PRIVATE, sizeof(*procinfo), 0666)) < 0) {
		ha_perror("Cannot shmget for process status");
		return;
	}
	if (((long)(shm = shmat(ipcid, NULL, 0))) == -1) {
		ha_perror("Cannot shmat for process status");
		shm = NULL;
		return;
	}
	if (shm) {
		procinfo = (struct pstat_shm*) shm;
		memset(shm, 0, PAGE_SIZE);
	}

	/*
	 * Go ahead and "remove" our shared memory now...
	 *
	 * This is cool because the manual says:
	 *
	 *	IPC_RMID    is  used  to mark the segment as destroyed. It
	 *	will actually  be  destroyed  after  the  last detach.
	 */
	if (shmctl(ipcid, IPC_RMID, NULL) < 0) {
		ha_perror("Cannot IPC_RMID proc status shared memory id");
	}
}


#define	KEY_HOST	"node"
#define KEY_HOPS	"hopfudge"
#define KEY_KEEPALIVE	"keepalive"
#define KEY_DEADTIME	"deadtime"
#define KEY_WATCHDOG	"watchdog"
#define	KEY_BAUDRATE	"baud"
#define	KEY_UDPPORT	"udpport"
#define	KEY_FACILITY	"facility"
#define	KEY_LOGFILE	"logfile"
#define	KEY_DBGFILE	"debugfile"
#define KEY_AUTH	"auth"

struct directive {
	const char * name;
	int (*add_func) (const char *);
}Directives[] =
{	{KEY_HOST,	add_node}
,	{KEY_HOPS,	set_hopfudge}
,	{KEY_KEEPALIVE,	set_keepalive}
,	{KEY_DEADTIME,	set_deadtime_interval}
,	{KEY_WATCHDOG,	set_watchdogdev}
,	{KEY_BAUDRATE,	set_baudrate}
,	{KEY_UDPPORT,	set_udpport}
,	{KEY_FACILITY,  set_facility}
,	{KEY_LOGFILE,   set_logfile}
,	{KEY_DBGFILE,   set_dbgfile}
,	{KEY_AUTH, 	set_auth}
};

extern const struct hb_media_fns	ip_media_fns;
extern const struct hb_media_fns	serial_media_fns;
extern const struct hb_media_fns	ppp_udp_media_fns;

const struct hb_media_fns* hbmedia_types[] = {
	&ip_media_fns,
	&serial_media_fns,
	&ppp_udp_media_fns,
};


/*
 *	Parse the configuration file and stash away the data
 */
int
parse_config(const char * cfgfile)
{
	FILE	*	f;
	char		buf[MAXLINE];
	char *		cp;
	char		directive[MAXLINE];
	int		dirlength;
	char		option[MAXLINE];
	int		optionlength;
	char		msg[MAXLINE];
	int		errcount = 0;

	if ((f = fopen(cfgfile, "r")) == NULL) {
		sprintf(msg, "Cannot open config file [%s]", cfgfile);
		ha_error(msg);
		return(HA_FAIL);
	}

	/* It's ugly, but effective  */

	while (fgets(buf, MAXLINE, f) != NULL) {
		char *	bp = buf;
		int	j;

		/* Skip over white space */
		bp += strspn(bp, WHITESPACE);

		/* Zap comments on the line */
		if ((cp = strchr(bp, COMMENTCHAR)) != NULL)  {
			*cp = EOS;
		}

		/* Ignore blank (and comment) lines */
		if (*bp == EOS) {
			continue;
		}
		/* Now we expect a directive name*/

		dirlength = strcspn(bp, WHITESPACE);
		strncpy(directive, bp, dirlength);
		directive[dirlength] = EOS;
		if (!islegaldirective(directive)) {
			sprintf(msg, "Illegal directive [%s] in %s"
			,	directive, cfgfile);
			ha_error(msg);
			++errcount;
			continue;
		}

		bp += dirlength;

		/* Skip over Delimiters */
		bp += strspn(bp, DELIMS);

		/* Check first for "parse" type (whole line) directives */

		for (j=0; j < DIMOF(hbmedia_types); ++j) {
			if (hbmedia_types[j]->parse == NULL)  {
				continue;
			}
			if (strcmp(directive, hbmedia_types[j]->type) == 0) {
				if (hbmedia_types[j]->parse(bp) != HA_OK) {
					errcount++;
				}
				*bp = EOS;
			}
		}
		while (*bp != EOS) {
			optionlength = strcspn(bp, DELIMS);
			strncpy(option, bp, optionlength);
			option[optionlength] = EOS;
			bp += optionlength;
			if (!add_option(directive, option)) {
				errcount++;
			}

			/* Skip over Delimiters */
			bp += strspn(bp, DELIMS);
		}
	}
	fclose(f);
	return(errcount ? HA_FAIL : HA_OK);
}

/*
 *	Dump the configuration file - as a configuration file :-)
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
		if (sysmedia[j]->vf->type != last_media) {
			if (last_media != NULL) {
				puts("\n");
			}
			printf("# %s heartbeat channel -------------\n"
			,	sysmedia[j]->vf->description);
			printf(" %s", sysmedia[j]->vf->type);
			last_media = sysmedia[j]->vf->type;
		}
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
	char	buf[MAXLINE];
	char	msg[MAXLINE];
	int	rc = HA_OK;
	FILE *	f;

	if ((f = fopen(cfgfile, "r")) == NULL) {
		sprintf(msg, "Cannot open config file [%s]", cfgfile);
		ha_error(msg);
		return(HA_FAIL);
	}

	while (fgets(buf, MAXLINE-1, f) != NULL) {
		char *	bp = buf;
		char *	endp;
		char	token[MAXLINE];

		if (*bp == COMMENTCHAR) {
			continue;
		}

		/* Skip over white space */
		bp += strspn(bp, WHITESPACE);
		if (*bp == EOS) {
			continue;
		}
		endp = bp + strcspn(bp, WHITESPACE);
		strncpy(token, bp, endp - bp);
		token[endp-bp] = EOS;
		if (lookup_node(token) == NULL) {
			sprintf(msg, "Bad nodename in %s [%s]", cfgfile
			,	token);
			ha_error(msg);
			rc = HA_FAIL;
		}
	}
	return(rc);
}

/*
 *	Is this a legal directive name?
 */
int
islegaldirective(const char *directive)
{
	int	j;

	for (j=0; j < DIMOF(Directives); ++j) {
		if (strcmp(directive, Directives[j].name) == 0) {
			return(HA_OK);
		}
	}
	for (j=0; j < DIMOF(hbmedia_types); ++j) {
		if (strcmp(directive, hbmedia_types[j]->type) == 0) {
			return(HA_OK);
		}
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
	char	msg[MAXLINE];

	for (j=0; j < DIMOF(Directives); ++j) {
		if (strcmp(option, Directives[j].name) == 0) {
			return((*Directives[j].add_func)(value));
		}
	}
	for (j=0; j < DIMOF(hbmedia_types); ++j) {
		if (strcmp(option, hbmedia_types[j]->type) == 0
		&&	hbmedia_types[j]->new != NULL) {
			struct hb_media* mp = hbmedia_types[j]->new(value);
			sysmedia[nummedia] = mp;
			if (mp == NULL) {
				sprintf(msg, "Illegal %s [%s] in config file"
				,	hbmedia_types[j]->description, value);
				ha_error(msg);
				return(HA_FAIL);
			}else{
				++nummedia;
				return(HA_OK);
			}
		}
	}
	sprintf(msg, "Illegal configuration directive [%s]", option);
	ha_error(msg);
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
add_node(const char * value)
{
	struct node_info *	hip;
	if (config->nodecount >= MAXNODE) {
		return(HA_FAIL);
	}
	hip = &config->nodes[config->nodecount];
	++config->nodecount;
	strcpy(hip->status, INITSTATUS);
	strcpy(hip->nodename, value);
	hip->rmt_lastupdate = 0L;
	hip->local_lastupdate = time(NULL);
	hip->track.nmissing = 0;
	hip->track.last_seq = NOSEQUENCE;
	return(HA_OK);
}

/* Set the hopfudge variable */
int
set_hopfudge(const char * value)
{
	config->hopfudge = atoi(value);

	if (config->hopfudge >= 0) {
		return(HA_OK);
	}
	return(HA_FAIL);
}

/*
 *  Set authentication method and key.
 *  Open and parse the keyfile if needed
 */

int
set_auth(const char * value)
{
	FILE *	f;
	char	buf[MAXLINE];
	char	method[MAXLINE];
	char	key[MAXLINE];
	int	i;
	int	src;
	int	rc = HA_OK;
	int	authnum;
	struct stat keyfilestat;
	authnum = atoi(value);

	if ((f = fopen(KEYFILE, "r")) == NULL) {
		ha_log(LOG_ERR, "Cannot open keyfile [%s].  Stop."
		,	KEYFILE);
		return(HA_FAIL);
	}

	if (stat(KEYFILE, &keyfilestat) < 0
	||	keyfilestat.st_mode & (S_IROTH | S_IRGRP)) {
		ha_log(LOG_ERR, "Bad permissions on keyfile"
		" [%s], 600 recommended.", KEYFILE);
		return(HA_FAIL);
	}

	while(fgets(buf, MAXLINE, f) != NULL) {
		struct auth_type *	at;
		if (*buf == COMMENTCHAR) {
			continue;
		}

		key[0] = EOS;
		if ((src=sscanf(buf, "%d%s%s", &i, method, key)) >= 2) {

			char *	cpkey;
			if (ANYDEBUG) {
				ha_log(LOG_DEBUG
				,	"Found authentication method [%s]"
				,	 method);
			}

			if ((i < 0) || (i >= MAXAUTH)) {
				ha_log(LOG_ERR, "Invalid authnum [%d] in "
				KEYFILE);
				rc = HA_FAIL;
				continue;
			}

			if ((at = findauth(method)) == NULL) {
				ha_log(LOG_ERR, "Invalid authtype [%s]"
				,	method);
				rc = HA_FAIL;
				continue;
			}

			if (strlen(key) > 0 && !at->needskey) {
				ha_log(LOG_INFO
				,	"Auth method [%s] doesn't use a key"
				,	method);
				rc = HA_FAIL;
			}
			if (strlen(key) == 0 && at->needskey) {
				ha_log(LOG_ERR
				,	"Auth method [%s] requires a key"
				,	method);
				rc = HA_FAIL;
			}

			cpkey =	malloc(strlen(key)+1);
			if (cpkey == NULL) {
				ha_log(LOG_ERR, "Out of memory for authkey");
				return(HA_FAIL);
			}
			strcpy(cpkey, key);
			config->auth_config[i].key = cpkey;
			config->auth_config[i].auth = at;
			if (i == authnum) {
				config->authnum = i;
				config->authmethod = config->auth_config+i;
			}
		}else{
			ha_log(LOG_ERR, "Auth line [%s] is invalid."
			,	buf);
			rc = HA_FAIL;
		}
	}

	fclose(f);
	if (!config->authmethod) {
		ha_log(LOG_ERR
		,	"Key number [%s] not found in keyfile [%s]"
		" - cannot start", value, KEYFILE);
		rc = HA_FAIL;
	}
	return(rc);
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
/* Set the watchdog device */
int
set_watchdogdev(const char * value)
{

	if (watchdogdev != NULL) {
		fprintf(stderr, "%s: Watchdog device multiply specified.\n"
		,	cmdname);
		return(HA_FAIL);
	}
	if ((watchdogdev = (char *)malloc(strlen(value)+1)) == NULL) {
		fprintf(stderr, "%s: Out of memory for watchdog device\n"
		,	cmdname);
		return(HA_FAIL);
	}
	strcpy(watchdogdev, value);
	return(HA_OK);
}

int
set_baudrate(const char * value)
{
	static int	baudset = 0;
	extern int	baudrate;
	extern int	serial_baud;
	if (baudset) {
		fprintf(stderr, "%s: Baudrate multiply specified.\n"
		,	cmdname);
		return(HA_FAIL);
	}
	++baudset;
	baudrate = atoi(value);
	switch(baudrate)  {
		case 9600:	serial_baud = B9600; break;
		case 19200:	serial_baud = B19200; break;
#ifdef B38400
		case 38400:	serial_baud = B38400; break;
#endif
#ifdef B57600
		case 57600:	serial_baud = B57600; break;
#endif
#ifdef B115200
		case 115200:	serial_baud = B115200; break;
#endif
#ifdef B230400
		case 230400:	serial_baud = B230400; break;
#endif
#ifdef B460800
		case 460800:	serial_baud = B460800; break;
#endif
		default:
		fprintf(stderr, "%s: invalid baudrate [%s] specified.\n"
		,	cmdname, value);
		return(HA_FAIL);
		break;
	}
	return(HA_OK);
}

int
set_udpport(const char * value)
{
	int		port = atoi(value);
	struct servent*	service;
	extern int	udpport;

	if (port <= 0) {
		fprintf(stderr, "%s: invalid port [%s] specified.\n"
		,	cmdname, value);
		return(HA_FAIL);
	}

	/* Make sure this port isn't reserved for something else */
	if ((service=getservbyport(htons(port), "udp")) != NULL) {
		if (strcmp(service->s_name, HA_SERVICENAME) != 0) {
			char msg[MAXLINE];
			sprintf(msg
			,	"%s: udp port %s reserved for service \"%s\"."
			,	cmdname, value, service->s_name);
			fprintf(stderr, "%s\n", msg);
			ha_error(msg);
			return(HA_FAIL);
		}
	}
	endservent();
	udpport = port;
	return(HA_OK);
}

/* set syslog facility config variable */
int
set_facility(const char * value)
{
	int		i;

	for(i = 0; facilitynames[i].c_name != NULL; ++i) {
		if(strcmp(value, facilitynames[i].c_name) == 0) {
			config->log_facility = facilitynames[i].c_val;
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
	return(HA_OK);
}

/* set syslog facility config variable */
int
set_logfile(const char * value)
{
	strncpy(config->logfile, value, PATH_MAX);
	return(HA_OK);
}


/* Look up the node in the configuration, returning the node info structure */
struct node_info *
lookup_node(const char * h)
{
	int			j;


	for (j=0; j < config->nodecount; ++j) {
		if (strcmp(h, config->nodes[j].nodename) == 0) {
			return(config->nodes+j);
		}
	}
	return(NULL);
}
char *
ha_timestamp(void)
{
	static char ts[64];
	struct tm*	ttm;
	time_t		now;

	time(&now);
	ttm = localtime(&now);

	sprintf(ts, "%04d/%02d/%02d_%02d:%02d:%02d"
	,	ttm->tm_year+1900, ttm->tm_mon+1, ttm->tm_mday
	,	ttm->tm_hour, ttm->tm_min, ttm->tm_sec);
	return(ts);
}

/* Very unsophisticated HA-error-logging function (deprecated) */
void
ha_error(const char *	msg)
{
	ha_log(LOG_ERR, msg);
}

/* Equally unsophisticated HA-logging function */
void
ha_perror(const char *	msg)
{
	const char *	err;
	char	errornumber[16];

	if (errno < 0 || errno >= sys_nerr) {
		sprintf(errornumber, "error %d\n", errno);
		err = errornumber;
	}else{
		err = sys_errlist[errno];
	}
	ha_log(LOG_ERR, err);
}

/* HA-logging function */
void
ha_log(int priority, const char * fmt, ...)
{
	va_list ap;
	FILE *	fp;
	char buf[MAXLINE];

	va_start(ap, fmt);
	vsnprintf(buf, MAXLINE, fmt, ap);
	if (config->log_facility < 0) {

		fp = fopen(priority == LOG_DEBUG
		?	config->dbgfile : config->logfile, "a");

		if (fp == NULL) {
			fp = stderr;
		}

		fprintf(fp, "heartbeat: %s %s: %s\n", ha_timestamp()
		,	ha_log_priority[LOG_PRI(priority)], buf);

		if (fp != stderr) {
			fclose(fp);
		}
	}else{
		syslog(priority, "%s", buf);
	}
	va_end(ap);
}


/*
 *	This routine starts everything up and kicks off the heartbeat
 *	process.
 */
int
initialize_heartbeat()
{
/*
 *	Things we have to do:
 *
 *	Create all our pipes
 *	Open all our heartbeat channels
 *	fork all our children, and start the old ticker going...
 *
 *	Everything is forked from the parent process.  That's easier to
 *	monitor, and easier to shut down.
 *		(when we get around to doing those things :-))
 */

	int		j;
	struct stat	buf;
	int		pid;
	FILE *		fifo;
	int		ourproc = 0;

	localdie = NULL;

	if (stat(FIFONAME, &buf) < 0) {
		ha_log(LOG_ERR, "FIFO %s does not exist", FIFONAME);
		return(HA_FAIL);
	}else if (!S_ISFIFO(buf.st_mode)) {
		ha_log(LOG_ERR, "%s is not a FIFO", FIFONAME);
		return(HA_FAIL);
	}

	if (pipe(status_pipe) < 0) {
		ha_error("cannot create status pipe");
		return(HA_FAIL);
	}

	/* Open all our heartbeat channels */

	for (j=0; j < nummedia; ++j) {
		struct hb_media* smj = sysmedia[j];

		if (pipe(smj->wpipe) < 0) {
			ha_error("cannot create hb channel pipe");
			return(HA_FAIL);
		}
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "opening %s %s", smj->vf->type
			,	smj->name);
		}
		if (smj->vf->open(smj) != HA_OK) {
			ha_log(LOG_ERR, "cannot open %s %s"
			,	smj->vf->type
			,	smj->name);
			return(HA_FAIL);
		}
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "%s channel %s now open..."
			,	smj->vf->type, smj->name);
		}
	}
	ADDPROC(getpid());

	ourproc = procinfo->nprocs;
	procinfo->nprocs++;
	curproc = &procinfo->info[ourproc];
	curproc->type = PROC_CONTROL;
	curproc->pid = getpid();


	/* Now the fun begins... */
/*
 *	Optimal starting order:
 *		master_status_process();
 *		read_child();
 *		write_child();
 *		control_process(FILE * f);
 *
 */
	ourproc = procinfo->nprocs;
	procinfo->nprocs++;
	switch ((pid=fork())) {
		case -1:	ha_error("Can't fork master status process!");
				return(HA_FAIL);
				break;

		case 0:		/* Child */
				curproc = &procinfo->info[ourproc];
				curproc->type = PROC_MST_STATUS;
				curproc->pid = getpid();
				master_status_process();
				ha_error("master status process exiting");
				cleanexit(1);
	}
	ADDPROC(pid);
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "master status process pid: %d\n", pid);
	}

	for (j=0; j < nummedia; ++j) {
		struct hb_media* mp = sysmedia[j];

		ourproc = procinfo->nprocs;
		procinfo->nprocs++;

		switch ((pid=fork())) {
			case -1:	ha_error("Can't fork write process");
					return(HA_FAIL);
					break;

			case 0:		/* Child */
					curproc = &procinfo->info[ourproc];
					curproc->type = PROC_HBWRITE;
					curproc->pid = getpid();
					write_child(mp);
					ha_error("write process exiting");
					cleanexit(1);
		}
		ADDPROC(pid);
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "write process pid: %d\n", pid);
		}
		ourproc = procinfo->nprocs;
		procinfo->nprocs++;

		switch ((pid=fork())) {
			case -1:	ha_error("Can't fork read process");
					return(HA_FAIL);
					break;

			case 0:		/* Child */
					curproc = &procinfo->info[ourproc];
					curproc->type = PROC_HBREAD;
					curproc->pid = getpid();
					read_child(mp);
					ha_error("read child process exiting");
					cleanexit(1);
		}
		ADDPROC(pid);
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "read child process pid: %d\n", pid);
		}
	}

	fifo = fopen(FIFONAME, "r");
	if (fifo == NULL) {
		ha_error("FIFO open failed.");
	}
	(void)open(FIFONAME, O_WRONLY);	/* Keep reads from failing */
	control_process(fifo);
	ha_error("control_process exiting");
	cleanexit(1);
	/*NOTREACHED*/
	return(HA_OK);
}



void
read_child(struct hb_media* mp)
{
	int	msglen;
	char	msg[MAXLINE];
	int	rc;
	int	statusfd = status_pipe[P_WRITEFD];
	for (;;) {
		struct	ha_msg*	m = mp->vf->read(mp);
		char *		sm;
		if (m == NULL) {
			continue;
		}

		sm = msg2string(m);
		if (sm != NULL) {
			msglen = strlen(sm);
			if (DEBUGPKT) {
				ha_log(LOG_DEBUG
				, "Writing %d bytes/%d fields to status pipe"
				,	msglen, m->nfields);
			}
			if (DEBUGPKTCONT) {
				ha_log(LOG_DEBUG, sm);
			}

			if ((rc=write(statusfd, sm, msglen)) != msglen)  {
				/* Try one extra time if we got EINTR */
				if (errno != EINTR
				||	(rc=write(statusfd, sm, msglen))
				!=	msglen)  {
					sprintf(msg
					,	"Write failure [%d/%d] %s"
					,	rc
					,	errno
					,	"to status pipe");
					ha_perror(msg);
				}
			}
			free(sm);
		}
		ha_msg_del(m);
	}
}


void
write_child(struct hb_media* mp)
{
	int	ourpipe =	mp->wpipe[P_READFD];
	FILE *	ourfp		= fdopen(ourpipe, "r");
	for (;;) {
		struct ha_msg *	msgp = msgfromstream(ourfp);
		if (msgp == NULL) {
			continue;
		}
		if (mp->vf->write(mp, msgp) != HA_OK) {
			char msg[MAXLINE];
			sprintf(msg, "write failure on %s %s."
			,	mp->vf->type, mp->name);
			ha_perror(msg);
		}
		ha_msg_del(msgp);
	}
}


/* The master control process -- reads control fifo, sends msgs to cluster */
/* Not much to this one, eh? */
void
control_process(FILE * fp)
{
	int	statusfd = status_pipe[P_WRITEFD];
	struct msg_xmit_hist	msghist;
	init_xmit_hist (&msghist);

	/* Catch and propagate debugging level signals... */
	signal(SIGUSR1, parent_debug_sig);
	signal(SIGUSR2, parent_debug_sig);

	for(;;) {
		struct ha_msg *	msg = controlfifo2msg(fp);
		char *		smsg;
		int		j;
		int		len;
		const char *	cseq;
		unsigned long	seqno;

		if (msg == NULL || (cseq = ha_msg_value(msg, F_SEQ)) == NULL
		||	sscanf(cseq, "%lx", &seqno) != 1 ||	seqno <= 0) {
			ha_error("control_process: bad sequence number");
			continue;
		}
		/* Convert it to a string and log original message for re-xmit */
		smsg = msg2string(msg);

		/* If it didn't convert, throw it away */
		if (smsg == NULL) {
			continue;
		}
		add2_xmit_hist (&msghist, msg, seqno);

		len = strlen(smsg);

		/* Copy the message to the status process */
		write(statusfd, smsg, len);

		/* And send it to all our heartbeat interfaces */
		for (j=0; j < nummedia; ++j) {
			write(sysmedia[j]->wpipe[P_WRITEFD], smsg, len);
		}
		free(smsg);
	}
	/* That's All Folks... */
}

/* The master status process */
void
master_status_process(void)
{
	struct node_info *	thisnode;
	FILE *			f = fdopen(status_pipe[P_READFD], "r");
	struct ha_msg *	msg = NULL;


	init_status_alarm();
	init_watchdog();

	clearerr(f);

	for (;; (msg != NULL) && (ha_msg_del(msg),msg=NULL, 1)) {
		time_t	msgtime;
		const char *	from;
		const char *	ts;
		const char *	type;
		unsigned char * cksum;

		msg = msgfromstream(f);

		if (msg == NULL) {
			ha_error("NULL message in master_status_process");
			continue;
		}

		/* Extract message type, originator, timestamp, cksum and auth*/
		type = ha_msg_value(msg, F_TYPE);
		from = ha_msg_value(msg, F_ORIG);
		ts = ha_msg_value(msg, F_TIME);

		if (from == NULL || ts == NULL || type == NULL) {
			ha_error("master_status_process: missing from/ts/type");
			continue;
		}

		if (!isauthentic(msg)) {
			ha_log(LOG_DEBUG
			,       "master_status_process: node [%s]"
			" failed authentication", from);
			continue;
		}else if(ANYDEBUG) {
			ha_log(LOG_DEBUG
			,       "master_status_process: node [%s] cksum [%s] ok"
			,	from, cksum);
		}
		/* If a node isn't in the configfile but */

		thisnode = lookup_node(from);
		if (thisnode == NULL) {
#if defined(MITJA)
			ha_log(LOG_WARN
			,   "master_status_process: new node [%s] in message"
			,	from);
			add_node(from);
#else
			ha_log(LOG_ERR
			,   "master_status_process: bad node [%s] in message"
			,	from);
			ha_log_message(msg);
			continue;
#endif
		}

		/* Is this message a duplicate, or destined for someone else? */
		if (should_drop_message(thisnode, msg)) {
			continue;
		}

		/* Is this a status update message? */
		if (strcasecmp(type, T_STATUS) == 0) {
			const char *	status;


			sscanf(ts, "%lx", &msgtime);
			status = ha_msg_value(msg, F_STATUS);
			if (status == NULL)  {
				ha_error("master_status_process: "
				"status update without "
				F_STATUS " field");
				continue;
			}

			/* Do we already have a newer status? */
			if (msgtime < thisnode->rmt_lastupdate) {
				continue;
			}

			heartbeat_monitor(msg);

			thisnode->rmt_lastupdate = msgtime;
			thisnode->local_lastupdate = time(NULL);

			/* Is the status the same? */
			if (strcasecmp(thisnode->status, status) != 0) {
				ha_log(LOG_INFO
				,	"node %s: status %s"
				,	thisnode->nodename
				,	status);
				notify_world(msg, thisnode->status);
				strcpy(thisnode->status, status);
			}

			/* Did we get a status update on ourselves? */
			if (thisnode == curnode) {
				tickle_watchdog();
			}
		}else{
			notify_world(msg, thisnode->status);
		}
	}
}

/* Function called to set up status alarms */
void
init_status_alarm(void)
{
	signal(SIGALRM, ding);
	alarm(1);
}


/* Notify the (external) world of an HA event */
void
notify_world(struct ha_msg * msg, const char * ostatus)
{
/*
 *	We invoke our "rc" script with the following arguments:
 *
 *	0:	RC_ARG0	(always the same)
 *	1:	lowercase version of command ("type" field)
 *
 *	All message fields get put into environment variables
 *
 *	The rc script, in turn, runs the scripts it finds in the rc.d
 *	directory (or whatever we call it... ) with the same arguments.
 *
 *	We set the following environment variables for the RC script:
 *	HA_CURHOST:	the node name we're running on
 *	HA_OSTATUS:	Status of node (before this change)
 *
 */
	char		command[STATUSLENG];
	const char *	argv[MAXFIELDS+3];
	const char *	fp;
	char *		tp;
	int		pid, status;

	tp = command;

	fp  = ha_msg_value(msg, F_TYPE);
	ASSERT(fp != NULL && strlen(fp) < STATUSLENG);

	if (fp == NULL || strlen(fp) > STATUSLENG)  {
		return;
	}

	while (*fp) {
		if (isupper(*fp)) {
			*tp = tolower(*fp);
		}else{
			*tp = *fp;
		}
		++fp; ++tp;
	}
	*tp = EOS;
	argv[0] = RC_ARG0;
	argv[1] = command;
	argv[2] = NULL;

	switch ((pid=fork())) {

		case -1:	ha_error("Can't fork to notify world!");
				break;


		case 0:	{	/* Child */
				int	j;
				for (j=0; j < msg->nfields; ++j) {
					char ename[64];
					sprintf(ename, "HA_%s", msg->names[j]);
					setenv(ename, msg->values[j], 1);
				}
				setenv(OLDSTATUS, ostatus, 1);
				execv(RCSCRIPT, (char **)argv);

				ha_log(LOG_ERR, "cannot exec %s", RCSCRIPT);
				cleanexit(1);
				/*NOTREACHED*/
				break;
			}


		default:	/* Parent */
#if WAITFORCOMMANDS
				waitpid(pid, &status, 0);
#else
				(void)status;
#endif
	}
}

void
debug_sig(int sig)
{
	switch(sig) {
		case SIGUSR1:
			++debug;
			break;

		case SIGUSR2:
			if (debug > 0) {
				--debug;
			}else{
				debug=0;
			}
			break;
	}
	ha_log(LOG_DEBUG, "debug now set to %d [pid %d]", debug, getpid());
	if (curproc) {
		const char *	ct;

		switch(curproc->type) {
			case PROC_UNDEF:	ct = "UNDEF";		break;
			case PROC_CONTROL:	ct = "CONTROL";		break;
			case PROC_MST_STATUS:	ct = "MST_STATUS";	break;
			case PROC_HBREAD:	ct = "HBREAD";		break;
			case PROC_HBWRITE:	ct = "HBWRITE";		break;
			case PROC_PPP:		ct = "PPP";		break;
			default:		ct = "huh?";		break;
		}

		ha_log(LOG_DEBUG, "MSG info: %ld/%ld age %ld [pid%d/%s]"
		,	curproc->allocmsgs, curproc->totalmsgs
		,	time(NULL) - curproc->lastmsg, curproc->pid, ct);
	}
}


void
parent_debug_sig(int sig)
{
	debug_sig(sig);
	signal_all(sig);
}

/* Ding!  Activated once per second in the status process */
void
ding(int sig)
{
	static int dingtime = 1;
	signal(SIGALRM, ding);

	if (debug) {
		ha_log(LOG_DEBUG, "Ding!");
	}

	dingtime --;
	if (dingtime <= 0) {
		dingtime = config->heartbeat_interval;
		/* Send out our status update */
		send_local_status();
	}
	/* Now we need to scan our nodes to see if any have timed out */
	check_node_timeouts();
	alarm(1);
}

/* See if any nodes have timed out */
void
check_node_timeouts(void)
{
	time_t	now = time(NULL);
	struct node_info *	hip;
	time_t	TooOld = now - config->deadtime_interval;
	int	j;


	for (j=0; j < config->nodecount; ++j) {
		hip= &config->nodes[j];
		/* If it's recently updated, or already dead, ignore it */
		if (hip->local_lastupdate >= TooOld
		||	strcmp(hip->status, DEADSTATUS) == 0 ) {
			continue;
		}
		mark_node_dead(hip);
	}


}

/* Set our local status to the given value, and send it out*/
int
set_local_status(const char * newstatus)
{
	if (strcmp(newstatus, curnode->status) != 0
	&&	strlen(newstatus) > 1 && strlen(newstatus) < STATUSLENG) {
		strcpy(curnode->status, newstatus);
		send_local_status();
		return(HA_OK);
	}
	return(HA_FAIL);
}

int
send_cluster_msg(struct ha_msg* msg)
{
	char *	smsg;
	const char *	type;

	if (msg == NULL || (type = ha_msg_value(msg, F_TYPE)) == NULL) {
		ha_error("Invalid message in send_cluster_msg");
		return(HA_FAIL);
	}

	if ((smsg = msg2string(msg)) == NULL) {
		ha_error("out of memory in send_cluster_msg");
		return(HA_FAIL);
	}

	{
	        int     ffd = open(FIFONAME, O_WRONLY);
		int	length;

		if (ffd < 0) {
			free(smsg);
			return(HA_FAIL);
		}

		length=strlen(smsg);
		write(ffd, smsg, length);
		close(ffd);
	}
	free(smsg);

	return(HA_OK);
}

/* Send our local status out to the cluster */
int
send_local_status(void)
{
	struct ha_msg *	m;
	int		rc;


	if (debug){
		ha_log(LOG_DEBUG, "Sending local status");
	}
	if ((m=ha_msg_new(0)) == NULL) {
		ha_error("Cannot send local status");
		return(HA_FAIL);
	}
	if (ha_msg_add(m, F_TYPE, T_STATUS) == HA_FAIL
	||	ha_msg_add(m, F_STATUS, curnode->status) == HA_FAIL) {
		ha_error("send_local_status: Cannot create local status msg");
		rc = HA_FAIL;
	}else{
		rc = send_cluster_msg(m);
	}

	ha_msg_del(m);
	return(rc);
}

/* Mark the given node dead */
void
mark_node_dead(struct node_info *hip)
{
	struct ha_msg *	hmsg;
	char		timestamp[16];

	if ((hmsg = ha_msg_new(6)) == NULL) {
		ha_error("no memory to mark node dead");
		return;
	}

	sprintf(timestamp, "%lx", time(NULL));

	if (	ha_msg_add(hmsg, F_TYPE, T_STATUS) == HA_FAIL
	||	ha_msg_add(hmsg, F_SEQ, "1") == HA_FAIL
	||	ha_msg_add(hmsg, F_TIME, timestamp) == HA_FAIL
	||	ha_msg_add(hmsg, F_ORIG, hip->nodename) == HA_FAIL
	||	ha_msg_add(hmsg, F_STATUS, "dead") == HA_FAIL
	||	ha_msg_add(hmsg, F_COMMENT, "timeout") == HA_FAIL) {
		ha_error("no memory to mark node dead");
		ha_msg_del(hmsg);
		return;
	}
	ha_log(LOG_WARNING, "node %s: is dead", hip->nodename);

	heartbeat_monitor(hmsg);
	notify_world(hmsg, hip->status);
	strcpy(hip->status, "dead");
	ha_msg_del(hmsg);
}

#define	MONFILE "/proc/ha/.control"
struct fieldname_map {
	const char *	from;
	const char *	to;
};
struct fieldname_map fmap [] = {
	{F_SEQ,		NULL},		/* Drop sequence number */
	{F_TTL,		NULL},
	{F_TYPE,	NULL},
	{F_ORIG,	"node"},
	{F_COMMENT,	"reason"},
	{F_STATUS,	"status"},
	{F_TIME,	"nodetime"},
	{F_LOAD,	"loadavg"},
	{F_AUTH,	NULL},
};

static int	monfd = -1;

#define		RETRYINTERVAL	(3600*24)	/* Once A Day... */

void init_monitor()
{
	static time_t	lasttry = 0;
	int		j;
	time_t	now;

	if (monfd >= 0) {
		return;
	}
	now = time(NULL);

	if ((now - lasttry) < RETRYINTERVAL) {
		return;
	}

	if ((monfd = open(MONFILE, O_WRONLY)) < 0) {
		ha_perror("Cannot open " MONFILE);
		lasttry = now;
		return;
	}

	for (j=0; j < config->nodecount; ++j) {
		char		mon[MAXLINE];
		sprintf(mon, "add=?\ntype=node\nnode=%s\n"
		,	config->nodes[j].nodename);
		write(monfd, mon, strlen(mon));
	}
}

void
heartbeat_monitor(struct ha_msg * msg)
{
	char		mon[MAXLINE];
	char *		outptr;
	int		j;
	int		k;
	const char *	last = mon + MAXLINE-1;
	int		rc, size;

	init_monitor();
	if (monfd < 0) {
		return;
	}

	sprintf(mon, "hb=?\nhbtime=%lx\n", time(NULL));
	outptr = mon + strlen(mon);

	for (j=0; j < msg->nfields; ++j) {
		const char *	name = msg->names[j];
		const char *	value = msg->values[j];
		int	namelen, vallen;

		if (name == NULL || value == NULL) {
			continue;
		}
		for (k=0; k < DIMOF(fmap); ++k) {
			if (strcmp(name, fmap[k].from) == 0) {
				name = fmap[k].to;
				break;
			}
		}
		namelen = strlen(name);
		vallen = strlen(value);
		if (outptr + (namelen+vallen+2) >= last) {
			ha_error("monitor message too long");
			return;
		}
		strcat(outptr, name);
		outptr += namelen;
		strcat(outptr, "=");
		outptr += 1;
		strcat(outptr, value);
		outptr += vallen;
		strcat(outptr, "\n");
		outptr += 1;
	}


	size = outptr - mon;
	errno = 0;
	if ((rc=write(monfd, mon, size)) != size) {
		ha_perror("cannot write monitor message");
		close(monfd);
		monfd = -1;
	}
}

/*  usage statement */
void
usage(void)
{
	const char *	optionargs = OPTARGS;
	const char *	thislet;

	fprintf(stderr, "\nUsage: %s [-", cmdname);
	for (thislet=optionargs; *thislet; ++thislet) {
		if (thislet[0] != ':' &&  thislet[1] != ':') {
			fputc(*thislet, stderr);
		}
	}
	fputc(']', stderr);
	for (thislet=optionargs; *thislet; ++thislet) {
		if (thislet[1] == ':') {
			const char *	desc = "unknown-flag-argument";

			/* Put a switch statement here eventually... */

			fprintf(stderr, " [-%c %s]", *thislet, desc);
		}
	}
	fprintf(stderr, "\n");
	cleanexit(1);
}


int
main(int argc, const char ** argv)
{
	int	flag;
	int	argerrs = 0;
	int	j;
	extern int	optind;

	cmdname = argv[0];
	Argc = argc;
	Argv = argv;


	while ((flag = getopt(argc, (char **)argv, OPTARGS)) != EOF) {

		switch(flag) {

			case 'v':
				++verbose;
				break;

			case 'd':
				++debug;
				break;

			default:
				++argerrs;
				break;
		}
	}

	if (optind > argc) {
		++argerrs;
	}
	if (argerrs) {
		usage();
	}


	setenv(HADIRENV, HA_D, 1);
	setenv(DATEFMT, HA_DATEFMT, 1);
	setenv(HAFUNCENV, HA_FUNCS, 1);

	init_procinfo();
	/* Perform static initialization for all our heartbeat medium types */
	for (j=0; j < DIMOF(hbmedia_types); ++j) {
		if (hbmedia_types[j]->init() != HA_OK) {
			ha_log(LOG_ERR
			,	"Initialization failure for %s channel"
			,	hbmedia_types[j]->type);
			return(HA_FAIL);
		}
	}

	if (init_config(CONFIG_NAME) && parse_ha_resources(RESOURCE_CFG)) {
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG
			,	"HA configuration OK.  Heartbeat started.\n");
		}
		if (verbose) {
			dump_config();
		}
		make_daemon();
		setenv(LOGFENV, config->logfile, 1);
		setenv(DEBUGFENV, config->dbgfile, 1);
		initialize_heartbeat();
	}else{
		ha_log(LOG_ERR, "Configuration error, heartbeat not started.");
		cleanexit(1);
	}
	cleanexit(0);

	/*NOTREACHED*/
	return(HA_FAIL);
}

void
cleanexit(rc)
	int	rc;
{
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "Exiting from pid %d [%d]"
		,	getpid(), rc);
	}
	if(config->log_facility >= 0) {
		closelog();
	}
	exit(rc);
}

void
signal_all(int sig)
{
	int us = getpid();
	int j;
	for (j=0; j < num_procs; ++j) {
		if (processes[j] != us) {
			if (ANYDEBUG) {
				ha_log(LOG_DEBUG,
				       "%d: Signalling process %d [%d]"
				       ,	getpid(), processes[j], sig);
			}
			kill(processes[j], sig);
		}
	}
	switch (sig) {
		case SIGTERM:
			if (localdie) {
				(*localdie)();
			}
			cleanexit(sig);
			break;
	}
}

#define	IGNORESIG(s)	((void)signal((s), SIG_IGN))

void
make_daemon(void)
{
	pid_t	pid;
	FILE *	lockfd;

	/* See if we're already running... */

	if ((lockfd = fopen(PIDFILE, "r")) != NULL
	&&	fscanf(lockfd, "%d", &pid) == 1 && pid > 0) {
		if (kill(pid, 0) >= 0 || errno != ESRCH) {
			fprintf(stderr, "%s: already running [pid %d].\n"
			,	cmdname, pid);
			exit(HA_FAILEXIT);
		}
	}
	if (lockfd != NULL) {
		fclose(lockfd);
	}

	/* Guess not. Go ahead and start things up*/

	if ((pid = fork()) > 0) {
		lockfd = fopen(PIDFILE, "w");
		if (lockfd != NULL) {
			fprintf(lockfd, "%d\n", pid);
			fclose(lockfd);
		}
		exit(HA_OKEXIT);
	}else if (pid < 0) {
		fprintf(stderr, "%s: could not start daemon\n", cmdname);
		perror("fork");
		exit(HA_FAILEXIT);
	}
	if (setsid() < 0) {
		fprintf(stderr, "%s: could not start daemon\n", cmdname);
		perror("setsid");
	}

#ifdef	SIGTTOU
	(void)signal(SIGTTOU, SIG_IGN);
#endif
#ifdef	SIGTTIN
	(void)signal(SIGTTIN, SIG_IGN);
#endif

	/* Maybe we shouldn't do this on Linux */
#ifdef	SIGCHLD
	(void)signal(SIGCHLD, SIG_IGN);
#endif

#ifdef	SIGHUP
	(void)signal(SIGHUP, SIG_IGN);
#endif
#ifdef	SIGQUIT
	(void)signal(SIGQUIT, SIG_IGN);
#endif
#ifdef	SIGSTP
	(void)signal(SIGSTP, SIG_IGN);
#endif
	(void)signal(SIGUSR1, debug_sig);
	(void)signal(SIGUSR2, debug_sig);

	(void)signal(SIGTERM, signal_all);
	umask(022);
	close(FD_STDIN);
	close(FD_STDOUT);
	if (!debug) {
		close(FD_STDERR);
	}
	chdir(HA_D);
}

void
init_watchdog(void)
{
	if (watchdogfd < 0 && watchdogdev != NULL) {
		watchdogfd = open(watchdogdev, O_WRONLY);
		if (watchdogfd >= 0) {
			ha_log(LOG_NOTICE, "Using watchdog device: %s"
			       , watchdogdev);
			tickle_watchdog();
		}else{
			ha_log(LOG_ERR, "Cannot open watchdog device: %s"
			,	watchdogdev);
		}
	}
}

void
tickle_watchdog(void)
{
	if (watchdogfd >= 0) {
		if (write(watchdogfd, "", 1) != 1) {
			char msg[128];
			close(watchdogfd);
			watchdogfd=-1;
			sprintf(msg, "Watchdog write failure: closing %s!\n"
			,	watchdogdev);
			ha_perror(msg);
		}
	}
}

void
ha_assert(const char * assertion, int line, const char * file)
{
	ha_log(LOG_ERR, "Assertion \"%s\" failed on line %d in file \"%s\""
	,	assertion, line, file);
	cleanexit(1);
}

/*
 *	Check to see if we should copy this packet further into the ring
 */
int
should_ring_copy_msg(struct ha_msg *m)
{
	const char *	us = curnode->nodename;
	const char *	from;	/* Originating Node name */
	const char *	ttl;	/* Time to live */

	/* Get originator and time to live field values */
	if ((from = ha_msg_value(m, F_ORIG)) == NULL
	||	(ttl = ha_msg_value(m, F_TTL)) == NULL) {
			ha_error("bad packet in should_copy_ring_pkt");
	}
	/* Is this message from us? */
	if (strcmp(from, us) == 0 || atoi(ttl) <= 0) {
		/* Avoid infinite loops... Ignore this message */
		return(0);
	}

	/* Must be OK */
	return(1);
}

void
dump_msg(const struct ha_msg *msg)
{
	char *	s = msg2string(msg);
	ha_log(LOG_DEBUG, "Message dump: %s", s);
	free(s);
}

/*
 *	Right now, this is a little too simple.  There is no provision for
 *	sequence number wraparounds.  But, it will take a very long
 *	time to wrap around (~ 100 years)
 *
 *	I suspect that there are better ways to do this, but this will
 *	do for now...
 */
#define	SEQGAP	100	/* A heuristic number */
#define	KEEPIT	0
#define	DROPIT	1

/*
 *	Should we ignore this packet, or pay attention to it?
 */
int
should_drop_message(struct node_info * thisnode, const struct ha_msg *msg)
{
	struct seqtrack *	t = &thisnode->track;
	const char *		cseq = ha_msg_value(msg, F_SEQ);
	const char *		to = ha_msg_value(msg, F_TO);
	unsigned long		seq;
	int			IsToUs;
	int			j;
	char			msgbuf[MAXLINE];


	if (cseq  == NULL || sscanf(cseq, "%lx", &seq) != 1 ||	seq <= 0) {
		ha_error("should_drop_message: bad sequence number");
		dump_msg(msg);
		return(DROPIT);
	}

	/*
	 * We need to do sequence number processing on every
	 * packet, even those that aren't sent to us.
	 */
	IsToUs = (to == NULL) || (strcmp(to, curnode->nodename) == 0);

	/* Is this packet in sequence? */
	if (t->last_seq == NOSEQUENCE || seq == (t->last_seq+1)) {
		t->last_seq = seq;
		return(IsToUs ? KEEPIT : DROPIT);
	}else if (seq == t->last_seq) {
		/* Same as last-seen packet -- very common case */
		if (DEBUGPKT) {
			ha_log(LOG_DEBUG,
			       "should_drop_message: Duplicate packet(1)");
		}
		return(DROPIT);
	}

	/* Not in sequence... Hmmm... */

	/* Is it newer than the last packet we got? */

	if (seq > t->last_seq) {

		/* Yes.  Record the missing packets */
		unsigned long	k;
		unsigned long	nlost;
		nlost = ((unsigned long)(seq - (t->last_seq+1)));
		sprintf(msgbuf, "%lu lost packet(s) for [%s] [%lu:%lu]"
		,	nlost, thisnode->nodename, t->last_seq, seq);
		ha_error(msgbuf);

		if (nlost > SEQGAP) {
			/* Something bad happened.  Start over */
			/* This keeps the loop below from going a long time */
			t->nmissing = 0;
			t->last_seq = seq;
			ha_error("lost a lot of packets!");
			return(IsToUs ? KEEPIT : DROPIT);
		}
		/* Try and Record each of the missing sequence numbers */
		for(k = t->last_seq+1; k < seq; ++k) {
			if (t->nmissing < MAXMISSING-1) {
				t->seqmissing[t->nmissing] = k;
				++t->nmissing;
			}else{
				int		minmatch = -1;
				unsigned long	minseq;
				/*
				 * Replace the lowest numbered missing seqno
				 * with this one
				 */
				for (j=0; j < MAXMISSING; ++j) {
					if (t->seqmissing[j] == NOSEQUENCE) {
						minmatch = j;
						break;
					}
					if (minmatch < 0
					|| t->seqmissing[j] < minseq) {
						minmatch = j;
						minseq = t->seqmissing[j];
					}
				}
				t->seqmissing[minmatch] = k;
			}
		}
		t->last_seq = seq;
		return(IsToUs ? KEEPIT : DROPIT);
	}
	/*
	 * This packet appears to be older than the last one we got.
	 */

	/*
	 * Is it a (recorded) missing packet?
	 */
	for (j=0; j < t->nmissing; ++j) {
		/* Is this one of our missing packets? */
		if (seq == t->seqmissing[j]) {
			/* Yes.  Delete it from the list */
			t->seqmissing[j] = NOSEQUENCE;
			/* Did we delete the last one on the list */
			if (j == (t->nmissing-1)) {
				t->nmissing --;
			}
			return(IsToUs ? KEEPIT : DROPIT);
		}
	}
	/*
	 * Is it a the result of a restart?
	 *
	 * We say it's the result of a restart
	 *	IF the sequence number is a small or a lot smaller than
	 *		the last known sequence number
	 *	AND the timestamp on the packet is newer than the
	 *		last known timestamp for that node.
	 */

	/* Does this look like a restart? */
	if (seq < SEQGAP || ((seq+SEQGAP) < t->last_seq)) {
		const char *	sts;
		time_t	newts = 0L;
		if ((sts = ha_msg_value(msg, F_TIME)) == NULL
		||	sscanf(sts, "%lx", &newts) != 1 || newts == 0L) {
			/* Toss it.  No valid timestamp */
			ha_error("should_drop_message: bad timestamp");
			return(DROPIT);
		}
		/* Is the timestamp newer, and the sequence number smaller? */
		if (newts > thisnode->rmt_lastupdate) {
			/* Yes.  Looks like a software restart to me... */
			thisnode->rmt_lastupdate = newts;
			ha_log(LOG_NOTICE  /* or just INFO ? */
			,	"node %s seq restart %ld vs %ld"
			,	thisnode->nodename
			,	seq, t->last_seq);
			t->nmissing = 0;
			t->last_seq = seq;
			return(IsToUs ? KEEPIT : DROPIT);
		}
	}
	/* This is a duplicate packet (or a really old one we lost track of) */
	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "should_drop_message: Duplicate packet");
		dump_msg(msg);
	}
	return(DROPIT);

}

/* Initialize the transmit history */
void
init_xmit_hist (struct msg_xmit_hist * hist)
{
	int	j;

	hist->lastmsg = MAXMSGHIST-1;
	hist->hiseq = hist->lowseq = 0;
	for (j=0; j< MAXMSGHIST; ++j) {
		hist->msgq[j] = NULL;
		hist->seqnos[j] = 0;
	}
}

/* Add a packet to a channel's transmit history */
void
add2_xmit_hist (struct msg_xmit_hist * hist, struct ha_msg* msg
,	unsigned long seq)
{
	int	slot;

	/* Figure out which slot to put the message in */
	slot = hist->lastmsg+1;
	if (slot >= MAXMSGHIST) {
		slot = 0;
	}
	hist->hiseq = seq;
	if (hist->lowseq == 0) {
		hist->lowseq = seq;
	}
	/* Throw away old packet in this slot */
	if (hist->msgq[slot] != NULL) {
		hist->lowseq = hist->seqnos[slot];
		ha_msg_del(hist->msgq[slot]);
	}
	hist->msgq[slot] = msg;
	hist->seqnos[slot] = seq;
	hist->lastmsg = slot;
}


#ifdef IRIX
void
setenv(const char *name, const char * value, int why)
{
	char * envp = xmalloc(strlen(name)+strlen(value)+2);
	sprintf(envp, "%s=%s", name, value);
	putenv(envp);
}
#endif
/*
 * $Log: heartbeat.c,v $
 * Revision 1.4  1999/09/27 04:14:42  alanr
 * We now allow multiple strings, and the code for logging seems to also be working...  Thanks Guyscd ..
 *
 * Revision 1.3  1999/09/26 22:00:02  alanr
 * Allow multiple auth strings in auth file... (I hope?)
 *
 * Revision 1.2  1999/09/26 14:01:05  alanr
 * Added Mijta's code for authentication and Guenther Thomsen's code for serial locking and syslog reform
 *
 * Revision 1.1.1.1  1999/09/23 15:31:24  alanr
 * High-Availability Linux
 *
 * Revision 1.34  1999/09/16 05:50:20  alanr
 * Getting ready for 0.4.3...
 *
 * Revision 1.33  1999/09/15 17:47:13  alanr
 * removed the floating point load average calculation.  We didn't use it for anything anyway...
 *
 * Revision 1.32  1999/09/14 22:35:00  alanr
 * Added shared memory for tracking memory usage...
 *
 * Revision 1.31  1999/08/28 21:08:07  alanr
 * added code to handle SIGUSR1 and SIGUSR2 to diddle debug levels and
 * added code to not start heartbeat up if it's already running...
 *
 * Revision 1.30  1999/08/25 06:34:26  alanr
 * Added code to log outgoing messages in a FIFO...
 *
 * Revision 1.29  1999/08/18 04:27:31  alanr
 * #ifdefed out setting signal handler for SIGCHLD to SIG_IGN
 *
 * Revision 1.28  1999/08/17 03:48:11  alanr
 * added log entry...
 *
 */
