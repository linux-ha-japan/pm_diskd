/* 
 * ccm.c: Consensus Cluster Service Program 
 *
 * Copyright (c) International Business Machines  Corp., 2002
 * Author: Ram Pai (linuxram@us.ibm.com)
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
#include <ccm.h>

#define SECOND   1000
#define OPTARGS  "dv"

int global_debug=0;
int global_verbose=0;

/*
 * hearbeat event source.
 *   
 */
static gboolean hb_input_prepare(gpointer source_data
		,       GTimeVal* current_time
		,       gint* timeout, gpointer user_data);
static gboolean hb_input_check(gpointer source_data
		,       GTimeVal* current_time
		,       gpointer user_data);
static gboolean hb_input_dispatch(gpointer source_data
		,       GTimeVal* current_time
		,       gpointer user_data);
static void hb_input_destroy(gpointer user_data);

static GSourceFuncs hb_input_SourceFuncs = {
        hb_input_prepare,
        hb_input_check,
        hb_input_dispatch,
        hb_input_destroy,
};

typedef struct hb_srcdata_s {
	GPollFD 	hbpoll;
	longclock_t  	t;
	char		timeflag;
	GMainLoop	*mainloop;
} hb_srcdata_t;

static gboolean
hb_input_prepare(gpointer source_data, GTimeVal* current_time
		,       gint* timeout, gpointer user_data)
{
	hb_srcdata_t *sdata = (hb_srcdata_t *)source_data;

	(void)_heartbeat_h_Id; /* Make compiler happy */
	(void)_ha_msg_h_Id; /* Make compiler happy */

	if(!sdata->timeflag){
		sdata->t = ccm_get_time();
		sdata->timeflag = TRUE;
	}
	*timeout = 1*SECOND;
	return FALSE;
}

static gboolean
hb_input_check(gpointer source_data, GTimeVal* current_time
		,       gpointer        user_data)
{

	hb_srcdata_t *sdata = (hb_srcdata_t *)source_data;
	GPollFD        *hbpoll =  (GPollFD *) &(sdata->hbpoll);

	/* return true only if there is a pending event or
	 * if it is more the one second since the last
	 * run of ccm algorithm. 
	 */
	if ((hbpoll->revents != 0) || 
			ccm_timeout(sdata->t, ccm_get_time(), 1)){
		sdata->timeflag = FALSE;
		return TRUE;
	}
	return FALSE;
}

static gboolean
hb_input_dispatch(gpointer source_data, GTimeVal* current_time
	,       gpointer        user_data)
{
	void  *ccmuser = (void *)user_data;
	hb_srcdata_t *sdata = (hb_srcdata_t *)source_data;

	int ret = ccm_take_control(ccmuser);
	if (ret)  {
		// TOBEDONE: remove all the sources from the
		// event loop before quiting.
		g_main_quit(sdata->mainloop);
		return FALSE;
	}
	return TRUE;
}

static void
hb_input_destroy(gpointer user_data)
{
	// close connections to all the clients
	client_delete_all();
	return;
}



/*
 * client messaging  events sources...
 *   
 */
static gboolean clntCh_input_prepare(gpointer source_data
		,       GTimeVal* current_time
		,       gint* timeout, gpointer user_data);
static gboolean clntCh_input_check(gpointer source_data
		,       GTimeVal* current_time
		,       gpointer user_data);
static gboolean clntCh_input_dispatch(gpointer source_data
		,       GTimeVal* current_time
		,       gpointer user_data);
static void clntCh_input_destroy(gpointer user_data);

static GSourceFuncs clntCh_input_SourceFuncs = {
        clntCh_input_prepare,
        clntCh_input_check,
        clntCh_input_dispatch,
        clntCh_input_destroy,
};

static gboolean
clntCh_input_prepare(gpointer source_data, GTimeVal* current_time
		,       gint* timeout, gpointer user_data)
{
	*timeout = 10*SECOND;
	return FALSE;
}

static gboolean
clntCh_input_check(gpointer source_data, GTimeVal* current_time
		,       gpointer        user_data)
{
	GPollFD*	gpfd = source_data;
        if(gpfd->revents != 0 || (gpfd+1)->revents !=0){
		return TRUE;
	}
	return FALSE;
}


static gboolean
clntCh_input_dispatch(gpointer source_data, GTimeVal* current_time
	,       gpointer        user_data)
{
	GPollFD*	gpfd = source_data;
	struct IPC_CHANNEL *client = 
			(struct IPC_CHANNEL *)user_data;

	if(gpfd->revents&G_IO_HUP || (gpfd+1)->revents&G_IO_HUP) {
		cl_log(LOG_INFO, "dispatch:received HUP");
		client_delete(client);
		g_main_remove_poll(gpfd);
		g_main_remove_poll(gpfd+1);
		g_source_remove_by_source_data(source_data);
		g_free(gpfd);
	}
	return TRUE; /* TOBEDONE */
}


static void
clntCh_input_destroy(gpointer user_data)
{
	return;
}



/*
 * client connection events source..
 *   
 */
static gboolean waitCh_input_prepare(gpointer source_data
		,       GTimeVal* current_time
		,       gint* timeout, gpointer user_data);
static gboolean waitCh_input_check(gpointer source_data
		,       GTimeVal* current_time
		,       gpointer user_data);
static gboolean waitCh_input_dispatch(gpointer source_data
		,       GTimeVal* current_time
		,       gpointer user_data);
static void waitCh_input_destroy(gpointer user_data);

static GSourceFuncs waitCh_input_SourceFuncs = {
        waitCh_input_prepare,
        waitCh_input_check,
        waitCh_input_dispatch,
        waitCh_input_destroy,
};

static gboolean
waitCh_input_prepare(gpointer source_data, GTimeVal* current_time
		,       gint* timeout, gpointer user_data)
{
	*timeout = 10*SECOND;
	return FALSE;
}

static gboolean
waitCh_input_check(gpointer source_data, GTimeVal* current_time
		,       gpointer        user_data)
{
	GPollFD*	gpfd = source_data;
        return		gpfd->revents != 0;
}

static gboolean
waitCh_input_dispatch(gpointer source_data, GTimeVal* current_time
	,       gpointer        user_data)
{
	struct IPC_WAIT_CONNECTION *wait_ch = 
			(struct IPC_WAIT_CONNECTION *)user_data;
	struct IPC_CHANNEL *newclient;
	GPollFD		*newsource;


 	if ((newclient = wait_ch->ops->accept_connection(wait_ch, 
			NULL)) != NULL) {
		/* accept the connection */
		if(global_verbose)
			cl_log(LOG_DEBUG,"accepting a new connection");
		/* inform our client manager about this new client */
		client_add(newclient);

		/* add this source to the event loop */
		if((newsource = (GPollFD *) g_malloc(2*sizeof(GPollFD))) 
			== NULL) {
			cl_log(LOG_ERR,"waitCh_input_dispatch: not enough memory");
			return FALSE;
		}
		newsource->fd = newclient->ops->get_send_select_fd(newclient);
		newsource->events = G_IO_IN|G_IO_HUP|G_IO_ERR;
		g_main_add_poll(newsource, G_PRIORITY_LOW);
		(newsource+1)->fd = newclient->ops->get_recv_select_fd(newclient);
		(newsource+1)->events = G_IO_IN|G_IO_HUP|G_IO_ERR;
		g_main_add_poll((newsource+1), G_PRIORITY_LOW);
		g_source_add(G_PRIORITY_LOW, FALSE, &clntCh_input_SourceFuncs
		       , newsource, newclient, NULL);
	}
	return TRUE;
}

static void
waitCh_input_destroy(gpointer user_data)
{
	struct IPC_WAIT_CONNECTION *wait_ch = 
			(struct IPC_WAIT_CONNECTION *)user_data;

	wait_ch->ops->destroy(wait_ch);
	return;
}

static struct IPC_WAIT_CONNECTION *
wait_channel_init(void)
{
	struct IPC_WAIT_CONNECTION *wait_ch;
	mode_t mask;
	char path[] = PATH_ATTR;
	char ccmfifo[] = CCMFIFO;
	char domainsocket[] = IPC_DOMAIN_SOCKET;

	GHashTable * attrs = g_hash_table_new(g_str_hash,g_str_equal);
	g_hash_table_insert(attrs, path, ccmfifo);

	mask = umask(0);
	wait_ch = ipc_wait_conn_constructor(domainsocket, attrs);
	if (wait_ch == NULL){
		cl_perror("Can't create wait channel");
		exit(1);
	}
	mask = umask(mask);

	g_hash_table_destroy(attrs);

	return wait_ch;
}

static void
usage(const char *cmd)
{
	fprintf(stderr, "\nUsage: %s [-dv]\n", cmd);
}


//
// debug facilitator.
//
static void
ccm_debug(int signum) 
{
	switch(signum) {
	case SIGUSR1:
		global_debug = !global_debug;
		break;
	case SIGUSR2:
		global_verbose = !global_verbose;
		break;
	}
}


//
// The main function!
//
int
main(int argc, char **argv)
{
	GMainLoop*	mainloop;
	hb_srcdata_t	srcdata;
	void		*ccmdata;

	GPollFD		waitchan;
	struct IPC_WAIT_CONNECTION *wait_ch;

	char *cmdname;
	char *tmp_cmdname = strdup(argv[0]);
	int  flag;

	if ((cmdname = strrchr(tmp_cmdname, '/')) != NULL) {
		++cmdname;
	} else {
		cmdname = tmp_cmdname;
	}
	cl_log_enable_stderr(TRUE);
	cl_log_set_entity(cmdname);
	cl_log_set_facility(LOG_DAEMON);
	while ((flag = getopt(argc, argv, OPTARGS)) != EOF) {
		switch(flag) {
			case 'v':
				global_verbose = 1;
				break;
			case 'd': 
				global_debug = 1;
				break;
			default:
				usage(cmdname);
				return 1;
		}
	}


	signal(SIGUSR1, ccm_debug);
	signal(SIGUSR2, ccm_debug);
	IGNORESIG(SIGPIPE);

	/* initialize the client tracking system */
	client_init();

	mainloop = g_main_new(TRUE);

	/* 
	 * heartbeat is the main source of events. 
	 * This source must be listened 
	 * at high priority 
	 */
	ccmdata = ccm_initialize();
	if(!ccmdata) {
		exit(1);
	}
	srcdata.hbpoll.fd  = 	ccm_get_fd(ccmdata);
        srcdata.hbpoll.events = G_IO_IN|G_IO_HUP|G_IO_ERR;
	srcdata.timeflag  = FALSE;
	srcdata.mainloop  = mainloop;
        g_main_add_poll(&srcdata.hbpoll, G_PRIORITY_HIGH);
	g_source_add(G_PRIORITY_HIGH, FALSE, &hb_input_SourceFuncs
		       , &srcdata, ccmdata, NULL);


	/* the clients wait channel is the other source of events.
	 * This source delivers the clients connection events.
	 * listen to this source at a relatively lower priority.
	 */
	wait_ch = wait_channel_init();
	waitchan.fd = wait_ch->ops->get_select_fd(wait_ch);
	waitchan.events = G_IO_IN|G_IO_HUP|G_IO_ERR;
	g_main_add_poll(&waitchan, G_PRIORITY_LOW);
	g_source_add(G_PRIORITY_LOW, FALSE, &waitCh_input_SourceFuncs
		       , &waitchan, wait_ch, NULL);
	
	cl_log_enable_stderr(FALSE);
	g_main_run(mainloop);
	g_main_destroy(mainloop);

	free(tmp_cmdname);
	/*this program should never terminate,unless killed*/
	return(1);
}
