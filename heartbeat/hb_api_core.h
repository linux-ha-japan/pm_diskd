#ifndef _HB_API_CORE_H
#define _HB_API_CORE_H 1
#include <sys/types.h>
#include <ha_msg.h>
/*
 * Types of messages. 
 * DROPIT and/or DUPLICATE are only used when a debugging callback
 * is registered.
 */ 

#define	KEEPIT		1	/* A set of bits */
#define	DROPIT		2
#define DUPLICATE	4

#define	ALLTREATMENTS	(KEEPIT|DROPIT|DUPLICATE)
#define	DEBUGTREATMENTS	(DROPIT|DUPLICATE)
#define	DEFAULTREATMENT	(KEEPIT)

#define NR_TYPES 3

#define	API_NEWCLIENT		"signon"
#define	API_SETFILTER		"setfilter"
#	define	F_FILTERMASK	"fmask"
#define	API_NODELIST		"nodelist"
#	define	F_NODENAME	"node"
#define	API_NODELIST_END	"nodelist-end"
#define	API_NODESTATUS		"nodestatus"
/*	F_STATUS	"status" */

#define	API_IFLIST		"iflist"
#	define	F_IFNAME		"ifname"
#define	API_IFLIST_END		"iflist-end"
#define	API_IFSTATUS		"ifstatus"


#define	API_SUCCESS		"OK"
#define	API_FAILURE		"fail"
#define	API_BADREQ		"badreq"
#define	API_MORE		"ok/more"
void api_heartbeat_monitor(struct ha_msg *msg, int msgtype, const char *iface);
void api_process_request(struct ha_msg *msg);

/* Generic message callback structure and callback function definition */

struct message_callback {
	pid_t pid; /* which client registered the callback */
    void (*message_callback) (const struct ha_msg * msg, const char *iface
	, const char *node, pid_t pid);
};

typedef void (message_callback_t) (const struct ha_msg * msg, const char *iface
				  , const char *node);
#endif
