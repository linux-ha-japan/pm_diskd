const static char * _heartbeat_c_Id = "$Id: heartbeat.c,v 1.182 2002/04/14 09:06:09 alan Exp $";

/*
 * heartbeat: Linux-HA heartbeat code
 *
 * Copyright (C) 1999,2000,2001 Alan Robertson <alanr@unix.sh>
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

/*
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
 *	udpport 694
 *
 *	"Serial" lines tell us about our heartbeat configuration.
 *	If there is more than one serial port configured, we are in a "ring"
 *	configuration, every message not originated on our node is echoed
 *	to the other serial port(s)
 *
 *	"Node" lines tell us about the cluster configuration.
 *	We had better find our uname -n nodename here, or we won't start up.
 *
 *	We complain if we find extra nodes in the stream that aren't
 *	in the master configuration file.
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
 *	This has kind of happened over time.  Haresources and authkeys are
 *	decidely global, whereas ha.cf has remained more local.
 *
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
 *	If every second, each node writes out 150 chars of status,
 *	and we have 8 nodes, and the data rate would be about 1200 chars/sec.
 *	This would require about 12000 bps.  Better run faster than that.
 *
 *	for such a cluster...  With good UARTs and CTS/RTS, and good cables,
 *	you should be able to.  Maybe 56K would be a good choice...
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
 *
 ****** Wish List: ************************************************************
 *	[not necessarily in priority order]
 *
 *	Heartbeat API conversion to unix domain sockets:
 *		We ought to convert to UNIX domain sockets because we get
 *		better verification of the user, and we would get notified when
 *		they die.
 *
 *	Fuzzy heartbeat timing
 *		Right now, the code works in such a way that it systematically
 *		gets everyone heartbeating on the same time intervals, so that
 *		they happen at precisely the same time. This isn't too good
 *		for non-switched ethernet (CSMA/CD) environments, where it
 *		generates gobs of collisions, packet losses and
 *		retransmissions.  It's especially bad if all the clocks are
 *		in sync, which of course, every good system administrator
 *		strives to do ;-) This is due to Alan Cox who pointed out
 *		section 3.3 "Timers" in RFC 1058, which it states:
 *
 *       	  "It is undesirable for the update messages to become
 *		   synchronized, since it can lead to unnecessary collisions
 *		   on broadcast networks."
 *
 *		In particular, on Linux, if you set your all the clocks in
 *		your cluster via NTP (as you should), and heartbeat every
 *		second, then all the machines in the world will all try and
 *		heartbeat at precisely the same time, because alarm(2) wakes
 *		up on even second boundaries, which combined with the use of
 *		NTP (recommended), will systematically cause LOTS of
 *		unnecessary collisions.
 *
 *		Martin Lichtin suggests:
 *		Could you skew the heartbeats, based on the interface IP#?
 *		Probably want to use select(2) to wake up more precisely.
 *
 *		AlanR replied:
 *		I've thought about using setitimer(2), which would probably
 *		be slightly more compatible with the way the code is currently
 *		 written.
 *
 *		I thought that perhaps I could set each machine to a different
 *		interval in a +- 0.25 second range.  For example, one machine
 *		might heartbeat at 0.75 second interval, and another at a 1.25
 *		second interval.  The tendency would be then for the timers to
 *		wander across second boundaries, and even if they started out
 *		in sync, they would be unlikely to stay in sync.
 *		[but in retrospect, I'm not 100% sure about this]
 *
 *		This would keep me from having to generate a random number for
 *		every single heartbeat as the RFC suggests.
 *
 *		Of course, there are only 100 ticks/second, so if the clocks
 *		get closely synchronized, you can only have 100 different
 *		times to heartbeat.  I suppose if you have something like
 *		50-100 nodes, you ought to use a switch, and not a hub, and
 *		this would likely eliminate the problems.
 *
 *	Multicast heartbeats
 *		We really need to add UDP/IP multicast to our suite of
 *		heartbeat types.  Fundamentally, cluster communications are
 *		perhaps best thought of as multicast in nature.  Broadcast
 *		(like we do now) is basically a degenerate multicast case.
 *		One of the pieces of code listed on the linux-ha web site
 *		does multicast heartbeats.  Perhaps we could just borrow
 *		the correct parts from them.
 *
 *	Unicast heartbeats
 *		Some applications of heartbeat have certain machines which are
 *		not really full members of the cluster, but which would like
 *		to participate in the heartbeat API.  Although they
 *		could theoretically use multicast, there are practical barriers
 *		to doing so.  This is NOT intended to replace
 *		multicast/broadcast heartbeats for the entire cluster, but
 *		to allow one or two machines to join the cluster in a unicast
 *		mode.
 *
 *	Nearest Neighbor heartbeating (? maybe?)
 *		This is a candidate to replace the current policy of full-ring
 *		heartbeats In this policy, each machine only heartbeats to it's
 *		nearest neighbors.  The nearest neighbors only forward on
 *		status CHANGES to their neighbors.  This means that the total
 *		ring traffic in the non-error case is reduced to the same as
 *		a 3-node cluster.  This is a huge improvement.  It probably
 *		means that 19200 would be fast enough for almost any size
 *		network. Non-heartbeat admin traffic would need to be
 *		forwarded to all members of the ring as it was before.
 *
 *	IrDA heartbeats
 *		This is a near-exact replacement for ethernet with lower
 *		bandwidth, low costs and fewer points of failure.
 *		The role of an ethernet hub is replaced by a mirror, which
 *		is less likely to fail.  But if it does, it might mean
 *		seven years of bad luck :-)  On the other hand, the "mirror"
 *		could be a white painted board ;-)
 *
 *		The idea would be to make a bracket with the IrDA transceivers
 *		on them all facing the same way, then mount the bracket with
 *		the transceivers all facing the mirror.  Then each of the
 *		transceivers would be able to "see" each other.
 *
 *		I do kind of wonder if the kernel's IrDA stacks would be up
 *		to so much contention as it seems unlikely that they'd ever
 *		been tested in such a stressful environment.  But, it seems
 *		really cool to me, and it only takes one port per machine
 *		rather than two like we need for serial rings.
 *
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
#include <sys/wait.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <dirent.h>
#include <netdb.h>
#include <ltdl.h>
#ifdef _POSIX_MEMLOCK
#	include <sys/mman.h>
#endif
#ifdef _POSIX_PRIORITY_SCHEDULING
#	include <sched.h>
#endif

#include <clplumbing/longclock.h>
#include <clplumbing/proctrack.h>
#include <heartbeat.h>
#include <ha_msg.h>
#include <hb_api_core.h>
#include <test.h>
#include <hb_proc.h>
#include <pils/plugin.h>
#include <hb_module.h>
#include <HBcomm.h>

#include "setproctitle.h"

#define OPTARGS		"dkMrRsvlC:"

#define	IGNORESIG(s)	((void)signal((s), SIG_IGN))

/*
 *	Note that the _RSC defines below are bit fields!
 */
#define NO_RESOURCES		"none"
#define NO_RSC			0

#define LOCAL_RESOURCES		"local"
#define LOCAL_RSC		1

#define FOREIGN_RESOURCES	"foreign"
#define	FOREIGN_RSC		2

#define ALL_RSC			(LOCAL_RSC|FOREIGN_RSC)
#define ALL_RESOURCES		"all"

enum standby { NOT, ME, OTHER, DONE };
enum standby going_standby = NOT;
TIME_T  standby_running = 0L;

const char *		rsc_msg[] =	{NO_RESOURCES, LOCAL_RESOURCES,
					FOREIGN_RESOURCES, ALL_RESOURCES};
int		verbose = 0;

static char hbname []= "heartbeat";
char *	cmdname = hbname;
int		Argc = -1;
int		debug = 0;
int 		nice_failback = 0;
int		other_holds_resources = NO_RSC;
int		other_is_stable = 0; /* F_ISSTABLE */
int		takeover_in_progress = 0;
int		killrunninghb = 0;
int		rpt_hb_status = 0;
int		RunAtLowPrio = 0;
int		DoManageResources = 1;
int		childpid = -1;
char *		watchdogdev = NULL;
int		watchdogfd = -1;
TIME_T		starttime = 0L;
TIME_T		next_statsdump = 0L;
void		(*localdie)(void);
void		ha_glib_msg_handler(const gchar *log_domain
,			GLogLevelFlags log_level, const gchar *message
,			gpointer user_data);


struct hb_media*		sysmedia[MAXMEDIA];
extern struct hb_media_fns**	hbmedia_types;
extern int			num_hb_media_types;
extern PILPluginUniv*		PluginLoadingSystem;
int				nummedia = 0;
int				status_pipe[2];	/* The Master status pipe */


const char *ha_log_priority[8] = {
	"EMERG",
	"ALERT",
	"CRIT",
	"ERROR",
	"WARN",
	"notice",
	"info",
	"debug"
};

#define REAPER_SIG			0x0001UL
#define TERM_SIG			0x0002UL
#define PARENT_DEBUG_USR1_SIG		0x0004UL
#define PARENT_DEBUG_USR2_SIG		0x0008UL
#define REREAD_CONFIG_SIG		0x0010UL
#define DING_SIG			0x0020UL
#define FALSE_ALARM_SIG			0x0040UL

volatile unsigned long pending_handlers = 0;

struct sys_config *	config = NULL;
struct node_info *	curnode = NULL;

volatile struct pstat_shm *	procinfo = NULL;
volatile struct process_info *	curproc = NULL;

int	countbystatus(const char * status, int matchornot);
int	setline(int fd);
void	cleanexit(int rc);
void    reaper_sig(int sig);
void    reaper_action(void);
void    term_sig(int sig);
void    term_cleanexit(int sig);
void    term_action(void);
void	debug_sig(int sig);
void	debug_action(void);
void	signal_all(int sig);
void	force_shutdown(void);
void	emergency_shutdown(void);
void	signal_action(void);
void	parent_debug_usr1_sig(int sig);
void	parent_debug_usr1_action(void);
void	parent_debug_usr2_sig(int sig);
void	parent_debug_usr2_action(void);
void	reread_config_sig(int sig);
void	reread_config_action(void);
void	ding_sig(int sig);
void	ding_action(void);
void	ignore_signal(int sig);
void	false_alarm_sig(int sig);
void	false_alarm_action(void);
void    process_pending_handlers(void);
void	trigger_restart(int quickrestart);
void	restart_heartbeat(void);
int	parse_config(const char * cfgfile);
int	parse_ha_resources(const char * cfgfile);
void	dump_config(void);
char *	ha_timestamp(void);
int	add_option(const char *	option, const char * value);
int   	parse_authfile(void);
int 	encode_resources(const char *p);
void	init_watchdog(void);
void	tickle_watchdog(void);
void	usage(void);
int	init_config(const char * cfgfile);
void	init_procinfo(void);
int	initialize_heartbeat(void);
void	init_status_alarm(void);
void	ha_versioninfo(void);
static	const char * core_proc_name(enum process_type t);

static	void CoreProcessRegistered(ProcTrack* p);
static	void CoreProcessDied(ProcTrack* p, int status, int signo
,	int exitcode, int waslogged);
static	const char * CoreProcessName(ProcTrack* p);

static	void StonithProcessDied(ProcTrack* p, int status, int signo
,	int exitcode, int waslogged);
static	const char * StonithProcessName(ProcTrack* p);

typedef void	(*RemoteRscReqFunc)	(GHook *  data);
static	void	RscMgmtProcessRegistered(ProcTrack* p);
static	void	RscMgmtProcessDied(ProcTrack* p, int status
,	int signo, int exitcode, int waslogged);
static	const char * RscMgmtProcessName(ProcTrack* p);
static	void	StartNextRemoteRscReq(void);
static	void	InitRemoteRscReqQueue(void);
static	void	QueueRemoteRscReq(RemoteRscReqFunc, struct ha_msg* data);

static	void	PerformQueuedNotifyWorld(GHook* g);
static	void	ManagedChildRegistered(ProcTrack* p);
static	void	ManagedChildDied(ProcTrack* p, int status
,	int signo, int exitcode, int waslogged);
static	const char * ManagedChildName(ProcTrack* p);
void	KillTrackedProcess(ProcTrack* p, void * data);
static	void FinalCPShutdown(void);

void	dump_proc_stats(volatile struct process_info * proc);
void	dump_all_proc_stats(void);
void	check_for_timeouts(void);
void	check_comm_isup(void);
int	send_resources_held(const char *str, int stable, const char * comment);
int	send_standby_msg(enum standby state);
int	send_local_starting(void);
int	send_local_status(const char *);
int	set_local_status(const char * status);
void	process_resources(const char * type, struct ha_msg* msg
,		struct node_info * thisnode);
static	void AuditResources(void);
void	request_msg_rexmit(struct node_info *, unsigned long lowseq
,		unsigned long hiseq);
void	check_rexmit_reqs(void);
void	mark_node_dead(struct node_info* hip);
void	takeover_from_node(const char * nodename);
void	change_link_status(struct node_info* hip, struct link *lnk
,		const char * new);
static	void comm_now_up(void);
static	void CreateInitialFilter(void);
static int FilterNotifications(const char * msgtype);
void	notify_world(struct ha_msg * msg, const char * ostatus);
long	get_running_hb_pid(void);
void	make_daemon(void);
void	heartbeat_monitor(struct ha_msg * msg, int status, const char * iface);
void	send_to_all_media(char * smsg, int len);
int	should_drop_message(struct node_info* node, const struct ha_msg* msg,
				const char *iface);
int	is_lost_packet(struct node_info * thisnode, unsigned long seq);
void	Initiate_Reset(Stonith* s, const char * nodename);
void	healed_cluster_partition(struct node_info* node);
void	add2_xmit_hist (struct msg_xmit_hist * hist, struct ha_msg* msg
,		unsigned long seq);
void	init_xmit_hist (struct msg_xmit_hist * hist);
void	process_rexmit(struct msg_xmit_hist * hist, struct ha_msg* msg);
void	process_clustermsg(FILE * f);
void	process_registermsg(FILE * f);
void	nak_rexmit(unsigned long seqno, const char * reason);
void	req_our_resources(int getthemanyway);
void	giveup_resources(void);
void	go_standby(enum standby who);
void	make_realtime(void);
void	make_normaltime(void);
int	IncrGeneration(unsigned long * generation);
void	ask_for_resources(struct ha_msg *msg);
void	process_control_packet(struct msg_xmit_hist* msghist
,	struct ha_msg * msg);
static void	start_a_child_client(gpointer childentry, gpointer pidtable);

/* The biggies */
void control_process(FILE * f, int ofd);
void read_child(struct hb_media* mp);
void write_child(struct hb_media* mp);
void master_status_process(void);		/* The real biggie */

#ifdef IRIX
	void setenv(const char *name, const char * value, int);
#endif

pid_t		processes[MAXPROCS];
pid_t		master_status_pid;
int		send_status_now = 1;	/* Send initial status immediately */
int		dump_stats_now = 0;
int		parse_only = 0;
int		RestartRequested = 0;
static int	shutdown_in_progress = 0;
static int	WeAreRestarting = 0;
static sigset_t	CommonSignalSet;

enum comm_state {
	COMM_STARTING,
	COMM_LINKSUP
};
enum comm_state	heartbeat_comm_state = COMM_STARTING;

enum rsc_state {
	R_INIT,			/* Links not up yet */
	R_STARTING,		/* Links up, start message issued */
	R_BOTHSTARTING,		/* Links up, start msg received & issued  */
				/* BOTHSTARTING now equiv to STARTING (?) */
	R_RSCRCVD,		/* Resource Message received */
	R_STABLE,		/* Local resources acquired, too... */
	R_SHUTDOWN,		/* We're in shutdown... */
};
static enum rsc_state	resourcestate = R_INIT;

static ProcTrack_ops CoreProcessTrackOps = {
	CoreProcessDied,
	CoreProcessRegistered,
	CoreProcessName
};
int CoreProcessCount = 0;

static ProcTrack_ops RscMgmtProcessTrackOps = {
	RscMgmtProcessDied,
	RscMgmtProcessRegistered,
	RscMgmtProcessName
};


static ProcTrack_ops StonithProcessTrackOps = {
	StonithProcessDied,
	NULL,
	StonithProcessName
};

static ProcTrack_ops ManagedChildTrackOps = {
	ManagedChildDied,
	ManagedChildRegistered,
	ManagedChildName
};

int	managed_child_count= 0;
int	ResourceMgmt_child_count = 0;

/*
 * A helper to allow us to pass things into the anonproc
 * environment without any warnings about passing const strings
 * being passed into a plain old (non-const) gpointer.
 */
struct const_string {
	const char * str;
};

#define	RSCMGMTPROC(p, s)						\
	 {							\
	 	static struct const_string cstr = {(s)};		\
		NewTrackedProc((p), 1, PT_LOGNORMAL		\
		,	&cstr, &RscMgmtProcessTrackOps);		\
	}

void
init_procinfo()
{
	int	ipcid;
	struct pstat_shm *	shm;

	(void)_heartbeat_c_Id;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;
	(void)_setproctitle_h_Id;

	if ((ipcid = shmget(IPC_PRIVATE, sizeof(*procinfo), 0666)) < 0) {
		ha_perror("Cannot shmget for process status");
		return;
	}

	/*
	 * Casting this address into a long stinks, but there's no other
	 * way because of the way the shared memory API is designed.
	 */
	if (((long)(shm = shmat(ipcid, NULL, 0))) == -1L) {
		ha_perror("Cannot shmat for process status");
		shm = NULL;
		return;
	}
	if (shm) {
		procinfo = shm;
		memset(shm, 0, PAGE_SIZE);
	}

	/*
	 * Go ahead and "remove" our shared memory now...
	 *
	 * This is cool because the manual says:
	 *
	 *	IPC_RMID    is used to mark the segment as destroyed. It
	 *	will actually be destroyed after the last detach.
	 *
	 * Not all the Shared memory implementations have as clear a
	 * description of this fact as Linux, but they all work this way
	 * anyway (as far as we have ever tested).
	 */
	if (shmctl(ipcid, IPC_RMID, NULL) < 0) {
		ha_perror("Cannot IPC_RMID proc status shared memory id");
	}
	procinfo->giveup_resources = 1;
	procinfo->i_hold_resources = NO_RSC;
}

void
ha_versioninfo(void)
{
	static int	everprinted=0;

	ha_log(LOG_INFO, "%s: version %s", cmdname, VERSION);

	/*
	 * The reason why we only do this once is that we are doing it with
	 * our priority which could hang the machine, and forking could
	 * possibly cause us to miss a heartbeat if this is done
	 * under load.
	 * FIXME!  We really need fork anyway...
	 */
	if (ANYDEBUG && !everprinted) {
		char	cmdline[MAXLINE];
		char	buf[MAXLINE];
		FILE *	f;

		/*
		 * Do 'strings' on ourselves, and look for version info...
		 */

		/* This command had better be well-behaved! */

		snprintf(cmdline, MAXLINE
			/* Break up the string so RCS won't react to it */
		,	"strings %s/%s | grep '^\\$"
			"Id" ": .*\\$' | sort -u"
		,	HALIB, cmdname);


		if ((f = popen(cmdline, "r")) == NULL) {
			ha_perror("Cannot run: %s", cmdline);
			return;
		}
		while (fgets(buf, MAXLINE, f)) {
			++everprinted;
			if (buf[strlen(buf)-1] == '\n') {
				buf[strlen(buf)-1] = EOS;
			}
			ha_log(LOG_INFO, "%s", buf);
		}
		pclose(f);
	}
}

/* Look up the interface in the node struct, returning the link info structure*/
struct link *
lookup_iface(struct node_info * hip, const char *iface)
{
	struct link *lnk;
	int j = 0;
	while((lnk = &hip->links[j]) && lnk->name) {
		if (strcmp(lnk->name, iface) == 0) {
			return lnk;
		}
	j++;
	}
	return NULL;
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
	TIME_T		now;

	now = time(NULL);
	ttm = localtime(&now);

	snprintf(ts, sizeof(ts), "%04d/%02d/%02d_%02d:%02d:%02d"
	,	ttm->tm_year+1900, ttm->tm_mon+1, ttm->tm_mday
	,	ttm->tm_hour, ttm->tm_min, ttm->tm_sec);
	return(ts);
}

/* Very unsophisticated HA-error-logging function (deprecated) */
void
ha_error(const char *	msg)
{
	ha_log(LOG_ERR, "%s", msg);
}


/* HA-logging function */
void
ha_log(int priority, const char * fmt, ...)
{
	va_list		ap;
	FILE *		fp = NULL;
	char *		fn = NULL;
	char		buf[MAXLINE];
	int		logpri = LOG_PRI(priority);
	const char *	pristr;

	va_start(ap, fmt);
	vsnprintf(buf, MAXLINE, fmt, ap);
	va_end(ap);

	if (logpri < 0 || logpri >= DIMOF(ha_log_priority)) {
		pristr = "(undef)";
	}else{
		pristr = ha_log_priority[logpri];
	}

	if (config && config->log_facility >= 0) {
		syslog(priority, "%s: %s", pristr,  buf);
	}

	if (config) {
		if (priority == LOG_DEBUG) {
			if (config->use_dbgfile) {
				fn = config->dbgfile;
			}else{
				return;
			}
		}else{
			if (config->use_logfile) {
				fn = config->logfile;
			}
		}
	}

	if (!config  || fn != NULL || config->log_facility < 0) {
		if (fn) {
			fp = fopen(fn, "a");
		}

		if (fp == NULL) {
			fp = stderr;
		}

		fprintf(fp, "heartbeat: %s %s: %s\n", ha_timestamp()
		,	pristr,  buf);

		if (fp != stderr) {
			fclose(fp);
		}
	}
}

extern int	sys_nerr;
void
ha_perror(const char * fmt, ...)
{
	const char *	err;
	char	errornumber[16];

	va_list ap;
	char buf[MAXLINE];

	if (errno < 0 || errno >= sys_nerr) {
		sprintf(errornumber, "error %d\n", errno);
		err = errornumber;
	}else{
#ifdef HAVE_STRERROR
		err = strerror(errno);
#else
		err = sys_errlist[errno];
#endif
	}
	va_start(ap, fmt);
	vsnprintf(buf, MAXLINE, fmt, ap);
	va_end(ap);

	ha_log(LOG_ERR, "%s: %s", buf, err);

}

void
ha_glib_msg_handler(const gchar *log_domain,	GLogLevelFlags log_level
,	const gchar *message, gpointer user_data)
{
	GLogLevelFlags	level = (log_level & G_LOG_LEVEL_MASK);
	int	ha_level;

	switch(level) {
		case G_LOG_LEVEL_ERROR:		ha_level = LOG_ERR; break;
		case G_LOG_LEVEL_CRITICAL:	ha_level = LOG_ERR; break;
		case G_LOG_LEVEL_WARNING:	ha_level = LOG_WARNING; break;
		case G_LOG_LEVEL_MESSAGE:	ha_level = LOG_NOTICE; break;
		case G_LOG_LEVEL_INFO:		ha_level = LOG_INFO; break;
		case G_LOG_LEVEL_DEBUG:		ha_level = LOG_DEBUG; break;

		default:			ha_level = LOG_WARNING; break;
	}


	ha_log(ha_level, "%s", message);
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
 */

	int		j;
	struct stat	buf;
	int		pid;
	FILE *		fifo;
	int		ourproc = 0;
	int		fifoofd;

	localdie = NULL;
	starttime = time(NULL);


	if (IncrGeneration(&config->generation) != HA_OK) {
		ha_perror("Cannot get/increment generation number");
		return(HA_FAIL);
	}
	ha_log(LOG_INFO, "Heartbeat generation: %lu", config->generation);

	if (stat(FIFONAME, &buf) < 0 ||	!S_ISFIFO(buf.st_mode)) {
		ha_log(LOG_INFO, "Creating FIFO %s.", FIFONAME);
		unlink(FIFONAME);
		if (mkfifo(FIFONAME, FIFOMODE) < 0) {
			ha_perror("Cannot make fifo %s.", FIFONAME);
			return(HA_FAIL);
		}
	}

	if (stat(FIFONAME, &buf) < 0) {
		ha_log(LOG_ERR, "FIFO %s does not exist", FIFONAME);
		return(HA_FAIL);
	}else if (!S_ISFIFO(buf.st_mode)) {
		ha_log(LOG_ERR, "%s is not a FIFO", FIFONAME);
		return(HA_FAIL);
	}

	if (stat(API_REGFIFO, &buf) < 0 || !S_ISFIFO(buf.st_mode)) {
		ha_log(LOG_INFO, "Creating FIFO %s.", API_REGFIFO);
		unlink(API_REGFIFO);
		if (mkfifo(API_REGFIFO, 0420) < 0) {
			ha_perror("Cannot make fifo %s.", API_REGFIFO);
			return(HA_FAIL);
		}
	}

	if (stat(API_REGFIFO, &buf) < 0) {
		ha_log(LOG_ERR, "FIFO %s does not exist", API_REGFIFO);
		return(HA_FAIL);
	}else if (!S_ISFIFO(buf.st_mode)) {
		ha_log(LOG_ERR, "%s is not a FIFO", API_REGFIFO);
		return(HA_FAIL);
	}

	if (pipe(status_pipe) < 0) {
		ha_perror("cannot create status pipe");
		return(HA_FAIL);
	}

	/* Clean up tmp files from our resource scripts */
	system("rm -fr " RSC_TMPDIR);

	/* Remake the temporary directory ... */
	mkdir(RSC_TMPDIR
	,	S_IRUSR|S_IWUSR|S_IXUSR
	|	S_IRGRP|S_IWGRP|S_IXGRP	
	|	S_IROTH|S_IWOTH|S_IXOTH	|	S_ISVTX /* sticky bit */);

	/* Open all our heartbeat channels */

	for (j=0; j < nummedia; ++j) {
		struct hb_media* smj = sysmedia[j];

		if (pipe(smj->wpipe) < 0) {
			ha_perror("cannot create hb channel pipe");
			return(HA_FAIL);
		}
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "opening %s %s (%s)", smj->type
			,	smj->name, smj->description);
		}
		if (smj->vf->open(smj) != HA_OK) {
			ha_log(LOG_ERR, "cannot open %s %s"
			,	smj->type
			,	smj->name);
			return(HA_FAIL);
		}
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "%s channel %s now open..."
			,	smj->type, smj->name);
		}
	}

	CoreProcessCount = 0;
	procinfo->nprocs = 0;
	ourproc = procinfo->nprocs;
	curproc = &procinfo->info[ourproc];
	curproc->type = PROC_CONTROL;

	NewTrackedProc(getpid(), 0, PT_LOGVERBOSE, GINT_TO_POINTER(ourproc)
	,	&CoreProcessTrackOps);
	curproc->pstat = RUNNING;


	/* Now the fun begins... */
/*
 *	Optimal starting order:
 *		write_child();
 *		read_child();
 *		master_status_process();
 *		control_process(FILE * f, int ofd);
 *
 */
	ourproc = procinfo->nprocs;

	for (j=0; j < nummedia; ++j) {
		struct hb_media* mp = sysmedia[j];

		ourproc = procinfo->nprocs;

		switch ((pid=fork())) {
			case -1:	ha_perror("Can't fork write process");
					return(HA_FAIL);
					break;

			case 0:		/* Child */
					curproc = &procinfo->info[ourproc];
					curproc->type = PROC_HBWRITE;
					make_realtime();
					while (curproc->pid != getpid()) {
						sleep(1);
					}
					write_child(mp);
					ha_perror("write process exiting");
					cleanexit(1);
		}
		NewTrackedProc(pid, 0, PT_LOGVERBOSE, GINT_TO_POINTER(ourproc)
		,	&CoreProcessTrackOps);

		ourproc = procinfo->nprocs;

		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "write process pid: %d\n", pid);
		}

		switch ((pid=fork())) {
			case -1:	ha_perror("Can't fork read process");
					return(HA_FAIL);
					break;

			case 0:		/* Child */
					curproc = &procinfo->info[ourproc];
					curproc->type = PROC_HBREAD;
					while (curproc->pid != getpid()) {
						sleep(1);
					}
					make_realtime();
					read_child(mp);
					ha_perror("read child process exiting");
					cleanexit(1);
		}
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "read child process pid: %d\n", pid);
		}
		NewTrackedProc(pid, 0, PT_LOGVERBOSE, GINT_TO_POINTER(ourproc)
		,	&CoreProcessTrackOps);
	}



	ourproc = procinfo->nprocs;
	switch ((pid=fork())) {
		case -1:	ha_perror("Can't fork master status process!");
				return(HA_FAIL);
				break;

		case 0:		/* Child */
				curproc = &procinfo->info[ourproc];
				curproc->type = PROC_MST_STATUS;
				make_realtime();
				while (curproc->pid != getpid()) {
					sleep(1);
				}
				master_status_process();
				ha_perror("master status process exiting");
				cleanexit(1);
	}
	master_status_pid = pid;

	NewTrackedProc(pid, 0, PT_LOGVERBOSE, GINT_TO_POINTER(ourproc)
	,	&CoreProcessTrackOps);

	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "master status process pid: %d\n", pid);
	}

	fifo = fopen(FIFONAME, "r");
	if (fifo == NULL) {
		ha_perror("FIFO open failed.");
	}
	fifoofd = open(FIFONAME, O_WRONLY);	/* Keep reads from failing */
	control_process(fifo, fifoofd);

	/*NOTREACHED*/
	ha_log(LOG_ERR, "control_process exiting?");
	cleanexit(1);
	return(HA_FAIL);
}

/*
 *	Make us behave like a soft real-time process.
 *	We need scheduling priority and being locked in memory.
 */

void
make_realtime()
{

#ifdef SCHED_RR
#	define HB_SCHED_POLICY	SCHED_RR
#endif

#ifdef HB_SCHED_POLICY
	struct sched_param	sp;
	int			staticp;

	if (RunAtLowPrio) {
		ha_log(LOG_INFO, "Request to set high priority ignored.");
		return;
	}
	if (ANYDEBUG) {
		ha_log(LOG_INFO, "Setting process %d to realtime", getpid());
	}
	if ((staticp=sched_getscheduler(0)) < 0) {
		ha_log(LOG_ERR, "unable to get scheduler parameters.");
	}else{
		memset(&sp, 0, sizeof(sp));
		sp.sched_priority = HB_STATIC_PRIO;
		if (sched_setscheduler(0, HB_SCHED_POLICY, &sp) < 0) {
			ha_log(LOG_ERR, "unable to set scheduler parameters.");
		}else if (ANYDEBUG) {
			ha_log(LOG_INFO
			,	"scheduler priority set to %d", HB_STATIC_PRIO);
		}
	}
#endif

#ifdef MCL_FUTURE
	if (mlockall(MCL_FUTURE) < 0) {
		ha_log(LOG_ERR, "unable to lock pid %d in memory", getpid());
	}else if (ANYDEBUG) {
		ha_log(LOG_INFO, "pid %d locked in memory.", getpid());
	}
#endif
}

void
make_normaltime()
{
#ifdef HB_SCHED_POLICY
	struct sched_param	sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = 0;
	if (sched_setscheduler(0, SCHED_OTHER, &sp) < 0) {
		ha_log(LOG_ERR, "unable to (re)set scheduler parameters.");
	}else if (ANYDEBUG) {
		ha_log(LOG_INFO
		,	"scheduler priority set to %d", 0);
	}
#endif
#ifdef _POSIX_MEMLOCK
	/* Not strictly necessary. */
	munlockall();
#endif
}

/* Create a read child process (to read messages from hb medium) */
void
read_child(struct hb_media* mp)
{
	int	msglen;
	int	rc;
	int	statusfd = status_pipe[P_WRITEFD];
	FILE *  statusfp = fdopen(statusfd, "w");

	curproc->pstat = RUNNING;
	set_proc_title("%s: read: %s %s", cmdname, mp->type, mp->name);

	process_pending_handlers();
	for (;;) {
		struct	ha_msg*	m = mp->vf->read(mp);
		char *		sm;

		if (pending_handlers) {
			process_pending_handlers();
		}

		if (m == NULL) {
			continue;
		}

		sm = msg2if_string(m, mp->name);
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

			if ((rc=fwrite(sm, 1,  msglen, statusfp)) != msglen)  {
				if (pending_handlers) {
					process_pending_handlers();
				}
				/* Try one extra time if we got EINTR */
				if (errno != EINTR
				||	(rc=fwrite(sm, 1,  msglen, statusfp))
				!=	msglen)  {
					ha_perror("Write failure [%d/%d] %s"
					,	rc
					,	errno
					,	"to status pipe");
				}
			}
			fflush(statusfp);
			ha_free(sm);
			if (pending_handlers) {
				process_pending_handlers();
			}
		}
		ha_msg_del(m);
	}
}


/* Create a write child process (to write messages to hb medium) */
void
write_child(struct hb_media* mp)
{
	int	ourpipe =	mp->wpipe[P_READFD];
	FILE *	ourfp		= fdopen(ourpipe, "r");

	siginterrupt(SIGALRM, 1);
	signal(SIGALRM, ignore_signal);
	set_proc_title("%s: write: %s %s", cmdname, mp->type, mp->name);
	curproc->pstat = RUNNING;

	for (;;) {
		struct ha_msg * msgp = if_msgfromstream(ourfp, NULL);

		if (pending_handlers) {
			process_pending_handlers();
		}
		if (msgp == NULL) {
			continue;
		}
		if (mp->vf->write(mp, msgp) != HA_OK) {
			ha_perror("write failure on %s %s."
			,	mp->type, mp->name);
		}
		ha_msg_del(msgp); msgp = NULL;
		if (pending_handlers) {
			process_pending_handlers();
		}
	}
}


/* The master control process -- reads control fifo, sends msgs to cluster */
/* Not a lot to this one, eh? */
void
control_process(FILE * fp, int fifoofd)
{
	struct msg_xmit_hist	msghist;

	init_xmit_hist (&msghist);

	/* Catch and propagate debugging level signals... */
	signal(SIGUSR1, parent_debug_usr1_sig);
	signal(SIGUSR2, parent_debug_usr2_sig);
	signal(SIGQUIT, signal_all); /* From Master Status Process */
	signal(SIGTERM, signal_all); /* Shutdown signal - from anyone... */
	siginterrupt(SIGALRM, 1);
	siginterrupt(SIGTERM, 1);
	siginterrupt(SIGUSR1, 1);
	siginterrupt(SIGUSR2, 1);
	siginterrupt(SIGQUIT, 1);

	set_proc_title("%s: control process", cmdname);
	make_realtime();

	for(;;) {
		struct ha_msg *	msg;

		/* Process pending signals */
		process_pending_handlers();
		msg = controlfifo2msg(fp);

		if (msg) {
			process_control_packet(&msghist, msg);
			if (fifoofd > 0) {
				/* FIFO Reads will now fail if writers die */
				close(fifoofd);
				fifoofd = -1;
			}
		}else if (feof(fp)) {
			break;
		}
	}

	if (ANYDEBUG) {
		ha_log(LOG_INFO, "Control Process: entering pause loop.");
	}

	/*
	 * Sometimes kernels forget to deliver one or more of our
	 * SIGCHLDs to us.  We set the alarm to tell us to just
	 * give up and poll for the stupid things from time to time...
	 */
	init_status_alarm();

	/* Wait for shutdown to complete */

	while (CoreProcessCount > 1) {
		/* Poll for SIGCHLDs */
		reaper_action();
		pause();
	}

	/* That's All Folks... */
	cleanexit(0);
}

/*
 * Control (outbound) packet processing...
 *
 * This is where the reliable multicast protocol is implemented -
 * through the use of process_rexmit(), and add2_xmit_hist().
 * process_rexmit(), and add2_xmit_hist() use msghist to track sent
 * packets so we can retransmit them if they get lost.
 *
 * NOTE: It's our job to dispose of the packet we're given...
 */
void
process_control_packet(struct msg_xmit_hist*	msghist
,		struct ha_msg *	msg)
{
	int	statusfd = status_pipe[P_WRITEFD];

	char *		smsg;
	const char *	type;
	int		len;
	const char *	cseq;
	unsigned long	seqno = -1;
	const  char *	to;
	int		IsToUs;

	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, "got msg in process_control_packet");
	}
	if ((type = ha_msg_value(msg, F_TYPE)) == NULL) {
		ha_log(LOG_ERR, "process_control_packet: no type in msg.");
		ha_msg_del(msg);
		return;
	}
	if ((cseq = ha_msg_value(msg, F_SEQ)) != NULL) {
		if (sscanf(cseq, "%lx", &seqno) != 1
		||	seqno <= 0) {
			ha_log(LOG_ERR, "process_control_packet: "
			"bad sequence number");
			smsg = NULL;
			ha_msg_del(msg);
			return;
		}
	}

	to = ha_msg_value(msg, F_TO);
	IsToUs = (to != NULL) && (strcmp(to, curnode->nodename) == 0);

	/* Is this a request to retransmit a packet? */
	if (strcasecmp(type, T_REXMIT) == 0 && IsToUs) {
		/* OK... Process retransmit request */
		process_rexmit(msghist, msg);
		ha_msg_del(msg);
		return;
	}
	/* Convert the incoming message to a string */
	smsg = msg2string(msg);

	/* If it didn't convert, throw original message away */
	if (smsg == NULL) {
		ha_msg_del(msg);
		return;
	}
	/* Remember Messages with sequence numbers */
	if (cseq != NULL) {
		add2_xmit_hist (msghist, msg, seqno);
	}

	len = strlen(smsg);

	/* Copy the message to the status process */
	write(statusfd, smsg, len);

	send_to_all_media(smsg, len);
	ha_free(smsg);

	/*  Throw away "msg" here if it's not saved above */
	if (cseq == NULL) {
		ha_msg_del(msg);
	}
	/* That's All Folks... */
}

/* Send this message to all of our heartbeat media */
void
send_to_all_media(char * smsg, int len)
{
	int	j;

	/* Throw away some packets if testing is enabled */
	if (TESTSEND) {
		if (TestRand(send_loss_prob)) {
			return;
		}
	}

	/* Send the message to all our heartbeat interfaces */
	for (j=0; j < nummedia; ++j) {
		int	wrc;
		alarm(2);
		wrc=write(sysmedia[j]->wpipe[P_WRITEFD], smsg, len);
		if (wrc < 0) {
			ha_perror("Cannot write to media pipe %d"
			,	j);
			ha_log(LOG_ERR, "Shutting down.");
			force_shutdown();
		}else if (wrc != len) {
			ha_log(LOG_ERR
			,	"Short write on media pipe %d [%d vs %d]"
			,	j, wrc, len);
		}
		alarm(0);
	}
}


/*
 *	What are our abstract event sources?
 *
 *	Queued signals to be handled ("polled" high priority)
 *	Sending a heartbeat message (timeout-based) (high priority)
 *	Retransmitting packets for the protocol (timed medium priority)
 *	Timing out on heartbeats from other nodes (timed low priority)
 *
 *		We currently combine all our timed/polled events together.
 *		The only one that has critical timing needs is sending
 *		out heartbeat messages
 *
 *	Messages from the network (file descriptor medium-high priority)
 *
 *	API requests from clients (file descriptor medium-low priority)
 *
 *	Registration requests from clients (file descriptor low priority)
 *
 */

/*
 * Combined polled/timed events...
 */
static gboolean polled_input_prepare(gpointer source_data
,	GTimeVal* current_time
,	gint* timeout, gpointer user_data);
static gboolean polled_input_check(gpointer source_data
,	GTimeVal* current_time
,	gpointer user_data);
static gboolean polled_input_dispatch(gpointer source_data
,	GTimeVal* current_time
,	gpointer user_data);
static void polled_input_destroy(gpointer user_data);

static GSourceFuncs polled_input_SourceFuncs = {
	polled_input_prepare,
	polled_input_check,
	polled_input_dispatch,
	polled_input_destroy,
};


/*
 * Messages from the cluster are one of our inputs...
 */
static gboolean clustermsg_input_prepare(gpointer source_data
,	GTimeVal* current_time, gint* timeout, gpointer user_data);
static gboolean clustermsg_input_check(gpointer source_data
,	GTimeVal* current_time, gpointer user_data);
static gboolean clustermsg_input_dispatch(gpointer source_data
,	GTimeVal* current_time, gpointer user_data);
static void clustermsg_input_destroy(gpointer user_data);

static GSourceFuncs clustermsg_input_SourceFuncs = {
	clustermsg_input_prepare,
	clustermsg_input_check,
	clustermsg_input_dispatch,
	clustermsg_input_destroy,
};

/*
 * API registration requests are one of our inputs
 */
static gboolean APIregistration_input_prepare(gpointer source_data
,	GTimeVal* current_time, gint* timeout, gpointer user_data);
static gboolean APIregistration_input_check(gpointer source_data
,	GTimeVal* current_time, gpointer user_data);
static gboolean APIregistration_input_dispatch(gpointer source_data
,	GTimeVal* current_time, gpointer user_data);
static void APIregistration_input_destroy(gpointer user_data);

static GSourceFuncs APIregistration_input_SourceFuncs = {
	APIregistration_input_prepare,
	APIregistration_input_check,
	APIregistration_input_dispatch,
	APIregistration_input_destroy,
};

/*
 * Messages from registered API clients are also inputs
 */
static gboolean APIclients_input_prepare(gpointer source_data
,	GTimeVal* current_time, gint* timeout, gpointer user_data);
static gboolean APIclients_input_check(gpointer source_data
,	GTimeVal* current_time, gpointer	user_data);
static gboolean APIclients_input_dispatch(gpointer source_data
,	GTimeVal* current_time , gpointer user_data);
static void APIclients_input_destroy(gpointer user_data);

/* NOT static! */
GSourceFuncs APIclients_input_SourceFuncs = {
	APIclients_input_prepare,
	APIclients_input_check,
	APIclients_input_dispatch,
	APIclients_input_destroy,
};
void LookForClockJumps(void);

static int			ClockJustJumped = 0;

static gboolean MSPFinalShutdown(gpointer p);

/* The master status process */
void
master_status_process(void)
{
	FILE *			f;
	FILE *			regfifo;
	int			fd, regfd;
	volatile struct process_info *	pinfo;
	int			allstarted;
	int			j;
	GPollFD			ClusterMsgGFD;
	GPollFD			APIRegistrationGFD;
	GMainLoop*		mainloop;

	init_status_alarm();
	init_watchdog();
	siginterrupt(SIGTERM, 1);
	signal(SIGTERM, term_sig);

	send_local_status(UPSTATUS);	/* We're pretty sure we're up ;-) */

	set_proc_title("%s: master status process", cmdname);

	/* We open it this way to keep the open from hanging... */
	if ((f = fdopen(status_pipe[P_READFD], "r")) == NULL) {
		ha_perror ("master_status_process: unable to open"
		" status_pipe(READ)");
		cleanexit(1);
	}

	if ((regfd = open(API_REGFIFO, O_RDWR)) < 0) {
		ha_log(LOG_ERR
		,	"master_status_process: Can't open " API_REGFIFO);
		cleanexit(1);
	}
	if (DEBUGPKT) {
		ha_log(LOG_DEBUG
		, "master_status_process: opened socket %d for REGISTER :%s"
		,	regfd, API_REGFIFO);
	}

	if ((regfifo = fdopen(regfd, "r")) == NULL) {
		ha_log(LOG_ERR
		,	"master_status_process: Can't fdopen " API_REGFIFO);
		cleanexit(1);
	}
	fd = -1;
	fd = fileno(f);			clearerr(f);
	regfd = fileno(regfifo);	clearerr(regfifo);


	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "Waiting for child processes to start");
	}
	/* Wait until all the child processes are really running */
	do {
		allstarted = 1;
		for (pinfo=procinfo->info; pinfo < curproc; ++pinfo) {
			if (pinfo->pstat == FORKED) {
				if (ANYDEBUG) {
					ha_log(LOG_DEBUG
					, "Waiting for pid %d type %d stat %d"
					, pinfo->pid, pinfo->type
					, pinfo->pstat);
				}
				allstarted=0;
				sleep(1);
			}
		}
	}while (!allstarted);
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG
		,	"All your child process are belong to us");
	}
	curproc->pstat = RUNNING;

	g_source_add(G_PRIORITY_HIGH, FALSE, &polled_input_SourceFuncs
	,	NULL, NULL, NULL);

	ClusterMsgGFD.fd = fd;
	ClusterMsgGFD.events = G_IO_IN|G_IO_HUP|G_IO_ERR;
	g_main_add_poll(&ClusterMsgGFD, G_PRIORITY_DEFAULT);
	g_source_add(G_PRIORITY_DEFAULT, FALSE, &clustermsg_input_SourceFuncs
	,	&ClusterMsgGFD,	f, NULL);

	APIRegistrationGFD.fd = regfd;
	APIRegistrationGFD.events = G_IO_IN|G_IO_HUP|G_IO_ERR;
	g_main_add_poll(&APIRegistrationGFD, G_PRIORITY_LOW);
	g_source_add(G_PRIORITY_LOW, FALSE, &APIregistration_input_SourceFuncs
	,	&APIRegistrationGFD, regfifo, NULL);

	/* Reset timeout times to "now" */
	for (j=0; j < config->nodecount; ++j) {
		struct node_info *	hip;
		struct tms		proforma_tms;
		hip= &config->nodes[j];
		hip->local_lastupdate = times(&proforma_tms);
	}

	mainloop = g_main_new(TRUE);
	g_main_run(mainloop);
}


/*
 *	Queued signals to be handled ("polled" high priority)
 *	Sending a heartbeat message (timeout-based) (high priority)
 *	Retransmitting packets for the protocol (timed medium priority)
 *	Timing out on heartbeats from other nodes (timed low priority)
 */

void
LookForClockJumps(void)
{
	static TIME_T	lastnow = 0L;
	TIME_T		now = time(NULL);

	/* Check for clock jumps */
	if (now < lastnow) {
		ha_log(LOG_INFO
		,	"Clock jumped backwards. Compensating.");
		init_status_alarm();
		send_status_now = 1;
		ClockJustJumped = 1;
		standby_running = 0L;
		other_is_stable = 1;
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG
			, "Clock Jumped: other now stable");
		}
	}else{
		ClockJustJumped = 0;
	}
}

static gboolean
polled_input_prepare(gpointer source_data, GTimeVal* current_time
,	gint* timeout, gpointer user_data)
{

	/* MUST set timeout FIXME!! */
	*timeout = 1000;
	LookForClockJumps();

	return (pending_handlers != 0)
	||	send_status_now
	||	dump_stats_now
	||	ClockJustJumped;
}

static longclock_t	NextPoll = 0UL;
static longclock_t	local_takeover_time = 0L;

#define	POLL_INTERVAL	250

static gboolean
polled_input_check(gpointer source_data, GTimeVal* current_time
,	gpointer	user_data)
{
	struct tms	proforma_tms;
	longclock_t		now = times(&proforma_tms);

	LookForClockJumps();

	return (cmp_longclock(now, NextPoll) >= 0);
}

static gboolean
polled_input_dispatch(gpointer source_data, GTimeVal* current_time
,	gpointer	user_data)
{
	longclock_t	now = time_longclock();

	NextPoll = add_longclock(now, msto_longclock(POLL_INTERVAL));

	LookForClockJumps();

	if (pending_handlers) {
		process_pending_handlers();
	}
	if (send_status_now) {
		send_status_now = 0;
		send_local_status(NULL);

	}
	if (dump_stats_now) {
		dump_stats_now = 0;
		dump_all_proc_stats();
	}

	/* Scan nodes and links to see if any have timed out */
	if (!ClockJustJumped) {
		/* We'll catch it again next time around... */
		check_for_timeouts();
	}

	/* Check to see we need to resend any rexmit requests... */
	check_rexmit_reqs();

	/* See if our comm channels are working yet... */
	if (heartbeat_comm_state != COMM_LINKSUP) {
		check_comm_isup();
	}

	/* Check for "time to take over local resources */
	if (nice_failback && resourcestate == R_RSCRCVD
	&&	cmp_longclock(now, local_takeover_time) > 0) {
		resourcestate = R_STABLE;
		req_our_resources(0);
		ha_log(LOG_INFO,"local resource transition completed.");
		send_resources_held(rsc_msg[procinfo->i_hold_resources]
		,	1, NULL);
		AuditResources();
	}


	return TRUE;
}

/*
 *	This should be something the code can register for.
 *	and a nice set of hooks to call, etc...
 */
static void
comm_now_up()
{
	static int	linksupbefore = 0;
	if (linksupbefore) {
		return;
	}
	linksupbefore = 1;

	/* Update our local status... */
	send_local_status(ACTIVESTATUS);

	send_local_starting();

	/* Start each of our known child clients */
	g_list_foreach(config->client_list
	,	start_a_child_client, config->client_children);
}


static void
polled_input_destroy(gpointer user_data)
{
}


static gboolean
clustermsg_input_prepare(gpointer source_data, GTimeVal* current_time
,	gint* timeout, gpointer user_data)
{
	return FALSE;
}

static gboolean
clustermsg_input_check(gpointer source_data, GTimeVal* current_time
,	gpointer	user_data)
{
	GPollFD*	gpfd = source_data;
	return gpfd->revents != 0;
}

static gboolean
clustermsg_input_dispatch(gpointer source_data, GTimeVal* current_time
,	gpointer	user_data)
{
	FILE *		f = user_data;
	GPollFD*	gpfd = source_data;


	if (fileno(f) != gpfd->fd) {
		/* Bad boojum! */
		ha_log(LOG_ERR, "FD mismatch in clustermsg_input_dispatch");
	}


	/* Process the incoming cluster message */
	process_clustermsg(f);
	return TRUE;
}


static void
clustermsg_input_destroy(gpointer user_data)
{
}

static gboolean
APIregistration_input_prepare(gpointer source_data, GTimeVal* current_time
,	gint* timeout, gpointer user_data)
{
	return FALSE;
}

static gboolean
APIregistration_input_check(gpointer source_data, GTimeVal* current_time
,	gpointer	user_data)
{
	GPollFD*	gpfd = source_data;
	return gpfd->revents != 0;
}

static gboolean
APIregistration_input_dispatch(gpointer source_data, GTimeVal* current_time
,	gpointer	user_data)
{
	GPollFD*	gpfd = source_data;
	FILE *		regfifo = user_data;
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG
		,	"Processing register message from"
		" regfd %d.\n", gpfd->fd);
	}
	if (fileno(regfifo) != gpfd->fd) {
		/* Bad boojum! */
		ha_log(LOG_ERR
		,	"FD mismatch in APIregistration_input_dispatch");
	}
	process_registermsg(regfifo);
	return TRUE;
}


static void
APIregistration_input_destroy(gpointer user_data)
{
}

/*
 * All the other input sources are static - they don't come and go as we run
 * Our API clients can register and unregister at any time...
 * Our user_data here is a client_proc_t* telling us which client sent us
 * the message.
 */

static gboolean
APIclients_input_prepare(gpointer source_data, GTimeVal* current_time
,	gint* timeout, gpointer	user_data)
{
	return FALSE;
}

static gboolean
APIclients_input_check(gpointer source_data, GTimeVal* current_time
,	gpointer	user_data)
{
	GPollFD*	gpfd = source_data;
	return gpfd->revents != 0;

}

static gboolean
APIclients_input_dispatch(gpointer source_data, GTimeVal* current_time
,	gpointer	user_data)
{
	GPollFD*	gpfd = source_data;
	client_proc_t*	client = user_data;

	if (gpfd != & client->gpfd) {
		/* Bad boojum! */
		ha_log(LOG_ERR, "GPFD mismatch in APIclients_input_dispatch");
	}
	/* Process a single API client request */
	ProcessAnAPIRequest(client);
	return TRUE;
}

static void
APIclients_input_destroy(gpointer user_data)
{
}

static gboolean
MSPFinalShutdown(gpointer p)
{
	if (procinfo->restart_after_shutdown) {
		if (procinfo->giveup_resources) {
			ha_log(LOG_INFO, "Resource shutdown completed"
			".  Restart triggered.");
		}else{
			ha_log(LOG_INFO, "MSP: Quick shutdown complete.");
		}
	}
	/* Tell init process we're going away */
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "Sending SIGQUIT to pid %d"
		,       processes[0]);
	}
	kill(processes[0], SIGQUIT);
	cleanexit(0);
	/* NOTREACHED*/
	return FALSE;
		kill(processes[0], SIGQUIT);
		cleanexit(0);
}

/*
 * Process a message coming in from our status FIFO
 */
void
process_clustermsg(FILE * f)
{
	struct node_info *	thisnode = NULL;
	struct ha_msg *		msg = NULL;
	char			iface[MAXIFACELEN];
	struct	link *		lnk;

	TIME_T			msgtime = 0;
	TIME_T			now = time(NULL);
	const char *		from;
	const char *		ts;
	const char *		type;
	int			action;
	clock_t			messagetime;
	struct tms		proforma_tms;
	const char *		cseq;
	unsigned long		seqno = 0;
	int			stgen;
	const char *		cstgen;



	strncpy(iface, "?", 2);
	if ((msg = if_msgfromstream(f, iface)) == NULL) {
		return;
	}
	now = time(NULL);
	messagetime = times(&proforma_tms);

	if (standby_running) {
		/* if there's a standby timer running, verify if it's
		   time to enable the standby messages again... */
		if (now >= standby_running) {
			standby_running = 0L;
			other_is_stable = 1;
			going_standby = NOT;
			ha_log(LOG_WARNING, "No reply to standby request"
			".  Standby request cancelled.");
		}
	}

	/* Extract message type, originator, timestamp, auth */
	type = ha_msg_value(msg, F_TYPE);
	from = ha_msg_value(msg, F_ORIG);
	ts = ha_msg_value(msg, F_TIME);
	cseq = ha_msg_value(msg, F_SEQ);
	cstgen = ha_msg_value(msg, F_STGEN);

	if (!isauthentic(msg)) {
		ha_log(LOG_WARNING
		,       "process_clustermsg: node [%s]"
		" failed authentication", from ? from : "?");
		if (ANYDEBUG) {
			ha_log_message(msg);
		}
		goto psm_done;
	}else if (DEBUGDETAILS) {
		ha_log(LOG_DEBUG
		,       "process_clustermsg: node [%s] auth ok"
		,	from ? from :"?");
	}

	if (from == NULL || ts == NULL || type == NULL) {
		ha_log(LOG_ERR
		,	"process_clustermsg: %s: iface %s, from %s"
		,	"missing from/ts/type"
		,	iface
		,	(from? from : "<?>"));
		ha_log_message(msg);
		goto psm_done;
	}
	if (cseq != NULL) {
		sscanf(cseq, "%lx", &seqno);
	}else{
		seqno = 0L;
		if (strncmp(type, NOSEQ_PREFIX, STRLEN(NOSEQ_PREFIX)) != 0) {
			ha_log(LOG_ERR
			,	"process_clustermsg: %s: iface %s, from %s"
			,	"missing seqno"
			,	iface
			,	(from? from : "<?>"));
			ha_log_message(msg);
			goto psm_done;
		}
	}

	stgen = (cstgen ? atoi(cstgen) : 0);

	sscanf(ts, TIME_X, &msgtime);

	if (ts == 0 || msgtime == 0) {
		goto psm_done;
	}


	thisnode = lookup_node(from);

	if (thisnode == NULL) {
#if defined(MITJA)
		/* If a node isn't in the configfile, add it... */
		ha_log(LOG_WARNING
		,   "process_status_message: new node [%s] in message"
		,	from);
		add_node(from, NORMALNODE);
		thisnode = lookup_node(from);
		if (thisnode == NULL) {
			goto psm_done;
		}
#else
		/* If a node isn't in the configfile - whine */
		ha_log(LOG_ERR
		,   "process_status_message: bad node [%s] in message"
		,	from);
		ha_log_message(msg);
		goto psm_done;
#endif
	}

	/* Throw away some incoming packets if testing is enabled */
	if (TESTRCV) {
		if (thisnode != curnode && TestRand(rcv_loss_prob)) {
			goto psm_done;
		}
	}
	thisnode->anypacketsyet = 1;
	/* See if our comm channels are working yet... */
	if (heartbeat_comm_state != COMM_LINKSUP) {
		check_comm_isup();
	}

	lnk = lookup_iface(thisnode, iface);

	/* Is this message a duplicate, or destined for someone else? */

	action=should_drop_message(thisnode, msg, iface);

	switch (action) {
		case DROPIT:
		/* Ignore it */
		heartbeat_monitor(msg, action, iface);
		goto psm_done;

		case DUPLICATE:
		heartbeat_monitor(msg, action, iface);
		case KEEPIT:

		/* Even though it's a DUP, it could update link status*/
		if (lnk) {
			lnk->lastupdate = messagetime;
			/* Is this from a link which was down? */
			if (strcasecmp(lnk->status, LINKUP) != 0) {
				change_link_status(thisnode, lnk
				,	LINKUP);
			}
		}
		if (action == DUPLICATE) {
			goto psm_done;
		}
		break;
	}


	thisnode->track.last_iface = iface;


	/*
	 * FIXME: ALL messages ought to go through a GHashTable
	 * and get called as functions so it's  easily extensible
	 * without messing up this logic.  It would be faster, too!
	 * parameters to these functions should be:
	 *	type
	 *	thisnode
	 *	iface
	 *	msg
	 */

	if (strcasecmp(type, T_STATUS) == 0
	||	strcasecmp(type, T_NS_STATUS) == 0) {

	/* Is this a status update (i.e., "heartbeat") message? */
		const char *	status;

		status = ha_msg_value(msg, F_STATUS);
		if (status == NULL)  {
			ha_log(LOG_ERR, "process_status_message: "
			"status update without "
			F_STATUS " field");
			goto psm_done;
		}

		/* Do we already have a newer status? */
		if (msgtime < thisnode->rmt_lastupdate
		&&		seqno < thisnode->status_seqno) {
			goto psm_done;
		}

		/* Have we seen an update from here before? */

		if (thisnode->local_lastupdate) {
			clock_t		heartbeat_interval;
			heartbeat_interval = messagetime
			-	thisnode->local_lastupdate;
			if (heartbeat_interval > config->warntime_interval) {
				ha_log(LOG_WARNING
				,	"Late heartbeat: Node %s:"
				" interval %ld ms"
				,	thisnode->nodename
				,	(heartbeat_interval*1000) / CLK_TCK);
			}
		}


		/* Is the node status the same? */
		if (strcasecmp(thisnode->status, status) != 0
		&& 	stgen >= thisnode->status_gen) {
			ha_log(LOG_INFO
			,	"Status update for node %s: status %s"
			,	thisnode->nodename
			,	status);
			if (ANYDEBUG) {
				ha_log(LOG_DEBUG
				,	"Status seqno: %ld msgtime: %ld"
				" status generation: %d"
				,	seqno, msgtime, stgen);
			}
			
			notify_world(msg, thisnode->status);
			strncpy(thisnode->status, status
			, 	sizeof(thisnode->status));
			heartbeat_monitor(msg, action, iface);
		}else{
			heartbeat_monitor(msg, NOCHANGE, iface);
		}

		/* Did we get a status update on ourselves? */
		if (thisnode == curnode) {
			tickle_watchdog();
		}

		thisnode->rmt_lastupdate = msgtime;
		thisnode->local_lastupdate = messagetime;
		thisnode->status_seqno = seqno;
		thisnode->status_gen = stgen;

	}else if (strcasecmp(type, T_REXMIT) == 0) {
		heartbeat_monitor(msg, PROTOCOL, iface);
		if (thisnode != curnode) {
			/* Forward to control process */
			send_cluster_msg(msg);
		}

	/* END OF STATUS/ LINK PROTOCOL CODE */


	/* Did we get a "shutdown complete" message? */

	}else if (strcasecmp(type, T_SHUTDONE) == 0) {
		process_resources(type, msg, thisnode);
	    	heartbeat_monitor(msg, action, iface);
		if (thisnode == curnode) {
			if (ANYDEBUG) {
				ha_log(LOG_DEBUG
				,	"Received T_SHUTDONE from ourselves.");
		    	}
			/* Trigger final shutdown in a second */
			g_timeout_add(1000, MSPFinalShutdown, NULL);
		}else{
			thisnode->has_resources = FALSE;
			other_is_stable = 0;
			other_holds_resources= NO_RSC;

		    	ha_log(LOG_INFO
			,	"Received shutdown notice from '%s'"
			". Resources being acquired."
			,	thisnode->nodename);
			takeover_from_node(thisnode->nodename);
		}

	}else if (strcasecmp(type, T_STARTING) == 0
	||	strcasecmp(type, T_RESOURCES) == 0) {
		/*
		 * process_resources() will deal with T_STARTING
		 * and T_RESOURCES messages appropriately.
		 */
		heartbeat_monitor(msg, action, iface);
		process_resources(type, msg, thisnode);

	}else if (strcasecmp(type, T_ASKRESOURCES) == 0) {

		/* someone wants to go standby!!! */
		heartbeat_monitor(msg, action, iface);
		ask_for_resources(msg);

	}else	if (strcasecmp(type, T_ASKRELEASE) == 0) {
		if (thisnode != curnode) {
			/*
			 * Queue for later handling...
			 */
			QueueRemoteRscReq(PerformQueuedNotifyWorld, msg);
			/* Mama don't let them free my msg! */
			return;
		}
		heartbeat_monitor(msg, action, iface);

	}else if (strcasecmp(type, T_ACKRELEASE) == 0) {

		/* Ignore this, we're shutting down! */
		if (shutdown_in_progress) {
			goto psm_done;
		}
		heartbeat_monitor(msg, action, iface);
		notify_world(msg, thisnode->status);
	}else{
		/* None of the above... */
		heartbeat_monitor(msg, action, iface);
		notify_world(msg, thisnode->status);
	}
psm_done:
	ha_msg_del(msg);  msg = NULL;
}

/* Process a registration request from a potential client */
void
process_registermsg(FILE *regfifo)
{
	struct ha_msg *		msg = NULL;

	/* Ill-behaved clients can cause this... */

	if ((msg = msgfromstream(regfifo)) == NULL) {
		return;
	}
	api_heartbeat_monitor(msg, APICALL, "<api>");
	api_process_registration(msg);
	ha_msg_del(msg);  msg = NULL;
}

/*
 * Here starts the nice_failback thing. The main purpouse of
 * nice_failback is to create a controlled failback. This
 * means that when the primary comes back from an outage it
 * stays quiet and acts as a secondary/backup server.
 * There are some more comments about it in nice_failback.txt
 */

/*
 * At this point nice failback deals with two nodes and is
 * an interim measure. The new version using the API is coming soon!
 *
 * This piece of code treats five different situations:
 *
 * 1. Node1 is starting and Node2 is down (or vice-versa)
 *    Take the resources. req_our_resources(), mark_node_dead()
 *
 * 2. Node1 and Node2 are starting at the same time
 *    Let both machines req_our_resources().
 *
 * 3. Node1 is starting and Node2 holds no resources
 *    Just like #2
 *
 * 4. Node1 is starting and Node2 has (his) local resources
 *    Let's ask for our local resources. req_our_resources()
 *
 * 5. Node1 is starting and Node2 has both local and foreign
 *	resources (all resources)
 *    Do nothing :)
 *
 */
/*
 * About the nice_failback resource takeover model:
 *
 * There are two principles that seem to guarantee safety:
 *
 *      1) Take all unclaimed resources if the other side is stable.
 *	      [Once you do this, you are also stable].
 *
 *      2) Take only unclaimed local resources when a timer elapses
 *		without things becoming stable by (1) above.
 *	      [Once this occurs, you're stable].
 *
 * Stable means that we have taken the resources we think we ought to, and
 * won't take any more without another transition ocurring.
 *
 * The other side is stable whenever it says it is (in its RESOURCE
 * message), or if it is dead.
 *
 * The nice thing about the stable bit in the resources message is that it
 * enables you to tell if the other side is still messing around, or if
 * they think they're done messing around.  If they're done, then it's safe
 * to proceed.  If they're not, then you need to wait until they say
 * they're done, or until a timeout occurs (because no one has become stable).
 *
 * When the timeout occurs, you're both deadlocked each waiting for the
 * other to become stable.  Then it's safe to take your local resources
 * (unless, of course, for some unknown reason, the other side has taken
 * them already).
 *
 * If a node dies die, then they'll be marked dead, and its resources will
 * be marked unclaimed.  In this case, you'll take over everything - whether
 * local resources through mark_node_dead() or remote resources through
 * mach_down.
 */

#define	UPD_RSC(cur, up)	((up == NO_RSC) ? NO_RSC : ((up)|(cur)))

void
process_resources(const char * type, struct ha_msg* msg, struct node_info * thisnode)
{
	static int		resources_requested_yet = 0;

	enum rsc_state		newrstate = resourcestate;
	int			first_time = 1;

	if (!DoManageResources || heartbeat_comm_state != COMM_LINKSUP) {
		return;
	}

	if (!nice_failback) {
		/* Original ("normal") starting behavior */
		if (!WeAreRestarting && !resources_requested_yet) {
			resources_requested_yet=1;
			req_our_resources(0);
		}
		return;
	}

	/* Otherwise, we're in the nice_failback case */

	/* This first_time switch looks buggy -- FIXME */

	if (first_time && WeAreRestarting) {
		resourcestate = newrstate = R_STABLE;
	}


	/*
	 * Deal with T_STARTING messages coming from the other side.
	 *
	 * These messages are a request for resource usage information.
	 * The appropriate reply is a T_RESOURCES message.
	 */

	 if (strcasecmp(type, T_STARTING) == 0 && (thisnode != curnode)) {

		switch(resourcestate) {

		case R_RSCRCVD:
		case R_STABLE:
		case R_SHUTDOWN:
			break;
		case R_STARTING:
			newrstate = R_BOTHSTARTING;
			/* ??? req_our_resources(); ??? */
			break;

		default:
			ha_log(LOG_ERR, "Received '%s' message in state %d"
			,	T_STARTING, resourcestate);
			return;

		}
		other_is_stable = 0;
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG
			, "process_resources: other now unstable");
		}
		if (takeover_in_progress) {
			ha_log(LOG_WARNING
			,	"T_STARTING received during takeover.");
		}
		send_resources_held(rsc_msg[procinfo->i_hold_resources]
		,	resourcestate == R_STABLE, NULL);
	}

	/* Manage resource related messages... */

	if (strcasecmp(type, T_RESOURCES) == 0) {
		const char *p;
		int n;
		/*
		 * There are four possible resource answers:
		 *
		 * "I don't hold any resources"			NO_RSC
		 * "I hold only LOCAL resources"		LOCAL_RSC
		 * "I hold only FOREIGN resources"		FOREIGN_RSC
		 * "I hold ALL resources" (local+foreign)	ALL_RSC
		 */

		p=ha_msg_value(msg, F_RESOURCES);
		if (p == NULL) {
			ha_log(LOG_ERR
			,	T_RESOURCES " message without " F_RESOURCES
			" field.");
			return;
		}

		switch (resourcestate) {

		case R_BOTHSTARTING:
		case R_STARTING:	newrstate = R_RSCRCVD;
		case R_RSCRCVD:
		case R_STABLE:
		case R_SHUTDOWN:
					break;

		default:		ha_log(LOG_ERR,	T_RESOURCES
					" message received in state %d"
					,	resourcestate);
					return;
		}

		n = encode_resources(p);

		if (thisnode != curnode) {
			/*
			 * This T_RESOURCES message is from the other side.
			 */

			const char *	f_stable;

			/* f_stable is NULL when msg from takeover script */
			if ((f_stable = ha_msg_value(msg, F_ISSTABLE)) != NULL){
				if (strcmp(f_stable, "1") == 0) {
					if (!other_is_stable) {
						ha_log(LOG_INFO
						,	"remote resource"
						" transition completed.");
						other_is_stable = 1;
					}
				}else{
					other_is_stable = 0;
					if (ANYDEBUG) {
						ha_log(LOG_DEBUG
						, "process_resources(2): %s"
						, " other now unstable");
					}
				}
			}

			other_holds_resources
			=	UPD_RSC(other_holds_resources,n);

			if (resourcestate != R_STABLE && other_is_stable) {
				ha_log(LOG_INFO
				,	"remote resource transition completed."
				);
				req_our_resources(0);
				newrstate = R_STABLE;
				send_resources_held
				(	rsc_msg[procinfo->i_hold_resources]
				,	1, NULL);
			}
		}else{
			const char * comment = ha_msg_value(msg, F_COMMENT);

			/*
			 * This T_RESOURCES message is from us.  It might be
			 * from the "mach_down" script or our own response to
			 * the other side's T_STARTING message.  The mach_down
			 * script sets the info (F_COMMENT) field to "mach_down"
			 * We set it to "shutdown" in giveup_resources().
			 *
			 * We do this so the audits work cleanly AND we can
			 * avoid a potential race condition.
			 *
			 * Also, we could now time how long a takeover is
			 * taking to occur, and complain if it takes "too long"
			 * 	[ whatever *that* means ]
			 */
				/* Probably unnecessary */
			procinfo->i_hold_resources
			=	UPD_RSC(procinfo->i_hold_resources, n);

			if (comment) {
				if (strcmp(comment, "mach_down") == 0) {
					ha_log(LOG_INFO
					,	"mach_down takeover complete.");
					takeover_in_progress = 0;
					/* FYI: This also got noted earlier */
					procinfo->i_hold_resources
					|=	FOREIGN_RSC;
					other_is_stable = 1;
					if (ANYDEBUG) {
						ha_log(LOG_DEBUG
						, "process_resources(3): %s"
						, " other now stable");
					}
				}else if (strcmp(comment, "shutdown") == 0) {
					resourcestate = newrstate = R_SHUTDOWN;
				}
			}
		}
	}
	if (strcasecmp(type, T_SHUTDONE) == 0) {
		if (thisnode != curnode) {
			other_is_stable = 0;
			other_holds_resources = NO_RSC;
			if (ANYDEBUG) {
				ha_log(LOG_DEBUG
				, "process_resources(4): %s"
				, " other now stable - T_SHUTDONE");
			}
		}else{
			resourcestate = newrstate = R_SHUTDOWN;
			procinfo->i_hold_resources = 0;
		}
	}

	if (resourcestate != newrstate) {
		if (ANYDEBUG) {
			ha_log(LOG_INFO
			,	"STATE %d => %d", resourcestate, newrstate);
		}
	}

	resourcestate = newrstate;

	if (resourcestate == R_RSCRCVD && local_takeover_time == 0L) {
		local_takeover_time =	add_longclock(time_longclock()
		,	secsto_longclock(RQSTDELAY));
	}

	AuditResources();
}

static void
AuditResources(void)
{
	if (!nice_failback) {
		return;
	}

	/*******************************************************
	 *	Look for for duplicated or orphaned resources
	 *******************************************************/

	/*
	 *	Do both nodes own our local resources?
	 */

	if ((procinfo->i_hold_resources & LOCAL_RSC) != 0
	&&	(other_holds_resources & FOREIGN_RSC) != 0) {
		ha_log(LOG_ERR, "Both machines own our resources!");
	}

	/*
	 *	Do both nodes own foreign resources?
	 */

	if ((other_holds_resources & LOCAL_RSC) != 0
	&&	(procinfo->i_hold_resources & FOREIGN_RSC) != 0) {
		ha_log(LOG_ERR, "Both machines own foreign resources!");
	}

	/*
	 *	If things are stable, look for orphaned resources...
	 */

	if (resourcestate == R_STABLE && other_is_stable
	&&	!shutdown_in_progress) {
		/*
		 *	Does someone own local resources?
		 */

		if ((procinfo->i_hold_resources & LOCAL_RSC) == 0
		&&	(other_holds_resources & FOREIGN_RSC) == 0) {
			ha_log(LOG_ERR, "No one owns our local resources!");
		}

		/*
		 *	Does someone own foreign resources?
		 */

		if ((other_holds_resources & LOCAL_RSC) == 0
		&&	(procinfo->i_hold_resources & FOREIGN_RSC) == 0) {
			ha_log(LOG_ERR, "No one owns foreign resources!");
		}
	}
}

void
check_auth_change(struct sys_config *conf)
{
	if (conf->rereadauth) {
		/* parse_authfile() resets 'rereadauth' */
		if (parse_authfile() != HA_OK) {
			/* OOPS.  Sayonara. */
			ha_log(LOG_ERR
			,	"Authentication reparsing error, exiting.");
			force_shutdown();
			cleanexit(1);
		}
	}
}

/* Function called to set up status alarms */
void
init_status_alarm(void)
{
	siginterrupt(SIGALRM, 1);
	signal(SIGALRM, ding_sig);
	ding_action();
}

/*
 * We look at the directory /etc/ha.d/rc.d to see what
 * scripts are there to avoid trying to run anything
 * which isn't there.
 */
static GHashTable* RCScriptNames = NULL;

static void
CreateInitialFilter(void)
{
	DIR*	dp;
	struct dirent*	dep;
	static char foo[] = "bar";
	RCScriptNames = g_hash_table_new(g_str_hash, g_str_equal);
	if ((dp = opendir(HEARTBEAT_RC_DIR)) == NULL) {
		ha_perror("Cannot open directory " HEARTBEAT_RC_DIR);
		return;
	}
	while((dep = readdir(dp)) != NULL) {
		if (dep->d_name[0] == '.') {
			continue;
		}
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG
			,	"CreateInitialFilter: %s", dep->d_name);
		}
		g_hash_table_insert(RCScriptNames, g_strdup(dep->d_name),foo);
	}
	closedir(dp);
}
static int
FilterNotifications(const char * msgtype)
{
	int		rc;
	if (RCScriptNames == NULL) {
		CreateInitialFilter();
	}
	rc = g_hash_table_lookup(RCScriptNames, msgtype) != NULL;

	if (DEBUGDETAILS) {
		ha_log(LOG_DEBUG
		,	"FilterNotifications(%s) => %d"
		,	msgtype, rc);
	}

	return rc;
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
	char 		rc_arg0 [] = RC_ARG0;
	char *	const argv[MAXFIELDS+3] = {rc_arg0, command, NULL};
	const char *	fp;
	char *		tp;
	int		pid, status;

	if (!DoManageResources) {
		return;
	}

	tp = command;

	fp  = ha_msg_value(msg, F_TYPE);
	ASSERT(fp != NULL && strlen(fp) < STATUSLENG);

	if (fp == NULL || strlen(fp) >= STATUSLENG
	||	 !FilterNotifications(fp)) {
		return;
	}

	if (ANYDEBUG) {
		ha_log(LOG_DEBUG
		,	"notify_world: invoking %s: OLD status: %s"
		,	RC_ARG0,	(ostatus ? ostatus : "(none)"));
	}


	while (*fp) {
		if (isupper((unsigned int)*fp)) {
			*tp = tolower((unsigned int)*fp);
		}else{
			*tp = *fp;
		}
		++fp; ++tp;
	}
	*tp = EOS;

	switch ((pid=fork())) {

		case -1:	ha_perror("Can't fork to notify world!");
				break;


		case 0:	{	/* Child */
				int	j;
				make_normaltime();
				set_proc_title("%s: notify_world()", cmdname);
				setpgrp();
				signal(SIGCHLD, SIG_DFL);
				for (j=0; j < msg->nfields; ++j) {
					char ename[64];
					sprintf(ename, "HA_%s", msg->names[j]);
					setenv(ename, msg->values[j], 1);
				}
				if (ostatus) {
					setenv(OLDSTATUS, ostatus, 1);
				}
				if (nice_failback) {
					setenv(HANICEFAILBACK, "yes", 1);
				}
				if (ANYDEBUG) {
					ha_log(LOG_DEBUG
					,	"notify_world: Running %s %s"
					,	argv[0], argv[1]);
				}
				execv(RCSCRIPT, argv);

				ha_log(LOG_ERR, "cannot exec %s", RCSCRIPT);
				cleanexit(1);
				/*NOTREACHED*/
				break;
			}


		default:	/* Parent */
				/*
				 * If "hook" is non-NULL, we want to queue
				 * it to run later (possibly now)
				 * So, we need a different discipline
				 * for managing such a process...
				 */
				/* We no longer need the "hook" parameter */
				RSCMGMTPROC(pid, "notify world");

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
 	PILSetDebugLevel(PluginLoadingSystem, NULL, NULL
			,	debug);
	ha_log(LOG_DEBUG, "debug now set to %d [pid %d]", debug, getpid());
	dump_proc_stats(curproc);
}

/* Signal handler to use with SIGCHLD to free the
 * resources of any exited children using wait3(2).
 * This stops zombie processes from hanging around
 */

void
reaper_sig(int sig)
{
	signal(sig, reaper_sig);
	pending_handlers|=REAPER_SIG;
}

/*
 *	We need to handle the case of the exiting process is one of our
 *	client children that we spawn as requested when we started up.
 */
void
reaper_action(void)
{
	int status;
	pid_t	pid;

	while((pid=wait3(&status, WNOHANG, NULL)) > 0) {

		/* If they're in the API client table, remove them... */
		api_remove_client_pid(pid, "died");

		ReportProcHasDied(pid, status);

	}/*endwhile*/
}
/***********************************************************************
 * Track the core heartbeat processes
 ***********************************************************************/

/* Log things about registered core processes */
static void
CoreProcessRegistered(ProcTrack* p)
{
	++CoreProcessCount;

	if (p->pid > 0) {
		processes[procinfo->nprocs] = p->pid;
		procinfo->info[procinfo->nprocs].pstat = FORKED;
		procinfo->info[procinfo->nprocs].pid = p->pid;
		procinfo->nprocs++;
	}

}

/* Handle the death of a core heartbeat process */
static void
CoreProcessDied(ProcTrack* p, int status, int signo, int exitcode, int waslogged)
{
	-- CoreProcessCount;

	if (shutdown_in_progress) {
		p->privatedata = NULL;
		ha_log(LOG_INFO,"Core process %d exited. %d remaining"
		,	p->pid, CoreProcessCount);

		if (CoreProcessCount <= 1) {
			FinalCPShutdown();
		}
		return;
	}
	/* UhOh... */
	ha_log(LOG_ERR
	,	"Core heartbeat process died! Restarting.");
	trigger_restart(FALSE);
	/*NOTREACHED*/
	p->privatedata = NULL;
	return;
}

static const char *
CoreProcessName(ProcTrack* p)
{
	int	procindex = GPOINTER_TO_INT(p->privatedata);
	volatile struct process_info *	pi = procinfo->info+procindex;

	return (pi ? core_proc_name(pi->type) : "Core heartbeat process");
	
}
static void
FinalCPShutdown(void)
{
	struct rlimit		oflimits;
	int			j;

	ha_log(LOG_INFO,"Heartbeat shutdown complete.");
	if (procinfo->restart_after_shutdown) {
		restart_heartbeat();
	}
	IGNORESIG(SIGTERM);
	/* Kill any lingering processes, etc.*/
	kill(-getpid(), SIGTERM);


	getrlimit(RLIMIT_NOFILE, &oflimits);
	for (j=oflimits.rlim_cur; j >= 0; --j) {
		close(j);
	}
	unlink(PIDFILE);
	cleanexit(0);
}

/***********************************************************************
 * Track our managed child processes...
 ***********************************************************************/

static void
ManagedChildRegistered(ProcTrack* p)
{
	struct client_child*	managedchild = p->privatedata;

	managed_child_count++;
	managedchild->pid = p->pid;
}

/* Handle the death of one of our managed child processes */
static void
ManagedChildDied(ProcTrack* p, int status, int signo, int exitcode
,	int waslogged)
{
	struct client_child*	managedchild = p->privatedata;

	managedchild->pid = 0;
	managed_child_count --;

	/* If they exit 100 we won't restart them */

	if (managedchild->respawn && !shutdown_in_progress
	&&	exitcode != 100) {
		struct tms	proforma_tms;
		clock_t		now = times(&proforma_tms);
		clock_t		minticks = CLK_TCK * 30;
		++managedchild->respawncount;

		if ((now - p->startticks) < minticks) {
			++managedchild->shortrcount;
		}else{
			managedchild->shortrcount = 0;
		}
		if (managedchild->shortrcount > 10) {
			ha_log(LOG_ERR
			,	"Client %s %s"
			,	managedchild->command
			,	"respawning too fast");
			managedchild->shortrcount = 0;
		}else{
			ha_log(LOG_INFO
			,	"Respawning client %s:"
			,	managedchild->command);
			start_a_child_client(managedchild
			,	config->client_children);
		}
	}
	p->privatedata = NULL;

	/* On quick restart, these are the only ones killed */
	if (managed_child_count == 0 && shutdown_in_progress
	&&	! procinfo->giveup_resources) {

		g_timeout_add(1000, MSPFinalShutdown, NULL);
	}
}

/* Handle the death of one of our managed child processes */

static const char *
ManagedChildName(ProcTrack* p)
{
		struct client_child*	managedchild = p->privatedata;
		return managedchild->command;
}

static void
RscMgmtProcessRegistered(ProcTrack* p)
{
	ResourceMgmt_child_count ++;
}
/* Handle the death of a resource management process */
static void
RscMgmtProcessDied(ProcTrack* p, int status, int signo, int exitcode
,	int waslogged)
{
	ResourceMgmt_child_count --;
	p->privatedata = NULL;
	StartNextRemoteRscReq();
}

static const char *
RscMgmtProcessName(ProcTrack* p)
{
	struct const_string * s = p->privatedata;

	return (s && s->str ? s->str : "heartbeat child");
}

/***********************************************************************
 *
 * RemoteRscRequests are resource management requests from other nodes
 *
 * Our "privatedata" is a GHook.  This GHook points back to the
 * queue entry for this object. Its "data" element points to the message
 * which we want to give to the function which the hook points to...
 * QueueRemoteRscReq is the function which sets up the hook, then queues
 * it for later execution.
 *
 * StartNextRemoteRscReq() is the function which runs the hook,
 * when the time is right.  Basically, we won't run the hook if any
 * other asynchronous resource management operations are going on.
 * This solves the problem of a remote request coming in and conflicting
 * with a different local resource management request.  It delays
 * it until the local startup/takeover/etc. operations are complete.
 * At this time, it has a clear picture of what's going on, and
 * can safely do its thing.
 *
 * So, we queue the job to do in a Ghook.  When the Ghook runs, it
 * will create a ProcTrack object to track the completion of the process.
 *
 * When the process completes, it will clean up the ProcTrack, which in
 * turn will remove the GHook from the queue, destroying it and the
 * associated struct ha_msg* from the original message.
 *
 ***********************************************************************/

static GHookList	RemoteRscReqQueue = {0,0,0};
static GHook*		RunningRemoteRscReq = NULL;

/* Initialized the remote resource request queue */
static void
InitRemoteRscReqQueue(void)
{
	if (RemoteRscReqQueue.is_setup) {
		return;
	}
	g_hook_list_init(&RemoteRscReqQueue, sizeof(GHook));
}

/* Queue a remote resource request */
static void
QueueRemoteRscReq(RemoteRscReqFunc func, struct ha_msg* msg)
{
	GHook*	hook;

	InitRemoteRscReqQueue();
	hook = g_hook_alloc(&RemoteRscReqQueue);

	if (ANYDEBUG) {
		ha_log(LOG_DEBUG
		,	"Queueing remote resource request (hook = 0x%x)"
		,	(unsigned int)hook);
		ha_log_message(msg);
	}
	hook->func = func;
	hook->data = msg;
	hook->destroy = (GDestroyNotify)(ha_msg_del);
	g_hook_append(&RemoteRscReqQueue, hook);
	StartNextRemoteRscReq();
}

/* If the time is right, start the next remote resource request */
static void
StartNextRemoteRscReq(void)
{
	GHook*		hook;
	RemoteRscReqFunc	func;

	/* We can only run one of these at a time... */
	if (ResourceMgmt_child_count != 0) {
		return;
	}

	RunningRemoteRscReq = NULL;

	/* Run the first hook in the list... */

	hook = g_hook_first_valid(&RemoteRscReqQueue, FALSE);
	if (hook == NULL) {
		ResourceMgmt_child_count = 0;
		return;
	}

	RunningRemoteRscReq = hook;
	func = hook->func;

	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "StartNextRemoteRscReq() - calling hook");
	}
	/* Call the hook... */
	func(hook);
	g_hook_unref(&RemoteRscReqQueue, hook);
	g_hook_destroy_link(&RemoteRscReqQueue, hook);
}


/*
 * Perform a queued notify_world() call
 *
 * The Ghook and message are automatically destroyed by our
 * caller.
 */

static void
PerformQueuedNotifyWorld(GHook* hook)
{
	struct ha_msg* m = hook->data;
	/*
	 * We have been asked to run a notify_world() which
	 * we would like to have done earlier...
	 */
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "PerformQueuedNotifyWorld() msg follows");
		ha_log_message(m);
	}
	notify_world(m, curnode->status);
	/* "m" is automatically destroyed when "hook" is */
}



void
KillTrackedProcess(ProcTrack* p, void * data)
{
	int	nsig = GPOINTER_TO_INT(data);
	int	pid = p->pid;
	const char *	porg;
	const char * pname;

	pname = p->ops->proctype(p);

	if (p->isapgrp) {
		pid = -p->pid;
		porg = "process group";
	}else{
		pid =  p->pid;
		porg = "process";
	}
	ha_log(LOG_INFO, "killing %s %s %d with signal %d", pname, porg
	,	p->pid, nsig);
	/* Suppress logging this process' death */
	p->loglevel = PT_LOGNONE;
	kill(pid, nsig);
}

struct StonithProcHelper {
	char *		nodename;
};
	

/* Handle the death of a STONITH process */
static void
StonithProcessDied(ProcTrack* p, int status, int signo, int exitcode, int waslogged)
{
	struct StonithProcHelper*	h = p->privatedata;

	if (signo != 0 || exitcode != 0) {
		ha_log(LOG_ERR, "STONITH of %s failed.  Retrying..."
		,	(const char*) p->privatedata);
		Initiate_Reset(config->stonith, h->nodename);
	}else{
		/* We need to finish taking over the other side's resources */
		takeover_from_node(h->nodename);
	}
	g_free(h->nodename);	h->nodename=NULL;
	g_free(p->privatedata);	p->privatedata = NULL;
}

static const char *
StonithProcessName(ProcTrack* p)
{
	static char buf[100];
	struct StonithProcHelper *	h = p->privatedata;
	snprintf(buf, sizeof(buf), "STONITH %s", h->nodename);
	return buf;
}

static void
start_a_child_client(gpointer childentry, gpointer pidtable)
{
	struct client_child*	centry = childentry;
	pid_t			pid;

	ha_log(LOG_INFO, "Starting child client %s (%d,%d)"
	,	centry->command, centry->u_runas
	,	centry->g_runas);

	if (centry->pid != 0) {
		ha_log(LOG_ERR, "OOPS! client %s already running as pid %d"
		,	centry->command, centry->pid);
	}

	/*
	 * We need to ensure that the exec will succeed before
	 * we bother forking.  We don't want to respawn something that
	 * won't exec in the first place.
	 */

	if (access(centry->command, F_OK|X_OK) < 0) {
		ha_perror("Cannot exec %s", centry->command);
		return;
	}

	/* We need to fork so we can make child procs not real time */
	switch(pid=fork()) {

		case -1:	ha_log(LOG_ERR
				,	"start_a_child_client: Cannot fork.");
				return;

		default:	/* Parent */
				NewTrackedProc(pid, 1, PT_LOGVERBOSE
				,	centry, &ManagedChildTrackOps);
				return;

		case 0:		/* Child */
				break;
	}

	/* Child process:  start the managed child */
	make_normaltime();
	setpgrp();

	/* Limit peak resource usage, maximize success chances */
	if (centry->shortrcount > 0) {
		alarm(0);
		sleep(1);
	}

	ha_log(LOG_INFO, "Starting %s as uid %d  gid %d (pid %d)"
	,	centry->command, centry->u_runas
	,	centry->g_runas, getpid());

	if (	setgid(centry->g_runas) < 0
	||	setuid(centry->u_runas) < 0
	||	siginterrupt(SIGALRM, 0) < 0) {

		ha_perror("Cannot setup child process %s"
		,	centry->command);
	}else{
		const char *	devnull = "/dev/null";
		int	j;
		struct rlimit		oflimits;
		signal(SIGCHLD, SIG_DFL);
		alarm(0);
		IGNORESIG(SIGALRM);

		/* A precautionary measure */
		getrlimit(RLIMIT_NOFILE, &oflimits);
		for (j=0; j < oflimits.rlim_cur; ++j) {
			close(j);
		}
		(void)open(devnull, O_RDONLY);	/* Stdin:  fd 0 */
		(void)open(devnull, O_WRONLY);	/* Stdout: fd 1 */
		(void)open(devnull, O_WRONLY);	/* Stderr: fd 2 */
		(void)execl(centry->command, centry->command, NULL);

		/* Should not happen */
		ha_perror("Cannot exec %s", centry->command);
	}
	/* Suppress respawning */
	exit(100);
}

void
term_sig(int sig)
{
	signal(sig, term_sig);
	pending_handlers |= TERM_SIG;
}

void
term_cleanexit(int sig)
{
	cleanexit(100+sig);
}

void
term_action(void)
{
	IGNORESIG(SIGTERM);
	make_normaltime();
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "Process %d processing SIGTERM", getpid());
	}
	if (curproc->type == PROC_MST_STATUS) {
		if (procinfo->giveup_resources) {
			giveup_resources();
		}else{
			shutdown_in_progress = 1;
			DisableProcLogging();	/* We're shutting down */
			/* Kill our managed children... */
			ForEachProc(&ManagedChildTrackOps, KillTrackedProcess
			,	GINT_TO_POINTER(SIGTERM));
		}
		
	}else if (curproc->type == PROC_CONTROL) {
		signal_all(SIGTERM);
	}else{
		cleanexit(100+SIGTERM);
	}
}

static const char *
core_proc_name(enum process_type t)
{
	const char *	ct = "huh?";
	switch(t) {
		case PROC_UNDEF:	ct = "UNDEF";		break;
		case PROC_CONTROL:	ct = "CONTROL";		break;
		case PROC_MST_STATUS:	ct = "MST_STATUS";	break;
		case PROC_HBREAD:	ct = "HBREAD";		break;
		case PROC_HBWRITE:	ct = "HBWRITE";		break;
		case PROC_PPP:		ct = "PPP";		break;
		default:		ct = "core process huh?";		break;
	}
	return ct;
}
void
dump_proc_stats(volatile struct process_info * proc)
{
	const char *	ct;
	unsigned long	curralloc;

	if (!proc) {
		return;
	}

	ct = core_proc_name(proc->type);

	ha_log(LOG_INFO, "MSG stats: %ld/%ld age %ld [pid%d/%s]"
	,	proc->allocmsgs, proc->totalmsgs
	,	time(NULL) - proc->lastmsg, proc->pid, ct);

	if (proc->numalloc > proc->numfree) {
		curralloc = proc->numalloc - proc->numfree;
	}else{
		curralloc = 0;
	}

	ha_log(LOG_INFO, "ha_malloc stats: %lu/%lu  %lu/%lu [pid%d/%s]"
	,	curralloc, proc->numalloc
	,	proc->nbytes_alloc, proc->nbytes_req, proc->pid, ct);

	ha_log(LOG_INFO, "RealMalloc stats: %lu total malloc bytes."
	" pid [%d/%s]", proc->mallocbytes, proc->pid, ct);
}

void
dump_all_proc_stats()
{
	int	j;

	for (j=0; j < procinfo->nprocs; ++j) {
		dump_proc_stats(procinfo->info+j);
	}
}

static void
__parent_debug_action(int sig)
{
	int	olddebug = debug;

	debug_sig(sig);
	signal_all(sig);

	if (debug == 1 && olddebug == 0) {
		ha_versioninfo();
	}
}

void
parent_debug_usr1_sig(int sig)
{
	signal(sig, parent_debug_usr1_sig);
	pending_handlers|=PARENT_DEBUG_USR1_SIG;
}

void
parent_debug_usr1_action(void)
{
	__parent_debug_action(SIGUSR1);
}

void
parent_debug_usr2_sig(int sig)
{
	signal(sig, parent_debug_usr2_sig);
	pending_handlers|=PARENT_DEBUG_USR2_SIG;
}

void
parent_debug_usr2_action(void)
{
	__parent_debug_action(SIGUSR2);
}

void
trigger_restart(int quickrestart)
{
	procinfo->restart_after_shutdown = 1;
	procinfo->giveup_resources = (quickrestart ? FALSE : TRUE);
	if (kill(master_status_pid, SIGTERM) >= 0) {
		/* Tell master status proc to shut down */
		/* He'll send us a SIGQUIT when done */
		/* Meanwhile, we'll just go on... */
		return;
	}
	ha_perror("MSP signal failed (trigger_restart)");
	emergency_shutdown();
	/*NOTREACHED*/
	return;
}

/*
 *	Restart heartbeat - we never return from this...
 */
void
restart_heartbeat(void)
{
	int			j;
	pid_t			curpid = getpid();
	struct rlimit		oflimits;
	int			killsig = SIGTERM;
	int			quickrestart;

	shutdown_in_progress = 1;
	send_local_status(NULL);
	/*
	 * We need to do these things:
	 *
	 *	Wait until a propitious time
	 *
	 *	Kill our child processes
	 *
	 *	close most files...
	 *
	 *	re-exec ourselves with the -R option
	 */
	make_normaltime();
	ha_log(LOG_INFO, "Restarting heartbeat.");
	quickrestart = (procinfo->giveup_resources ? FALSE : TRUE);


	/* They'll try and make sure everyone gets it - even us ;-) */
	IGNORESIG(SIGTERM);

	/* Kill our child processes */
	for (j=0; j < procinfo->nprocs; ++j) {
		pid_t	pid = procinfo->info[j].pid;
		if (pid != curpid) {
			ha_log(LOG_INFO, "Killing process %d with signal %d"
			,	pid, killsig);
			kill(pid, killsig);
		}
	}
	ha_log(LOG_INFO, "Done killing processes for restart.");

	if (quickrestart) {
		/* Kill any lingering takeover processes, etc. */
		kill(-getpid(), SIGTERM);
		sleep(1);
	}


	ha_log(LOG_INFO, "Performing heartbeat restart exec.");
	ha_log(LOG_INFO, "Closing files first...");

	getrlimit(RLIMIT_NOFILE, &oflimits);
	for (j=3; j < oflimits.rlim_cur; ++j) {
		close(j);
	}

	if (quickrestart) {
		if (nice_failback) {
			execl(HALIB "/heartbeat", "heartbeat", "-R"
			,	"-C", rsc_msg[procinfo->i_hold_resources], NULL);
		}else{
			execl(HALIB "/heartbeat", "heartbeat", "-R", NULL);
		}
	}else{
		/* Make sure they notice we're dead */
		sleep(config->deadtime_interval+1);
		/* "Normal" restart (not quick) */
		unlink(PIDFILE);
		execl(HALIB "/heartbeat", "heartbeat", NULL);
	}
	ha_log(LOG_ERR, "Could not exec " HALIB "/heartbeat");
	ha_log(LOG_ERR, "Shutting down...");
	kill(curpid, SIGTERM);
}

void
reread_config_sig(int sig)
{
	signal(sig, reread_config_sig);
	pending_handlers|=REREAD_CONFIG_SIG;
}

void
reread_config_action(void)
{
	int	j;
	int	signal_children = 0;

	/* If we're the control process, tell our children */
	if (curproc->type == PROC_CONTROL) {
		struct	stat	buf;
		if (stat(CONFIG_NAME, &buf) < 0) {
			ha_perror("Cannot stat " CONFIG_NAME);
			return;
		}
		if (buf.st_mtime != config->cfg_time) {
			trigger_restart(TRUE);
			/* We'll wait for the SIGQUIT */
		}
		if (stat(KEYFILE, &buf) < 0) {
			ha_perror("Cannot stat " KEYFILE);
		}else if (buf.st_mtime != config->auth_time) {
			config->rereadauth = 1;
			ha_log(LOG_INFO, "Rereading authentication file.");
			signal_children = 1;
		}else{
			ha_log(LOG_INFO, "Configuration unchanged.");
		}
	}else{
		/*
		 * We are not the control process, and we received a SIGHUP
		 * signal.  This means the authentication file has changed.
		 */
		if (parse_authfile() != HA_OK) {
			/* OOPS.  Sayonara. */
			ha_log(LOG_ERR
			,	"Authentication reparsing error, exiting.");
			force_shutdown();
			cleanexit(1);
		}

	}

	if (ParseTestOpts() && curproc->type == PROC_CONTROL) {
		signal_children = 1;
	}
	if (signal_children) {
		for (j=0; j < procinfo->nprocs; ++j) {
			if (procinfo->info+j != curproc) {
				kill(procinfo->info[j].pid, SIGHUP);
			}
		}
	}
}

#define	ONEDAY	(24*60*60)

#define	HB_uS_HZ 	10L		/* 10 HZ = 100ms/tick */
#define	HB_uS_PERIOD	(1000000L/HB_uS_HZ)
			/* 100000 microseconds = 100 milliseconds = 10 HZ */

/* Ding!  Activated once per second in the status process */
void
ding_sig(int sig)
{
	signal(sig, ding_sig);
	pending_handlers|=DING_SIG;
}

void
ding_action(void)
{
	TIME_T			now = time(NULL);
	struct tms		proforma_tms;
	clock_t			clknow = times(&proforma_tms);
	static clock_t		clknext = 0L;
	struct itimerval	nexttime =
	{	{(HB_uS_PERIOD/1000000), (HB_uS_PERIOD % 1000000)}	/* Repeat Interval */
	,	{(HB_uS_PERIOD/1000000), (HB_uS_PERIOD % 1000000)}};	/* Timer Value */

	if (DEBUGDETAILS) {
		ha_log(LOG_DEBUG, "Ding!");
	}

	if (clknow >= clknext) {
		clknext = clknow + config->heartbeat_interval * CLK_TCK;
		if (clknext - clknow > 1) {
			clknext --; /* Be conservative */
		}
		/* Note that it's time to send out our status update */
		send_status_now = 1;
	}
	if (now > next_statsdump) {
		if (next_statsdump != 0L) {
			dump_stats_now = 1;
		}
		next_statsdump = now + ONEDAY;
	}
	setitimer(ITIMER_REAL, &nexttime, NULL);
}

void
ignore_signal(int sig)
{
	signal(sig, ignore_signal);
}

void
false_alarm_sig(int sig)
{
	signal(sig, false_alarm_sig);
	pending_handlers|=FALSE_ALARM_SIG;
}

void
false_alarm_action(void)
{
	ha_log(LOG_ERR, "Unexpected alarm in process %d", getpid());
}


void
process_pending_handlers(void)
{
	

	while (pending_handlers) {
		unsigned long	handlers;

		/* Block signals... */
		if (sigprocmask(SIG_BLOCK, &CommonSignalSet, NULL) < 0) {
			ha_log(LOG_ERR, "Could not block signals");
		}
			handlers = pending_handlers;
			pending_handlers=0;

		/* Allow signals ... */
		if (sigprocmask(SIG_UNBLOCK, &CommonSignalSet, NULL) < 0) {
			ha_log(LOG_ERR, "Could not unblock signals");
		}

		if (handlers&TERM_SIG) {
			term_action();
		}

		if (handlers&PARENT_DEBUG_USR1_SIG) {
			parent_debug_usr1_action();
		}

		if (handlers&PARENT_DEBUG_USR2_SIG) {
			parent_debug_usr2_action();
		}

		if (handlers&REREAD_CONFIG_SIG) {
			reread_config_action();
		}

		if (handlers&DING_SIG) {
			ding_action();

		}
		if (handlers&FALSE_ALARM_SIG) {
			false_alarm_action();
		}

		if (handlers&REAPER_SIG) {
			reaper_action();
		}
	}
}

/* See if any nodes or links have timed out */
void
check_for_timeouts(void)
{
	struct tms		proforma_tms;
	clock_t			now = times(&proforma_tms);
	struct node_info *	hip;
	clock_t			dead_ticks
	=	(CLK_TCK * config->deadtime_interval);
	clock_t			TooOld;
	int			j;

	if (heartbeat_comm_state != COMM_LINKSUP) {
		/*
		 * Compute alternative dead_ticks value for very first
		 * dead interval.
		 *
		 * We do this because for some unknown reason sometimes
		 * the network is slow to start working.  Experience indicates that
		 * 30 seconds is generally enough.  It would be nice to have a
		 * better way to detect that the network isn't really working,
		 * but I don't know any easy way.  Patches are being accepted ;-)
		 */
		dead_ticks = (CLK_TCK * config->initial_deadtime);
	}
	TooOld = now - dead_ticks;

	/* We need to be careful to handle clock_t wrapround carefully */
	if (now < dead_ticks) {
		return; /* Ignore timeouts during wraparound */
			/* This doubles our timeout at this time */
			/* Sorry. */
	}

	for (j=0; j < config->nodecount; ++j) {
		hip= &config->nodes[j];
		if (hip->local_lastupdate > now) {
			/* This means wraparound has occurred */
			/* Fudge it to make comparisons work */
			hip->local_lastupdate = 0L;
		}
		/* If it's recently updated, or already dead, ignore it */
		if (hip->local_lastupdate >= TooOld
		||	strcmp(hip->status, DEADSTATUS) == 0 ) {
			continue;
		}
		mark_node_dead(hip);
	}

	/* Check all links status of all nodes */

	for (j=0; j < config->nodecount; ++j) {
		struct link *lnk;
		int i = 0;
		hip = &config->nodes[j];

		if (hip == curnode) continue;

		while((lnk = &hip->links[i]) && lnk->name) {
			if (lnk->lastupdate > now) {
					lnk->lastupdate = 0L;
			}
			if (lnk->lastupdate >= TooOld
			||  strcmp(lnk->status, DEADSTATUS) == 0 ) {
				i++;
				continue;
			}
			change_link_status(hip, lnk, DEADSTATUS);
			i++;
		}
	}
}

/*
 * The anypktsyet field in the node structure gets set to TRUE whenever we
 * either hear from a node, or we declare it dead, and issue a fake "dead"
 * status packet.
 */

void
check_comm_isup(void)
{
	struct node_info *	hip;
	int	j;
	int	heardfromcount = 0;

	if (heartbeat_comm_state == COMM_LINKSUP) {
		return;
	}
	for (j=0; j < config->nodecount; ++j) {
		hip= &config->nodes[j];

		if (hip->anypacketsyet) {
			++heardfromcount;
		}
	}

	if (heardfromcount >= config->nodecount) {
		heartbeat_comm_state = COMM_LINKSUP;
		comm_now_up();
	}
}

/* Set our local status to the given value, and send it out */
int
set_local_status(const char * newstatus)
{
	if (strcmp(newstatus, curnode->status) != 0
	&&	strlen(newstatus) > 1 && strlen(newstatus) < STATUSLENG) {

		/* We can't do this because of conflicts between the two
		 * paths the updates otherwise arrive through...
		 */

		strncpy(curnode->status, newstatus, sizeof(curnode->status));
		send_local_status(newstatus);
		ha_log(LOG_INFO, "Local status now set to: '%s'", newstatus);
		return(HA_OK);
	}

	ha_log(LOG_INFO, "Unable to set local status to: %s", newstatus);
	return(HA_FAIL);
}

int
send_cluster_msg(struct ha_msg* msg)
{
	char *		smsg;
	const char *	type;
	int		rc = HA_OK;

	if (msg == NULL || (type = ha_msg_value(msg, F_TYPE)) == NULL) {
		ha_perror("Invalid message in send_cluster_msg");
		return(HA_FAIL);
	}

	if ((smsg = msg2string(msg)) == NULL) {
		ha_log(LOG_ERR, "out of memory in send_cluster_msg");
		return(HA_FAIL);
	}

	{
	/*
	 * Opening the FIFO for each message is a dumb idea.  It's slow,
	 * it's hell on real time behavior (it accesses the filesystem for
	 * the FIFO pathname for every message), and it doesn't work
	 * reliably on FreeBSD.  An eminently bad idea.
	 * That's why we don't do it (any more) ;-)
	 */
		static int	ffd = -1;
		int		length;
		int		wrc;

		if (ffd < 0) {
			ffd = open(FIFONAME, O_WRONLY);
			if (ffd < 0) {
				ha_free(smsg);
				return(HA_FAIL);
			}
		}

		length=strlen(smsg);
		/*
		 * The single retry we allow won't often make
		 * the problem go away, but it's possible, and
		 * *really* don't want to shut down
		 */
		if ((wrc = write(ffd, smsg, length)) != length
		&&	(wrc >= 0 || errno != EINTR
		||	(wrc = write(ffd, smsg, length)) != length)) {
			ha_perror("cannot write message to FIFO! [rc=%d]"
			,	wrc);
			rc = HA_FAIL;
			close(ffd);
			ffd = -1;
                        force_shutdown();
		}
		if (DEBUGPKTCONT) {
			ha_log(LOG_DEBUG, "%d bytes written to %s by %d"
			,	length, FIFONAME, getpid());
			ha_log(LOG_DEBUG, "Packet content: %s", smsg);
		}
#ifdef OPEN_FIFO_FOR_EACH_MESSAGE
		if (close(ffd) < 0) {
			ha_perror ("unable to close ffd %d", ffd);
		}
		ffd = -1;
#endif

	}
	ha_free(smsg);

	return(rc);
}


/* Translates the resources_held string into an integer */
int
encode_resources(const char *p)
{
	int i;

	for (i=0; i < DIMOF(rsc_msg); i++) {
		if (strcmp(rsc_msg[i], p) == 0) {
			return i;
			break;
		}
	}
	ha_log(LOG_ERR, "encode_resources: bad resource type [%s]", p);
	return 0;
}


/* Send the "I hold resources" or "I don't hold" resource messages */
int
send_resources_held(const char *str, int stable, const char * comment)
{
	struct ha_msg * m;
	int		rc = HA_OK;
	char		timestamp[16];

	if (!nice_failback) {
		return HA_OK;
	}
	sprintf(timestamp, TIME_X, (TIME_T) time(NULL));

	if (ANYDEBUG) {
		ha_log(LOG_DEBUG
		,	"Sending hold resources msg: %s, stable=%d # %s"
		,	str, stable, (comment ? comment : "<none>"));
	}
	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send local starting msg");
		return(HA_FAIL);
	}
	if ((ha_msg_add(m, F_TYPE, T_RESOURCES) != HA_OK)
	||  (ha_msg_add(m, F_RESOURCES, str) != HA_OK)
	||  (ha_msg_add(m, F_ISSTABLE, (stable ? "1" : "0")) != HA_OK)) {
		ha_log(LOG_ERR, "send_resources_held: Cannot create local msg");
		rc = HA_FAIL;
	}else if (comment) {
		rc = ha_msg_add(m, F_COMMENT, comment);
	}
	if (rc == HA_OK) {
		rc = send_cluster_msg(m);
	}

	ha_msg_del(m);
	return(rc);
}


/* Send "standby" related msgs out to the cluster */
int
send_standby_msg(enum standby state)
{
	const char * standby_msg[] = { "not", "me", "other", "done"};
	struct ha_msg * m;
	int		rc;
	char		timestamp[16];

	sprintf(timestamp, TIME_X, (TIME_T) time(NULL));

	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "Sending standby [%s] msg"
		,			standby_msg[state]);
	}
	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send standby [%s] msg"
		,			standby_msg[state]);
		return(HA_FAIL);
	}
	if ((ha_msg_add(m, F_TYPE, T_ASKRESOURCES) != HA_OK)
	||  (ha_msg_add(m, F_COMMENT, standby_msg[state]) != HA_OK)) {
		ha_log(LOG_ERR, "send_standby_msg: "
		"Cannot create standby reply msg");
		rc = HA_FAIL;
	}else{
		rc = send_cluster_msg(m);
	}

	ha_msg_del(m);
	return(rc);
}


/* Send the starting msg out to the cluster */
int
send_local_starting(void)
{
	struct ha_msg * m;
	int		rc;

	if (ANYDEBUG) {
		ha_log(LOG_DEBUG
		,	"Sending local starting msg: resourcestate = %d"
		,	resourcestate);
	}
	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send local starting msg");
		return(HA_FAIL);
	}
	if ((ha_msg_add(m, F_TYPE, T_STARTING) != HA_OK)) {
		ha_log(LOG_ERR, "send_local_starting: "
		"Cannot create local starting msg");
		rc = HA_FAIL;
	}else{
		rc = send_cluster_msg(m);
	}

	ha_msg_del(m);
	resourcestate = R_STARTING;
	return(rc);
}


/* Send our local status out to the cluster */
int
send_local_status(const char * st)
{
	struct ha_msg *	m;
	int		rc;
	static int	statusgen=0;
	int		gen=statusgen;
	char		cgen[20];


	if (st != NULL) {
		++gen;
	}else{
		st = curnode->status;
	}
	snprintf(cgen, sizeof(cgen), "%d", gen);

	if (DEBUGDETAILS){
		ha_log(LOG_DEBUG, "PID %d: Sending local status"
		" curnode = %lx status: %s"
		,	getpid(), (unsigned long)curnode, st);
	}
	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send local status.");
		return(HA_FAIL);
	}
	if (ha_msg_add(m, F_TYPE, T_STATUS) != HA_OK
	||	ha_msg_add(m, F_STATUS, st) != HA_OK
	||	ha_msg_add(m, F_STGEN, cgen) != HA_OK) {
		ha_log(LOG_ERR, "send_local_status: "
		"Cannot create local status msg");
		rc = HA_FAIL;
	}else{
		rc = send_cluster_msg(m);
	}

	ha_msg_del(m);
	return(rc);
}

/* Mark the given link dead */
void
change_link_status(struct node_info *hip, struct link *lnk, const char * newstat)
{
	struct ha_msg *	lmsg;

	if ((lmsg = ha_msg_new(6)) == NULL) {
		ha_log(LOG_ERR, "no memory to mark link dead");
		return;
	}

	strncpy(lnk->status, newstat, sizeof(lnk->status));
	ha_log(LOG_INFO, "Link %s:%s %s.", hip->nodename
	,	lnk->name, lnk->status);

	if (	ha_msg_add(lmsg, F_TYPE, T_IFSTATUS) != HA_OK
	||	ha_msg_add(lmsg, F_NODE, hip->nodename) != HA_OK
	||	ha_msg_add(lmsg, F_IFNAME, lnk->name) != HA_OK
	||	ha_msg_add(lmsg, F_STATUS, lnk->status) != HA_OK) {
		ha_log(LOG_ERR, "no memory to mark link dead");
		ha_msg_del(lmsg);
		return;
	}
	heartbeat_monitor(lmsg, KEEPIT, "<internal>");
	notify_world(lmsg, NULL);
	ha_msg_del(lmsg);
}

/* Mark the given node dead */
void
mark_node_dead(struct node_info *hip)
{
	ha_log(LOG_WARNING, "node %s: is dead", hip->nodename);

	hip->anypacketsyet = 1;
	if (hip == curnode) {
		/* We may die too soon for this to actually be received */
		/* But, we tried ;-) */
		send_resources_held(NO_RESOURCES, 1, NULL);
		/* Uh, oh... we're dead! */
		ha_log(LOG_ERR, "No local heartbeat. Forcing shutdown.");
		kill(procinfo->info[0].pid, SIGTERM);
		return;
	}
	standby_running = 0L;

	strncpy(hip->status, DEADSTATUS, sizeof(hip->status));
	standby_running = 0L;
	if (!hip->has_resources
	||	(nice_failback && other_holds_resources == NO_RSC)) {
		ha_log(LOG_INFO, "Dead node %s held no resources."
		,	hip->nodename);
	}else{
		/* We have to Zap them before we take the resources */
		/* This often takes a few seconds. */
		if (config->stonith) {
			/* FIXME: We shouldn't do these if it's a 'ping' node */
			Initiate_Reset(config->stonith, hip->nodename);
			/* It will call takeover_from_node() later */
			return;
		}
	}
	/* FIXME: We shouldn't do these if it's a 'ping' node */
	/* nice_failback needs us to do this anyway... */
	takeover_from_node(hip->nodename);
}

/* We take all resources over from a given node */
void
takeover_from_node(const char * nodename)
{
	struct node_info *	hip = lookup_node(nodename);
	struct ha_msg *	hmsg;
	char		timestamp[16];

	if (hip == 0) {
		return;
	}
	if ((hmsg = ha_msg_new(6)) == NULL) {
		ha_log(LOG_ERR, "no memory to mark node dead");
		return;
	}

	sprintf(timestamp, TIME_X, (TIME_T) time(NULL));

	if (	ha_msg_add(hmsg, F_TYPE, T_STATUS) != HA_OK
	||	ha_msg_add(hmsg, F_SEQ, "1") != HA_OK
	||	ha_msg_add(hmsg, F_TIME, timestamp) != HA_OK
	||	ha_msg_add(hmsg, F_ORIG, hip->nodename) != HA_OK
	||	ha_msg_add(hmsg, F_STATUS, "dead") != HA_OK) {
		ha_log(LOG_ERR, "no memory to mark node dead");
		ha_msg_del(hmsg);
		return;
	}

	/* Sending this message triggers the "mach_down" script */

	heartbeat_monitor(hmsg, KEEPIT, "<internal>");
	notify_world(hmsg, hip->status);

	/*
	 * STONITH has already successfully completed...
	 */
	if (nice_failback) {

		/* mach_down is out there acquiring foreign resources */
		/* So, make a note of it... */
		procinfo->i_hold_resources |= FOREIGN_RSC;

		other_holds_resources = NO_RSC;
		other_is_stable = 1;	/* Not going anywhere */
		takeover_in_progress = 1;
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG
			,	"mark_node_dead: other now stable");
		}
		/*
		 * We MUST do this now, or the other side might come
		 * back up and think they can own their own resources
		 * when they do due to receiving an interim
		 * T_RESOURCE message from us.
		 */
		/* case 1 - part 1 */
		/* part 2 is done by the mach_down script... */
		req_our_resources(0);
		/* req_our_resources turns on the LOCAL_RSC bit */

	}
	hip->anypacketsyet = 1;
	ha_msg_del(hmsg);
}

void
Initiate_Reset(Stonith* s, const char * nodename)
{
	const char*	result = "bad";
	struct ha_msg*	hmsg;
	int		pid;
	int		exitcode = 0;
	struct StonithProcHelper *	h;
	/*
	 * We need to fork because the stonith operations block for a long
	 * time (10 seconds in common cases)
	 */
	switch((pid=fork())) {

		case -1:	ha_log(LOG_ERR, "Cannot fork.");
				return;
		default:
				h = g_new(struct StonithProcHelper, 1);
				h->nodename = g_strdup(nodename);
				NewTrackedProc(pid, 1, PT_LOGVERBOSE, h
				,	&StonithProcessTrackOps);
				/* StonithProcessDied is called when done */
				return;

		case 0:		/* Child */
				break;

	}
	/* Guard against possibly hanging Stonith code... */
	make_normaltime();
	setpgrp();
	set_proc_title("%s: Initiate_Reset()", cmdname);
	signal(SIGCHLD, SIG_DFL);

	ha_log(LOG_INFO
	,	"Resetting node %s with [%s]"
	,	nodename
	,	s->s_ops->getinfo(s, ST_DEVICEID));

	switch (s->s_ops->reset_req(s, ST_GENERIC_RESET, nodename)){

	case S_OK:
		result="OK";
		ha_log(LOG_INFO
		,	"node %s now reset.", nodename);
		exitcode = 0;
		break;

	case S_BADHOST:
		ha_log(LOG_ERR
		,	"Device %s cannot reset host %s."
		,	s->s_ops->getinfo(s, ST_DEVICEID)
		,	nodename);
		exitcode = 100;
		result = "badhost";
		break;

	default:
		ha_log(LOG_ERR, "Host %s not reset!", nodename);
		exitcode = 1;
		result = "bad";
	}

	if ((hmsg = ha_msg_new(6)) == NULL) {
		ha_log(LOG_ERR, "no memory for " T_REXMIT);
	}

	if (	hmsg != NULL
	&& 	ha_msg_add(hmsg, F_TYPE, T_STONITH)    == HA_OK
	&&	ha_msg_add(hmsg, F_NODE, nodename) == HA_OK
	&&	ha_msg_add(hmsg, F_APIRESULT, result) == HA_OK) {
		/* Send a Stonith message */
		if (send_cluster_msg(hmsg) != HA_OK) {
			ha_log(LOG_ERR, "cannot send " T_STONITH
			" request for %s", nodename);
		}
	}else{
		ha_log(LOG_ERR
		,	"Cannot send reset reply message [%s] for %s", result
		,	nodename);
	}
	exit (exitcode);
}

void
healed_cluster_partition(struct node_info *t)
{
	ha_log(LOG_WARNING
	,	"Cluster node %s returning after partition"
	,	t->nodename);
	/* Give up our resources, and restart ourselves */
	/* This is cleaner than lots of other options. */
	/* And, it really should work every time... :-) */
	procinfo->restart_after_shutdown = 1;
	procinfo->giveup_resources = 1;
	giveup_resources();
}

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


#define	RETRYINTERVAL	(3600*24)	/* Once A Day... */

/*
 * Values of msgtype:
 *	KEEPIT
 *	DROPIT
 *	DUPLICATE
 */
void
heartbeat_monitor(struct ha_msg * msg, int msgtype, const char * iface)
{
	api_heartbeat_monitor(msg, msgtype, iface);
}

void
req_our_resources(int getthemanyway)
{
	FILE *	rkeys;
	char	cmd[MAXLINE];
	char	getcmd[MAXLINE];
	char	buf[MAXLINE];
	int	finalrc = HA_OK;
	int	rc;
	int	rsc_count = 0;
	int	pid;
	int	upcount;

	if (nice_failback) {

		if (((other_holds_resources & FOREIGN_RSC) != 0
		||	(procinfo->i_hold_resources & LOCAL_RSC) != 0)
		&&	!getthemanyway) {

			if (going_standby == NOT) {
				/* Someone already owns our resources */
				ha_log(LOG_INFO
				,   "Local Resource acquisition completed"
				". (none)");
				return;
			}
		}

		/*
		 * We MUST do this now, or the other side might think they
		 * can have our resources, due to an interim T_RESOURCE
		 * message
		 */
		procinfo->i_hold_resources |= LOCAL_RSC;
	}

	/* We need to fork so we can make child procs not real time */
	switch(pid=fork()) {

		case -1:	ha_log(LOG_ERR, "Cannot fork.");
				return;
		default:
				RSCMGMTPROC(pid, "req_our_resources");
				return;

		case 0:		/* Child */
				break;
	}

	make_normaltime();
	set_proc_title("%s: req_our_resources()", cmdname);
	setpgrp();
	signal(SIGCHLD, SIG_DFL);
	alarm(0);
	IGNORESIG(SIGALRM);
	siginterrupt(SIGALRM, 0);
	if (nice_failback) {
		setenv(HANICEFAILBACK, "yes", 1);
	}
	upcount = countbystatus(ACTIVESTATUS, TRUE);

	/* Our status update is often not done yet */
	if (strcmp(curnode->status, ACTIVESTATUS) != 0) {
		upcount++;
	}
 
	/* Are we all alone in the world? */
	if (upcount < 2) {
		setenv(HADONTASK, "yes", 1);
	}
	sprintf(cmd, HALIB "/ResourceManager listkeys %s", curnode->nodename);

	if ((rkeys = popen(cmd, "r")) == NULL) {
		ha_log(LOG_ERR, "Cannot run command %s", cmd);
		exit(1);
	}


	for (;;) {
		errno = 0;
		if (fgets(buf, MAXLINE, rkeys) == NULL) {
			if (ferror(rkeys)) {
				ha_perror("req_our_resources: fgets failure");
			}
			break;
		}
		++rsc_count;

		if (buf[strlen(buf)-1] == '\n') {
			buf[strlen(buf)-1] = EOS;
		}
		sprintf(getcmd, HALIB "/req_resource %s", buf);
		if ((rc=system(getcmd)) != 0) {
			ha_perror("%s returned %d", getcmd, rc);
			finalrc=HA_FAIL;
		}
	}
	rc=pclose(rkeys);
	if (rc < 0 && errno != ECHILD) {
		ha_perror("pclose(%s) returned %d", cmd, rc);
	}else if (rc > 0) {
		ha_log(LOG_ERR, "[%s] exited with 0x%x", cmd, rc);
	}

	if (rsc_count == 0) {
		ha_log(LOG_INFO, "No local resources [%s]", cmd);
	}else{
		if (ANYDEBUG) {
			ha_log(LOG_INFO, "%d local resources from [%s]"
			,	rsc_count, cmd);
		}
	}
	send_resources_held(rsc_msg[procinfo->i_hold_resources], 1
	,	"req_our_resources()");
	ha_log(LOG_INFO, "Resource acquisition completed.");
	exit(0);
}

void
go_standby(enum standby who)
{
	FILE *		rkeys;
	char		cmd[MAXLINE];
	char		buf[MAXLINE];
	int		finalrc = HA_OK;
	int		rc = 0;
	pid_t		pid;

	/*
	 * We consider them unstable because they're about to pick up
	 * our resources.
	 */
	if (who == ME) {
		other_is_stable = 0;
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "go_standby: other is unstable");
		}
	}
	/* We need to fork so we can make child procs not real time */

	switch((pid=fork())) {

		case -1:	ha_log(LOG_ERR, "Cannot fork.");
				return;

				/*
				 * We shouldn't block here, because then we
				 * aren't sending heartbeats out...
				 */
		default:	
				if (who == ME) {
					RSCMGMTPROC(pid, "go_standby");
				}else{
					RSCMGMTPROC(pid, "go_standby");
				}
				/* waitpid(pid, NULL, 0); */
				return;

		case 0:		/* Child */
				break;
	}

	make_normaltime();
	setpgrp();
	signal(SIGCHLD, SIG_DFL);

	if (who == ME) {
		procinfo->i_hold_resources = NO_RSC;
		/* Make sure they know what we're doing and that we're
		 * not done yet (not stable)
		 * Since heartbeat doesn't guarantee message ordering
		 * this could theoretically have problems, but all that
		 * happens if it gets out of order is that we get
		 * a funky warning message (or maybe two).
		 */
		send_resources_held(rsc_msg[procinfo->i_hold_resources], 0, "standby");
	}
	/*
	 *	We could do this ourselves fairly easily...
	 */

	sprintf(cmd, HALIB "/ResourceManager listkeys '.*'");

	if ((rkeys = popen(cmd, "r")) == NULL) {
		ha_log(LOG_ERR, "Cannot run command %s", cmd);
		return;
	}
	ha_log(LOG_INFO
	,	"%s all HA resources (standby)."
	,	who == ME ? "Giving up" : "Acquiring");

	while (fgets(buf, MAXLINE, rkeys) != NULL) {
		if (buf[strlen(buf)-1] == '\n') {
			buf[strlen(buf)-1] = EOS;
		}
		if (who == ME) {
			sprintf(cmd, HALIB "/ResourceManager givegroup %s",buf);
		}else{
			if (who == OTHER) {
				sprintf(cmd, HALIB
					"/ResourceManager takegroup %s", buf);
			}
		}
		if ((rc=system(cmd)) != 0) {
			ha_log(LOG_ERR, "%s returned %d", cmd, rc);
			finalrc=HA_FAIL;
		}
	}
	pclose(rkeys);
	if (ANYDEBUG) {
		ha_log(LOG_INFO, "go_standby: who: %d", who);
	}
	if (who == ME) {
		ha_log(LOG_INFO, "All HA resources relinquished (standby).");
	}else if (who == OTHER) {
		procinfo->i_hold_resources |= FOREIGN_RSC;
		ha_log(LOG_INFO, "All resources acquired (standby).");
	}
	send_standby_msg(DONE);
	exit(rc);

}

void
giveup_resources(void)
{
	FILE *		rkeys;
	char		cmd[MAXLINE];
	char		buf[MAXLINE];
	int		finalrc = HA_OK;
	int		rc;
	pid_t		pid;
	struct ha_msg *	m;


	if (shutdown_in_progress) {
		ha_log(LOG_INFO, "Heartbeat shutdown already underway.");
		return;
	}
	if (ANYDEBUG) {
		ha_log(LOG_INFO, "giveup_resources: current status: %s"
		,	curnode->status);
	}
	DisableProcLogging();	/* We're shutting down */
	/* Kill all our managed children... */
	ForEachProc(&ManagedChildTrackOps, KillTrackedProcess
	,	GINT_TO_POINTER(SIGTERM));
	ForEachProc(&RscMgmtProcessTrackOps, KillTrackedProcess
	,	GINT_TO_POINTER(SIGKILL));
	procinfo->i_hold_resources = NO_RSC ;
	resourcestate = R_SHUTDOWN; /* or we'll get a whiny little comment
				out of the resource management code */
	if (nice_failback) {
		send_resources_held(rsc_msg[procinfo->i_hold_resources]
		,	0, "shutdown");
	}
	shutdown_in_progress =1;
	ha_log(LOG_INFO, "Heartbeat shutdown in progress. (%d)", getpid());

	/* We need to fork so we can make child procs not real time */

	switch((pid=fork())) {

		case -1:	ha_log(LOG_ERR, "Cannot fork.");
				return;

		default:
				RSCMGMTPROC(pid, "giveup_resources");
				return;

		case 0:		/* Child */
				break;
	}

	make_normaltime();
	setpgrp();
	set_proc_title("%s: giveup_resources()", cmdname);

	/* We don't want to be interrupted while shutting down */

	signal(SIGCHLD, SIG_DFL);
	siginterrupt(SIGCHLD, 0);

	alarm(0);
	IGNORESIG(SIGALRM);
	siginterrupt(SIGALRM, 0);

	IGNORESIG(SIGTERM);
	siginterrupt(SIGTERM, 0);

	ha_log(LOG_INFO, "Giving up all HA resources.");
	/*
	 *	We could do this ourselves fairly easily...
	 */

	sprintf(cmd, HALIB "/ResourceManager listkeys '.*'");

	if ((rkeys = popen(cmd, "r")) == NULL) {
		ha_log(LOG_ERR, "Cannot run command %s", cmd);
		exit(1);
	}

	while (fgets(buf, MAXLINE, rkeys) != NULL) {
		if (buf[strlen(buf)-1] == '\n') {
			buf[strlen(buf)-1] = EOS;
		}
		sprintf(cmd, HALIB "/ResourceManager givegroup %s", buf);
		if ((rc=system(cmd)) != 0) {
			ha_log(LOG_ERR, "%s returned %d", cmd, rc);
			finalrc=HA_FAIL;
		}
	}
	pclose(rkeys);
	ha_log(LOG_INFO, "All HA resources relinquished.");

	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send final shutdown msg");
		exit(1);
	}
	if ((ha_msg_add(m, F_TYPE, T_SHUTDONE) != HA_OK
	||	ha_msg_add(m, F_STATUS, DEADSTATUS) != HA_OK)) {
		ha_log(LOG_ERR, "giveup_resources: Cannot create local msg");
	}else{
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "Sending T_SHUTDONE.");
		}
		rc = send_cluster_msg(m);
	}

	ha_msg_del(m);
	exit(0);
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
			switch(thislet[0]) {
			case 'C':	desc = "Current-resource-state";
					break;
			}

			fprintf(stderr, " [-%c %s]", *thislet, desc);
		}
	}
	fprintf(stderr, "\n");
	fprintf(stderr, "\t-C only valid with -R\n");
	fprintf(stderr, "\t-r is mutually exclusive with -R\n");
	cleanexit(1);
}


extern int	optind;
int
main(int argc, char * argv[], char * envp[])
{
	int		flag;
	int		argerrs = 0;
	char *		CurrentStatus=NULL;
	char *		tmp_cmdname;
	long		running_hb_pid = get_running_hb_pid();

	num_hb_media_types = 0;

	/* Redirect messages from glib functions to our handler */
	g_log_set_handler(NULL
	,	G_LOG_LEVEL_ERROR	| G_LOG_LEVEL_CRITICAL
	|	G_LOG_LEVEL_WARNING	| G_LOG_LEVEL_MESSAGE
	|	G_LOG_LEVEL_INFO	| G_LOG_LEVEL_DEBUG
	|	G_LOG_FLAG_RECURSION	| G_LOG_FLAG_FATAL

	,	ha_glib_msg_handler, NULL);

	tmp_cmdname=strdup(argv[0]);
	if ((cmdname = strrchr(tmp_cmdname, '/')) != NULL) {
		++cmdname;
	}else{
		cmdname = tmp_cmdname;
	}

	Argc = argc;

	while ((flag = getopt(argc, argv, OPTARGS)) != EOF) {

		switch(flag) {

			case 'C':
				CurrentStatus = optarg;
				procinfo->i_hold_resources
				=	encode_resources(CurrentStatus);
				break;
			case 'd':
				++debug;
				break;
			case 'k':
				++killrunninghb;
				break;
			case 'M':
				DoManageResources=0;
				break;
			case 'r':
				++RestartRequested;
				break;
			case 'R':
				++WeAreRestarting;
				break;
			case 's':
				++rpt_hb_status;
				break;
			case 'l':
				++RunAtLowPrio;
				break;

			case 'v':
				++verbose;
				break;

			default:
				++argerrs;
				break;
		}
	}

	if (optind > argc) {
		++argerrs;
	}
	if (argerrs || (CurrentStatus && !WeAreRestarting)) {
		usage();
	}

	init_set_proc_title(argc, argv, envp);
	set_proc_title("%s", cmdname);

	hbmedia_types = ha_malloc(sizeof(struct hbmedia_types **));

	if (hbmedia_types == NULL) {
		ha_log(LOG_ERR, "Allocation of hbmedia_types failed.");
		cleanexit(1);
	}



	setenv(HADIRENV, HA_D, 1);
	setenv(DATEFMT, HA_DATEFMT, 1);
	setenv(HAFUNCENV, HA_FUNCS, 1);

	init_procinfo();

	if (module_init() != HA_OK) {
		ha_log(LOG_ERR, "Heartbeat not started: module init error.");
		return(HA_FAIL);
	}

	/*
	 *	We've been asked to shut down the currently running heartbeat
	 *	process
	 */

	if (killrunninghb) {

		if (running_hb_pid < 0) {
			fprintf(stderr
			,	"INFO: Heartbeat already stopped.\n");
			cleanexit(0);
		}

		if (kill((pid_t)running_hb_pid, SIGTERM) >= 0) {
			/* Wait for the running heartbeat to die */
			alarm(0);
			do {
				sleep(1);
				continue;
			}while (kill((pid_t)running_hb_pid, 0) >= 0);
			cleanexit(0);
		}
		fprintf(stderr, "ERROR: Could not kill pid %ld",
			running_hb_pid);
		perror(" ");
		cleanexit(1);
	}

	/*
	 *	Report status of heartbeat processes, etc.
	 *	We report in both Red Hat and SuSE formats...
	 */
	if (rpt_hb_status) {

		if (running_hb_pid < 0) {
			printf("%s is stopped. No process\n", cmdname);
		}else{
			struct utsname u;
			uname(&u);
			printf("%s OK [pid %ld et al] is running on %s...\n"
			,	cmdname, running_hb_pid, u.nodename);
		}
		cleanexit(0);
	}

	/*
	 *	We think we just performed an "exec" of ourselves to restart.
	 */

	if (WeAreRestarting) {

		if (running_hb_pid < 0) {
			fprintf(stderr, "ERROR: %s is not running.\n", cmdname);
			cleanexit(1);
		}
		if (running_hb_pid != getpid()) {
			fprintf(stderr
			,	"ERROR: Heartbeat already running [pid %ld].\n"
			,	running_hb_pid);
			cleanexit(1);
		}

		/*
		 * Nice_failback complicates things a bit here...
		 * We need to allow for the possibility that the user might
		 * have changed nice_failback options in the config file
		 */
		if (CurrentStatus) {
			ha_log(LOG_INFO, "restart: i_hold_resources = %s"
			,	rsc_msg[procinfo->i_hold_resources]);
		}

		if (nice_failback) {
			/* nice_failback is currently ON */

			if (CurrentStatus == NULL) {
				/* From !nice_failback to nice_failback */
				procinfo->i_hold_resources = LOCAL_RSC;
				send_resources_held(rsc_msg[LOCAL_RSC],1, NULL);
				ha_log(LOG_INFO, "restart: assuming LOCAL_RSC");
			}else{
				/* From nice_failback to nice_failback */
				/* Cool. Nothing special to do. */;
			}
		}else{
			/* nice_failback is currently OFF */

			if (CurrentStatus == NULL) {
				/* From !nice_failback to !nice_failback */
				/* Cool. Nothing special to do. */ ;
			}else{
				/* From nice_failback to not nice_failback */
				if ((procinfo->i_hold_resources & LOCAL_RSC)) {
					/* We expect to have those */
					ha_log(LOG_INFO, "restart: acquiring"
					" local resources.");
					req_our_resources(0);
				}else{
					ha_log(LOG_INFO, "restart: "
					" local resources already acquired.");
				}
			}
		}
	}

	/*
	 *	We've been asked to restart currently running heartbeat process
	 *	(or at least get it to reread it's configuration files)
	 */

	if (RestartRequested) {
		if (running_hb_pid < 0) {
			fprintf(stderr
			,	"ERROR: Heartbeat not currently running.\n");
			cleanexit(1);
		}

		if (init_config(CONFIG_NAME)&&parse_ha_resources(RESOURCE_CFG)){
			ha_log(LOG_INFO
			,	"Signalling heartbeat pid %ld to reread"
			" config files", running_hb_pid);

			if (kill(running_hb_pid, SIGHUP) >= 0) {
				cleanexit(0);
			}
			ha_perror("Unable to send SIGHUP to pid %ld"
			,	running_hb_pid);
		}else{
			ha_log(LOG_INFO
			,	"Config errors: Heartbeat pid %ld NOT restarted"
			,	running_hb_pid);
		}
		cleanexit(1);
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
		if (config->log_facility >= 0) {
			char	facility[40];
			snprintf(facility, sizeof(facility)
			,	"%s", config->facilityname);
			setenv(LOGFACILITY, facility, 1);
		}
		ParseTestOpts();
		ha_versioninfo();
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
	if (localdie) {
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG, "Calling localdie() function");
		}
		(*localdie)();
	}
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "Exiting from pid %d [rc=%d]"
		,	getpid(), rc);
	}
	if (config && config->log_facility >= 0) {
		closelog();
	}
	exit(rc);
}

void
force_shutdown(void)
{
	ha_log(LOG_ERR, "Beginning forced shutdown.");
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "sending SIGTERM to Control Process: %d"
		,	processes[0]);
	}

	if (curproc->pid == processes[0]) {
		signal_all(SIGTERM);
		return;
	}else if (kill(processes[0], SIGTERM >= 0)) {
		/* Kill worked! */
		return;
	}
	ha_perror("Could not signal Control Process");
	emergency_shutdown();

}

void
emergency_shutdown(void)
{
	make_normaltime();
	IGNORESIG(SIGTERM);
	ha_log(LOG_ERR, "Emergency Shutdown: Attempting to kill everything ourselves.\n");
	kill(-getpgrp(), SIGTERM);
	sleep(2);
	kill(-getpgrp(), SIGKILL);
	/*NOTREACHED*/
	cleanexit(100);
}

void
signal_all(int sig)
{
	int us = getpid();
	int j;

	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, "pid %d: received signal %d", us, sig);
		if (curproc) {
			ha_log(LOG_DEBUG, "pid %d: type is %d", us
			,	curproc->type);
		}
	}

	if (sig == SIGTERM) {
		IGNORESIG(SIGTERM);
		make_normaltime();
	}

	/* We're going to wait for the master status process to shut down */
	if (curproc && curproc->type == PROC_CONTROL) {
		DisableProcLogging();
		shutdown_in_progress++;
		if (sig == SIGTERM) {
			if (ANYDEBUG) {
				ha_log(LOG_DEBUG, "sending SIGTERM to MSP: %d"
				,	master_status_pid);
			}
			if (kill(master_status_pid, SIGTERM) >= 0) {
				/* Tell master status proc to shut down */
				/* He'll send us a SIGQUIT when done */
				/* Meanwhile, we'll just go on... */
				return;
			}
			ha_perror("MSP signal failed");
			emergency_shutdown();
			/*NOTREACHED*/
			return;
		}else if (sig == SIGQUIT) {
			/* All Resources are now released.  Shut down. */
			ha_log(LOG_INFO, "control process Received SIGQUIT");
			sig = SIGTERM;
		}
	}

	for (j=0; j < procinfo->nprocs; ++j) {
		if (processes[j] != us && processes[j] != 0) {
			if (ANYDEBUG) {
				ha_log(LOG_DEBUG
				,	"%d: Signalling process %d [%d]"
				,	us, processes[j], sig);
			}
			kill(processes[j], sig);
		}
	}
	switch (sig) {
		case SIGTERM:
			if (curproc && curproc->type == PROC_CONTROL) {
				return;
			}
			cleanexit(1);
			break;
	}
}


long
get_running_hb_pid()
{
	long	pid;
	FILE *	lockfd;
	if ((lockfd = fopen(PIDFILE, "r")) != NULL
	&&	fscanf(lockfd, "%ld", &pid) == 1 && pid > 0) {
		if (kill((pid_t)pid, 0) >= 0 || errno != ESRCH) {
			fclose(lockfd);
			return(pid);
		}
	}
	if (lockfd != NULL) {
		fclose(lockfd);
	}
	return(-1L);
}

struct signalfoo {
	int	signo;
	void	(*handler)(int);
};

extern pid_t getsid(pid_t);

void
make_daemon(void)
{
	long			pid;
	FILE *			lockfd;
	int			j;
	sigset_t		oursigset;
	struct sigaction	commonaction;

	static const struct signalfoo siglist [] =
	{	{SIGHUP,	reread_config_sig}
	,	{SIGTERM,	term_sig}
	,	{SIGALRM,	false_alarm_sig}
	,	{SIGUSR1,	debug_sig}
	,	{SIGUSR2,	debug_sig}
	,	{SIGCHLD,	reaper_sig}
	};
	static int ignoresigs[] = {
				SIGINT,
				SIGQUIT,
#ifdef	SIGTTOU
				SIGTTOU,
#endif
#ifdef	SIGTTIN
				SIGTTIN,
#endif

#ifdef	SIGSTP
				SIGSTP,
#endif
	};


	/* See if heartbeat is already running... */

	if ((pid=get_running_hb_pid()) > 0 && pid != getpid()) {
		ha_log(LOG_ERR, "%s: already running [pid %ld].\n"
		,	cmdname, pid);
		fprintf(stderr, "%s: already running [pid %ld].\n"
		,	cmdname, pid);
		exit(HA_FAILEXIT);
	}

	/* Guess not. Go ahead and start things up */

	if (!WeAreRestarting) {
		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "%s: could not start daemon\n"
			,	cmdname);
			perror("fork");
			exit(HA_FAILEXIT);
		}else if (pid > 0) {
			exit(HA_OKEXIT);
		}
	}
	pid = (long) getpid();
	lockfd = fopen(PIDFILE, "w");
	if (lockfd != NULL) {
		fprintf(lockfd, "%ld\n", pid);
		fclose(lockfd);
	}else{
		fprintf(stderr, "%s: could not create pidfile [%s]\n"
		,	cmdname, PIDFILE);
		exit(HA_FAILEXIT);
	}
	if (getsid(0) != pid) {
		if (setsid() < 0) {
			fprintf(stderr, "%s: setsid() failure.", cmdname);
			perror("setsid");
		}
	}

	sigemptyset(&CommonSignalSet);
	for (j=0; j < DIMOF(siglist); ++j) {
		sigaddset(&CommonSignalSet, siglist[j].signo);
	}
	if (sigprocmask(SIG_UNBLOCK, &CommonSignalSet, NULL) < 0) {
		fprintf(stderr
		,	"%s: could not unblock signals!\n"
		,	cmdname);
	}
	for (j=0; j < DIMOF(siglist); ++j) {
		commonaction.sa_handler = siglist[j].handler;
		commonaction.sa_mask = CommonSignalSet;
		commonaction.sa_flags = SA_NOCLDSTOP;
		sigaction(siglist[j].signo,  &commonaction, NULL);
		siginterrupt(siglist[j].signo, 1);
	}
	for (j=0; j < DIMOF(ignoresigs);  ++j) {
		IGNORESIG(ignoresigs[j]);
	}
	/*
	 * This signal is generated by our ttys in order to cause output
	 * flushing, but we don't want to see it in our software.
         */
	siginterrupt(SIGINT, 0);
	sigemptyset(&oursigset);
	sigaddset(&oursigset, SIGINT);
	if (sigprocmask(SIG_BLOCK, &oursigset, NULL) < 0) {
		fprintf(stderr
		,	"%s: could not block SIGINT signals\n"
		,	cmdname);
	}

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
			if (fcntl(watchdogfd, F_SETFD, FD_CLOEXEC)) {
				ha_perror("Error setting the "
				"close-on-exec flag for watchdog");
			}
			ha_log(LOG_NOTICE, "Using watchdog device: %s"
			,	watchdogdev);
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
			close(watchdogfd);
			watchdogfd=-1;
			ha_perror("Watchdog write failure: closing %s!\n"
			,	watchdogdev);
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
			ha_log(LOG_ERR, "bad packet in should_copy_ring_pkt");
			return(0);
	}
	/* Is this message from us? */
	if (strcmp(from, us) == 0 || ttl == NULL || atoi(ttl) <= 0) {
		/* Avoid infinite loops... Ignore this message */
		return(0);
	}

	/* Must be OK */
	return(1);
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

/*
 *	Should we ignore this packet, or pay attention to it?
 */
int
should_drop_message(struct node_info * thisnode, const struct ha_msg *msg,
					const char *iface)
{
	struct seqtrack *	t = &thisnode->track;
	const char *		cseq = ha_msg_value(msg, F_SEQ);
	const char *		to = ha_msg_value(msg, F_TO);
	const char *		type = ha_msg_value(msg, F_TYPE);
	const char *		cgen = ha_msg_value(msg, F_HBGENERATION);
	unsigned long		seq;
	unsigned long		gen = 0;
	int			IsToUs;
	int			j;
	int			isrestart = 0;
	int			ishealedpartition = 0;
	int			is_status = 0;

	/* Some packet types shouldn't have sequence numbers */
	if (type != NULL && strncmp(type, NOSEQ_PREFIX, sizeof(NOSEQ_PREFIX)-1)
	==	0) {
		/* Is this a sequence number rexmit NAK? */
		if (strcasecmp(type, T_NAKREXMIT) == 0) {
			const char *	cnseq = ha_msg_value(msg, F_FIRSTSEQ);
			unsigned long	nseq;
			if (cnseq  == NULL || sscanf(cnseq, "%lx", &nseq) != 1
			||	nseq <= 0) {
				ha_log(LOG_ERR
				, "should_drop_message: bad nak seq number");
				return(DROPIT);
			}

			ha_log(LOG_ERR , "%s: node %s seq %ld"
			,	"Irretrievably lost packet"
			,	thisnode->nodename, nseq);
			is_lost_packet(thisnode, nseq);
			return(DROPIT);
		}
		return(KEEPIT);
	}
	if (strcasecmp(type, T_STATUS) == 0) {
		is_status = 1;
	}

	if (cseq  == NULL || sscanf(cseq, "%lx", &seq) != 1 ||	seq <= 0) {
		ha_log(LOG_ERR, "should_drop_message: bad sequence number");
		ha_log_message(msg);
		return(DROPIT);
	}

	/* Extract the heartbeat generation number */
	if (cgen != NULL) {
		sscanf(cgen, "%lx", &gen);
	}

	/*
	 * We need to do sequence number processing on every
	 * packet, even those that aren't sent to us.
	 */
	IsToUs = (to == NULL) || (strcmp(to, curnode->nodename) == 0);

	/* Does this looks like a replay attack... */
	if (gen < t->generation) {
		ha_log(LOG_DEBUG
		,	"should_drop_message: attempted replay attack"
		" [%s]? [curgen = %ld]", thisnode->nodename, t->generation);
		return(DROPIT);

	}else if (is_status) {
		/* Look for apparent restarts/healed partitions */
		if (gen == t->generation && gen > 0) {
			/* Is this a message from a node that was dead? */
			if (strcmp(thisnode->status, DEADSTATUS) == 0) {
				/* Is this stale data? */
				if (seq <= thisnode->status_seqno) {
					return DROPIT;
				}

				/* They're now alive, but were dead. */
				/* No restart occured. UhOh. */

				healed_cluster_partition(thisnode);
				ishealedpartition=1;
			}
		}else if (gen > t->generation) {
			isrestart = 1;
			if (t->generation > 0) {
				ha_log(LOG_INFO, "Heartbeat restart on node %s"
				,	thisnode->nodename);
			}
			thisnode->rmt_lastupdate = 0L;
			thisnode->local_lastupdate = 0L;
			thisnode->status_seqno = 0L;
			thisnode->status_gen = 0L;
			thisnode->has_resources = TRUE;
		}
		t->generation = gen;
	}

	/* Is this packet in sequence? */
	if (t->last_seq == NOSEQUENCE || seq == (t->last_seq+1)) {
		t->last_seq = seq;
		t->last_iface = iface;
		return(IsToUs ? KEEPIT : DROPIT);
	}else if (seq == t->last_seq) {
		/* Same as last-seen packet -- very common case */
		if (DEBUGPKT) {
			ha_log(LOG_DEBUG
			,	"should_drop_message: Duplicate packet(1)");
		}
		return(DUPLICATE);
	}

	/* Not in sequence... Hmmm... */

	/* Is it newer than the last packet we got? */

	if (seq > t->last_seq) {
		unsigned long	k;
		unsigned long	nlost;
		nlost = ((unsigned long)(seq - (t->last_seq+1)));
		ha_log(LOG_WARNING, "%lu lost packet(s) for [%s] [%lu:%lu]"
		,	nlost, thisnode->nodename, t->last_seq, seq);

		if (nlost > SEQGAP) {
			/* Something bad happened.  Start over */
			/* This keeps the loop below from going a long time */
			t->nmissing = 0;
			t->last_seq = seq;
			t->last_iface = iface;
			ha_log(LOG_ERR, "lost a lot of packets!");
			return(IsToUs ? KEEPIT : DROPIT);
		}else{
			request_msg_rexmit(thisnode, t->last_seq+1L, seq-1L);
		}

		/* Try and Record each of the missing sequence numbers */
		for(k = t->last_seq+1; k < seq; ++k) {
			if (t->nmissing < MAXMISSING-1) {
				t->seqmissing[t->nmissing] = k;
				++t->nmissing;
			}else{
				int		minmatch = -1;
				unsigned long	minseq = INT_MAX;
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
		t->last_iface = iface;
		return(IsToUs ? KEEPIT : DROPIT);
	}
	/*
	 * This packet appears to be older than the last one we got.
	 */

	/*
	 * Is it a (recorded) missing packet?
	 */
	if (is_lost_packet(thisnode, seq)) {
		return(IsToUs ? KEEPIT : DROPIT);
	}

	if (ishealedpartition || isrestart) {
		const char *	sts;
		TIME_T	newts = 0L;
		if ((sts = ha_msg_value(msg, F_TIME)) == NULL
		||	sscanf(sts, TIME_X, &newts) != 1 || newts == 0L) {
			/* Toss it.  No valid timestamp */
			ha_log(LOG_ERR, "should_drop_message: bad timestamp");
			return(DROPIT);
		}

		thisnode->rmt_lastupdate = newts;
		t->nmissing = 0;
		t->last_seq = seq;
		t->last_rexmit_req = 0L;
		t->last_iface = iface;
		return(IsToUs ? KEEPIT : DROPIT);
	}
	/* This is a duplicate packet (or a really old one we lost track of) */
	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "should_drop_message: Duplicate packet");
		ha_log_message(msg);
	}
	return(DROPIT);

}

/*
 * Is this the sequence number of a lost packet?
 * If so, clean up after it.
 */
int
is_lost_packet(struct node_info * thisnode, unsigned long seq)
{
	struct seqtrack *	t = &thisnode->track;
	int			j;

	for (j=0; j < t->nmissing; ++j) {
		/* Is this one of our missing packets? */
		if (seq == t->seqmissing[j]) {
			/* Yes.  Delete it from the list */
			t->seqmissing[j] = NOSEQUENCE;
			/* Did we delete the last one on the list */
			if (j == (t->nmissing-1)) {
				t->nmissing --;
			}

			/* Swallow up found packets */
			while (t->nmissing > 0
			&&	t->seqmissing[t->nmissing-1] == NOSEQUENCE) {
				t->nmissing --;
			}
			if (t->nmissing == 0) {
				ha_log(LOG_INFO, "No pkts missing from %s!"
				,	thisnode->nodename);
			}
			return 1;
		}
	}
	return 0;
}

void
request_msg_rexmit(struct node_info *node, unsigned long lowseq
,	unsigned long hiseq)
{
	struct ha_msg*	hmsg;
	char		low[16];
	char		high[16];
	struct tms	proforma_tms;
	if ((hmsg = ha_msg_new(6)) == NULL) {
		ha_log(LOG_ERR, "no memory for " T_REXMIT);
	}

	sprintf(low, "%lu", lowseq);
	sprintf(high, "%lu", hiseq);


	if (	hmsg != NULL
	&& 	ha_msg_add(hmsg, F_TYPE, T_REXMIT) == HA_OK
	&&	ha_msg_add(hmsg, F_TO, node->nodename)==HA_OK
	&&	ha_msg_add(hmsg, F_FIRSTSEQ, low) == HA_OK
	&&	ha_msg_add(hmsg, F_LASTSEQ, high) == HA_OK) {
		/* Send a re-transmit request */
		if (send_cluster_msg(hmsg) != HA_OK) {
			ha_log(LOG_ERR, "cannot send " T_REXMIT
			" request to %s", node->nodename);
		}
		node->track.last_rexmit_req = times(&proforma_tms);
	}else{
		ha_log(LOG_ERR, "no memory for " T_REXMIT);
	}
	ha_msg_del(hmsg);
}
void
check_rexmit_reqs(void)
{
	clock_t	now = 0L;
	int	j;

	for (j=0; j < config->nodecount; ++j) {
		struct node_info *	hip = &config->nodes[j];
		struct seqtrack *	t = &hip->track;
		int			seqidx;
		struct tms		proforma_tms;

		if (t->nmissing <= 0 ) {
			continue;
		}
		/* We rarely reach this code, so avoid the extra system call */
		if (now == 0L) {
			now = times(&proforma_tms);
		}
		/* Allow for lbolt wraparound here */
		if ((now - t->last_rexmit_req) <= CLK_TCK
		&&	now >= t->last_rexmit_req) {
			continue;
		}
		/* Time to ask for some packets again ... */
		for (seqidx = 0; seqidx < t->nmissing; ++seqidx) {
			if (t->seqmissing[seqidx] != NOSEQUENCE) {
				/*
				 * The code for asking for these by groups here
				 * is complicated.  This code is not.
				 */
				request_msg_rexmit(hip, t->seqmissing[seqidx]
				,	t->seqmissing[seqidx]);
			}
		}
	}
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
		hist->lastrexmit[j] = 0L;
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
		/* Lowseq is less than the lowest recorded seqno */
		hist->lowseq = hist->seqnos[slot];
		ha_msg_del(hist->msgq[slot]);
	}
	hist->msgq[slot] = msg;
	hist->seqnos[slot] = seq;
	hist->lastrexmit[slot] = 0L;
	hist->lastmsg = slot;
}

#define	MAX_REXMIT_BATCH	10
void
process_rexmit (struct msg_xmit_hist * hist, struct ha_msg* msg)
{
	const char *	cfseq;
	const char *	clseq;
	unsigned long	fseq = 0;
	unsigned long	lseq = 0;
	unsigned long	thisseq;
	int		firstslot = hist->lastmsg-1;
	int		rexmit_pkt_count = 0;

	if ((cfseq = ha_msg_value(msg, F_FIRSTSEQ)) == NULL
	||	(clseq = ha_msg_value(msg, F_LASTSEQ)) == NULL
	||	(fseq=atoi(cfseq)) <= 0 || (lseq=atoi(clseq)) <= 0
	||	fseq > lseq) {
		ha_log(LOG_ERR, "Invalid rexmit seqnos");
		ha_log_message(msg);
	}

	for (thisseq = lseq; thisseq >= fseq; --thisseq) {
		int	msgslot;
		int	foundit = 0;
		if (thisseq <= hist->lowseq) {
			/* Lowseq is less than the lowest recorded seqno */
			nak_rexmit(thisseq, "seqno too low");
			continue;
		}
		if (thisseq > hist->hiseq) {
			nak_rexmit(thisseq, "seqno too high");
			continue;
		}

		for (msgslot = firstslot
		;	!foundit && msgslot != (firstslot+1); --msgslot) {
			char *		smsg;
			int		len;
			struct tms	proforma_tms;
			clock_t		now = times(&proforma_tms);
			clock_t		last_rexmit;
			if (msgslot < 0) {
				msgslot = MAXMSGHIST;
			}
			if (hist->msgq[msgslot] == NULL) {
				continue;
			}
			if (hist->seqnos[msgslot] != thisseq) {
				continue;
			}

			/*
			 * We resend a packet unless it has been re-sent in
			 * the last second.  We treat lbolt wraparound as though
			 * the packet needs resending
			 */
			last_rexmit = hist->lastrexmit[msgslot];
			if (last_rexmit != 0L && now > last_rexmit
			&&	(now - last_rexmit) < CLK_TCK) {
				/* Continue to outer loop */
				goto NextReXmit;
			}
			/*
			 *	Don't send too many packets all at once...
			 *	or we could flood serial links...
			 */
			++rexmit_pkt_count;
			if (rexmit_pkt_count > MAX_REXMIT_BATCH) {
				return;
			}
			/* Found it!	Let's send it again! */
			firstslot = msgslot -1;
			foundit=1;
			if (1) {
				ha_log(LOG_INFO, "Retransmitting pkt %lu"
				,	thisseq);
			}
			if (DEBUGPKT) {
				ha_log_message(hist->msgq[msgslot]);
			}
			smsg = msg2string(hist->msgq[msgslot]);

			/* If it didn't convert, throw original message away */
			if (smsg != NULL) {
				len = strlen(smsg);
				hist->lastrexmit[msgslot] = now;
				send_to_all_media(smsg, len);
			}

		}
		if (!foundit) {
			nak_rexmit(thisseq, "seqno not found");
		}
NextReXmit:/* Loop again */;
	}
}
void
nak_rexmit(unsigned long seqno, const char * reason)
{
	struct ha_msg*	msg;
	char	sseqno[32];

	sprintf(sseqno, "%lx", seqno);
	ha_log(LOG_ERR, "Cannot rexmit pkt %lu: %s", seqno, reason);

	if ((msg = ha_msg_new(6)) == NULL) {
		ha_log(LOG_ERR, "no memory for " T_NAKREXMIT);
		return;
	}

	if (ha_msg_add(msg, F_TYPE, T_NAKREXMIT) != HA_OK
	||	ha_msg_add(msg, F_FIRSTSEQ, sseqno) != HA_OK
	||	ha_msg_add(msg, F_COMMENT, reason) != HA_OK) {
		ha_log(LOG_ERR, "cannot create " T_NAKREXMIT " msg.");
		ha_msg_del(msg);
		return;
	}
	send_cluster_msg(msg);
	ha_msg_del(msg);
}


int
ParseTestOpts()
{
	const char *	openpath = HA_D "/OnlyForTesting";
	FILE *	fp;
	static struct TestParms p;
	char	name[64];
	char	value[64];
	int	something_changed = 0;

	if ((fp = fopen(openpath, "r")) == NULL) {
		if (TestOpts) {
			ha_log(LOG_INFO, "Test Code Now disabled.");
			something_changed=1;
		}
		TestOpts = NULL;
		return something_changed;
	}
	TestOpts = &p;
	something_changed=1;

	memset(&p, 0, sizeof(p));
	p.send_loss_prob = 0;
	p.rcv_loss_prob = 0;

	ha_log(LOG_INFO, "WARNING: Enabling Test Code");

	while((fscanf(fp, "%[a-zA-Z_]=%s\n", name, value) == 2)) {
		if (strcmp(name, "rcvloss") == 0) {
			p.rcv_loss_prob = atof(value);
			p.enable_rcv_pkt_loss = 1;
			ha_log(LOG_INFO, "Receive loss probability = %.3f"
			,	p.rcv_loss_prob);
		}else if (strcmp(name, "xmitloss") == 0) {
			p.send_loss_prob = atof(value);
			p.enable_send_pkt_loss = 1;
			ha_log(LOG_INFO, "Xmit loss probability = %.3f"
			,	p.send_loss_prob);
		}else{
			ha_log(LOG_INFO, "Cannot recognize test param [%s]"
			,	name);
		}
	}
	ha_log(LOG_INFO, "WARNING: Above Options Now Enabled.");
	return something_changed;
}

#ifndef HB_VERS_FILE
	/* This file needs to be persistent across reboots, but isn't really a log */
#	define HB_VERS_FILE VAR_LIB_D "/hb_generation"
#endif

#define	GENLEN	16	/* Number of chars on disk for gen # and '\n' */



/*
 *	Increment our generation number
 *	It goes up each time we restart to prevent replay attacks.
 */

#ifndef O_SYNC
#	define O_SYNC 0
#endif

int
IncrGeneration(unsigned long * generation)
{
	char		buf[GENLEN+1];
	int		fd;
	int		flags = 0;

	(void)_ha_msg_h_Id;
	(void)_heartbeat_h_Id;

	if ((fd = open(HB_VERS_FILE, O_RDONLY)) < 0
	||	read(fd, buf, sizeof(buf)) < 1) {
		ha_log(LOG_WARNING, "No Previous generation starting at 1");
		snprintf(buf, sizeof(buf), "%*d", GENLEN, 0);
		flags = O_CREAT;
	}
	close(fd);

	buf[GENLEN] = EOS;
	sscanf(buf, "%lu", generation);
	++(*generation);
	snprintf(buf, sizeof(buf), "%*lu\n", GENLEN-1, *generation);

	if ((fd = open(HB_VERS_FILE, O_WRONLY|O_SYNC|flags, 0644)) < 0) {
		return HA_FAIL;
	}
	if (write(fd, buf, GENLEN) != GENLEN) {
		close(fd);
		return HA_FAIL;
	}

	/*
	 * Some UNIXes don't implement O_SYNC.
	 * So we do an fsync here for good measure.  It can't hurt ;-)
	 */

	if (fsync(fd) < 0) {
		ha_perror("fsync failure on " HB_VERS_FILE);
		return HA_FAIL;
	}
	if (close(fd) < 0) {
		ha_perror("close failure on " HB_VERS_FILE);
		return HA_FAIL;
	}
	/*
	 * We *really* don't want to lose this data.  We won't be able to
	 * join the cluster again without it.
	 */
	sync();
#if HAVE_UNRELIABLE_FSYNC
	sleep(10);
#endif
	return HA_OK;
}

#define	STANDBY_INIT_TO	10L	/* timeout for initial reply */
#define	STANDBY_RSC_TO	600L	/* timeout waiting for resource handling */

void
ask_for_resources(struct ha_msg *msg)
{

	const char *	info;
	const char *	from;
	int 		msgfromme;
	TIME_T 		now = time(NULL);
	int		message_ignored = 0;
	const enum standby	orig_standby = going_standby;

	if (!nice_failback) {
		ha_log(LOG_INFO
		,	"Standby mode only implemented when nice_failback on");
		return;
	}
	info = ha_msg_value(msg, F_COMMENT);
	from = ha_msg_value(msg, F_ORIG);

	if (info == NULL || from == NULL) {
		ha_log(LOG_ERR, "Received standby message without info/from");
		return;
	}
	msgfromme = strcmp(from, curnode->nodename) == 0;

	if (ANYDEBUG){
		ha_log(LOG_DEBUG
		,	"Received standby message %s from %s in state %d "
		,	info, from, going_standby);
	}

	if (standby_running && now < standby_running
	&&	strcasecmp(info, "me") == 0) {
		ha_log(LOG_ERR
		,	"Standby in progress"
		"- new request from %s ignored [%ld secs left]"
		,	from, standby_running - now);
		return;
	}

	/* Starting the STANDBY 3-phased protocol */

	switch(going_standby) {
	case NOT:
		if (!other_is_stable) {
			ha_log(LOG_WARNING, "standby message [%s] from %s"
			" ignored.  Other side is in flux.", info, from);
			return;
		}
		if (resourcestate != R_STABLE) {
			ha_log(LOG_WARNING, "standby message [%s] from %s"
			" ignored.  local resources in flux.", info, from);
			return;
		}
		if (strcasecmp(info, "me") == 0) {
			standby_running = now + STANDBY_INIT_TO;
			if (ANYDEBUG) {
				ha_log(LOG_DEBUG
				, "ask_for_resources: other now unstable");
			}
			other_is_stable = 0;
			ha_log(LOG_INFO, "%s wants to go standby", from);
			if (msgfromme) {
				/* We want to go standby */
				if (ANYDEBUG) {
					ha_log(LOG_INFO
					,	"i_hold_resources: %d"
					,	procinfo->i_hold_resources);
				}
				going_standby = ME;
			}else{
				if (ANYDEBUG) {
					ha_log(LOG_INFO
					,	"other_holds_resources: %d"
					,	other_holds_resources);
				}
				/* Other node wants to go standby */
				going_standby = OTHER;
				send_standby_msg(going_standby);
			}
		}else{
			message_ignored = 1;
		}
		break;

	case ME:
		/* Other node is alive, so give up our resources */
		if (!msgfromme) {
			standby_running = now + STANDBY_RSC_TO;
			if (strcasecmp(info,"other") == 0) {
				ha_log(LOG_INFO
				,	"standby: %s can take our resources"
				,	from);
				go_standby(ME);
				/* Our child proc sends a "done" message */
				/* after all the resources are released	*/
			}else{
				message_ignored = 1;
			}
		}else if (strcasecmp(info, "done") == 0) {
			/*
			 * The "done" message came from our child process
			 * indicating resources are completely released now.
			 */
			ha_log(LOG_INFO
			,	"Standby process finished. /Me secondary");
			going_standby = DONE;
			procinfo->i_hold_resources = NO_RSC;
			standby_running = now + STANDBY_RSC_TO;
		}else{
			message_ignored = 1;
		}
		break;
	case OTHER:
		if (strcasecmp(info, "done") == 0) {
			standby_running = now + STANDBY_RSC_TO;
			if (!msgfromme) {
				/* It's time to acquire resources */

				ha_log(LOG_INFO
				,	"standby: Acquire [%s] resources"
				,	from);
				/* go_standby gets *all* resources */
				/* req_our_resources(1); */
				go_standby(OTHER);
				going_standby = DONE;
			}else{
				message_ignored = 1;
			}
		}else if (!msgfromme || strcasecmp(info, "other") != 0) {
			/* We expect an "other" message from us */
			/* But, that's not what this one is ;-) */
			message_ignored = 1;
		}
		break;

	case DONE:
		if (strcmp(info, "done")== 0) {
			standby_running = 0L;
			going_standby = NOT;
			if (msgfromme) {
				ha_log(LOG_INFO
				,	"Standby process done. /Me primary");
				procinfo->i_hold_resources = ALL_RSC;
			}else{
				ha_log(LOG_INFO
				,	"Other node completed standby"
				" takeover.");
			}
			send_resources_held(rsc_msg[procinfo->i_hold_resources], 1, NULL);
			going_standby = NOT;
		}else{
			message_ignored = 1;
		}
		break;
	}
	if (message_ignored){
		ha_log(LOG_ERR
		,	"Ignored standby message '%s' from %s in state %d"
		,	info, from, orig_standby);
	}
	if (ANYDEBUG) {
		ha_log(LOG_INFO, "New standby state: %d", going_standby);
	}
}

int
countbystatus(const char * status, int matchornot)
{
	int	count = 0;
	int	matches;
	int	j;

	matchornot = (matchornot ? TRUE : FALSE);

	for (j=0; j < config->nodecount; ++j) {
		matches = (strcmp(config->nodes[j].status, status) == 0);
		if (matches == matchornot) {
			++count;
		}
	}
	return count;
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
 * Revision 1.182  2002/04/14 09:06:09  alan
 * Made yet another attempt to get all our SIGCHLDs.
 *
 * Revision 1.181  2002/04/14 00:39:30  alan
 * Put in a comment about "strings" needing to run in a separate
 * process...
 *
 * Revision 1.180  2002/04/13 22:45:37  alan
 * Changed a little of the code in heartbeat.c to use the new longclock_t
 * type and functions.  It ought to completely replace the use of
 * times() *everywhere*
 *
 * Reorganized some of the code for handling nice_failback to not all
 * be in the process_resources() function...
 *
 * Moved all the code which is triggered when our links first come up to
 * a single function, instead of scattered about in several different
 * places.
 *
 * Moved the code to take over local resources out of the process_clustermsg()
 * function into the poll loop code.  This eliminates calling the
 * process_resources() function for every packet.
 *
 * Moved the resource auditing code out into a separate function so
 * I could call it in more than one place.
 *
 * Moved all the resource handling code in the process_clustermsg() function
 * to be together, so it's more readable.
 *
 * Revision 1.179  2002/04/13 03:46:52  alan
 * more signal diddles...
 *
 * Revision 1.178  2002/04/13 02:07:14  alan
 * Made some changes to the way signals are handled so we don't lose
 * SIGCHLDs when shutting down.
 *
 * Revision 1.177  2002/04/12 19:36:14  alan
 * Previous changes broke nice_failback.
 * There was some code which was sent out the STARTING messages
 * which had been called because it was before some code which
 * bypassed protocol processing.  This code is now at the
 * end of the loop.
 *
 * Revision 1.176  2002/04/12 15:14:28  alan
 * Changed the processing of resource requests so we eliminate some
 * timing holes.
 *
 * First, we ignore ip-request-resp messages during shutdowns, so that we
 * don't acquire any new resources while we're shutting down :-)
 *
 * Secondly, we needed to queue ip-requests and don't answer them right
 * away if we're running any resource acquisition code ourselves.
 * This is because if we've just started a resource takeover ourselves,
 * and someone asks to have it back, we'll answer that they can have it
 * without releasing it ourselves because we don't realize that
 * we're acquiring it, because we don't have it quite yet.
 * By delaying until all resource acquisition/release processes
 * are complete, we can give an accurate answer to this request.
 *
 * Two things caused these bug to appear:
 * We now always answer any ip-request (if we're managing resources at all),
 * and we keep our links up for a little while longer than we used to
 * while we're shutting down.  So, the windows for these two behaviors have
 * been opened up a little wider - though I suspect they've both been
 * possible before.  Other changes made takeovers run faster, so the
 * combination was effective in making the bug apparent ;-)
 *
 * Solving the first was easy - we just filter out ip-request-resp
 * messages when shutting down.  The second one required that a queue be added
 * for handling incoming resource acquisition messages.  To implement this
 * queue we used Glib GHook-s, which are good things for recording a function
 * and a pointer to data in, and later running them.
 *
 * GHooks are a handy kludge to have around...
 *
 * Revision 1.175  2002/04/11 18:33:54  alan
 * Takeover/failover is much faster and a little safer than it was before...
 *
 * For the normal failback case
 * 	If the other machine is down, resources are taken immediately
 *
 * 	If the other machine is up, resources are requested and taken over
 * 		when they have been released.  If the other machine
 * 		never releases them, they are never taken over.
 * 		No background process is ever spawned to "eventually" take
 * 		them over.
 *
 * For the nice failback case
 * 	All resources are acquired *immediately* after the other machine is
 * 		declared dead.
 *
 * Changed the rules about initial deadtime:
 *
 * It now only insists the time be equal to deadtime.
 *
 * It gives a warning if its less than 10 seconds.
 *
 * If not specified, here is how it defaults...
 * 	If deadtime is less than or equal to 10 seconds, then it defaults it to be
 * 	twice the deadtime.
 *
 * 	If deadtime is greater than 10 seconds, then it defaults it to be
 * 	the same as deadtime.
 *
 * Revision 1.174  2002/04/10 21:05:33  alan
 * Put in some changes to control_process() to hopefully make it
 * exit completely reliably.
 * After 300 iterations, I saw a case where it hung in the read for control
 * packets, and didn't respond to signals, but all its children were dead.
 * I now close the FIFO, so that all reads will fail with EOF, and then
 * changed the read loop to drop out when it got EOF.
 * I added a  loop afterwards which consists of a pause and poll for signals
 * until all its children died.
 *
 * Revision 1.173  2002/04/10 07:41:14  alan
 * Enhanced the process tracking code, and used the enhancements ;-)
 * Made a number of minor fixes to make the tests come out better.
 * Put in a retry for writing to one of our FIFOs to allow for
 * an interrupted system call...
 * If a timeout came just as we started our system call, then
 * this could help.  Since it didn't go with a dead process, or
 * other symptoms this could be helpful.
 *
 * Revision 1.172  2002/04/09 21:53:26  alan
 * A large number of minor cleanups related to exit, cleanup, and process
 * management.  It all looks reasonably good.
 * One or two slightly larger changes (but still not major changes) in
 * these same areas.
 * Basically, now we wait for everything to be done before we exit, etc.
 *
 * Revision 1.171  2002/04/09 06:37:27  alan
 * Fixed the STONITH code so it works again ;-)
 *
 * Also tested (and fixed) the case of graceful shutdown nodes not getting
 * STONITHed.  We also don't STONITH nodes which had no resources at
 * the time they left the cluster, at least when nice_failback is set.
 *
 * Revision 1.170  2002/04/07 13:54:06  alan
 * This is a pretty big set of changes ( > 1200 lines in plain diff)
 *
 * The following major bugs have been fixed
 *  - STONITH operations are now a precondition for taking over
 *    resources from a dead machine
 *
 *  - Resource takeover events are now immediately terminated when shutting
 *    down - this keeps resources from being held after shutting down
 *
 *  - heartbeat could sometimes fail to start due to how it handled its
 *    own status through two different channels.  I restructured the handling
 *    of local status so that it's now handled almost exactly like handling
 *    the status of remote machines
 *
 * There is evidence that all these serious bugs have been around a long time,
 * even though they are rarely (if ever) seen.
 *
 * The following minor bugs have been fixed:
 *
 *  - the standby test now retries during transient conditions...
 *
 *  - the STONITH code for the test method "ssh" now uses "at" to schedule
 *    the stonith operation on the other node so it won't hang when using
 *    newer versions of ssh.
 *
 * The following new test was added:
 *  - SimulStart - starting all nodes ~ simultaneously
 *
 * The following significant restructuring of the code occurred:
 *
 *  - Completely rewrote the process management and death-of-child code to
 *    be uniform, and be based on a common semi-object-oriented approach
 *    The new process tracking code is very general, and I consider it to
 *    be part of the plumbing for the OCF.
 *
 *  - Completely rewrote the event handling code to be based on the Glib
 *    mainloop paradigm. The sets of "inputs" to the main loop are:
 *     - "polled" events like signals, and once-per-loop occurrances
 *     - messages from the cluster and users
 *     - API registration requests from potential clients
 *     - API calls from clients
 *
 *
 * The following minor changes were made:
 *
 *  - when nice_failback is taking over resources, since we always negotiate for
 *    taking them over, so we no longer have a timeout waiting for the other
 *    side to reply.  As a result, the timeout for waiting for the other
 *    side is now much longer than it was.
 *
 *  - transient errors for standby operations now print WARN instead of EROR
 *
 *  - The STONITH and standby tests now don't print funky output to the
 *    logs.
 *
 *  - added a new file TESTRESULTS.out for logging "official" test results.
 *
 * Groundwork was laid for the following future changes:
 *  - merging the control and master status processes
 *
 *  - making a few other things not wait for process completion in line
 *
 *  - creating a comprehensive asynchronous action structure
 *
 *  - getting rid of the "interface" kludge currently used for tracking
 *    activity on individual interfaces
 *
 * The following things still need to be tested:
 *
 *  - STONITH testing (including failures)
 *
 *  - clock jumps
 *
 *  - protocol retransmissions
 *
 *  - cross-version compatability of status updates (I added a new field)
 *
 * Revision 1.169  2002/04/04 17:55:27  alan
 * Put in a whole bunch of new code to manage processes much more generally, and powerfully.
 * It fixes two important bugs:  STONITH wasn't waited on before we took over resources.
 * And, we didn't stop our takeover processes before we started to shut down.
 *
 * Revision 1.168  2002/04/02 19:40:36  alan
 * Failover was completely broken because of a typo in the configure.in file
 * Changed the run level priorities so that heartbeat starts after
 * drbd by default.
 * Changed it so that heartbeat by default runs in init level 5 too...
 *
 * Fixed a problem which happened when both nodes started about simultaneously.
 * The result was that hb_standby wouldn't work afterwards.
 *
 * Raised the debug level of some reasonably verbose messages so that you can
 * turn on debug 1 and not be flooded with log messages.
 *
 * Changed the code so that in the case of nice_failback there is no waiting for
 * the other side to give up resources, because we negotiate this in advance.
 * It gets this information through and environment variable.
 *
 * Revision 1.167  2002/03/27 01:59:58  alan
 * Hopefully, fixed a bug where requests to retransmit packets
 * (and other unsequenced protocol packets) get dropped because they don't
 * have sequence numbers.
 *
 * Revision 1.166  2002/03/15 14:26:36  alan
 * Added code to help debug the current missing to/from/ts/,etc. problem...
 *
 * Revision 1.165  2002/02/21 21:43:33  alan
 * Put in a few fixes to make the client API work more reliably.
 * Put in a few changes to the process exit handling code which
 * also cause heartbeat to (attempt to) restart when it finds one of it's
 * own processes dies.  Restarting was already broken :-(
 *
 * Revision 1.164  2002/02/12 18:13:39  alan
 * Did 3 things:
 * 	Changed the API test program to use syslog for some messages.
 * 	Changed the API code to be a little less verbose
 * 	Removed the ns_st file from the rc.d directory (since it does
 * 		nothing and is no longer needed)
 *
 * Revision 1.163  2002/02/12 15:22:29  alan
 * Put in code to filter out rc script execution on every possible message,
 * so that only those scripts that actually exist will we attempt to execute.
 *
 * Revision 1.162  2002/02/11 22:31:34  alan
 * Added a new option ('l') to make heartbeat run at low priority.
 * Added support for a new capability - to start and stop client
 * 	processes together with heartbeat itself.
 *
 * Revision 1.161  2002/02/10 23:09:25  alan
 * Added a little initial code to support starting client
 * programs when we start, and shutting them down when we stop.
 *
 * Revision 1.160  2002/02/09 21:21:42  alan
 * Minor message and indentation changes.
 *
 * Revision 1.159  2002/01/16 22:59:17  alan
 * Fixed a dumb error in restructuring the code.
 * I passed the retransmit history structure by value instead of by address,
 * so there was a HUGE memory leak.
 *
 * Revision 1.158  2001/10/26 11:08:31  alan
 * Changed the code so that SIGINT never interrupts us.
 * Changed the code so that SIGALRM doesn't interrupt certain child
 * processes (particularly in shutdown).  Ditto for shutdown wrt SIGTERM and
 * SIGCHILD.
 *
 * Revision 1.157  2001/10/25 14:34:17  alan
 * Changed the serial code to send a BREAK when one side first starts up their
 * conversation.
 * Configured the receiving code to flush I/O buffers when they receive a break
 * Configured the code to ignore SIGINTs (except for their buffer flush effect)
 * Configured the code to use SIGQUIT instead of SIGINT when communicating that
 * the shutdown resource giveup is complete.
 *
 * This is all to fix a bug which occurs because of leftover out-of-date messages
 * in the serial buffering system.
 *
 * Revision 1.156  2001/10/25 05:06:30  alan
 * A few changes to tighten up the definition of "stability" so we
 * don't complain about things falsely, nor do we prohibit attempting
 * takeovers unnecessarily either.
 *
 * Revision 1.155  2001/10/24 20:46:28  alan
 * A large number of patches.  They are in these categories:
 * 	Fixes from Matt Soffen
 * 	Fixes to test environment things - including changing some ERRORs to
 * 		WARNings and vice versa.
 * 	etc.
 *
 * Revision 1.154  2001/10/24 00:24:44  alan
 * Moving in the direction of being able to get rid of one of our
 * control processes.
 * Today's work: splitting control_process() into control_process() and
 * process_control_packet().
 * The idea is that once the control_process and the master_status_process
 * are merged, that this function can be called from the select already
 * present in master_status_process().
 *
 * Revision 1.153  2001/10/23 05:40:41  alan
 * Put in code to make the management of the audit periods work a little
 * more neatly.
 *
 * Revision 1.152  2001/10/23 04:19:24  alan
 * Put in code so that a "stop" really stops heartbeat (again).
 *
 * Revision 1.151  2001/10/22 05:22:53  alan
 * Fixed the split-brain (cluster partition) problem.
 * Also, fixed lots of space/tab nits in various places in heartbeat.
 *
 * Revision 1.150  2001/10/22 04:02:29  alan
 * Put in a patch to check the arguments to ha_log calls...
 *
 * Revision 1.149  2001/10/13 22:27:15  alan
 * Removed a superfluous signal_all(SIGTERM)
 *
 * Revision 1.148  2001/10/13 09:23:19  alan
 * Fixed a bug in the new standby code.
 * It now waits until resources are fully given up before taking them over.
 * It now also manages the nice_failback state consistency audits correctly.
 * Still need to make it work for the not nice_failback case...
 *
 * Revision 1.147  2001/10/13 00:23:05  alan
 * Put in comments about a serious problem with respect to resource takeover...
 *
 * Revision 1.146  2001/10/12 23:05:21  alan
 * Put in a message about standby only being implemented when nice_failback
 * is on.
 *
 * Revision 1.145  2001/10/12 22:38:06  alan
 * Added Luis' patch for providing the standby capability
 *
 * Revision 1.144  2001/10/12 17:18:37  alan
 * Changed the code to allow for signals happening while signals are being processed.
 * Changed the code to allow us to have finer heartbeat timing resolution.
 *
 * Revision 1.143  2001/10/10 13:18:35  alan
 * Fixed a typo on ClockJustJumped.  Oops!
 *
 * Revision 1.142  2001/10/09 19:22:52  alan
 * Made some minor changes to how we handle clock jumps and timeout other
 * nodes.  I'm not sure why it's necessary, or if it is for that matter.
 * But it shouldn't *hurt* anything either.  This problem reported by Matt Soffen.
 *
 * Revision 1.141  2001/10/04 02:45:06  alan
 * Added comments about the lousy realtime behavior of the old method
 * of sending messages.  Changed the indentation of one line.
 *
 * Revision 1.140  2001/10/03 18:09:51  alan
 * Changed the process titles a little so that the medium type is displayed.
 *
 * Revision 1.139  2001/10/02 20:15:40  alan
 * Debug code, etc. from Matt Soffen...
 *
 * Revision 1.138  2001/10/02 05:12:19  alan
 * Various portability fixes (make warnings go away) for Solaris.
 *
 * Revision 1.137  2001/10/02 04:22:45  alan
 * Fixed a minor bug regarding reporting how late a heartbeat is when there is no previous
 * heartbeat to compare it to.  In that circumstance, it shouldn't report at all.
 * Now, that's what it does too ;-)
 *
 * Revision 1.136  2001/10/01 22:00:54  alan
 * Improved Andreas Piesk's patch for no-stonith after shutdown.
 * Probably fixed a bug about not detecting status changes after a restart.
 * Fixed a few coding standards kind of things.
 *
 * Revision 1.135  2001/10/01 20:24:36  alan
 * Changed the code to not open the FIFO for every message we send to the cluster.
 * This should improve our worst-case (and average) latency.
 *
 * Revision 1.134  2001/09/29 19:08:24  alan
 * Wonderful security and error correction patch from Emily Ratliff
 * 	<ratliff@austin.ibm.com>
 * Fixes code to have strncpy() calls instead of strcpy calls.
 * Also fixes the number of arguments to several functions which were wrong.
 * Many thanks to Emily.
 *
 * Revision 1.133  2001/09/18 14:19:45  horms
 *
 * Signal handlers set flags and actions are executed accordingly
 * as part of event loops. This avoids problems with executing some
 * system calls within signal handlers. In paricular calling exec() from
 * within a signal may result in process with unexpected signal masks.
 *
 * Unset the signal mask for SIGTERM upon intialisation. This is harmless
 * and a good safety measure in case the calling process has masked
 * this signal.
 *
 * Revision 1.132  2001/09/07 05:48:30  horms
 * Changed recently added _handler funciotns to _sig to match previously defined signal handlers. Horms
 *
 * Revision 1.131  2001/09/07 05:46:39  horms
 * Changed recently added _handler funciotns to _sig to match previously defined signal handlers. Horms
 *
 * Revision 1.130  2001/09/07 01:09:06  alan
 * Put in code to make the glib error messages get redirected to whereever
 * the other ha_log messages go...
 *
 * Revision 1.129  2001/09/07 00:07:14  alan
 * Fixed the code for dealing with the test packet dropping facility.
 * It has been broken since I changed the startup order.
 *
 * Revision 1.128  2001/09/06 16:14:35  horms
 * Added code to set proctitle for heartbeat processes. Working on why heartbeat doesn't restart itself properly. I'd send the latter as a patch to the list but it is rather intertwined in the former
 *
 * Revision 1.127  2001/08/10 17:35:38  alan
 * Removed some files for comm plugins
 * Moved the rest of the software over to use the new plugin system for comm
 * plugins.
 *
 * Revision 1.126  2001/08/02 06:09:19  alan
 * Put in fix inspired by pubz@free.fr (aka erwan@mandrakesoft.com ?)
 * to fix recovery after a cluster partition.
 * He discovered that the problem was that the master status process was
 * setting a flag which the control process needed to check.  So, the result
 * was that it never saw the flag set because of the different address spaces.
 * So, I put the flag into the shared memory segment.
 *
 * Revision 1.125  2001/08/02 01:45:16  alan
 * copyright change and message change.
 *
 * Revision 1.124  2001/07/17 15:00:04  alan
 * Put in Matt's changes for findif, and committed my changes for the new module loader.
 * You now have to have glib.
 *
 * Revision 1.123  2001/07/04 17:00:56  alan
 * Put in changes to make the the sequence number updating report failure
 * if close or fsync fails.
 *
 * Revision 1.122  2001/07/03 14:09:07  alan
 * More debug for Matt Soffen...
 *
 * Revision 1.121  2001/07/02 22:29:35  alan
 * Put in a little more basic debug for heartbeat.
 *
 * Revision 1.120  2001/07/02 19:12:57  alan
 * Added debugging code around startup of child processes.
 *
 * Revision 1.119  2001/06/28 12:16:44  alan
 * Committed the *rest* of Juri Haberland's script patch that I thought I
 * had already applied :-(.
 *
 * Revision 1.118  2001/06/27 23:33:46  alan
 * Put in the changes to use times(&proforma_tms) instead of times(NULL)
 *
 * Revision 1.117  2001/06/23 07:01:48  alan
 * Changed CLOCKS_PER_SEC back into CLK_TCK.
 * Quite a few places, and add portability stuff for it to portability.h
 *
 * Revision 1.116  2001/06/16 12:19:08  alan
 * Updated various pieces of code to use CLOCKS_PER_SEC instead of CLK_TCK
 * and moved the portability #ifdefs to only two places...
 *
 * Revision 1.115  2001/06/08 04:57:47  alan
 * Changed "config.h" to <portability.h>
 *
 * Revision 1.114  2001/06/07 21:29:44  alan
 * Put in various portability changes to compile on Solaris w/o warnings.
 * The symptoms came courtesy of David Lee.
 *
 * Revision 1.113  2001/05/31 16:51:18  alan
 * Made not being able to create the PID file a fatal error...
 *
 * Revision 1.112  2001/05/31 13:50:56  alan
 * Moving towards getting modules working.  More debug also...
 *
 * Revision 1.111  2001/05/27 04:58:32  alan
 * Made some warnings go away.
 *
 * Revision 1.110  2001/05/26 17:38:01  mmoerz
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
 * Revision 1.109  2001/05/22 13:25:02  alan
 * Put in David Lee's portability fix for attaching shared memory segs
 * without getting alignment warnings...
 *
 * Revision 1.108  2001/05/21 15:29:50  alan
 * Moved David Lee's LOG_PRI (syslog) patch from heartbeat.c to heartbeat.h
 *
 * Revision 1.107  2001/05/21 15:11:50  alan
 * Added David Lee's change to work without the LOG_PRI macro in config.h
 *
 * Revision 1.106  2001/05/20 04:37:35  alan
 * Fixed a bug in the ha_versioninfo() function where a variable
 * was supposed to be static, but wasn't...
 *
 * Revision 1.105  2001/05/15 19:52:50  alan
 * More portability fixes from David Lee
 *
 * Revision 1.104  2001/05/11 14:55:06  alan
 * Followed David Lee's suggestion about splitting out all the heartbeat process
 * management stuff into a separate header file...
 * Also changed to using PATH_MAX for maximum pathname length.
 *
 * Revision 1.103  2001/05/11 06:20:26  alan
 * Fixed CFLAGS so we load modules from the right diurectory.
 * Fixed minor static symbol problems.
 * Fixed a bug which kept early error messages from coming out.
 *
 * Revision 1.102  2001/05/10 22:36:37  alan
 * Deleted Makefiles from CVS and made all the warnings go away.
 *
 * Revision 1.101  2001/05/09 23:21:21  mmoerz
 * autoconf & automake & libtool changes
 *
 * * following directories have been added:
 *
 *   - config	will contain autoconf/automake scripts
 *   - linux-ha	contains config.h which is generated by autoconf
 * 		will perhaps some day contain headers which are used throughout
 * 		linux-ha
 *   - replace	contains as the name implies replacement stuff for targets
 * 		where specific sources are missing.
 *
 * * following directories have been added to make a split up between c-code
 *   and shell scripts and to easy their installation with automake&autoconf
 *
 *   - heartbeat/init.d		containment of init.d script for heartbeat
 *   - heartbeat/logrotate.d	containment of logrotate script for heartbeat
 *
 *   - ldirectord/init.d		similar to heartbeat
 *   - ldirectord/logrotate.d	similar to heartbeat
 *
 * * general changes touching the complete repository:
 *
 *   - all Makefiles have been replaced by Makefile.ams.
 *
 *   - all .cvsingnore files have been enhanced to cope with the dirs/files
 *     that are added by automake/autoconf
 *     Perhaps it would be a nice idea to include those files, but the sum
 *     of their size if beyond 100KB and they are likely to vary from
 *     automake/autoconf version.
 *     Let's keep in mind that we will have to include them in distribution
 *     .tgz anyway.
 *
 *   - in dir replace setenv.c was placed to available on platform where
 *     putenv() has to be used since setenv is depricated (better rewrite
 *     code -> to be done later)
 *
 * * following changes have been made to the files of linux-ha:
 *
 *   - all .cvsignore files have been changed to ignore files generated by
 *     autoconf/automake and all files produced during the build-process
 *
 *   - heartbeat/heartbeat.c:	added #include <config.h>
 *
 *   - heartbeat/config.c:		added #include <config.h>
 *
 * * following files have been added:
 *    - Makefile.am: see above
 *    - configure.in: man autoconf/automake file
 *    - acconfig.h: here are additional defines that are needed for
 * 		 linux-ha/config.h
 *    - bootstrap: the shell script that 'compiles' the autoconf/automake script
 * 		into a useable form
 *    - config/.cvsignore: no comment
 *    - doc/Makefile.am: no comment
 *    - heartbeat/Makefile.am: no comment
 *    - heartbeat/lib/Makefile.am: no comment
 *    - heartbeat/init.d/.cvsignore: no comment
 *    - heartbeat/init.d/heartbeat: copy of hearbeat/hearbeat.sh
 *    - heartbeat/init.d/Makefile.am: no comment
 *    - heartbeat/logrotate.d/.cvsignore: no comment
 *    - heartbeat/logrotate.d/Makefile.am: no comment
 *    - heartbeat/logrotate.d/heartbeat: copy of hearbeat/heartbeat.logrotate
 *    - heartbeat/rc.d/Makefile.am: no comment
 *    - heartbeat/resource.d/Makefile.am: no comment
 *    - ldirectord/Makefile.am: no comment
 *    - ldirectord/init.d/Makefile.am: no comment
 *    - ldirectord/init.d/.cvsignore: no comment
 *    - ldirectord/init.d/ldiretord: copy of ldirectord/ldirectord.sh
 *    - ldirectord/logrotate.d/Makefile.am: no comment
 *    - ldirectord/logrotate.d/.cvsignore: no comment
 *    - ldirectord//ldiretord: copy of ldirectord/ldirectord.logrotate
 *    - linux-ha/.cvsignore: no comment
 *    - replace/.cvsignore: no comment
 *    - replace/setenv.c: replacement function for targets where setenv is missing
 *    - replace/Makefile.am: no comment
 *    - stonith/Makefile.am: no comment
 *
 * Revision 1.100  2001/04/19 13:41:54  alan
 * Removed the two annoying "error" messages that occur when heartbeat
 * is shut down.  They are: "controlfifo2msg: cannot create message"
 * and "control_process: NULL message"
 *
 * Revision 1.99  2001/03/16 03:01:12  alan
 * Put in a fix to Norbert Steinl's problem with the logger facility
 * and priority being wrong.
 *
 * Revision 1.98  2001/03/11 06:23:09  alan
 * Fixed the bug of quitting whenever stats needed to be printed.
 * This bug was reported by Robert_Macaulay@Dell.com.
 * The underlying problem was that the stonith code didn.t exit after
 * the child process completed, but returned, and then everything got
 * a bit sick after that ;-)
 *
 * Revision 1.97  2001/03/11 03:16:12  alan
 * Fixed the problem with mcast not incrementing nummedia.
 * Installed mcast module in the makefile.
 * Made the code for printing things a little more cautious about data it is
 * passed as a parameter.
 *
 * Revision 1.96  2001/03/06 21:11:05  alan
 * Added initdead (initial message) dead time to heartbeat.
 *
 * Revision 1.95  2001/02/01 11:52:04  alan
 * Change things to that things occur in the right order.
 * We need to not start timing message reception until we're completely started.
 * We need to Stonith the other guy before we take over their resources.
 *
 * Revision 1.94  2000/12/12 23:23:46  alan
 * Changed the type of times from time_t to TIME_T (unsigned long).
 * Added BuildPreReq: lynx
 * Made things a little more OpenBSD compatible.
 *
 * Revision 1.93  2000/12/04 22:11:22  alan
 * FreeBSD compatibility changes.
 *
 * Revision 1.92  2000/11/12 21:12:48  alan
 * Set the close-on-exec bit for the watchdog file descriptor.
 *
 * Revision 1.91  2000/11/12 04:29:22  alan
 * Fixed: syslog/file simultaneous logging.
 * 	Added a group for API clients.
 * 	Serious problem with replay attack protection.
 * 	Shutdown now waits for resources to be completely given up
 * 		before stopping heartbeat.
 * 	Made the stonith code run in a separate process.
 *
 * Revision 1.90  2000/09/10 03:48:52  alan
 * Fixed a couple of bugs.
 * - packets that were already authenticated didn't get reauthenticated correctly.
 * - packets that were irretrievably lost didn't get handled correctly.
 *
 * Revision 1.89  2000/09/02 23:26:24  alan
 * Fixed bugs surrounding detecting cluster partitions, and around
 * restarts.  Also added the unfortunately missing ifstat and ns_stat files...
 *
 * Revision 1.88  2000/09/01 22:35:50  alan
 * Minor change to make restarts after cluster partitions work more reliably.
 *
 * Revision 1.87  2000/09/01 21:15:23  marcelo
 * Fixed auth file reread wrt dynamic modules
 *
 * Revision 1.86  2000/09/01 21:10:46  marcelo
 * Added dynamic module support
 *
 * Revision 1.85  2000/09/01 06:27:49  alan
 * Added code to force a status update when we restart.
 *
 * Revision 1.84  2000/09/01 06:07:43  alan
 * Fixed the "missing library" problem, AND probably fixed the perennial
 * problem with partitioned cluster.
 *
 * Revision 1.83  2000/09/01 04:18:59  alan
 * Added missing products to Specfile.
 * Perhaps fixed the partitioned cluster problem.
 *
 * Revision 1.82  2000/08/13 04:36:16  alan
 * Added code to make ping heartbeats work...
 * It looks like they do, too ;-)
 *
 * Revision 1.81  2000/08/11 00:30:07  alan
 * This is some new code that does two things:
 * 	It has pretty good replay attack protection
 * 	It has sort-of-basic recovery from a split partition.
 *
 * Revision 1.80  2000/08/01 05:48:25  alan
 * Fixed several serious bugs and a few minor ones for the heartbeat API.
 *
 * Revision 1.79  2000/07/31 03:39:40  alan
 * This is a working version of heartbeat with the API code.
 * I think it even has a reasonable security policy in it.
 *
 * Revision 1.78  2000/07/31 00:05:17  alan
 * Put the high-priority stuff back into heartbeat...
 *
 * Revision 1.77  2000/07/31 00:04:32  alan
 * First working version of security-revised heartbeat API code.
 * Not all the security checks are in, but we're making progress...
 *
 * Revision 1.76  2000/07/26 05:17:19  alan
 * Added GPL license statements to all the code.
 *
 * Revision 1.75  2000/07/21 16:59:38  alan
 * More minor changes to the Stonith API.
 * I switched from enums to #defines so that people can use #ifdefs if in
 * the future they want to do so.  In fact, I changed the ONOFF code
 * in the Baytech module to do just that.  It's convenient that way :-)
 * I *still* don't define the ON/OFF operation code in the API though :-)
 *
 * Revision 1.74  2000/07/21 13:25:51  alan
 * Made heartbeat consistent with current Stonith API.
 *
 * Revision 1.73  2000/07/21 04:22:34  alan
 * Revamped the Stonith API to make it more readily extensible.
 * This nice improvement was suggested by Bhavesh Davda of Avaya.
 * Thanks Bhavesh!
 *
 * Revision 1.72  2000/07/20 16:51:54  alan
 * More API fixes.
 * The new API code now deals with interfaces changes, too...
 *
 * Revision 1.71  2000/07/17 19:27:52  alan
 * Fixed a bug in stonith code (it didn't always kill telnet command)
 *
 * Revision 1.70  2000/07/16 22:14:37  alan
 * Added stonith capabilities to heartbeat.
 * Still need to make the stonith code into a library...
 *
 * Revision 1.69  2000/07/16 20:42:53  alan
 * Added the late heartbeat warning code.
 *
 * Revision 1.68  2000/07/11 03:49:42  alan
 * Further evolution of the heartbeat API code.
 * It works quite a bit at this point - at least on the server side.
 * Now, on to the client side...
 *
 * Revision 1.67  2000/07/11 00:25:52  alan
 * Added a little more API code.  It looks like the rudiments are now working.
 *
 * Revision 1.66  2000/07/10 23:08:41  alan
 * Added code to actually put the API code in place.
 * Wonder if it works?
 *
 * Revision 1.65  2000/06/17 12:09:10  alan
 * Fixed the problem when one side or the other has no local resources.
 * Before it whined incessantly about being no one holding local resources.
 * Now, it thinks it owns local resources even if there aren't any.
 * (sort of like being the king of nothing).
 *
 * Revision 1.64  2000/06/15 14:24:31  alan
 * Changed the version #.  Minor comment changes.
 *
 * Revision 1.63  2000/06/15 06:03:50  alan
 * Missing '[' in debug message.  pretty low priority.
 *
 * Revision 1.62  2000/06/15 05:51:41  alan
 * Added a little more version info when debugging is turned on.
 *
 * Revision 1.61  2000/06/14 22:08:29  lclaudio
 * *** empty log message ***
 *
 * Revision 1.60  2000/06/14 15:43:14  alan
 * Put in a little shutdown code to make child processes that we've started go away.
 *
 * Revision 1.59  2000/06/14 06:17:35  alan
 * Changed comments quite a bit, and the code a little...
 *
 * Revision 1.58  2000/06/13 20:34:10  alan
 * Hopefully put the finishing touches on the restart/nice_failback code.
 *
 * Revision 1.57  2000/06/13 20:19:24  alan
 * Added code to make restarting (-R) work with nice_failback. But, not enough, yet...
 *
 * Revision 1.56  2000/06/13 17:59:53  alan
 * Fixed the nice_failback code to change the way it handles states.
 *
 * Revision 1.55  2000/06/13 04:20:41  alan
 * Fixed a bug for handling logfile.  It never worked, except by the default case.
 * Fixed a bug related to noting when various nodes were out of transition.
 *
 * Revision 1.54  2000/06/12 23:01:14  alan
 * Added comments about new behavior for -r flag with nice_failover.
 *
 * Revision 1.53  2000/06/12 22:03:11  alan
 * Put in a fix to the link status code, to undo something I'd broken, and also to simplify it.
 * I changed heartbeat.sh so that it uses the -r flag to restart heartbeat instead
 * of stopping and starting it.
 *
 * Revision 1.52  2000/06/12 06:47:35  alan
 * Changed a little formatting to make things read nicer on an 80-column screen.
 *
 * Revision 1.51  2000/06/12 06:11:09  alan
 * Changed resource takeover order to left-to-right
 * Added new version of nice_failback.  Hopefully it works wonderfully!
 * Regularized some error messages
 * Print the version of heartbeat when starting
 * Hosts now have three statuses {down, up, active}
 * SuSE compatability due to Friedrich Lobenstock and alanr
 * Other minor tweaks, too numerous to mention.
 *
 * Revision 1.50  2000/05/27 07:43:06  alan
 * Added code to set signal(SIGCHLD, SIG_DFL) in 3 places.  Fix due to lclaudio and Fabio Olive Leite
 *
 * Revision 1.49  2000/05/17 13:01:49  alan
 * Changed argv[0] and cmdname to be shorter.
 * Changed ha parsing function to close ha.cf.
 * Changed comments in ppp-udp so that it notes the current problems.
 *
 * Revision 1.48  2000/05/11 22:47:50  alan
 * Minor changes, plus code to put in hooks for the new API.
 *
 * Revision 1.47  2000/05/09 03:00:59  alan
 * Hopefully finished the removal of the nice_failback code.
 *
 * Revision 1.46  2000/05/09 00:38:44  alan
 * Removed most of the nice_failback code.
 *
 * Revision 1.45  2000/05/03 01:48:28  alan
 * Added code to make non-heartbeat child processes not run as realtime procs.
 * Also fixed the message about creating FIFO to not be an error, just info.
 *
 * Revision 1.44  2000/04/28 21:41:37  alan
 * Added the features to lock things in memory, and set our priority up.
 *
 * Revision 1.43  2000/04/27 12:50:20  alan
 * Changed the port number to 694.  Added the pristene target to the ldirectord
 * Makefile.  Minor tweaks to heartbeat.sh, so that it gives some kind of
 * message if there is no configuration file yet.
 *
 * Revision 1.42  2000/04/12 23:03:49  marcelo
 * Added per-link status instead per-host status. Now we will able
 * to develop link<->service dependacy scheme.
 *
 * Revision 1.41  2000/04/08 21:33:35  horms
 * readding logfile cleanup
 *
 * Revision 1.40  2000/04/05 13:40:28  lclaudio
 *   + Added the nice_failback feature. If the cluster is running when
 *	 the primary starts it acts as a secondary.
 *
 * Revision 1.39  2000/04/03 08:26:29  horms
 *
 *
 * Tidied up the output from heartbeat.sh (/etc/rc.d/init.d/heartbeat)
 * on Redhat 6.2
 *
 * Logging to syslog if a facility is specified in ha.cf is instead of
 * rather than as well as file logging as per instructions in ha.cf
 *
 * Fixed a small bug in shellfunctions that caused logs to syslog
 * to be garbled.
 *
 * Revision 1.38  1999/12/25 19:00:48  alan
 * I now send local status unconditionally every time the clock jumps backwards.
 *
 * Revision 1.37  1999/12/25 08:44:17  alan
 * Updated to new version stamp
 * Added Lars Marowsky-Bree's suggestion to make the code almost completely
 * immune from difficulties inherent in jumping the clock around.
 *
 * Revision 1.36  1999/11/27 16:00:02  alan
 * Fixed a minor bug about where a continue should go...
 *
 * Revision 1.35  1999/11/26 07:19:17  alan
 * Changed heartbeat.c so that it doesn't say "seqno not found" for a
 * packet which has been retransmitted recently.
 * The code continued to the next iteration of the inner loop.  It needed
 * to continue to the next iteration of the outer loop.  lOOPS!
 *
 * Revision 1.34  1999/11/25 20:13:15  alan
 * Minor retransmit updates.  Need to add another source file to CVS, too...
 * These updates were to allow us to simulate lots of packet losses.
 *
 * Revision 1.33  1999/11/23 08:50:01  alan
 * Put in the complete basis for the "reliable" packet transport for heartbeat.
 * This include throttling the packet retransmission on both sides, both
 * from the requestor not asking too often, and from the resender, who won't
 * retransmit a packet any more often than once a second.
 * I think this looks pretty good at this point (famous last words :-)).
 *
 * Revision 1.32  1999/11/22 20:39:49  alan
 * Removed references to the now-obsolete monitoring code...
 *
 * Revision 1.31  1999/11/22 20:28:23  alan
 * First pass of putting real packet retransmission.
 * Still need to request missing packets from time to time
 * in case retransmit requests get lost.
 *
 * Revision 1.30  1999/11/14 08:23:44  alan
 * Fixed bug in serial code where turning on flow control caused
 * heartbeat to hang.  Also now detect hangs and shutdown automatically.
 *
 * Revision 1.29  1999/11/11 04:58:04  alan
 * Fixed a problem in the Makefile which caused resources to not be
 * taken over when we start up.
 * Added RTSCTS to the serial port.
 * Added lots of error checking to the resource takeover code.
 *
 * Revision 1.28  1999/11/09 07:34:54  alan
 * *Correctly* fixed the problem Thomas Hepper reported.
 *
 * Revision 1.27  1999/11/09 06:13:02  alan
 * Put in Thomas Hepper's bug fix for the alarm occurring when waiting for
 * resources to be listed during initial startup.
 * Also, minor changes to make config work without a linker warning...
 *
 * Revision 1.26  1999/11/08 02:07:59  alan
 * Minor changes for reasons I can no longer recall :-(
 *
 * Revision 1.25  1999/11/06 03:41:15  alan
 * Fixed some bugs regarding logging
 * Also added some printout for initially taking over resources
 *
 * Revision 1.24  1999/10/25 15:35:03  alan
 * Added code to move a little ways along the path to having error recovery
 * in the heartbeat protocol.
 * Changed the code for serial.c and ppp-udp.c so that they reauthenticate
 * packets they change the ttl on (before forwarding them).
 *
 * Revision 1.23  1999/10/19 13:55:36  alan
 * Changed comments about being red hat compatible
 * Also, changed heartbeat.c to be both SuSE and Red Hat compatible in it's -s
 * output
 *
 * Revision 1.22  1999/10/19 01:55:54  alan
 * Put in code to make the -k option loop until the killed heartbeat stops running.
 *
 * Revision 1.21  1999/10/11 14:29:15  alanr
 * Minor malloc tweaks
 *
 * Revision 1.20  1999/10/11 05:18:07  alanr
 * Minor tweaks in mem stats, etc
 *
 * Revision 1.19  1999/10/11 04:50:31  alanr
 * Alan Cox's suggested signal changes
 *
 * Revision 1.18  1999/10/10 22:22:47  alanr
 * New malloc scheme + send initial status immediately
 *
 * Revision 1.17  1999/10/10 20:12:08  alanr
 * New malloc/free (untested)
 *
 * Revision 1.16  1999/10/05 18:47:52  alanr
 * restart code (-r flag) now works as I think it should
 *
 * Revision 1.15  1999/10/05 16:11:49  alanr
 * First attempt at restarting everything with -R/-r flags
 *
 * Revision 1.14  1999/10/05 06:17:06  alanr
 * Fixed various uninitialized variables
 *
 * Revision 1.13  1999/10/05 05:17:34  alanr
 * Added -s (status) option to heartbeat, and used it in heartbeat.sh...
 *
 * Revision 1.12  1999/10/05 04:35:10  alanr
 * Changed it to use the new heartbeat -k option to shut donw heartbeat.
 *
 * Revision 1.11  1999/10/05 04:09:45  alanr
 * Fixed a problem reported by Thomas Hepper where heartbeat won't start if a regular
 * file by the same name as the FIFO exists.  Now I just remove it...
 *
 * Revision 1.10  1999/10/05 04:03:42  alanr
 * added code to implement the -r (restart already running heartbeat process) option.
 * It seems to work and everything!
 *
 * Revision 1.9  1999/10/04 03:12:20  alanr
 * Shutdown code now runs from heartbeat.
 * Logging should be in pretty good shape now, too.
 *
 * Revision 1.8  1999/10/03 03:13:47  alanr
 * Moved resource acquisition to 'heartbeat', also no longer attempt to make the FIFO, it's now done in heartbeat.  It should now be possible to start it up more readily...
 *
 * Revision 1.7  1999/10/02 18:12:08  alanr
 * Create fifo in heartbeat.c and change ha_perror() to  a var args thing...
 *
 * Revision 1.6  1999/09/30 05:40:37  alanr
 * Thomas Hepper's fixes
 *
 * Revision 1.5  1999/09/29 03:22:09  alanr
 * Added the ability to reread auth config file on SIGHUP
 *
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
