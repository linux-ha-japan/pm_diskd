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

struct llc_ops {
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
	int		(*set_msg_callback) (const char * msgtype
	,			llc_msg_callback_t callback, void * p);

/*
 *	set_nstatus_callback:	Define callback for node status messages
 *				This is a message of type "st"
 *
 *	cbf:		callback function.
 *
 *	p:		private data - later passed to callback.
 */

	int		(*set_nstatus_callback) (llc_nstatus_callback_t cbf
	,		void * p);
/*
 *	set_ifstatus_callback:	Define callback for interface status messages
 *				This is a message of type "???"
 *			These messages are issued whenever an interface goes
 *			dead or becomes active again.
 *
 *	cbf:		callback function.
 *
 *	node:		the name of the node to get the interface updates for
 *			If node is NULL, it will receive notification for all
 *			nodes.
 *	
 *	iface:		The name of the interface to receive updates for.  If
 *			iface is NULL, it will receive notification for all
 *			interfaces.
 *
 *		If NULL is passed for both "node" and "iface", then "cbf" would
 *		be called for interface status change against any node in
 *		the cluster. 
 *
 *	p:		private data - later passed to callback.
 */

	int             (*set_ifstatus_callback) (llc_ifstatus_callback_t cbf,
                        const char * node, const char * iface, void * p);
 

/*
 *************************************************************************
 * Getting Current Information
 *************************************************************************
 */

/*
 *	init_nodewalk:	Initialize walk through list of list of known nodes
 */
	int		(*init_nodewalk)(void);
/*
 *	nextnode:	Return next node in the list of known nodes
 */
	const char *	(*nextnode)(void);
/*
 *	end_nodewalk:	End walk through the list of known nodes
 */
	int		(*end_nodewalk)(void);
/*
 *	node_status:	Return most recent heartbeat status of the given node
 */
	int		(*node_status)(const char * nodename);
/*
 *	init_ifwalk:	Initialize walk through list of list of known interfaces
 */
	int		(*init_ifwalk)(const char * node);
/*
 *	nextif:	Return next node in the list of known interfaces on node
 */
	const char *	(*nextif)(void);
/*
 *	end_ifwalk:	End walk through the list of known interfaces
 */
	int		(*end_ifwalk)(void);
/*
 *	if_status:	Return current status of the given interface
 */
	int		(*if_status)(const char * nodename, const char *iface);

/*
 *************************************************************************
 * Intracluster messaging
 *************************************************************************
 */

/*
 *	sendclustermsg:	Send the given message to all cluster members
 */
	int		(*sendclustermsg)(const struct ha_msg* msg);
/*
 *	sendnodemsg:	Send the given message to the given node in cluster.
 */
	int		(*sendnodemsg)(const struct ha_msg* msg
	,		const char * nodename);

/*
 *	inputfd:	Return fd which can be given to select(2) or poll(2)
 *			for determining when messages are ready to be read.
 *			Only to be used in select() or poll(), please...
 */
	int		(*inputfd)(void);
/*
 *	msgready:	Returns TRUE (1) when a message is ready to be read.
 */
	int		(*msgready)(void);
/*
 *	setmsgsignal:	Associates the given signal with the "message waiting"
 *			condition.
 */
	int		(*setmsgsignal)(int signo);
/*
 *	rcvmsg:	Cause the next message to be read - activating callbacks for
 *		processing the message.
 */
	int		(*rcvmsg)(int blocking);

/*
 *	Read the next message without any silly callbacks.
 *	(at least the next one not intercepted by another callback).
 *	NOTE: you must dispose of this message by calling ha_msg_del().
 */
	struct ha_msg* (*readmsg)(int blocking);

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

/* Do we need these higher levels ? */

#	define	LLC_FILTER_ALLHB	2
#	define	LLC_FILTER_RAW		3

	struct ha_msg*	(*setfmode)(int mode);
};


struct ll_cluster {
	void *		ll_cluster_private;
	struct llc_ops*	llc_ops;
};
