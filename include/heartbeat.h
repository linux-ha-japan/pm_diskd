/*
 * heartbeat.h: core definitions for the Linux-HA heartbeat program
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
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
#ifndef _HEARTBEAT_H
#	define _HEARTBEAT_H 1

static const char * _heartbeat_h_Id = "$Id: heartbeat.h,v 1.2 2001/10/01 20:34:14 alan Exp $";
#ifdef SYSV
#	include <sys/termio.h>
#	define TERMIOS	termio
#	define	GETATTR(fd, s)	ioctl(fd, TCGETA, s)
#	define	SETATTR(fd, s)	ioctl(fd, TCSETA, s)
#	define	FLUSH(fd)	ioctl(fd, TCFLSH, 2)
#else
#	define TERMIOS	termios
#	include <sys/termios.h>
#	define	GETATTR(fd, s)	tcgetattr(fd, s)
#	define	SETATTR(fd, s)	tcsetattr(fd, TCSAFLUSH, s)
#	define	FLUSH(fd)	tcflush(fd, TCIOFLUSH)
#endif


#include <limits.h>
#include <syslog.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <sys/times.h>
#include <netinet/in.h>
#include <HBauth.h>
#include <ha_msg.h>
#include <HBcomm.h>
#include <stonith/stonith.h>
#include <ltdl.h>

/*
 * <syslog.h> might not contain LOG_PRI...
 * So, we define it ourselves, or error out if we can't...
 */

#ifndef LOG_PRI
#  ifdef LOG_PRIMASK
 	/* David Lee <T.D.Lee@durham.ac.uk> reports this works on Solaris */
#	define	LOG_PRI(p)      ((p) & LOG_PRIMASK)
#  else
#	error	"Syslog.h does not define either LOG_PRI or LOG_PRIMASK."
#  endif 
#endif

#define	MAXLINE		1024
#define	MAXFIELDS	30		/* Max # of fields in a msg */
#define HOSTLENG	100		/* Maximum size of "uname -a" return */
#define STATUSLENG	32		/* Maximum size of status field */
#define	MAXIFACELEN	30		/* Maximum interface length */
#define	MAXSERIAL	4
#define	MAXMEDIA	12
#define	MAXNODE		100
#define	MAXPROCS	((MAXNODE*2)+2)

#define	FIFOMODE	0600
#define	RQSTDELAY	10

#define	HA_FAIL		0
#define	HA_OK		1

/* Reasons for calling mark_node_dead */
enum deadreason {
	HBTIMEOUT,	/* Can't communicate -  timeout */
	HBSHUTDOWN,	/* Node was gracefully shut down */
};

#ifndef HA_D
#	define	HA_D		"/etc/ha.d"
#endif
#ifndef VAR_RUN_D
#	define	VAR_RUN_D	"/var/run"
#endif
#ifndef VAR_LOG_D
#	define	VAR_LOG_D	"/var/log"
#endif
#ifndef HALIB
#	define HALIB		"/usr/lib/heartbeat"
#endif
#ifndef HA_MODULE_D
#	define HA_MODULE_D	HALIB "/modules"
#endif
#ifndef HA_PLUGIN_D
#	define HA_PLUGIN_D	HALIB "/plugins"
#endif
#ifndef TTY_LOCK_D
#	if !defined(__FreeBSD__)
#		define	TTY_LOCK_D	"/var/lock"
#	else
#		define	TTY_LOCK_D	"/var/spool/lock"
#	endif
#endif

/* This is consistent with OpenBSD, and is a good choice anyway */
#define	TIME_T	unsigned long
#define	TIME_F	"%lu"
#define	TIME_X	"%lx"

/* #define HA_DEBUG */
#define	DEFAULTLOG	VAR_LOG_D "/ha-log"
#define	DEFAULTDEBUG	VAR_LOG_D "/ha-debug"
#define	DEVNULL 	"/dev/null"

#define	HA_OKEXIT	0
#define	HA_FAILEXIT	1
#define	WHITESPACE	" \t\n\r\f"
#define	DELIMS		", \t\n\r\f"
#define	COMMENTCHAR	'#'
#define	STATUS		"STATUS"
#define	INITSTATUS	"init"		/* Status of a node we've never heard from */
#define	UPSTATUS	"up"		/* Listening (we might not be xmitting) */
#define	ACTIVESTATUS	"active"	/* fully functional, and all links are up */
#define	DEADSTATUS	"dead"		/* Status of non-working link or machine */
#define	LINKUP		"up"		/* The status assigned to a working link */
#define	LOADAVG		"/proc/loadavg"
#define	PIDFILE		VAR_RUN_D "/heartbeat.pid"
#define KEYFILE         HA_D "/authkeys"
#define HA_SERVICENAME	"ha-cluster" 	/* Our official reg'd service name */
#define	UDPPORT		694		/* Our official reg'd port number */

/* Environment variables we pass to our scripts... */
#define CURHOSTENV	"HA_CURHOST"
#define OLDSTATUS	"HA_OSTATUS"
#define DATEFMT		"HA_DATEFMT"	/* Format string for date(1) */
#define LOGFENV		"HA_LOGFILE"	/* well-formed log file :-) */
#define DEBUGFENV	"HA_DEBUGLOG"	/* Debug log file */
#define LOGFACILITY	"HA_LOGFACILITY"/* Facility to use for logger */
#define HADIRENV	"HA_DIR"	/* The base HA directory */
#define HAFUNCENV	"HA_FUNCS"	/* Location of ha shell functions */
#define HANICEFAILBACK	"HA_NICEFAILBACK"	/* Location of ha shell functions */


#define	DEFAULTBAUD	B19200	/* Default serial link speed */
#define	DEFAULTBAUDRATE	19200	/* Default serial link speed as int */

/* multicast defaults */
#define DEFAULT_MCAST_IPADDR "225.0.0.1" /* Default multicast group */
#define DEFAULT_MCAST_TTL 1	/* Default multicast TTL */
#define DEFAULT_MCAST_LOOP 0	/* Default mulitcast loopback option */

#define HB_STATIC_PRIO	1	/* Used with soft realtime scheduling */

#ifndef PPP_D
#	define	PPP_D		VAR_RUN_D "/ppp.d"
#endif
#ifndef FIFONAME
#	define	FIFONAME	VAR_LIB_D "/fifo"
#endif

#define	RCSCRIPT		HA_D "/harc"
#define CONFIG_NAME		HA_D "/ha.cf"
#define RESOURCE_CFG		HA_D "/haresources"

/* dynamic module directories */
#define COMM_MODULE_DIR	HA_MODULE_D "/comm"
#define AUTH_MODULE_DIR HA_MODULE_D "/auth"

#define	STATIC		/* static */
#define	MALLOCT(t)	((t *)(ha_malloc(sizeof(t))))

/* You may need to change this for your compiler */
#ifdef HAVE_STRINGIZE
#	define	ASSERT(X)	{if(!(X)) ha_assert(#X, __LINE__, __FILE__);}
#else
#	define	ASSERT(X)	{if(!(X)) ha_assert("X", __LINE__, __FILE__);}
#endif



#define HA_DATEFMT	"%Y/%m/%d_%T\t"
#define HA_FUNCS	HA_D "/shellfuncs"

#define	RC_ARG0		"harc"


/* Which side of a pipe is which? */

#define	P_READFD	0
#define	P_WRITEFD	1

#define	FD_STDIN	0
#define	FD_STDOUT	1
#define	FD_STDERR	2

#define	MAXMISSING	16
#define	NOSEQUENCE	0xffffffffUL
struct seqtrack {
	clock_t		last_rexmit_req;
	int		nmissing;
	unsigned long	generation;	/* Heartbeat generation # */
	unsigned long	last_seq;
	unsigned long	seqmissing[MAXMISSING];
	const char *	last_iface;
};

struct link {
	clock_t		lastupdate;
	const char *	name;
	int		isping;
	char		status[STATUSLENG]; /* up or down */
	time_t rmt_lastupdate; /* node's idea of last update time for this link */
};

#define	NORMALNODE	0
#define	PINGNODE	1

struct node_info {
	int	nodetype;
	char	nodename[HOSTLENG];	/* Host name from config file */
	char	status[STATUSLENG];	/* Status from heartbeat */
	struct link links[MAXMEDIA];
	int	nlinks;
	time_t	rmt_lastupdate;		/* node's idea of last update time */
	unsigned long	status_seqno;	/* Seqno of last status update */
	clock_t	local_lastupdate;	/* Date of last update in clock_t time*/
	int	anypacketsyet;		/* True after reception of 1st pkt */
	struct seqtrack	track;
};


#define MAXAUTH	16

struct sys_config {
	time_t	cfg_time;		/* Timestamp of config file */
	time_t	auth_time;		/* Timestamp of authorization file */
	time_t	rsc_time;		/* Timestamp of haresources file */
	int	format_vers;		/* Version of this info */
	int	nodecount;		/* Number of nodes in cluster */
	int	heartbeat_interval;	/* Seconds between heartbeats */
	int	deadtime_interval;	/* Seconds before declaring dead */
	int	initial_deadtime;	/* Secs before saying dead 1st time*/
	clock_t	warntime_interval;	/* Ticks before declaring dead */
	int	hopfudge;		/* hops beyond nodecount allowed */
	int     log_facility;		/* syslog facility, if any */
	char	facilityname[PATH_MAX];	/* syslog facility name (if any) */
	char    logfile[PATH_MAX];	/* path to log file, if any */
        int     use_logfile;            /* Flag to use the log file*/
	char	dbgfile[PATH_MAX];	/* path to debug file, if any */
        int     use_dbgfile;            /* Flag to use the debug file*/
	int	rereadauth;		/* 1 if we need to reread auth file */
	unsigned long	generation;	/* Heartbeat generation # */
	int	authnum;
	Stonith*	stonith;	/* Stonith method: WE NEED A LIST TO SUPPORT MULTIPLE STONITH DEVICES PER NODE -EZA */
	struct HBauth_info* authmethod;	/* auth_config[authnum] */
	struct node_info  nodes[MAXNODE];
	struct HBauth_info  auth_config[MAXAUTH];
};


struct hb_media {
	void *		pd;		/* Private Data */
	const char *	name;		/* Unique medium name */
	char*		type;		/* Medium type */
	char*		description;	/* Medium description */
	const struct hb_media_fns*vf;	/* Virtual Functions */
	int	wpipe[2];
		/* Read by the write child processes.
		 * Written to by control and tty read processes
		 *	(for status changes and ring passthrough).
		 */
};

int parse_authfile(void);

#define	MAXMSGHIST	100
struct msg_xmit_hist {
	struct ha_msg*		msgq[MAXMSGHIST];
	int			seqnos[MAXMSGHIST];
	clock_t			lastrexmit[MAXMSGHIST];
	int			lastmsg;
	int			hiseq;
	int			lowseq; /* one less than min actually present */
};

extern struct sys_config *	config;
extern struct node_info *	curnode;
extern int			verbose;
extern int			debug;
extern int			udpport;
extern int			RestartRequested;

#define	ANYDEBUG	(debug)
#define	DEBUGAUTH	(debug >=3)
#define	DEBUGMODULE	(debug >=3)
#define	DEBUGPKT	(debug >= 4)
#define	DEBUGPKTCONT	(debug >= 5)


/* Generally useful exportable HA heartbeat routines... */
extern void		ha_error(const char * msg);
extern void		ha_assert(const char *s, int line, const char * file);
extern void		ha_log(int priority, const char * fmt, ...);
extern void		ha_perror(const char * fmt, ...);
extern int		send_local_status(void);
extern int		send_cluster_msg(struct ha_msg*msg);
extern void		cleanexit(int exitcode);
extern void		check_auth_change(struct sys_config *);
extern void		(*localdie)(void);
extern int		should_ring_copy_msg(struct ha_msg* m);
extern int		add_msg_auth(struct ha_msg * msg);
extern unsigned char * 	calc_cksum(const char * authmethod, const char * key, const char * value);
struct node_info *	lookup_node(const char *);
struct link * lookup_iface(struct node_info * hip, const char *iface);
struct link *  iface_lookup_node(const char *);
int	add_node(const char * value, int nodetype);

void*		ha_malloc(size_t size);
void*		ha_calloc(size_t nmemb, size_t size);
void		ha_free(void *ptr);
void		ha_malloc_report(void);

#ifndef HAVE_SETENV
int setenv(const char *name, const char * value, int why);
#endif

#ifndef HAVE_SCANDIR
#include <dirent.h>
int
scandir (const char *directory_name,
	struct dirent ***array_pointer,
	int (*select_function) (const struct dirent *),

#ifdef USE_SCANDIR_COMPARE_STRUCT_DIRENT
	/* This is what the Linux man page says */
	int (*compare_function) (const struct dirent**, const struct dirent**)
#else
	/* This is what the Linux header file says ... */
	int (*compare_function) (const void *, const void *)
#endif
	);
#endif /* HAVE_SCANDIR */
#endif /* _HEARTBEAT_H */
