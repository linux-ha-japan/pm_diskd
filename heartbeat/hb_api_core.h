#ifndef _HB_API_CORE_H
#define _HB_API_CORE_H 1
#include <sys/types.h>
#include <ha_msg.h>
/*
 * Types of messages. 
 * DROPIT and/or DUPLICATE are only used when a debugging callback
 * is registered.
 */ 

#define	KEEPIT		0x01	/* A set of bits */
#define	NOCHANGE	0x02	/* A set of bits */
#define	DROPIT		0x04
#define DUPLICATE	0x08
#define APICALL		0x10
#define PROTOCOL	0x20

/*
 *	This next set of defines is for the types of packets that come through heartbeat.
 *
 *	Any given packet behaves like an enumeration (should only have one bit on), but the options
 *	from client software treat them more like a set (bit field), with more than one at a time
 *	being on.  Normally the client only requests KEEPIT packets, but for debugging may want to
 *	ask to see the others too.
 */
#define	DEBUGTREATMENTS	(DROPIT|DUPLICATE|APICALL|NOCHANGE|PROTOCOL)
#define	ALLTREATMENTS	(DEBUGTREATMENTS|KEEPIT)
#define	DEFAULTREATMENT	(KEEPIT)

#define NR_TYPES 3

#define	API_SIGNON		"signon"
#define	API_SIGNOFF		"signoff"
#define	API_SETFILTER		"setfilter"
#	define	F_FILTERMASK	"fmask"
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

#define	API_FIFO_DIR	VAR_RUN_D "/heartbeat-api" /* Or something better ;-)  FIXME!! */
#define	API_FIFO_LEN	(sizeof(API_FIFO_DIR)+32)
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
