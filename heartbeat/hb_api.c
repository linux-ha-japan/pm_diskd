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

#define	API_FIFO_DIR	VAR_RUN_D "/heartbeat-api" /* Or something better ;-)  FIXME!! */
#define	API_FIFO_LEN	(sizeof(API_FIFO_DIR)+32)

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

void api_send_client_msg(client_proc_t* client, struct ha_msg *msg);
void api_heartbeat_monitor(struct ha_msg *msg, int msgtype, const char *iface);
void api_remove_client(client_proc_t* client);
void api_add_client(struct ha_msg* msg);

/*
 * Much of the original structure of this code was due to
 * Marcelo Tosatti <marcelo@conectiva.com.br>
 *
 * It was significantly mangeled by Alan Robertson <alanr@suse.com>
 *
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
api_send_client_msg(client_proc_t* client, struct ha_msg *msg)
{
	char	fifoname[API_FIFO_LEN];
	FILE*	f;


	snprintf(fifoname, sizeof(fifoname), API_FIFO_DIR "/%d", client->pid);

	if ((f=fopen(fifoname, "w")) == NULL) {
		ha_perror("api_send_client: can't open %s", fifoname);
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

	fclose(f);
	if (kill(client->pid, client->signal) < 0 && errno == EEXIST) {
		ha_log(LOG_ERR, "api_send_client: client %d died", client->pid);
		api_remove_client(client);
		client=NULL;
		return;
	}
}

void
api_remove_client(client_proc_t* req)
{
	client_proc_t*	prev = NULL;
	client_proc_t*	client;
	
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

void
api_add_client(struct ha_msg* msg)
{
	pid_t		pid = 0;
	client_proc_t*	client;
	const char*	cpid;

	
	if ((cpid = ha_msg_value(msg, F_PID)) != NULL) {
		pid = atoi(cpid);
	}
	if (pid <= 0) {
		ha_log(LOG_ERR
		,	"api_add_client: bad pid [%d]", pid);
		return;
	}

	for (client=client_list; client != NULL; client=client->next) {
		if (client->pid == pid) {
			ha_log(LOG_ERR
			,	"duplicate client add request [%d]", pid);
			return;
		}
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
	if ((cpid = ha_msg_value(msg, F_FROMID)) != NULL) {
		strncpy(client->client_id, cpid, sizeof(client->client_id));
	}else{
		snprintf(client->client_id, sizeof(client->client_id)
		,	"%d", pid);
	}
	client->next = client_list;
	client_list = client;
	total_client_count++;
}
