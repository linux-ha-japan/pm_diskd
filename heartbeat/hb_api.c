/*
 * hb_api: Server-side heartbeat API code
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 * Copyright (C) 2000 Marcelo Tosatti <marcelo@conectiva.com.br>
 * Copyright (C) 2000 Conectiva S.A. 
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
 *	A little about the API FIFO structure...
 *
 *	We have two kinds of API clients:  casual and named
 *
 *	Casual clients just attach and listen in to messages, and ask
 *	the status of things. Casual clients are typically used as status
 *	agents, or debugging agents.
 *
 *	They can't send messages, and they are known only by their PID.
 *	Anyone in the group that owns the casual FIFO directory can use
 *	the casual API.  Casual clients create and delete their own
 *	FIFOs for the API (or are cleaned up after by heartbeat ;-))
 *	Hence, the casual client FIFO directory must be group writable,
 *	and sticky.
 *
 *	Named clients attach and listen in to messages, and they are also
 *	allowed to send messages to the other clients in the cluster with
 *	the same name. Named clients typically provide persistent services
 *	in the cluster.  A cluster manager would be an example
 *	of such a persistent service.
 *
 *	Their FIFOs are pre-created for them, and they neither create nor
 *	delete them - nor should they be able to.
 *	The named client FIFO directory must not be writable by group or other.
 *
 *	We deliver messages from named clients to clients in the cluster
 *	which are registered with the same name.  Each named client
 *	also receives the messages it sends.  I could allow them to send
 *	to any other service that they want, but right now that's overridden.
 *	We mark each packet with the service name that the packet came from.
 *
 *	A client can only register for a given name if their userid is the
 *	owner of the named FIFO for that name.
 *
 *	If a client has permissions to snoop on packets (debug mode),
 *	then they are allowed to receive all packets, but otherwise only
 *	clients registered with the same name will receive these messages.
 *
 *	It is important to make sure that each named client FIFO is owned by the
 *	same UID on each machine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <heartbeat.h>
#include <ha_msg.h>
#include <hb_api.h>
#include <hb_api_core.h>
#include <sys/stat.h>
#include <sys/time.h>

struct api_query_handler query_handler_list [] = {
	{ API_SIGNOFF, api_signoff },
	{ API_SETFILTER, api_setfilter },
	{ API_SETSIGNAL, api_setsignal },
	{ API_NODELIST, api_nodelist },
	{ API_NODESTATUS, api_nodestatus },
	{ API_IFSTATUS, api_ifstatus },
	{ API_IFLIST, api_iflist },
};

int		debug_client_count = 0;
int		total_client_count = 0;
client_proc_t*	client_list = NULL;	/* List of all our API clients */

void api_process_request(client_proc_t* client, struct ha_msg *msg);
static void api_send_client_msg(client_proc_t* client, struct ha_msg *msg);
static void api_remove_client(client_proc_t* client);
static void api_add_client(struct ha_msg* msg);
static client_proc_t*	find_client(const char * fromid, const char * pid);
static FILE*		open_reqfifo(client_proc_t* client);
static const char *	client_fifo_name(client_proc_t* client, int isreq);
static	uid_t		pid2uid(pid_t pid);
static int		ClientSecurityIsOK(client_proc_t* client);
static int		HostSecurityIsOK(void);

#define	MAXFD	64
#if  (MAXFD > FD_SETSIZE)
#	undef MAXFD
#	define	MAXFD	FD_SETSIZE
#endif

/*
 *	One client pointer per input FIFO.  It's indexed by file descriptor, so
 *	it's not densely populated.  We use this in conjunction with select(2)
 */
static client_proc_t*	FDclients[MAXFD];

/*
 * The original structure of this code was due to
 * Marcelo Tosatti <marcelo@conectiva.com.br>
 *
 * It has been significantly and repeatedly mangled into unrecognizable oblivion
 * by Alan Robertson <alanr@unix.sh>
 *
 */

/*
 *	Monitor messages.  Pass them along to interested clients (if any)
 */
void
api_heartbeat_monitor(struct ha_msg *msg, int msgtype, const char *iface)
{
	const char*	clientid;
	client_proc_t*	client;
	client_proc_t*	nextclient;

	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;


	/* This kicks out most messages, since debug clients are rare */

	if ((msgtype&DEBUGTREATMENTS) != 0 && debug_client_count <= 0) {
		return;
	}

	/* Verify that we understand what kind of message we've got here */

	if ((msgtype & ALLTREATMENTS) != msgtype || msgtype == 0) {
		ha_log(LOG_ERR, "heartbeat_monitor: unknown msgtype [%d]"
		,	msgtype);
		return;
	}

	/* See who this message is addressed to (if anyone) */

	clientid = ha_msg_value(msg, F_TOID);

	for (client=client_list; client != NULL; client=nextclient) {
		/*
		 * "client" might be removed by api_send_client_msg()
		 * so, we'd better fetch the next client now!
		 */
		nextclient=client->next;
	
		/* Is this message addressed to us? */
		if (clientid != NULL
		&&	strcmp(clientid, client->client_id) != 0) {
			continue;
		}

		/* Is this one of the types of messages we're interested in? */

		if ((msgtype & client->desired_types) != 0) {
			api_send_client_msg(client, msg);
		}

		/* If this is addressed to us, then no one else should get it */
		if (clientid != NULL) {
			break;	/* No one else should get it */
		}
	}
}
/*
 *	Periodically clean up after dead clients...
 */
void
api_audit_clients(void)
{
	static clock_t	audittime = 0L;
	static clock_t	lastnow = 0L;
	clock_t		now;
	client_proc_t*	client;
	client_proc_t*	nextclient;


	/* Allow for clock wraparound */
	now = times(NULL);
	if (now > lastnow && now < audittime) {
		lastnow = now;
		return;
	}

	lastnow = now;
	audittime = now + (CLK_TCK * 10); /* Every 10 seconds */

	for (client=client_list; client != NULL; client=nextclient) {
		nextclient=client->next;


		if (kill(client->pid, 0) < 0 && errno == ESRCH) {
			ha_log(LOG_ERR, "api_audit_clients: client %d died"
			,	client->pid);
			api_remove_client(client);
			client=NULL;
		}
	}
}


/**********************************************************************
 * API_SETFILTER: Set the types of messages we want to see
 **********************************************************************/
int api_setfilter(const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char **failreason)
{                                                            
	const char *	cfmask;
	unsigned	mask;
/*
 *	Record the types of messages desired by this client
 *		(desired_types)
 */
	if ((cfmask = ha_msg_value(msg, F_FILTERMASK)) == NULL
	||	(sscanf(cfmask, "%x", &mask) != 1)
	||	(mask&ALLTREATMENTS) == 0) {
		*failreason = "EINVAL";
		return I_API_BADREQ;
	}

	if ((client->desired_types  & DEBUGTREATMENTS)== 0
	&&	(mask&DEBUGTREATMENTS) != 0) {

		/* Only allowed to root and to our uid */
		if (client->uid != 0 && client->uid != getuid()) {
			*failreason = "EPERM";
			return I_API_BADREQ;
		}
		++debug_client_count;
	}else if ((client->desired_types & DEBUGTREATMENTS) != 0
	&&	(mask & DEBUGTREATMENTS) == 0) {
		--debug_client_count;
	}
	client->desired_types = mask;
	return I_API_RET;
}

/**********************************************************************
 * API_SIGNOFF: Sign off as a client
 **********************************************************************/

int api_signoff(const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char **failreason) 
{ 
		/* We send them no reply */
		ha_log(LOG_INFO, "Signing client %d off", client->pid);
		api_remove_client(client);
		return I_API_IGN;
}

/**********************************************************************
 * API_SETSIGNAL: Record the type of signal they want us to send.
 **********************************************************************/

int api_setsignal(const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason)
{
		const char *	csignal;
		unsigned	oursig;

		if ((csignal = ha_msg_value(msg, F_SIGNAL)) == NULL
		||	(sscanf(csignal, "%u", &oursig) != 1)) {
			return I_API_BADREQ;
		}
		/* Validate the signal number in the message ... */
		if (oursig < 0 || oursig == SIGKILL || oursig == SIGSTOP
		||	oursig >= 32) {
			/* These can't be caught (or is a bad signal). */
			*failreason = "EINVAL";
			return I_API_BADREQ;
		}

		client->signal = oursig;
		return I_API_RET;
}

/***********************************************************************
 * API_NODELIST: List the nodes in the cluster
 **********************************************************************/

int api_nodelist(const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason)
{
		int	j;
		int	last = config->nodecount-1;

		for (j=0; j <= last; ++j) {
			if (ha_msg_mod(resp, F_NODENAME
			,	config->nodes[j].nodename) != HA_OK) {
				ha_log(LOG_ERR
				,	"api_nodelist: "
				"cannot mod field/5");
				return I_API_IGN;
			}
			if (ha_msg_mod(resp, F_APIRESULT
			,	(j == last ? API_OK : API_MORE))
			!=	HA_OK) {
				ha_log(LOG_ERR
				,	"api_nodelist: "
				"cannot mod field/6");
				return I_API_IGN;
			}
			api_send_client_msg(client, resp);
		}
		return I_API_IGN;
}

/**********************************************************************
 * API_NODESTATUS: Return the status of the given node
 *********************************************************************/

int api_nodestatus(const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason)
{
		const char *		cnode;
		struct node_info *	node;

		if ((cnode = ha_msg_value(msg, F_NODENAME)) == NULL
		|| (node = lookup_node(cnode)) == NULL) {
			*failreason = "EINVAL";
			return I_API_BADREQ;
		}
		if (ha_msg_add(resp, F_STATUS, node->status) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_nodestatus: cannot add field");
			return I_API_IGN;
		}
		return I_API_RET;
}

/**********************************************************************
 * API_IFLIST: List the interfaces for the given machine
 *********************************************************************/

int api_iflist(const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason)
{
		struct link * lnk;
		int	j;
		int	last = config->nodecount-1;
		const char *		cnode;
		struct node_info *	node;

		if ((cnode = ha_msg_value(msg, F_NODENAME)) == NULL
		|| (node = lookup_node(cnode)) == NULL) {
			*failreason = "EINVAL";
			return I_API_BADREQ;
		}

		/* Find last link... */
 		for(j=0; (lnk = &node->links[j]) && lnk->name; ++j) {
			last = j;
                }            

		for (j=0; j <= last; ++j) {
			if (ha_msg_mod(resp, F_IFNAME
			,	node->links[j].name) != HA_OK) {
				ha_log(LOG_ERR
				,	"api_iflist: "
				"cannot mod field/1");
				return I_API_IGN;
			}
			if (ha_msg_mod(resp, F_APIRESULT
			,	(j == last ? API_OK : API_MORE))
			!=	HA_OK) {
				ha_log(LOG_ERR
				,	"api_iflist: "
				"cannot mod field/2");
				return I_API_IGN;
			}
			api_send_client_msg(client, resp);
		}
	return I_API_IGN;
}

/**********************************************************************
 * API_IFSTATUS: Return the status of the given interface...
 *********************************************************************/

int api_ifstatus(const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason)
{
		const char *		cnode;
		struct node_info *	node;
		const char *		ciface;
		struct link *		iface;

		if ((cnode = ha_msg_value(msg, F_NODENAME)) == NULL
		||	(node = lookup_node(cnode)) == NULL
		||	(ciface = ha_msg_value(msg, F_IFNAME)) == NULL
		||	(iface = lookup_iface(node, ciface)) == NULL) {
			*failreason = "EINVAL";
			return I_API_BADREQ;
		}
		if (ha_msg_mod(resp, F_STATUS,	iface->status) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_ifstatus: cannot add field/1");
			ha_log(LOG_ERR
			,	"name: %s, value: %s (if=%s)"
			,	F_STATUS, iface->status, ciface);
			return I_API_IGN;
		}
		return I_API_RET;
}

/*
 * Process an API request message from one of our clients
 */
void
api_process_request(client_proc_t* fromclient, struct ha_msg * msg)
{
	const char *	msgtype;
	const char *	reqtype;
	const char *	fromid;
	const char *	pid;
	client_proc_t*	client;
	struct ha_msg *	resp = NULL;
	const char *	failreason = NULL;
	int x;

	if (msg == NULL || (msgtype = ha_msg_value(msg, F_TYPE)) == NULL) {
		ha_log(LOG_ERR, "api_process_request: bad message type");
		return;
	}

	/* Things that aren't T_APIREQ are general packet xmit requests... */
	if (strcmp(msgtype, T_APIREQ) != 0) {

		/* Only named clients can send out packets to clients */

		if (fromclient->iscasual) {
			ha_log(LOG_ERR, "api_process_request: "
			"general message from casual client!");
			/* Bad Client! */
			api_remove_client(client);
			return;
		}

		/* We put their client ID info in the packet as the F_FROMID */
		if (ha_msg_mod(msg, F_FROMID, fromclient->client_id) != HA_OK) {
			ha_log(LOG_ERR, "api_process_request: "
			"cannot add F_FROMID field");
			return;
		}
		/* Is this too restrictive? */
		/* We also put their client ID info in the packet as F_TOID */

		if (ha_msg_mod(msg, F_TOID, fromclient->client_id) != HA_OK) {
			ha_log(LOG_ERR, "api_process_request: "
			"cannot add F_TOID field");
			return;
		}

		/* Mikey likes it! */
		if (send_cluster_msg(msg) != HA_OK) {
			ha_log(LOG_ERR, "api_process_request: "
			"cannot forward message to cluster");
		}
		return;
	}

	/* It must be a T_APIREQ request */

	fromid = ha_msg_value(msg, F_FROMID);
	pid = ha_msg_value(msg, F_PID);
	reqtype = ha_msg_value(msg, F_APIREQ);

	if ((fromid == NULL && pid == NULL) || reqtype == NULL) {
		ha_log(LOG_ERR, "api_process_request: no fromid/pid/reqtype"
		" in message.");
		return;
	}

	/*
	 * Create the response message
	 */
	if ((resp = ha_msg_new(4)) == NULL) {
		ha_log(LOG_ERR, "api_process_request: out of memory/1");
		return;
	}

	/* API response messages are of type T_APIRESP */
	if (ha_msg_add(resp, F_TYPE, T_APIRESP) != HA_OK) {
		ha_log(LOG_ERR, "api_process_request: cannot add field/2");
		ha_msg_del(resp); resp=NULL;
		return;
	}
	/* Echo back the type of API request we're responding to */
	if (ha_msg_add(resp, F_APIREQ, reqtype) != HA_OK) {
		ha_log(LOG_ERR, "api_process_request: cannot add field/3");
		ha_msg_del(resp); resp=NULL;
		return;
	}


	if ((client = find_client(fromid, pid)) == NULL) {
		ha_log(LOG_ERR, "api_process_request: msg from non-client");
		return;
	}

	/* See if they correctly stated their client id information... */
	if (client != fromclient) {
		ha_log(LOG_ERR, "Client mismatch! (impersonation?)");
		return;
	}

	/* See if this client FIFOs are (still) properly secured */

	if (!ClientSecurityIsOK(client)) {
		api_remove_client(client);
		ha_msg_del(resp); resp=NULL;
		return;
	}
	
	for(x = 0 ; x < DIMOF(query_handler_list); x++) { 

		int ret;

		if(strcmp(reqtype, query_handler_list[x].queryname) == 0) {
			ret = query_handler_list[x].handler(msg, resp, client
						, &failreason);
			switch(ret) {
			case I_API_IGN:
				ha_msg_del(resp); resp = NULL;
				return;
			case I_API_RET:
				if (ha_msg_add(resp, F_APIRESULT, API_OK)
				!=	HA_OK) {
					ha_log(LOG_ERR
					,	"api_process_request:"
					" cannot add field/8.1");
					ha_msg_del(resp); resp=NULL;
					return;
				}
				api_send_client_msg(client, resp);
				ha_msg_del(resp); resp=NULL;
				return;

			case I_API_BADREQ:
				goto bad_req;
			}
		}
	}


	/**********************************************************************
	 * Unknown request type...
	 *********************************************************************/
	ha_log(LOG_ERR, "Unknown API request");

	/* Common error return handling */
bad_req:
	ha_log(LOG_ERR, "api_process_request: bad request [%s]"
	,	reqtype);
	ha_log_message(msg);
	if (ha_msg_add(resp, F_APIRESULT, API_BADREQ) != HA_OK) {
		ha_log(LOG_ERR
		,	"api_process_request: cannot add field/11");
		ha_msg_del(resp);
		resp=NULL;
		return;
	}
	if (failreason) {
		if (ha_msg_add(resp, F_COMMENT,	failreason) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add failreason");
		}
	}
	api_send_client_msg(client, resp);
	ha_msg_del(resp);
	resp=NULL;
}

/*
 *	Register a new client.
 */
void
api_process_registration(struct ha_msg * msg)
{
	const char *	msgtype;
	const char *	reqtype;
	const char *	fromid;
	const char *	pid;
	struct ha_msg *	resp;
	client_proc_t*	client;

	if (msg == NULL
	||	(msgtype = ha_msg_value(msg, F_TYPE)) == NULL
	||	(reqtype = ha_msg_value(msg, F_APIREQ)) == NULL
	||	strcmp(msgtype, T_APIREQ) != 0
	||	strcmp(reqtype, API_SIGNON) != 0)  {
		ha_log(LOG_ERR, "api_process_registration: bad message");
		return;
	}
	fromid = ha_msg_value(msg, F_FROMID);
	pid = ha_msg_value(msg, F_PID);

	if (fromid == NULL && pid == NULL) {
		ha_log(LOG_ERR, "api_process_registration: no fromid in msg");
		return;
	}

	/*
	 *	Create the response message
	 */
	if ((resp = ha_msg_new(4)) == NULL) {
		ha_log(LOG_ERR, "api_process_registration: out of memory/1");
		return;
	}
	if (ha_msg_add(resp, F_TYPE, T_APIRESP) != HA_OK) {
		ha_log(LOG_ERR, "api_process_registration: cannot add field/2");
		ha_msg_del(resp); resp=NULL;
		return;
	}
	if (ha_msg_add(resp, F_APIREQ, reqtype) != HA_OK) {
		ha_log(LOG_ERR, "api_process_registration: cannot add field/3");
		ha_msg_del(resp); resp=NULL;
		return;
	}

	/*
	 *	Sign 'em up.
	 */
	api_add_client(msg);

	/* Make sure we can find them in the table... */
	if ((client = find_client(fromid, pid)) == NULL) {
		ha_log(LOG_ERR
		,	"api_process_registration: cannot add client");
		ha_msg_del(resp); resp=NULL;
		/* We can't properly reply to them.  Sorry they'll hang... */
		return;
	}
	if (ha_msg_mod(resp, F_APIRESULT, API_OK) != HA_OK) {
		ha_log(LOG_ERR
		,	"api_process_registration: cannot add field/4");
		ha_msg_del(resp); resp=NULL;
		return;
	}
	ha_log(LOG_INFO, "Signing client %d on to API", client->pid);
	api_send_client_msg(client, resp);
	ha_msg_del(resp); resp=NULL;
}

/*
 *	Send a message to a client process.
 */
void
api_send_client_msg(client_proc_t* client, struct ha_msg *msg)
{
	const char	* fifoname;
	FILE*	f;
	int	fd;


	/* See if this client is (still) properly secured */

	if (!ClientSecurityIsOK(client)) {
		api_remove_client(client);
		client=NULL;
		return;
	}

	fifoname = client_fifo_name(client, 0);

	if ((fd=open(fifoname, O_WRONLY|O_NDELAY)) < 0) {
		ha_perror("api_send_client: can't open %s", fifoname);
		api_remove_client(client);
		return;
	}
	if ((f = fdopen(fd, "w")) == NULL) {
		ha_perror("api_send_client: can't fdopen %s", fifoname);
		api_remove_client(client);
		return;
	}

	if (!msg2stream(msg, f)) {
		ha_log(LOG_ERR, "Cannot send message to client %d"
		,	client->pid);
	}

	if (fclose(f) == EOF) {
		ha_perror("Cannot send message to client %d (close)"
		,	client->pid);
	}
	if (kill(client->pid, client->signal) < 0 && errno == ESRCH) {
		ha_log(LOG_ERR, "api_send_client: client %d died", client->pid);
		api_remove_client(client);
		client=NULL;
		return;
	}
}

/*
 *	The range of file descriptors we have open for the request FIFOs
 */

static int	maxfd = -1;
static int	minfd = -1;

/*
 *	Make this client no longer a client ;-)
 */

void
api_remove_client(client_proc_t* req)
{
	client_proc_t*	prev = NULL;
	client_proc_t*	client;

	--total_client_count;
	if ((req->desired_types & DEBUGTREATMENTS) != 0) {
		--debug_client_count;
	}

	/* Locate the client data structure in our list */

	for (client=client_list; client != NULL; client=client->next) {
		/* Is this the client? */
		if (client->pid == req->pid) {
			/* Close the input FIFO */
			if (client->input_fifo != NULL) {
				int	fd = fileno(client->input_fifo);
				if (fd == maxfd) {
					--maxfd;
				}
				FDclients[fd] = NULL;
				fclose(client->input_fifo);
				client->input_fifo = NULL;
			}
			/* Clean up after casual clients */
			if (client->iscasual) {
				unlink(client_fifo_name(client, 0));
				unlink(client_fifo_name(client, 1));
			}
			if (prev == NULL) {
				client_list = client->next;
			}else{
				prev->next = client->next;
			}
			/* Zap! */
			memset(client, 0, sizeof(*client));
			ha_free(client);
			return;
		}
		prev = client;
	}
	ha_log(LOG_ERR,	"api_remove_client: could not find pid [%d]", req->pid);
}

/*
 *	Add the process described in this message to our list of clients.
 *
 *	The following fields are used:
 *	F_PID:		Mandantory.  The client process id.
 *	F_FROMID:	The client's identifying handle.
 *			If omitted, it defaults to the F_PID field as a
 *			decimal integer.
 */
void
api_add_client(struct ha_msg* msg)
{
	pid_t		pid = 0;
	int		fifoifd;
	FILE*		fifofp;
	client_proc_t*	client;
	const char*	cpid;
	const char *	fromid;

	
	/* Not a wonderful place to call it, but not too bad either... */

	if (!HostSecurityIsOK()) {
		return;
	}

	if ((cpid = ha_msg_value(msg, F_PID)) != NULL) {
		pid = atoi(cpid);
	}
	if (pid <= 0  || (kill(pid, 0) < 0 && errno == ESRCH)) {
		ha_log(LOG_ERR
		,	"api_add_client: bad pid [%d]", pid);
		return;
	}
	fromid = ha_msg_value(msg, F_FROMID);

	client = find_client(cpid, fromid);

	if (client != NULL) {
		if (kill(client->pid, 0) == 0 || errno != ESRCH) {
			ha_log(LOG_ERR
			,	"duplicate client add request");
			return;
		}
		api_remove_client(client);
	}
	if ((client = MALLOCT(client_proc_t)) == NULL) {
		ha_log(LOG_ERR
		,	"unable to add client pid %d [no memory]", pid);
		return;
	}
	/* Zap! */
	memset(client, 0, sizeof(*client));
	client->input_fifo = NULL;
	client->pid = pid;
	client->desired_types = DEFAULTREATMENT;
	client->signal = 0;

	if (fromid != NULL) {
		strncpy(client->client_id, fromid, sizeof(client->client_id));
		if (atoi(client->client_id) == pid) {
			client->iscasual = 1;
		}else{
			client->iscasual = 0;
		}
	}else{
		snprintf(client->client_id, sizeof(client->client_id)
		,	"%d", pid);
		client->iscasual = 1;
	}

	client->next = client_list;
	client_list = client;
	total_client_count++;

	/* Make sure their FIFOs are properly secured */
	if (!ClientSecurityIsOK(client)) {
		/* No insecure clients allowed! */
		api_remove_client(client);
		return;
	}
	if ((fifofp=open_reqfifo(client)) <= 0) {
		ha_log(LOG_ERR
		,	"Unable to open API FIFO for client %s"
		,	client->client_id);
		api_remove_client(client);
		return;
	}
	fifoifd=fileno(fifofp);
	if (fifoifd >= MAXFD) {
		ha_log(LOG_ERR
		,	"Too many API clients [%d]", total_client_count);
		api_remove_client(client);
		return;
	}
	client->input_fifo = fifofp;
	FDclients[fifoifd] = client;
	if (fifoifd > maxfd) {
		maxfd = fifoifd;
	}
	if (minfd < 0 || fifoifd < minfd) {
		minfd = fifoifd;
	}
}

/*
 *	Find the client that goes with this client id/pid
 */
client_proc_t*
find_client(const char * fromid, const char * cpid)
{
	pid_t	pid;
	client_proc_t* client;

	if (cpid != NULL) {
		pid = atoi(cpid);
	}

	for (client=client_list; client != NULL; client=client->next) {
		if (cpid && client->pid == pid) {
			return(client);
		}
		if (fromid && strcmp(fromid, client->client_id) == 0) {
			return(client);
		}
	}
	return(NULL);
}

/*
 *	Return the name of the client FIFO of the given type
 *		(request or response)
 */
static const char *
client_fifo_name(client_proc_t* client, int isrequest)
{
	static char	fifoname[MAXPATHLEN];
	const char *	dirprefix;
	const char *	fifosuffix;

	dirprefix = (client->iscasual ? CASUALCLIENTDIR : NAMEDCLIENTDIR);
	fifosuffix = (isrequest ? REQ_SUFFIX : RSP_SUFFIX);
	
	snprintf(fifoname, sizeof(fifoname), "%s/%s%s"
	,	dirprefix, client->client_id, fifosuffix);
	return(fifoname);
}


/*
 * Our Goal: To be as big a pain in the posterior as we can be :-)
 */

static int
HostSecurityIsOK(void)
{
	uid_t		our_uid = geteuid();
	struct stat	s;

	/*
	 * Check out the Heartbeat internal-use FIFO...
	 */

	if (stat(FIFONAME, &s) < 0) {
		ha_log(LOG_ERR
		,	"FIFO %s does not exist", FIFONAME);
		return(0);
	}

	/* Is the heartbeat FIFO internal-use pathname a FIFO? */

	if (!S_ISFIFO(s.st_mode)) {
		ha_log(LOG_ERR
		,	"%s is not a FIFO", FIFONAME);
		unlink(FIFONAME);
		return 0;
	}
	/*
	 * Check to make sure it isn't readable or writable by group or other.
	 */

	if ((s.st_mode&(S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)) != 0) {
		ha_log(LOG_ERR
		,	"FIFO %s is not secure.", FIFONAME);
		return 0;
	}

	/* Let's make sure it's owned by us... */

	if (s.st_uid != our_uid) {
		ha_log(LOG_ERR
		,	"FIFO %s not owned by uid %d.", FIFONAME
		,	our_uid);
		return 0;
	}

	/*
	 *	Now, let's check out the API registration FIFO
	 */

	if (stat(API_REGFIFO, &s) < 0) {
		ha_log(LOG_ERR
		,	"FIFO %s does not exist", API_REGFIFO);
		return(0);
	}

	/* Is the registration FIFO pathname a FIFO? */

	if (!S_ISFIFO(s.st_mode)) {
		ha_log(LOG_ERR
		,	"%s is not a FIFO", API_REGFIFO);
		unlink(FIFONAME);
		return 0;
	}
	/*
	 * Check to make sure it isn't readable or writable by other
	 * or readable by group.
	 */

	if ((s.st_mode&(S_IRGRP|S_IROTH|S_IWOTH)) != 0) {
		ha_log(LOG_ERR
		,	"FIFO %s is not secure.", API_REGFIFO);
		return 0;
	}

	/* Let's make sure it's owned by us... */
	if (s.st_uid != our_uid) {
		ha_log(LOG_ERR
		,	"FIFO %s not owned by uid %d.", API_REGFIFO
		,	our_uid);
		return 0;
	}


	/* 
	 * Check out the casual client FIFO directory
	 */

	if (stat(CASUALCLIENTDIR, &s) < 0) {
		ha_log(LOG_ERR
		,	"Directory %s does not exist", CASUALCLIENTDIR);
		return(0);
	}

	/* Is the Casual Client FIFO directory pathname really a directory? */

	if (!S_ISDIR(s.st_mode)) {
		ha_log(LOG_ERR
		,	"%s is not a Directory", CASUALCLIENTDIR);
		return(0);
	}

	/* Let's make sure it's owned by us... */

	if (s.st_uid != our_uid) {
		ha_log(LOG_ERR
		,	"Directory %s not owned by uid %d.", CASUALCLIENTDIR
		,	our_uid);
		return 0;
	}

	/* Make sure it isn't R,W or X by other. */

	if ((s.st_mode&(S_IROTH|S_IWOTH|S_IXOTH)) != 0) {
		ha_log(LOG_ERR
		,	"Directory %s is not secure.", CASUALCLIENTDIR);
		return 0;
	}

	/* Make sure it *is* executable and writable by group */

	if ((s.st_mode&(S_IXGRP|S_IWGRP)) != (S_IXGRP|S_IWGRP)){
		ha_log(LOG_ERR
		,	"Directory %s is not usable.", CASUALCLIENTDIR);
		return 0;
	}

	/* Make sure the casual client FIFO directory is sticky */

	if ((s.st_mode&(S_IXGRP|S_IWGRP|S_ISVTX)) != (S_IXGRP|S_IWGRP|S_ISVTX)){
		ha_log(LOG_ERR
		,	"Directory %s is not sticky.", CASUALCLIENTDIR);
		return 0;
	}

	/* 
	 * Check out the Named Client FIFO directory
	 */

	if (stat(NAMEDCLIENTDIR, &s) < 0) {
		ha_log(LOG_ERR
		,	"Directory %s does not exist", NAMEDCLIENTDIR);
		return(0);
	}

	/* Is the Named Client FIFO directory pathname actually a directory? */

	if (!S_ISDIR(s.st_mode)) {
		ha_log(LOG_ERR
		,	"%s is not a Directory", NAMEDCLIENTDIR);
		return(0);
	}

	/* Let's make sure it's owned by us... */

	if (s.st_uid != our_uid) {
		ha_log(LOG_ERR
		,	"Directory %s not owned by uid %d.", NAMEDCLIENTDIR
		,	our_uid);
		return 0;
	}

	/* Make sure it isn't R,W or X by other, or writable by group */

	if ((s.st_mode&(S_IXOTH|S_IROTH|S_IWOTH|S_IWGRP)) != 0) {
		ha_log(LOG_ERR
		,	"Directory %s is not secure.", NAMEDCLIENTDIR);
		return 0;
	}

	/* Make sure it *is* executable by group */

	if ((s.st_mode&(S_IXGRP)) != (S_IXGRP)) {
		ha_log(LOG_ERR
		,	"Directory %s is not usable.", NAMEDCLIENTDIR);
		return 0;
	}
	return 1;
}

/*
 *	We are the security tough-guys.  Or so we hope ;-)
 */
static int
ClientSecurityIsOK(client_proc_t* client)
{
	const char *	fifoname;
	struct stat	s;
	uid_t		client_uid;
	uid_t		our_uid;

	/* Does this client even exist? */

	if (kill(client->pid, 0) < 0 && errno == ESRCH) {
		ha_log(LOG_ERR
		,	"Client pid %d does not exist", client->pid);
		return(0);
	}
	client_uid = pid2uid(client->pid);
	our_uid = geteuid();


	/*
	 * Check the security of the Client's Request FIFO
	 */

	fifoname = client_fifo_name(client, 1);

	if (stat(fifoname, &s) < 0) {
		ha_log(LOG_ERR
		,	"FIFO %s does not exist", fifoname);
		return(0);
	}

	/* Is the request FIFO pathname a FIFO? */

	if (!S_ISFIFO(s.st_mode)) {
		ha_log(LOG_ERR
		,	"%s is not a FIFO", fifoname);
		unlink(fifoname);
		return 0;
	}

	/*
	 * Check to make sure it isn't writable by group or other,
	 * or readable by others.
	 */
	if ((s.st_mode&(S_IWGRP|S_IWOTH|S_IROTH)) != 0) {
		ha_log(LOG_ERR
		,	"FIFO %s is not secure.", fifoname);
		return 0;
	}

	/*
	 * The request FIFO shouldn't be group readable unless it's
 	 * grouped to our effective group id, and we aren't root. 
	 * If we're root, we can read it anyway, so there's no reason
	 * we should allow it to be group readable.
	 */

	if ((s.st_mode&S_IRGRP) != 0 && s.st_gid != getegid()
	&&	geteuid() != 0) {
		ha_log(LOG_ERR
		,	"FIFO %s is not secure (g+r).", fifoname);
		return 0;
	}

	/* Does it look like the given client pid can write this FIFO? */

	if (client_uid != s.st_uid) {
		ha_log(LOG_ERR
		,	"Client pid %d is not uid %ld like they"
		" must be to write FIFO %s"
		,	client->pid, (long)s.st_uid, fifoname);
		return 0;
	}

	/*
	 * Check the security of the Client's Response FIFO
	 */

	fifoname = client_fifo_name(client, 0);
	if (stat(fifoname, &s) < 0) {
		ha_log(LOG_ERR
		,	"FIFO %s does not exist", fifoname);
		return 0;
	}

	/* Is the response FIFO pathname a FIFO? */

	if (!S_ISFIFO(s.st_mode)) {
		ha_log(LOG_ERR
		,	"%s is not a FIFO", fifoname);
		unlink(fifoname);
		return 0;
	}

	/*
	 * Is the response FIFO secure?
	 */

	/*
	 * Check to make sure it isn't readable by group or other,
	 * or writable by others.
	 */
	if ((s.st_mode&(S_IRGRP|S_IROTH|S_IWOTH)) != 0) {
		ha_log(LOG_ERR
		,	"FIFO %s is not secure.", fifoname);
		return 0;
	}

	/*
	 * The response FIFO shouldn't be group writable unless it's
 	 * grouped to our effective group id, and we aren't root. 
	 * If we're root, we can write it anyway, so there's no reason
	 * we should allow it to be group writable.
	 */
	if ((s.st_mode&S_IWGRP) != 0 && s.st_gid != getegid()
	&&	geteuid() != 0) {
		ha_log(LOG_ERR
		,	"FIFO %s is not secure (g+w).", fifoname);
		return 0;
	}

	/* Does it look like the given client pid can read this FIFO? */

	if (client_uid != s.st_uid) {
		ha_log(LOG_ERR
		,	"Client pid %d is not uid %ld like they"
		" must be to read FIFO %s"
		,	client->pid, (long)s.st_uid, fifoname);
		return 0;
	}
	return 1;
}

/*
 * Open the request FIFO for the given client.
 */
static FILE*
open_reqfifo(client_proc_t* client)
{
	struct stat	s;
	const char *	fifoname = client_fifo_name(client, 1);
	int		fd;
	FILE *		ret;


	if (client->input_fifo != NULL) {
		return(client->input_fifo);
	}

	/* How about that! */
	client->uid = s.st_uid;
	fd = open(fifoname, O_RDONLY|O_NDELAY);
	if (fd < 0) {
		return(NULL);
	}
	if ((ret = fdopen(fd, "r")) != NULL) {
		setbuf(ret, NULL);
	}
	return ret;
}

#define	PROC	"/proc/"

/* Return the uid of the given pid */

static	uid_t
pid2uid(pid_t pid)
{
	struct stat	s;
	char	procpath[sizeof(PROC)+20];

	snprintf(procpath, sizeof(procpath), "%s%ld", PROC, (long)pid);

	if (stat(procpath, &s) < 0) {
		return(-1);
	}
	/*
	 * This isn't a perfect test.  On Linux we could look at the
	 * /proc/$pid/status file for the line that says:
	 *	Uid:    500     500     500     500 
	 * and parse it for find out whatever we want to know.
	 */
	return s.st_uid;
}

/* Compute the file descriptor set for select(2) */
int
compute_msp_fdset(fd_set* set, int fd1, int fd2)
{
	/* msp == Master Status Process */
	int	fd;
	int	newmax = -1;
	int	newmin = MAXFD + 1;
	int	pmax = (fd1 > fd2 ? fd1 : fd2);

	
	FD_ZERO(set);
	FD_SET(fd1, set);
	FD_SET(fd2, set);

	for (fd=minfd; fd <= maxfd; ++fd) {
		if (FDclients[fd]) {
			FD_SET(fd, set);
			newmax = fd;
			if (fd < newmin) {
				newmin = fd;
			}
		}
		
	}
	maxfd = newmax;
	minfd = newmin;
	return (pmax > newmax ? pmax : newmax)+1;
}

/* Process select(2)ed API FIFOs for messages */
void
process_api_msgs(fd_set* inputs, fd_set* exceptions)
{
	int		fd;
	client_proc_t*	client;

	/* Loop over the range of file descriptors open for our clients */
	for (fd=minfd; fd <= maxfd; ++fd) {

		/* Do we have a client on this file descriptor? */

		if ((client = FDclients[fd]) != NULL) {
			struct ha_msg*	msg;

			/* I'm not sure if this is ever happens... */
			/* But if it does, we're ready for it ;-) */

			if (FD_ISSET(fd, exceptions)) {
				if (kill(client->pid, 0) < 0
				&&	errno == ESRCH) {
					ha_log(LOG_ERR
					,	"Client pid %d died (exception)"
					,	client->pid);
					api_remove_client(client);
					continue;
				}
			}

			/* Skip if no input for this client */
			if (!FD_ISSET(fd, inputs)) {
				continue;
			}

			/* Got a message from 'client' */
			if (kill(client->pid, 0) < 0
			&&	errno == ESRCH) {
				ha_log(LOG_ERR
				,	"Client pid %d died (input)"
				,	client->pid);
				api_remove_client(client);
				continue;
			}

			/* See if we can read the message */
			if ((msg = msgfromstream(client->input_fifo)) == NULL) {
				/*
				 * If you kill -9 the process, then this
				 * may happen.  It appears that we get an
				 * input indication on the FIFO, but the
				 * process isn't quite gone yet.
				 * It'll never go away unless we close so
				 * we'll get a *nasty* high-priority infinite
				 * loop.  Better close it down ;-)
				 */
				ha_log(LOG_ERR, "No message from pid %d "
				,	client->pid);
				ha_log(LOG_ERR, "Removing client pid %d "
				,	client->pid);
				api_remove_client(client);
				continue;
			}

			/* Process the API request message... */
			api_heartbeat_monitor(msg, APICALL, "<api>");
			api_process_request(client, msg);
			ha_msg_del(msg); msg = NULL;
		}
	}
}
