#ifndef _HEARTBEAT_H
#	define _HEARTBEAT_H

static const char * _heartbeat_h_Id = "$Id: heartbeat.h,v 1.6 1999/10/02 18:12:09 alanr Exp $";
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
#include <db.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifndef PAGE_SIZE
#	include <sys/param.h>
#endif

#ifndef PAGE_SIZE
#	include <asm/page.h>	/* This is where Linux puts it */
#endif

#include <ha_msg.h>


#define	MAXLINE		1024
#define	MAXFIELDS	15		/* Max # of fields in a msg */
#define HOSTLENG	24		/* Maximum size of status field */
#define STATUSLENG	32		/* Maximum size of status field */
#define	MAXSERIAL	4
#define	MAXMEDIA	12
#define	MAXNODE		100
#define	MAXPROCS	((MAXNODE*2)+2)

#define	FIFOMODE	0600

#define	HA_FAIL		0
#define	HA_OK		1

#ifndef HA_D
#	define	HA_D		"/etc/ha.d"
#endif
#ifndef VAR_RUN_D
#	define	VAR_RUN_D	"/var/run"
#endif
#ifndef VAR_LOG_D
#	define	VAR_LOG_D	"/var/log"
#endif
#ifndef TTYLOCK_D
#	define	TTY_LOCK_D	"/var/lock"
#endif

#define	DEFAULTLOG	VAR_LOG_D "/ha-log"
#define	DEFAULTDEBUG	VAR_LOG_D "/ha-debug"

#define	HA_OKEXIT	0
#define	HA_FAILEXIT	1
#define	WHITESPACE	" \t\n\r\f"
#define	DELIMS		", \t\n\r\f"
#define	EOS		'\0'
#define	COMMENTCHAR	'#'
#define	STATUS		"STATUS"
#define	INITSTATUS	"unknown"
#define	DEADSTATUS	"dead"
#define	LOADAVG		"/proc/loadavg"
#define	PIDFILE		VAR_RUN_D "/heartbeat.pid"
#define KEYFILE         HA_D "/authkeys"
#define HA_SERVICENAME	"ha-heartbeat"
#define	UDPPORT		1001	/* probably already used :-) */

/* Environment variables we pass to our scripts... */
#define CURHOSTENV	"HA_CURHOST"
#define OLDSTATUS	"HA_OSTATUS"
#define DATEFMT		"HA_DATEFMT"	/* Format string for date(1) */
#define LOGFENV		"HA_LOGFILE"	/* well-formed log file :-) */
#define DEBUGFENV	"HA_DEBUGLOG"	/* Debug log file */
#define LOGFACILITY	"HA_LOGFACILITY"/* Facility to use for logger */
#define HADIRENV	"HA_DIR"	/* The base HA directory */
#define HAFUNCENV	"HA_FUNCS"	/* Location of ha shell functions */


#define	DEFAULTBAUD	B19200	/* Default serial link speed */
#define	DEFAULTBAUDRATE	19200	/* Default serial link speed as int */

#ifndef PPP_D
#	define	PPP_D		VAR_RUN_D "/ppp.d"
#endif
#ifndef FIFONAME
#	define	FIFONAME	VAR_RUN_D "/heartbeat-fifo"
#endif
#define	RCSCRIPT		HA_D "/harc"
#define CONFIG_NAME		HA_D "/ha.cf"
#define RESOURCE_CFG		HA_D "/haresources"

#define	STATIC		/* static */
#define	MALLOCT(t)	((t *)(malloc(sizeof(t))))

/* You may need to change this for your compiler */
#define	ASSERT(X)	{if(!(X)) ha_assert(__STRING(X), __LINE__, __FILE__);}



#define HA_DATEFMT	"%Y/%m/%d_%T\t"
#define HA_FUNCS	HA_D "/shellfuncs"

#define	RC_ARG0		"harc"
#define	DIMOF(a)	((sizeof(a)/sizeof(a[0])))


/* Which side of a pipe is which? */

#define	P_READFD	0
#define	P_WRITEFD	1

#define	FD_STDIN	0
#define	FD_STDOUT	1
#define	FD_STDERR	2

#define	MAXMISSING	16
#define	NOSEQUENCE	0xffffffffUL
struct seqtrack {
	int		nmissing;
	unsigned long	last_seq;
	unsigned long	seqmissing[MAXMISSING];
};
struct node_info {
	char	nodename[HOSTLENG];	/* Host name from config file */
	char	status[STATUSLENG];	/* Status from heartbeat */
	time_t	rmt_lastupdate;		/* node's idea of last update time */
	time_t	local_lastupdate;	/* Date of last update in localtime */
	struct seqtrack	track;
};

struct auth_info {
	struct auth_type *	auth;
	char *			key;
};

struct auth_type {
	const char *	authname;
	const unsigned char *	(*auth)(const struct auth_info * authinfo
	,	const char *data);
	int		needskey;
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
	int	hopfudge;		/* hops beyond nodecount allowed */
	int     log_facility;		/* syslog facility, if any */
	char    logfile[PATH_MAX];	/* path to log file, if any */
	char	dbgfile[PATH_MAX];	/* path to debug file, if any */
	int	rereadauth;		/* 1 if we need to reread auth file */
	int	authnum;
	struct auth_info* authmethod;	/* auth_config[authnum] */
	struct node_info  nodes[MAXNODE];
	struct auth_info  auth_config[MAXAUTH];
};


struct hb_media {
	void *			pd;	/* Private Data */
	const char *		name;	/* Unique medium name */
	const struct hb_media_fns*vf;	/* Virtual Functions */
	int	wpipe[2];
		/* Read by the write child processes.
		 * Written to by control and tty read processes
		 *	(for status changes and ring passthrough).
		 */
};

struct hb_media_fns {
	const char *	type;		/* Medium type */
	const char *	description;	/* Longer Description */
	int		(*init)(void);
	struct hb_media*(*new)(const char * token);
	int		(*parse)(const char * options);
	int		(*open)(struct hb_media *mp);
	int		(*close)(struct hb_media *mp);
	struct ha_msg*	(*read)(struct hb_media *mp);
	int		(*write)(struct hb_media *mp, struct ha_msg*msg);
};

#define	MAXMSGHIST	100
struct msg_xmit_hist {
	struct ha_msg*		msgq[MAXMSGHIST];
	int			seqnos[MAXMSGHIST];
	int			lastmsg;
	int			hiseq;
	int			lowseq; /* one less than min actually present */
};

enum process_type {
	PROC_UNDEF,
	PROC_CONTROL,
	PROC_MST_STATUS,
	PROC_HBREAD,
	PROC_HBWRITE,
	PROC_PPP,
};

struct process_info {
	enum process_type	type;
	pid_t			pid;
	unsigned long		totalmsgs;
	unsigned long		allocmsgs;
	time_t			lastmsg;
};


/* This figure contains a couple of probably unnecessary fudge factors */
#define	MXPROCS	((PAGE_SIZE-2*sizeof(int))/sizeof(struct process_info)-1)

struct pstat_shm {
	int	nprocs;
	struct process_info info [MXPROCS];
};

volatile extern struct pstat_shm *	procinfo;
volatile extern struct process_info *	curproc;

extern struct sys_config *	config;
extern struct node_info *	curnode;
extern int			verbose;
extern int			debug;
extern int			udpport;

#define	ANYDEBUG	(debug)
#define	DEBUGAUTH	(debug >=3)
#define	DEBUGPKT	(debug >= 4)
#define	DEBUGPKTCONT	(debug >= 5)


/* Generally useful exportable HA heartbeat routines... */
extern void		ha_error(const char * msg);
extern void		ha_assert(const char *s, int line, const char * file);
extern void		ha_log(int priority, const char * fmt, ...);
extern void		ha_perror(const char * fmt, ...);
extern int		send_local_status(void);
extern int		set_local_status(const char * status);
extern int		send_cluster_msg(struct ha_msg*msg);
extern void		cleanexit(int exitcode);
extern void		check_auth_change(struct sys_config *);
extern void		(*localdie)(void);
extern int		should_ring_copy_msg(struct ha_msg* m);
extern unsigned char * 	calc_cksum(const char * authmethod, const char * key, const char * value);
struct auth_type *	findauth(const char * type);
#endif
