/*
 * apphbd:	application heartbeat daemon
 *
 * This daemon implements an application heartbeat server.
 *
 * Clients register with it and are expected to check in from time to time
 * If they don't, we complain ;-)
 *
 * More details can be found in the <apphb.h> header file.
 *
 * Copyright(c) 2002 Alan Robertson <alanr@unix.sh>
 *
 *********************************************************************
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
 */

/*
 * General strategy:  We use the IPC abstraction library for all our
 * client-server communications.  We use the glib 'mainloop' paradigm
 * for all our event processing.
 *
 * The IPC connection socket is one event source.
 * Each socket connecting us to our clients are more event sources.
 * Each heartbeat timeout are also event sources.
 *
 * The only limit we have on the number of clients we can support is the
 * number of file descriptors we can have open.  It's been tested to
 * several hundred at a time.
 *
 * We use the Gmain_timeout timeouts instead of native glib mainloop
 * timeouts because they aren't affected by changes in the time of day
 * on the system.  They have identical semantics - except for working
 * correctly ;-)
 *
 *
 * TODO list:
 *
 *	- Implement plugins for notification mechanisms...
 * 
 *	- Consider merging all the timeouts into some kind of single
 *		timeout source.  This would probably more efficient for
 *		large numbers of clients.  But, it may not matter ;-)
 *
 *	- Implement command line option parsing:
 *		for notification plugins to load
 *		for the name of a watchdog device to open
 *		for the group id members must belong to
 *		for the name of configuration file to use?
 *
 *	- Implement a reload option for config file?
 *
 *
 *	Notification plugin API exported functions
 *		cregister (pid_t pid, const char * appname
 *		,	void * clienthandle)
 *		status(pid_t pid, const char * appname, apphb_event_t what)
 *
 *	Notification plugin imported functions:
 *		authenticate_client(void * handle, uidlist, gidlist)
 *			This returns TRUE if the app at apphandle
 *			properly authenticates according to the gidlist
 *			and the uidlist.
 */

#include <syslog.h>
#include <portability.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <apphb.h>
#define	time	footime
#define	index	fooindex
#include	<glib.h>
#undef time
#undef index
#include <clplumbing/longclock.h>
#include <clplumbing/ipc.h>
#include <clplumbing/Gmain_timeout.h>
#include <clplumbing/apphb_cs.h>
#include <pils/generic.h>
#include <pils/plugin.h>

#define __USE_UNIX98
#include <signal.h>

#ifndef PIDFILE
#	define	PIDFILE "/var/run/apphbd.pid"
#endif

const char *	cmdname = "apphbd";
#define		DBGMIN		1
#define		DBGDETAIL	2
int		debug = 0;



typedef struct apphb_client apphb_client_t;

/*
 * Per-client data structure.
 */
struct apphb_client {
	char *			appname;	/* application name */
	char *			appinst;	/* application name */
	pid_t			pid;		/* application pid */
	uid_t			uid;		/* application UID */
	gid_t			gid;		/* application GID */
	guint			timerid;	/* timer source id */
	guint			sourceid;	/* message source id */
	long			timerms;	/* heartbeat timeout in ms */
	gboolean		missinghb;	/* True if missing a hb */
	struct IPC_CHANNEL*	ch;		/* client comm channel */
	GPollFD*		ifd;		/* ifd for poll */
	GPollFD*		ofd;		/* ofd for poll */
	struct IPC_MESSAGE	rcmsg;		/* return code msg */
	struct apphb_rc		rc;		/* last return code */
	gboolean		deleteme;	/* Delete after next call */
};

typedef enum apphb_event apphb_event_t;
enum apphb_event {
	APPHB_HUP	= 1,
	APPHB_NOHB	= 2,
	APPHB_HBAGAIN	= 3,
	APPHB_HBUNREG	= 4,
};

#define	MAXNOTIFYPLUGIN	100

/*
 * Definitions for plugins
 */
typedef struct AppHBNotifyOps_s AppHBNotifyOps;
typedef struct AppHBNotifyImports_s AppHBNotifyImports;

/*
 * Plugin exported functions.
 */
struct AppHBNotifyOps_s {
	int (*cregister)(pid_t pid, const char * appname, const char * appinst
	,	uid_t uid, gid_t gid, void * handle);
	int (*status)(const char * appname, const char * appinst, pid_t pid
	,	uid_t uid, gid_t gid, apphb_event_t event);
};

AppHBNotifyOps*	NotificationPlugins[MAXNOTIFYPLUGIN];
int		n_Notification_Plugins = 0;
static int	watchdogfd = -1;


/*
 * Plugin imported functions.
 */
struct AppHBNotifyImports_s {
	gboolean (*auth)	(void * clienthandle
,	uid_t * uidlist, gid_t* gidlist, int nuid, int ngid);
};

static void apphb_notify(apphb_client_t* client, apphb_event_t event);
static long get_running_pid(gboolean * anypidfile);
static void make_daemon(void);
static int init_start(void);
static int init_stop(void);
static int init_status(void);
static int init_restart(void);
static void load_notification_plugin(const char *optarg);
static gboolean open_watchdog(const char * dev);
static void tickle_watchdog(void);
static void close_watchdog(void);

void apphb_client_remove(apphb_client_t* client);
static void apphb_putrc(apphb_client_t* client, int rc);
static gboolean	apphb_timer_popped(gpointer data);
static gboolean	tickle_watchdog_timer(gpointer data);
static apphb_client_t* apphb_client_new(struct IPC_CHANNEL* ch);
static int apphb_client_register(apphb_client_t* client, void* Msg, int len);
static void apphb_read_msg(apphb_client_t* client);
static int apphb_client_hb(apphb_client_t* client, void * msg, int msgsize);
void apphb_process_msg(apphb_client_t* client, void* msg,  int length);
static gboolean authenticate_client(void * clienthandle, uid_t * uidlist, gid_t* gidlist, int nuid, int ngid);

/* gmainloop "event source" functions for client communication */
static gboolean apphb_prepare(gpointer src, GTimeVal*now, gint*timeout
,	gpointer user);
static gboolean apphb_check(gpointer src, GTimeVal*now, gpointer user);
static gboolean apphb_dispatch(gpointer src, GTimeVal*now, gpointer user);

static GSourceFuncs apphb_eventsource = {
	apphb_prepare,
	apphb_check,
	apphb_dispatch,
	NULL
};

/* gmainloop "event source" functions for new client connections */
static gboolean apphb_new_prepare(gpointer src, GTimeVal*now, gint*timeout
,	gpointer user);
static gboolean apphb_new_check(gpointer src, GTimeVal*now, gpointer user);
static gboolean apphb_new_dispatch(gpointer src, GTimeVal*now, gpointer user);

static GSourceFuncs apphb_connsource = {
	apphb_new_prepare,
	apphb_new_check,
	apphb_new_dispatch,
	NULL
};

/* Send return code from current operation back to client... */
static void
apphb_putrc(apphb_client_t* client, int rc)
{
	client->rc.rc = rc;

	if (client->ch->ops->send(client->ch, &client->rcmsg) != IPC_OK) {
		client->deleteme = TRUE;
	}
	
}

/* Oops!  Client heartbeat timer expired! -- Bad client! */
static gboolean
apphb_timer_popped(gpointer data)
{
	apphb_client_t*	client = data;
	if (!client->deleteme) {
		apphb_notify(client, APPHB_NOHB);
	}
	client->missinghb = TRUE;
	client->timerid = 0;
	return FALSE;
}

/* gmainloop "event source" prepare function */
static gboolean
apphb_prepare(gpointer Src, GTimeVal*now, gint*timeout, gpointer Client)
{
	apphb_client_t*		client  = Client;

	/*
	 * We set deleteme instead of deleting clients immediately because
	 * we sometimes send replies back, and the prepare() function is
	 * a safe time to delete a client.
	 */
	if (client->deleteme) {
		/* Today is a good day to die! */
		apphb_client_remove(client);
		return FALSE;
	}
	if (debug >= DBGDETAIL) {
		 syslog(LOG_DEBUG, "apphb_prepare: client: %ld\n"
		,	(long)client->pid);
	}
	return FALSE;
}

/* gmainloop "event source" check function */
static gboolean
apphb_check(gpointer Src, GTimeVal*now, gpointer Client)
{
	GPollFD*		src = Src;
	apphb_client_t*		client  = Client;


	if (debug >= DBGDETAIL) {
		syslog(LOG_DEBUG, "apphb_check: client: %ld, revents: 0x%x"
		,	(long)client->pid, src->revents);
	}
	client->ch->ops->resume_io(client->ch);

	return src->revents != 0
	||	client->ch->ops->is_message_pending(client->ch);
}

/* gmainloop "event source" dispatch function */
static gboolean
apphb_dispatch(gpointer Src, GTimeVal* now, gpointer Client)
{
	GPollFD*		src = Src;
	apphb_client_t*		client  = Client;

	if (debug >= DBGDETAIL) {
		syslog(LOG_DEBUG, "apphb_dispatch: client: %ld, revents: 0x%x"
		,	(long)client->pid, src->revents);
	}
	if (src->revents & G_IO_HUP) {
		apphb_notify(client, APPHB_HUP);
		client->deleteme = TRUE;
		return TRUE;
	}

	client->ch->ops->resume_io(client->ch);

	while (client->ch->ops->is_message_pending(client->ch)) {
		if (client->ch->ch_status == IPC_DISCONNECT) {
			client->deleteme = TRUE;
		}else{
			apphb_read_msg(client);
		}
	}
	return TRUE;
}
#define	DEFAULT_TO	(10*60*1000)

/* Create new client (we don't know appname or pid yet) */
static apphb_client_t*
apphb_client_new(struct IPC_CHANNEL* ch)
{
	apphb_client_t*	ret;

	int	rdfd;
	int	wrfd;
	int	wrflags = G_IO_OUT|G_IO_NVAL;
	int	rdflags = G_IO_IN |G_IO_NVAL | G_IO_PRI | G_IO_HUP;

	ret = g_new(apphb_client_t, 1);

	ret->appname = NULL;
	ret->ch = ch;
	ret->timerid = 0;
	ret->pid = 0;
	ret->deleteme = FALSE;
	ret->missinghb = FALSE;

	/* Create the standard result code (errno) message to send client
	 * NOTE: this disallows multiple outstanding calls from a client
	 * (IMHO this is not a problem)
	 */
	ret->rcmsg.msg_body = &ret->rc;
	ret->rcmsg.msg_len = sizeof(ret->rc);
	ret->rcmsg.msg_done = NULL;
	ret->rcmsg.msg_private = NULL;
	ret->rc.rc = 0;

	/* Prepare GPollFDs to give g_main_add_poll() */

	wrfd = ch->ops->get_send_select_fd(ch);
	rdfd = ch->ops->get_recv_select_fd(ch);

	if (rdfd == wrfd) {
		/* We only need to poll one FD */
		/* FIXME: We ought to handle output blocking */
#if 0
		rdflags |= wrflags;
#endif
		wrflags = 0;
		ret->ofd = NULL;
	}else{
		/* We have to poll both FDs separately */
		ret->ofd = g_new(GPollFD, 1);
		ret->ofd->fd = wrfd;
		ret->ofd->events = wrflags;
		g_main_add_poll(ret->ofd, G_PRIORITY_DEFAULT);
	}
	ret->ifd = g_new(GPollFD, 1);
	ret->ifd->fd = rdfd;
	ret->ifd->events = rdflags;
	g_main_add_poll(ret->ifd, G_PRIORITY_DEFAULT);

	/* Set timer for this client... */
	ret->timerid = Gmain_timeout_add(DEFAULT_TO, apphb_timer_popped, ret);
	ret->timerms = DEFAULT_TO;

	/* Set up "real" input message source for this client */
	ret->sourceid = g_source_add(G_PRIORITY_HIGH, FALSE
	,	&apphb_eventsource, ret->ifd, ret, NULL);
	return ret;
}

/* Process client registration message */
static int
apphb_client_register(apphb_client_t* client, void* Msg,  int length)
{
	struct apphb_signupmsg*	msg = Msg;
	int			namelen = -1;
	uid_t			uidlist[1];
	gid_t			gidlist[1];
	IPC_Auth*		clientauth;
	int			j;

	if (client->appname) {
		return EEXIST;
	}

	if (length < sizeof(*msg)
	||	(namelen = strnlen(msg->appname, sizeof(msg->appname))) < 1
	||	namelen >= sizeof(msg->appname)
	||	strnlen(msg->appinstance, sizeof(msg->appinstance))
	>=	sizeof(msg->appinstance)) {
		return EINVAL;
	}

	if (msg->pid < 2 || (kill(msg->pid, 0) < 0 && errno != EPERM)
	||	(client->ch->farside_pid != msg->pid)) {
		return EINVAL;
	}

	client->pid = msg->pid;

	/* Make sure client is who they claim to be... */
	uidlist[0] = msg->uid;
	gidlist[0] = msg->gid;
	clientauth = ipc_set_auth(uidlist, gidlist, 1, 1);
	if (client->ch->ops->verify_auth(client->ch, clientauth) != IPC_OK) {
		ipc_destroy_auth(clientauth);
		return EINVAL;
	}
	ipc_destroy_auth(clientauth);
	client->appname = g_strdup(msg->appname);
	client->appinst = g_strdup(msg->appinstance);
	client->uid = msg->uid;
	client->gid = msg->gid;

	if (debug >= DBGMIN) {
		syslog(LOG_DEBUG
		,	"apphb_client_register: client: [%s]/[%s] pid %ld"
		" (uid,gid) = (%ld,%ld)\n"
		,	client->appname
		,	client->appinst
		,	(long)client->pid
		,	(long)client->uid
		,	(long)client->gid);
	}

	/* Tell the plugins something happened */
	for (j=0; j < n_Notification_Plugins; ++j) {
		NotificationPlugins[j]->cregister(client->pid
		,	client->appname, client->appinst
		,	client->uid, client->gid, client);
	}
	return 0;
}


/* Shut down the requested client */
void
apphb_client_remove(apphb_client_t* client)
{
	if (debug >= DBGMIN) {
		syslog(LOG_DEBUG, "apphb_client_remove: client: %ld\n"
		,	(long)client->pid);
	}
	if (client->sourceid) {
		g_source_remove(client->sourceid);
		client->sourceid=0;
	}
	if (client->timerid) {
		g_source_remove(client->timerid);
		client->timerid=0;
	}
	if (client->ifd) {
		g_main_remove_poll(client->ifd);
		g_free(client->ifd);
		client->ifd=NULL;
	}
	if (client->ofd) {
		g_main_remove_poll(client->ofd);
		g_free(client->ofd);
		client->ofd=NULL;
	}
	if (client->ch) {
		client->ch->ops->destroy(client->ch);
		client->ch = NULL;
	}
	g_free(client->appname);
	g_free(client->appinst);
	memset(client, 0, sizeof(*client));
}

/* Client requested disconnect */
static int
apphb_client_disconnect(apphb_client_t* client , void * msg, int msgsize)
{
	/* We can't delete it right away... */
	client->deleteme=TRUE;
	apphb_notify(client, APPHB_HBUNREG);
	return 0;
}

/* Client requested new timeout interval */
static int
apphb_client_set_timeout(apphb_client_t* client, void * Msg, int msgsize)
{
	struct apphb_msmsg*	msg = Msg;

	if (msgsize < sizeof(*msg) || msg->ms < 0) {
		return EINVAL;
	}
	client->timerms = msg->ms;
	return apphb_client_hb(client, Msg, msgsize);
}

/* Client heartbeat received */
static int
apphb_client_hb(apphb_client_t* client, void * Msg, int msgsize)
{
	if (client->missinghb) {
		apphb_notify(client, APPHB_HBAGAIN);
		client->missinghb = FALSE;
	}
		
	if (client->timerid) {
		g_source_remove(client->timerid);
		client->timerid = 0;
	}
	if (client->timerms > 0) {
		client->timerid = Gmain_timeout_add(client->timerms
		,	apphb_timer_popped, client);
	}
	return 0;
}


/* Read and process a client request message */
static void
apphb_read_msg(apphb_client_t* client)
{
	struct IPC_MESSAGE*	msg = NULL;
	
	switch (client->ch->ops->recv(client->ch, &msg)) {

		case IPC_OK:
		apphb_process_msg(client, msg->msg_body, msg->msg_len);
		if (msg->msg_done) {
			msg->msg_done(msg);
		}
		break;


		case IPC_BROKEN:
		client->deleteme = TRUE;
		break;


		case IPC_FAIL:
		syslog(LOG_CRIT, "OOPS! client %s (pid %d) read failure! [%s]"
		,	client->appname, client->pid
		,	strerror(errno));
		break;
	}
}

/*
 * Mappings between commands and strings
 */
struct hbcmd {
	const char *	msg;
	gboolean	senderrno;
	int		(*fun)(apphb_client_t* client, void* msg, int len);
};

/*
 * Put HEARTBEAT message first - it is by far the most common message...
 */
struct hbcmd	hbcmds[] =
{
	{HEARTBEAT,	FALSE, apphb_client_hb},
	{REGISTER,	TRUE, apphb_client_register},
	{SETINTERVAL,	TRUE, apphb_client_set_timeout},
	{UNREGISTER,	TRUE, apphb_client_disconnect},
};

/* Process a message from an app heartbeat client process */
void
apphb_process_msg(apphb_client_t* client, void* Msg,  int length)
{
	struct apphb_msg *	msg = Msg;
	const int		sz1	= sizeof(msg->msgtype)-1;
	int			rc	= EINVAL;
	gboolean		sendrc	= TRUE;
	int			j;


	if (length < sizeof(*msg)) {
		return;
	}

	msg->msgtype[sz1] = EOS;

	/* Which command are we processing? */

	for (j=0; j < DIMOF(hbcmds); ++j) {
		if (strcmp(msg->msgtype, hbcmds[j].msg) == 0) {
			sendrc = hbcmds[j].senderrno;

			if (client->appname == NULL
			&&	hbcmds[j].fun != apphb_client_register) {
				rc = ESRCH;
				break;
			}

			rc = hbcmds[j].fun(client, Msg, length);
		}
	}
	if (sendrc) {
		apphb_putrc(client, rc);
	}
}

/* gmainloop client connection source "prepare" function */
static gboolean
apphb_new_prepare(gpointer src, GTimeVal*now, gint*timeout
,	gpointer user)
{
	if (debug >= DBGDETAIL) {
		syslog(LOG_DEBUG, "apphb_new_prepare");
	}
	return FALSE;
}

/* gmainloop client connection source "check" function */
static gboolean
apphb_new_check(gpointer Src, GTimeVal*now, gpointer user)
{
	GPollFD*	src = Src;
	if (debug >= DBGDETAIL) {
		syslog(LOG_DEBUG, "apphb_new_check: revents: 0x%x"
		,	src->revents);
	}
	return src->revents != 0;
}

/* gmainloop client connection "dispatch" function */
/* This is where we accept connections from a new client */
static gboolean
apphb_new_dispatch(gpointer Src, GTimeVal*now, gpointer user)
{
	struct IPC_WAIT_CONNECTION*		conn = user;
	struct IPC_CHANNEL*			newchan;
	GPollFD*				src = Src;

	if (debug >= DBGMIN) {
		syslog(LOG_DEBUG, "apphb_new_dispatch: revents: 0x%x"
		,	src->revents);
	}
	newchan = conn->ops->accept_connection(conn, NULL);
	if (newchan != NULL) {
		/* This sets up comm channel w/client
		 * Ignoring the result value is OK, because
		 * the client registers itself w/event system.
		 */
		(void)apphb_client_new(newchan);
	}else{
		perror("accept_connection failed!");
	}
	return TRUE;
}

/*
 * This function is called whenever a heartbeat event occurs.
 * It could be replaced by a function which called the appropriate
 * set of plugins to distribute the notification along to whomever
 * is interested in whatever way is desired.
 */
static void
apphb_notify(apphb_client_t* client, apphb_event_t event)
{
	int	logtype = LOG_WARNING;
	const char *	msg;
	int		j;


	switch(event) {
	case	APPHB_HUP:
		msg = "hangup";
		logtype = LOG_ERR;
		break;

	case	APPHB_NOHB:
		msg = "failed to heartbeat";
		logtype = LOG_WARNING;
		break;

	case	APPHB_HBAGAIN:
		msg = "resumed heartbeats";
		logtype = LOG_INFO;
		break;

	case	APPHB_HBUNREG:
		msg = "unregistered";
		logtype = LOG_INFO;
		break;

	default:
		return;
	}
	if (event != APPHB_HBUNREG) {
		syslog(logtype, "apphb client '%s' / '%s' (pid %d) %s"
		,	client->appname, client->appinst, client->pid, msg);
	}
	
	/* Tell the plugins something happened */
	for (j=0; j < n_Notification_Plugins; ++j) {
		NotificationPlugins[j]->status(client->appname
		,	client->appinst, client->pid
		,	client->uid, client->gid, event);
	}
}

extern pid_t getsid(pid_t);

/*
 *	Main program for monitoring application heartbeats...
 */
GMainLoop*	mainloop = NULL;

#define OPTARGS	"sSrRwd:n:"
int
main(int argc, char ** argv)
{
	int		flag;
	const char *	watchdogdev = NULL;

	cmdname = argv[0];

	if (argc < 2) {
		return init_start();
	}

	while ((flag = getopt(argc, argv, OPTARGS)) != EOF) {
		switch(flag) {
			case 's':		/* Status */
			return init_status();

			case 'S':		/* Stop */
			return init_stop();

			case 'r':		/* Restart */
			return init_restart();

			case 'w':		/* Watchdog dev */
			watchdogdev = optarg;
			break;

			case 'n':		/* Plugin */
			load_notification_plugin(optarg);
			break;

			case 'd':		/* Debug */
			debug += 1;
		}
	}

	if (watchdogdev) {
		open_watchdog(watchdogdev);
	}
	return init_start();
}

static void
shutdown(int nsig)
{
	static int	shuttingdown = 0;
	signal(nsig, shutdown);

	if (!shuttingdown) {
		/* Let the watchdog get us if we can't shut down */
		tickle_watchdog();
		shuttingdown = 1;
	}
	g_main_quit(mainloop);
}


static int
init_start(void)
{
	char		path[] = PATH_ATTR;
	char		commpath[] = APPHBSOCKPATH;

	struct IPC_WAIT_CONNECTION*	wconn;
	GHashTable*	wconnattrs;

	int		wcfd;
	GPollFD		pollfd;
	
	openlog("apphbd", LOG_NDELAY|LOG_NOWAIT|LOG_PID, LOG_USER);

	make_daemon();
	/* Create a "waiting for connection" object */

	wconnattrs = g_hash_table_new(g_str_hash, g_str_equal);

	g_hash_table_insert(wconnattrs, path, commpath);

	wconn = ipc_wait_conn_constructor(IPC_ANYTYPE, wconnattrs);

	if (wconn == NULL) {
		syslog(LOG_CRIT, "UhOh! Failed to create wconn!");
		exit(1);
	}

	/* Set up GPollFD to watch for new connection events... */
	wcfd = wconn->ops->get_select_fd(wconn);
	pollfd.fd = wcfd;
	pollfd.events = G_IO_IN | G_IO_NVAL | G_IO_PRI | G_IO_HUP;
	pollfd.revents = 0;
	g_main_add_poll(&pollfd, G_PRIORITY_DEFAULT);

	/* Create a source to handle new connection requests */
	g_source_add(G_PRIORITY_HIGH, FALSE
	,	&apphb_connsource, &pollfd, wconn, NULL);


	/* Create the mainloop and run it... */
	mainloop = g_main_new(FALSE);
	syslog(LOG_INFO, "Starting %s", cmdname);
	if (watchdogfd >= 0) {
		Gmain_timeout_add(1000, tickle_watchdog_timer, NULL);
	}
	g_main_run(mainloop);
	close_watchdog();
	wconn->ops->destroy(wconn);
	unlink(PIDFILE);
	return 0;
}

static void
make_daemon(void)
{
	int	j;
	long	pid;
	FILE *	lockfd;

	if ((pid = get_running_pid(NULL)) > 0) {
		fprintf(stderr, "%s: already running: [pid %ld].\n"
		,	cmdname, pid);
		syslog(LOG_CRIT, "already running: [pid %ld]."
		,	pid);
		close_watchdog();
		exit(1);
	}

	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "%s: cannot start daemon.\n", cmdname);
		syslog(LOG_CRIT, "cannot start daemon.\n");
		exit(1);
	}else if (pid > 0) {
		exit(0);
	}

	lockfd = fopen(PIDFILE, "w");
	if (lockfd == NULL) {
		fprintf(stderr,  "%s: cannot create pid file\n" PIDFILE
		,	cmdname);
		syslog(LOG_CRIT, "cannot create pid file" PIDFILE);
		exit(1);
	}else{
		pid = getpid();
		fprintf(lockfd, "%ld\n", pid);
		fclose(lockfd);
	}

	umask(022);
	getsid(0);
	for (j=0; j < 3; ++j) {
		close(j);
		(void)open("/dev/null", j == 0 ? O_RDONLY : O_RDONLY);
	}

	IGNORESIG(SIGINT);
	IGNORESIG(SIGHUP);
	signal(SIGTERM, shutdown);
}

static long
get_running_pid(gboolean* anypidfile)
{
	long    pid;
	FILE *  lockfd;
	lockfd = fopen(PIDFILE, "r");

	if (anypidfile) {
		*anypidfile = (lockfd != NULL);
	}

	if (lockfd != NULL
	&&      fscanf(lockfd, "%ld", &pid) == 1 && pid > 0) {
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

static int
init_stop(void)
{
	long	pid;
	pid =	get_running_pid(NULL);

	if (pid > 0) {
		if (kill((pid_t)pid, SIGTERM) < 0) {
			fprintf(stderr, "Cannot kill pid %ld\n", pid);
			exit(1);
		}
	}
	return 0;
}
static int
init_restart(void)
{
	init_stop();
	return init_start();
}
static int
init_status(void)
{
	gboolean	anypidfile;
	long	pid =	get_running_pid(&anypidfile);

	if (pid > 0) {
		fprintf(stderr, "%s is running [pid: %ld]\n"
		,	cmdname, pid);
		return 0;
	}
	if (anypidfile) {
		fprintf(stderr, "%s is stopped [pidfile exists]\n"
		,	cmdname);
		return 1;
	}
	fprintf(stderr, "%s is stopped.\n", cmdname);
	return 3;
}

/*
 *	Notification plugin imported functions:
 *		authenticate_client(void * handle, uidlist, gidlist)
 *			This returns TRUE if the app at apphandle
 *			properly authenticates according to the gidlist
 *			and the uidlist.
 */

static gboolean
authenticate_client(void * clienthandle, uid_t * uidlist, gid_t* gidlist
,	int nuid, int ngid)
{
	struct apphb_client*	client = clienthandle;
	struct IPC_AUTH*	auth;
	struct IPC_CHANNEL*	ch;
	gboolean		rc = FALSE;


	if ((auth = ipc_set_auth(uidlist, gidlist, nuid, ngid)) == NULL) {
		return FALSE;
	}

	if (client != NULL && (ch = client->ch) != NULL) {
		rc =  ch->ops->verify_auth(ch, auth) == IPC_OK;
	}
	ipc_destroy_auth(auth);
	return rc;
}


static const char *	watchdogdev = NULL;

static gboolean
open_watchdog(const char * dev)
{

 	if (watchdogfd >= 0 || watchdogdev == NULL) {
		syslog(LOG_WARNING, "Watchdog device already open.");
		return FALSE;
	}
	watchdogfd = open(dev, O_WRONLY);
	if (watchdogfd >= 0) {
		if (fcntl(watchdogfd, F_SETFD, FD_CLOEXEC)) {
			syslog(LOG_WARNING, "Error setting the "
			"close-on-exec flag for watchdog");
		}
		syslog(LOG_NOTICE, "Using watchdog device: %s"
		,       watchdogdev);
		tickle_watchdog();
		watchdogdev = dev;
		return TRUE;
	}else{
		syslog(LOG_ERR, "Cannot open watchdog device: %s"
		,       watchdogdev);
	}
	return FALSE;
}

static void
close_watchdog(void)
{
	if (watchdogfd >= 0) {
		if (write(watchdogfd, "V", 1) != 1) {
			syslog(LOG_CRIT
			,	"Watchdog write magic character failure:"
			" closing %s!\n"
			,       watchdogdev);
		}
		close(watchdogfd);
		watchdogfd=-1;
	}
}

static void
tickle_watchdog(void)
{
	if (watchdogfd >= 0) {
		if (write(watchdogfd, "", 1) != 1) {
			syslog(LOG_CRIT
			,	"Watchdog write failure: closing %s!\n"
			,       watchdogdev);
			close_watchdog();
			watchdogfd=-1;
		}
	}
}

static gboolean
tickle_watchdog_timer(gpointer data)
{
	tickle_watchdog();
	return TRUE;
}

static PILPluginUniv*	pisys = NULL;
static GHashTable*	Notifications = NULL;


AppHBNotifyImports	piimports = {
	authenticate_client
};

static PILGenericIfMgmtRqst RegistrationRqsts [] =
{	{"AppHBNotification", &Notifications, &piimports, NULL, NULL},
	{NULL,			NULL,		NULL,     NULL, NULL}
};

static void
load_notification_plugin(const char * pluginname)
{
	PIL_rc	rc;
	void*	exports;
	if (pisys == NULL) {
		pisys = NewPILPluginUniv("/usr/lib/heartbeat/plugins");
		if (pisys == NULL) {
			return;
		}
		if ((rc = PILLoadPlugin(pisys, "InterfaceMgr", "generic"
		,	&RegistrationRqsts)) !=	PIL_OK) {

			syslog(LOG_ERR
			,       "ERROR: cannot load generic interface manager"
		       " [%s/%s]: %s"
			,       "InterfaceMgr", "generic"
			,       PIL_strerror(rc));
			return;
		}
	}
	rc = PILLoadPlugin(pisys, "AppHBNotification"
	,        pluginname, NULL);
	if (rc != PIL_OK) {
		syslog(LOG_ERR, "cannot load plugin %s", pluginname);
		return;
	}
	if ((exports = g_hash_table_lookup(Notifications, pluginname))
	== NULL) {
		syslog(LOG_ERR, "cannot find plugin %s", pluginname);
		return;
	}
	NotificationPlugins[n_Notification_Plugins] = exports;
	n_Notification_Plugins ++;
}
