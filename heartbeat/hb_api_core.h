/*
 * hb_api_core_h: Internal definitions and functions for the heartbeat API
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

#ifndef _HB_API_CORE_H
#	define _HB_API_CORE_H 1
#include <sys/types.h>
#include <ha_msg.h>

/*
 *   Per-client API data structure.
 */

typedef struct client_process {
	char        client_id[32];  /* Client identification */
	pid_t       pid;        /* PID of client process */
	uid_t       uid;        /* UID of client  process */
	int     iscasual;   /* 1 if this is a "casual" client */
	FILE*       input_fifo; /* Input FIFO file pointer */
	int     signal;     /* What signal to indicate new msgs with */
	int     desired_types;  /* A bit mask of desired message types*/
	struct client_process*  next;
}client_proc_t;

/*
 * Types of messages. 
 * DROPIT and/or DUPLICATE are only used when a debugging callback
 * is registered.
 */ 


/*
 *	This next set of defines is for the types of packets that come through
 *	heartbeat.
 *
 *	Any given packet behaves like an enumeration (should only have one bit
 *	on), but the options from client software treat them more like a set
 *	(bit field), with more than one at a time being on.  Normally the
 *	client only requests KEEPIT packets, but for debugging may want to
 *	ask to see the others too.
 */
#define	KEEPIT		0x01	/* A set of bits */
#define	NOCHANGE	0x02
#define	DROPIT		0x04
#define DUPLICATE	0x08
#define APICALL		0x10
#define PROTOCOL	0x20
#define	DEBUGTREATMENTS	(DROPIT|DUPLICATE|APICALL|NOCHANGE|PROTOCOL)
#define	ALLTREATMENTS	(DEBUGTREATMENTS|KEEPIT)
#define	DEFAULTREATMENT	(KEEPIT)

#define	API_SIGNON		"signon"
#define	API_SIGNOFF		"signoff"
#define	API_SETFILTER		"setfilter"
#	define	F_FILTERMASK	"fmask"
#define	API_SETSIGNAL		"setsignal"
#	define	F_SIGNAL	"signal"
#define	API_NODELIST		"nodelist"
#	define	F_NODENAME	"node"
#define	API_NODELIST_END	"nodelist-end"
#define	API_NODESTATUS		"nodestatus"

#define	API_IFLIST		"iflist"
#	define	F_IFNAME	"ifname"
#define	API_IFLIST_END		"iflist-end"
#define	API_IFSTATUS		"ifstatus"


#define	API_OK			"OK"
#define	API_FAILURE		"fail"
#define	API_BADREQ		"badreq"
#define	API_MORE		"ok/more"

#define	API_FIFO_DIR	VAR_RUN_D "/heartbeat-api"
#define	API_FIFO_LEN	(sizeof(API_FIFO_DIR)+32)

#define	NAMEDCLIENTDIR	API_FIFO_DIR
#define	CASUALCLIENTDIR	VAR_RUN_D "/heartbeat-casual"

#define	REQ_SUFFIX	".req"
#define	RSP_SUFFIX	".rsp"

#ifndef API_REGFIFO
#	define	API_REGFIFO	VAR_RUN_D "/heartbeat-register"
#endif

void api_heartbeat_monitor(struct ha_msg *msg, int msgtype, const char *iface);
void api_process_registration(struct ha_msg *msg);
void process_api_msgs(fd_set* inputs, fd_set* exceptions);
int  compute_msp_fdset(fd_set* set, int fd1, int fd2);
void api_audit_clients(void);

/* Return code for API query handlers */

#define I_API_RET 0 /* acknowledge client of successful API query */
#define I_API_IGN 1 /* do nothing */
#define I_API_BADREQ 2 /* send error msg to client with "failreason" as error reason */

/* Handler of API query */
typedef int (*api_query_handler_t) (const struct ha_msg* msg
			, struct ha_msg *resp, client_proc_t* client
			, const char **failreason);

struct api_query_handler {
		const char *queryname;
		api_query_handler_t handler;
};

/* Definitions of API query handlers */

int api_signoff (const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char **failreason);

int api_setfilter (const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char **failreason);

int api_setsignal (const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason);

int api_nodelist (const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason);

int api_nodestatus (const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason);

int api_ifstatus (const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason);

int api_iflist (const struct ha_msg* msg, struct ha_msg* resp
,	client_proc_t* client, const char** failreason);

#endif /* _HB_API_CORE_H */
