#include <ha_msg.h>

#define	LLC_PROTOCOL_VERSION	1

/*
 *	Low-level clustering API to heartbeat.
 */

typedef void (*llc_msg_callback_t) (const struct ha_msg* msg
,	void* private_data);

typedef void (*llc_nstatus_callback_t) (const char *node, const char * status
,	void* private_data);

typedef void (*llc_ifstatus_callback_t) (const char *node
,	const char * interface, const char * status
,	void* private_data);

typedef struct ll_cluster {
	void *		ll_cluster_private;
	struct llc_ops*	llc_ops;
}ll_cluster_t;

struct llc_ops {
	int		(*signon) (ll_cluster_t*, const char * service);
	int		(*signoff) (ll_cluster_t*);
	int		(*delete) (ll_cluster_t*);
	
/*
 *************************************************************************
 * Status Update Callbacks
 *************************************************************************
 */

/*
 *	set_msg_callback:	Define callback for the given message type 
 *
 *	msgtype:	Type of message being handled.  NULL for default case.
 *			Note that default case not reached for node
 *			status messages handled by nstatus_callback,
 *			or ifstatus messages handled by nstatus_callback,
 *			Not just those explicitly handled by "msg_hander"
 *			cases.
 *
 *	callback:	callback function.
 *
 *	p:		private data - later passed to callback.
 */
	int		(*set_msg_callback) (ll_cluster_t*, const char * msgtype
	,			llc_msg_callback_t callback, void * p);

/*
 *	set_nstatus_callback:	Define callback for node status messages
 *				This is a message of type "status"
 *
 *	cbf:		callback function.
 *
 *	p:		private data - later passed to callback.
 */

	int		(*set_nstatus_callback) (ll_cluster_t*
	,		llc_nstatus_callback_t cbf, 	void * p);
/*
 *	set_ifstatus_callback:	Define callback for interface status messages
 *				This is a message of type "ifstat"
 *			These messages are issued whenever an interface goes
 *			dead or becomes active again.
 *
 *	cbf:		callback function.
 *
 *
 *	p:		private data - later passed to callback.
 */

	int             (*set_ifstatus_callback) (ll_cluster_t*
,			llc_ifstatus_callback_t cbf, void * p);
 

/*************************************************************************
 * Getting Current Information
 *************************************************************************/

/*
 *	init_nodewalk:	Initialize walk through list of list of known nodes
 */
	int		(*init_nodewalk)(ll_cluster_t*);
/*
 *	nextnode:	Return next node in the list of known nodes
 */
	const char *	(*nextnode)(ll_cluster_t*);
/*
 *	end_nodewalk:	End walk through the list of known nodes
 */
	int		(*end_nodewalk)(ll_cluster_t*);
/*
 *	node_status:	Return most recent heartbeat status of the given node
 */
	const char *	(*node_status)(ll_cluster_t*, const char * nodename);
/*
 *	init_ifwalk:	Initialize walk through list of list of known interfaces
 */
	int		(*init_ifwalk)(ll_cluster_t*, const char * node);
/*
 *	nextif:	Return next node in the list of known interfaces on node
 */
	const char *	(*nextif)(ll_cluster_t*);
/*
 *	end_ifwalk:	End walk through the list of known interfaces
 */
	int		(*end_ifwalk)(ll_cluster_t*);
/*
 *	if_status:	Return current status of the given interface
 */
	const char*	(*if_status)(ll_cluster_t*, const char * nodename
,			const char *iface);

/*************************************************************************
 * Intracluster messaging
 *************************************************************************/

/*
 *	sendclustermsg:	Send the given message to all cluster members
 */
	int		(*sendclustermsg)(ll_cluster_t*
,			struct ha_msg* msg);
/*
 *	sendnodemsg:	Send the given message to the given node in cluster.
 */
	int		(*sendnodemsg)(ll_cluster_t*
,			struct ha_msg* msg
,			const char * nodename);

/*
 *	inputfd:	Return fd which can be given to select(2) or poll(2)
 *			for determining when messages are ready to be read.
 *			Only to be used in select() or poll(), please...
 */
	int		(*inputfd)(ll_cluster_t*);
/*
 *	msgready:	Returns TRUE (1) when a message is ready to be read.
 */
	int		(*msgready)(ll_cluster_t*);
/*
 *	setmsgsignal:	Associates the given signal with the "message waiting"
 *			condition.
 */
	int		(*setmsgsignal)(ll_cluster_t*, int signo);
/*
 *	rcvmsg:	Cause the next message to be read - activating callbacks for
 *		processing the message.  If no callback processes the message
 *		it will be ignored.  The message is automatically disposed of.
 *		It returns 1 if a message was received.
 */
	int		(*rcvmsg)(ll_cluster_t*, int blocking);

/*
 *	Return next message not intercepted by a callback.
 *	NOTE: you must dispose of this message by calling ha_msg_del().
 */
	struct ha_msg* (*readmsg)(ll_cluster_t*, int blocking);

/*
 *************************************************************************
 * Debugging
 *************************************************************************
 *
 *	setfmode: Set filter mode.  Analagous to promiscous mode in TCP.
 *
 *	LLC_FILTER_DEFAULT (default)
 *		In this mode, all messages destined for this pid
 *		are received, along with all that don't go to specific pids.
 *
 *	LLC_FILTER_PMODE See all messages, but filter heart beats
 *
 *				that don't tell us anything new.
 *	LLC_FILTER_ALLHB See all heartbeats, including those that
 *				 don't change status.
 *	LLC_FILTER_RAW	See all packets, from all interfaces, even
 *			dups.  Pkts with auth errors are still ignored.
 *
 *	Set filter mode.  Analagous to promiscous mode in TCP.
 *
 */
#	define	LLC_FILTER_DEFAULT	0
#	define	LLC_FILTER_PMODE	1
#	define	LLC_FILTER_ALLHB	2
#	define	LLC_FILTER_RAW		3

	int (*setfmode)(ll_cluster_t*, int mode);

	const char * (*errmsg)(ll_cluster_t*);
};

ll_cluster_t*
ll_cluster_new(const char * llctype);


