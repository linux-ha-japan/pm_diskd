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


typedef struct client_process {
	char		client_id[32];
	pid_t		pid;
	int		signal;		/* Defaults to zero */
	int		desired_types;	/* A bit mask */
	struct client_process*	next;
}client_proc_t;

int		debug_client_count = 0;
int		total_client_count = 0;
client_proc_t*	client_list = NULL;

static void api_send_client_msg(client_proc_t* client, struct ha_msg *msg);
static void api_remove_client(client_proc_t* client);
static void api_add_client(struct ha_msg* msg);
static client_proc_t*	find_client(const char * fromid, const char * pid);

/*
 * Much of the original structure of this code was due to
 * Marcelo Tosatti <marcelo@conectiva.com.br>
 *
 * It has been significantly mangled by Alan Robertson <alanr@suse.com>
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

	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;


	/* This kicks out most messages... */
	if ((msgtype&DEBUGTREATMENTS) != 0 && debug_client_count <= 0) {
		return;
	}

	if ((msgtype & ALLTREATMENTS) != msgtype || msgtype == 0) {
		ha_log(LOG_ERR, "heartbeat_monitor: unknown msgtype [%d]"
		,	msgtype);
		return;
	}


	clientid = ha_msg_value(msg, F_TOID);

	for (client=client_list; client != NULL; client=client->next) {
		if (clientid != NULL
		&&	strcmp(clientid, client->client_id) != 0) {
			continue;
		}
		if ((msgtype & client->desired_types) != 0) {
			api_send_client_msg(client, msg);
		}
		if (clientid != NULL) {
			break;	/* No one else should get it */
		}
	}
}

void
api_process_request(struct ha_msg * msg)
{
	const char *	msgtype;
	const char *	reqtype;
	const char *	fromid;
	const char *	pid;
	client_proc_t*	client;
	struct ha_msg *	resp = NULL;

	if (msg == NULL
	||	(msgtype = ha_msg_value(msg, F_TYPE)) == NULL
	||	(reqtype = ha_msg_value(msg, F_APIREQ)) == NULL
	||	strcmp(msgtype, T_APIREQ) != 0)  {
		ha_log(LOG_ERR, "api_process_request: bad message");
		return;
	}
	fromid = ha_msg_value(msg, F_FROMID);
	pid = ha_msg_value(msg, F_PID);

	if (fromid == NULL && pid == NULL) {
		ha_log(LOG_ERR, "api_process_request: no fromid/pid in msg");
		return;
	}

	if ((resp = ha_msg_new(4)) == NULL) {
		ha_log(LOG_ERR, "api_process_request: out of memory/1");
		return;
	}
	if (ha_msg_add(resp, F_TYPE, T_APIRESP) != HA_OK) {
		ha_log(LOG_ERR, "api_process_request: cannot add field/2");
		ha_msg_del(resp); resp=NULL;
		return;
	}
	if (ha_msg_add(resp, F_APIREQ, reqtype) != HA_OK) {
		ha_log(LOG_ERR, "api_process_request: cannot add field/3");
		ha_msg_del(resp); resp=NULL;
		return;
	}

	/*
	 *	Sign on a new client.
	 */

	if (strcmp(reqtype, API_SIGNON) == 0) {
		api_add_client(msg);
		if (ha_msg_mod(resp, F_APIRESULT, API_OK) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add field/4");
			ha_msg_del(resp); resp=NULL;
			return;
		}
		if ((client = find_client(fromid, pid)) == NULL) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add client");
			return;
		}
		ha_log(LOG_INFO, "Signing client %d on to API", client->pid);
		api_send_client_msg(client, resp);
		ha_msg_del(resp); resp=NULL;
		return;
	}


	if ((client = find_client(fromid, pid)) == NULL) {
		ha_log(LOG_ERR, "api_process_request: msg from non-client");
		return;
	}
	if (strcmp(reqtype, API_SIGNOFF) == 0) {
		/* We send them no reply */
		ha_log(LOG_INFO, "Signing client %d off", client->pid);
		api_remove_client(client);
		ha_msg_del(resp); resp=NULL;
		return;
	}else if (strcmp(reqtype, API_SETFILTER) == 0) {
	/*
	 *	Record the types of messages desired by this client
	 *		(desired_types)
	 */
		const char *	cfmask;
		unsigned	mask;
		if ((cfmask = ha_msg_value(msg, F_FILTERMASK)) == NULL
		||	(sscanf(cfmask, "%x", &mask) != 1)
		||	(mask&ALLTREATMENTS) == 0) {
			goto bad_req;
		}

		if ((client->desired_types  & DEBUGTREATMENTS)== 0
		&&	(mask&DEBUGTREATMENTS) != 0) {
			++debug_client_count;
		}else if ((client->desired_types & DEBUGTREATMENTS) != 0
		&&	(mask & DEBUGTREATMENTS) == 0) {
			--debug_client_count;
		}
		client->desired_types = mask;
		if (ha_msg_add(resp, F_APIRESULT, API_OK) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add field/8.1");
			ha_msg_del(resp); resp=NULL;
			return;
		}
		api_send_client_msg(client, resp);
		ha_msg_del(resp); resp=NULL;
		return;
	}else if (strcmp(reqtype, API_SETSIGNAL) == 0) {
	/*
	 *	Set a signal to send whenever a message arrives
	 */
		const char *	csignal;
		unsigned	oursig;
		if ((csignal = ha_msg_value(msg, F_SIGNAL)) == NULL
		||	(sscanf(csignal, "%u", &oursig) != 1)) {
			goto bad_req;
		}

		client->signal = oursig;
		if (ha_msg_add(resp, F_APIRESULT, API_OK) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add field/8.1");
			ha_msg_del(resp); resp=NULL;
			return;
		}
		api_send_client_msg(client, resp);
		ha_msg_del(resp); resp=NULL;
		return;
	/*
	 *	List the nodes in the cluster
	 */
	}else if (strcmp(reqtype, API_NODELIST) == 0) {
		int	j;
		int	last = config->nodecount-1;

		for (j=0; j <= last; ++j) {
			if (ha_msg_mod(resp, F_NODENAME
			,	config->nodes[j].nodename) != HA_OK) {
				ha_log(LOG_ERR
				,	"api_process_request: "
				"cannot mod field/5");
				ha_msg_del(resp); resp=NULL;
				return;
			}
			if (ha_msg_mod(resp, F_APIRESULT
			,	(j == last ? API_OK : API_MORE))
			!=	HA_OK) {
				ha_log(LOG_ERR
				,	"api_process_request: "
				"cannot mod field/6");
				ha_msg_del(resp); resp=NULL;
				return;
			}
			api_send_client_msg(client, resp);
		}
		ha_msg_del(resp); resp=NULL;
		return;
		
	}else if (strcmp(reqtype, API_NODESTATUS) == 0) {
	/*
	 *	Return the status of the given node
	 */
		const char *		cnode;
		struct node_info *	node;

		if ((cnode = ha_msg_value(msg, F_NODENAME)) == NULL
		|| (node = lookup_node(cnode)) == NULL) {
			goto bad_req;
		}
		if (ha_msg_add(resp, F_STATUS, node->status) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add field/7");
			ha_msg_del(resp); resp=NULL;
			return;
		}
		if (ha_msg_mod(resp, F_APIRESULT, API_OK) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add field/8");
			ha_msg_del(resp); resp=NULL;
			return;
		}
		api_send_client_msg(client, resp);
		ha_msg_del(resp); resp=NULL;
		return;
	}else if (strcmp(reqtype, API_IFLIST) == 0) {
		struct link * lnk;
	/*
	 *	List the set of our interfaces for the given host
	 */
		int	j;
		int	last = config->nodecount-1;
		const char *		cnode;
		struct node_info *	node;

		if ((cnode = ha_msg_value(msg, F_NODENAME)) == NULL
		|| (node = lookup_node(cnode)) == NULL) {
			goto bad_req;
		}

		/* Find last link... */
 		for(j=0; (lnk = &node->links[j]) && lnk->name; ++j) {
			last = j;
                }            

		for (j=0; j <= last; ++j) {
			if (ha_msg_mod(resp, F_IFNAME
			,	node->links[j].name) != HA_OK) {
				ha_log(LOG_ERR
				,	"api_process_request: "
				"cannot mod field/5");
				ha_msg_del(resp); resp=NULL;
				return;
			}
			if (ha_msg_mod(resp, F_APIRESULT
			,	(j == last ? API_OK : API_MORE))
			!=	HA_OK) {
				ha_log(LOG_ERR
				,	"api_process_request: "
				"cannot mod field/6");
				ha_msg_del(resp); resp=NULL;
				return;
			}
			api_send_client_msg(client, resp);
		}
		ha_msg_del(resp); resp=NULL;
		return;
	}else if (strcmp(reqtype, API_IFSTATUS) == 0) {
	/*
	 *	Return the status of the given interface for the given
	 *	node.
	 */
		const char *		cnode;
		struct node_info *	node;
		const char *		ciface;
		struct link *		iface;

		if ((cnode = ha_msg_value(msg, F_NODENAME)) == NULL
		||	(node = lookup_node(cnode)) == NULL
		||	(ciface = ha_msg_value(msg, F_IFNAME)) == NULL
		||	(iface = lookup_iface(node, ciface)) == NULL) {
			goto bad_req;
		}
		if (ha_msg_add(resp, F_STATUS,	iface->status) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add field/9");
			ha_msg_del(resp); resp=NULL;
			return;
		}
		if (ha_msg_mod(resp, F_APIRESULT, API_OK) != HA_OK) {
			ha_log(LOG_ERR
			,	"api_process_request: cannot add field/10");
			ha_msg_del(resp); resp=NULL;
			return;
		}
		api_send_client_msg(client, resp);
		ha_msg_del(resp); resp=NULL;
		return;
	}else{
		ha_log(LOG_ERR, "Unknown API request");
	}

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
	api_send_client_msg(client, resp);
	ha_msg_del(resp);
	resp=NULL;
}

/*
 *	Send a message to a client process.
 */
void
api_send_client_msg(client_proc_t* client, struct ha_msg *msg)
{
	char	fifoname[API_FIFO_LEN];
	FILE*	f;


	snprintf(fifoname, sizeof(fifoname), API_FIFO_DIR "/%d", client->pid);

	if ((f=fopen(fifoname, "w")) == NULL) {
		ha_perror("api_send_client: can't open %s", fifoname);
		api_remove_client(client);
		return;
	}
	if (fcntl(fileno(f), F_SETFL, O_NONBLOCK) < 0) {
		/* Oh well... Hope we don't actually block ;-) */
		ha_perror("Cannot set O_NOBLOCK on FD");
	}
	if (!msg2stream(msg, f)) {
		ha_log(LOG_ERR, "Cannot send message to client %d"
		,	client->pid);
	}

	if (fclose(f) == EOF) {
		ha_perror("Cannot send message to client %d (close)"
		,	client->pid);
	}
	if (kill(client->pid, client->signal) < 0 && errno == EEXIST) {
		ha_log(LOG_ERR, "api_send_client: client %d died", client->pid);
		api_remove_client(client);
		client=NULL;
		return;
	}
}

/*
 *	Make this client no longer a client ;-)
 */
void
api_remove_client(client_proc_t* req)
{
	client_proc_t*	prev = NULL;
	client_proc_t*	client;
	char	fifoname[API_FIFO_LEN];

	/* Do a little cleanup */
	snprintf(fifoname, sizeof(fifoname), API_FIFO_DIR "/%d", req->pid);
	unlink(fifoname);

	--total_client_count;
	if ((req->desired_types & DEBUGTREATMENTS) != 0) {
		--debug_client_count;
	}

	for (client=client_list; client != NULL; client=client->next) {
		if (client->pid == req->pid) {
			if (prev == NULL) {
				client_list = client->next;
			}else{
				prev->next = client->next;
			}
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
	client_proc_t*	client;
	const char*	cpid;
	const char *	fromid;

	
	if ((cpid = ha_msg_value(msg, F_PID)) != NULL) {
		pid = atoi(cpid);
	}
	if (pid <= 0) {
		ha_log(LOG_ERR
		,	"api_add_client: bad pid [%d]", pid);
		return;
	}
	fromid = ha_msg_value(msg, F_FROMID);

	client = find_client(cpid, fromid);

	if (client != NULL) {
		if (kill(client->pid, 0) == 0 || errno != EEXIST) {
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
	memset(client, 0, sizeof(*client));
	client->pid = pid;
	client->desired_types = DEFAULTREATMENT;
	client->signal = 0;
	if (fromid != NULL) {
		strncpy(client->client_id, cpid, sizeof(client->client_id));
	}else{
		snprintf(client->client_id, sizeof(client->client_id)
		,	"%d", pid);
	}
	client->next = client_list;
	client_list = client;
	total_client_count++;
}
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
