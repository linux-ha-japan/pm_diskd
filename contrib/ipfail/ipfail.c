/* ipfail.c
 * Author: Kevin Dwyer <Kevin.Dwyer@algx.net>/<kevin@pheared.net>
 * IP Failover plugin for Linux-HA.
 * This plugin uses ping nodes to determine a failure in an
 * interface's connectivity and forces a hb_standby. It is based on the
 * api_test.c program included with Linux-HA.
 * 
 * Setup: In your ha.cf file make sure you have a ping node setup for each
 *        interface.  Choosing something like the switch that you are connected
 *        to is a good idea.  Choosing your win95 reboot-o-matic is a bad idea.
 *        
 *        The way this works is by taking note of when a ping node dies.  
 *        When a death is detected, it communicates with the other side to see
 *        if the other side saw it die (sort of).  If it didn't, then we know
 *        who deserves to have the resources.
 *
 * There are ways to improve this, and I'm working on them.
 *
 */
/* 
 * api_test: Test program for testing the heartbeat API
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <portability.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <libgen.h>
#include <heartbeat.h>
#include <ha_msg.h>
#include <hb_api.h>
#include <clplumbing/cl_log.h>
#include <clplumbing/cl_signal.h>

void NodeStatus(const char *node, const char *status, void *private);
void LinkStatus(const char *node, const char *, const char *, void *);
void msg_ipfail_join(const struct ha_msg *, void *);
void msg_ping_nodes(const struct ha_msg *, void *);
void i_am_dead(const struct ha_msg *, void *);
void got_wake_up(const struct ha_msg *, void *);
void msg_resources(const struct ha_msg *, void *);
void gotsig(int);
void giveup(ll_cluster_t *);
void you_are_dead(ll_cluster_t *);
int ping_node_status(ll_cluster_t *);
void ask_ping_nodes(ll_cluster_t *, int);
void wake_up(ll_cluster_t *);

/* ICK! global vars. */
char node_name[200];	/* The node we are connected to */
char other_node[200];	/* The remote node in the pair */
int node_stable;        /* Other node stable? */
int need_standby;       /* Are we waiting for stability? */

void
NodeStatus(const char *node, const char *status, void *private)
{
	/* Callback for node status changes */

	cl_log(LOG_INFO, "Status update: Node %s now has status %s"
	,	node, status);
	if (strcmp(status, DEADSTATUS) == 0) {
		if (ping_node_status(private)) {
			cl_log(LOG_INFO, "NS: We are still alive!");
		}else{
			cl_log(LOG_INFO, "NS: We are dead. :<");
		}
	} else if (strcmp(status, PINGSTATUS) == 0) {
		/* A ping node just came up, if we died, request resources?
		 * If so, that would emulate the primary/secondary type of
		 * High-Availability, instead of nice_failback mode
		 */

		/* Lets make sure we weren't both down, and now half up. */
		int num_ping;

		num_ping = ping_node_status(private);
		ask_ping_nodes(private, num_ping);
		cl_log(LOG_INFO, "Checking remote count of ping nodes.");
	}
}

void
LinkStatus(const char *node, const char *lnk, const char *status,
		void *private)
{
	/* Callback for Link status changes */

	int num_ping=0;

	cl_log(LOG_INFO, "Link Status update: Link %s/%s now has status %s"
	,	node, lnk, status);

	if (strcmp(status, DEADSTATUS) == 0) {
		/* If we can still see pinging node, request resources */
		if ((num_ping = ping_node_status(private))) {
			ask_ping_nodes(private, num_ping);
			cl_log(LOG_INFO, "Checking remote count of ping nodes.");
		}
		else {
			cl_log(LOG_INFO, "We are dead. :<");
			giveup(private);
		}
	}
}

int
ping_node_status(ll_cluster_t *hb)
{
	/* ping_node_status: Takes the hearbeat cluster as input, 
	 * returns number of ping nodes found to be in the cluster, 
	 * and therefore alive.
	 */

	const char *node;
	int found=0;       /* Number of ping nodes found */

	if (hb->llc_ops->init_nodewalk(hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot start node walk");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(16);
	}
	while((node = hb->llc_ops->nextnode(hb))!= NULL) {

		if (strcmp(PINGSTATUS, 
				hb->llc_ops->node_status(hb, node)) == 0) {
			cl_log(LOG_DEBUG, "Found ping node %s!", node);
			found++;
		}
	}
	if (hb->llc_ops->end_nodewalk(hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot end node walk");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(17);
	}

	return found;
}

void
giveup(ll_cluster_t *hb)
{
	/* Giveup: Takes the heartbeat cluster as input, returns nothing.
	 * Forces the local node to go to standby.
	 */

	struct ha_msg *msg;
	char pid[10];

	if (node_stable) {
		memset(pid, 0, sizeof(pid));
		snprintf(pid, sizeof(pid), "%ld", (long)getpid());

		msg = ha_msg_new(3);
		ha_msg_add(msg, F_TYPE, T_ASKRESOURCES);
		ha_msg_add(msg, F_ORIG, node_name);
		ha_msg_add(msg, F_COMMENT, "me");

		hb->llc_ops->sendclustermsg(hb, msg);
		cl_log(LOG_DEBUG, "Message [" T_ASKRESOURCES "] sent.");
		ha_msg_del(msg);
		need_standby = 0;
	} else {
		need_standby = 1;
	}
}

void
msg_ipfail_join(const struct ha_msg *msg, void *private)
{
	/* msg_ipfail_join: When another ipfail client sends a join 
	 * message, call ask_ping_nodes() to compare ping node counts.
	 * Callback for the T_APICLISTAT message. 
	 */

	const char *type;
	const char *orig;
	if ((type = ha_msg_value(msg, F_TYPE)) == NULL) {
		type = "?";
	}
	if ((orig = ha_msg_value(msg, F_ORIG)) == NULL) {
		orig = "?";
	}

	if ((node_name[0] != 0) && (other_node[0] == 0) &&
	    (orig[0] != '?') && (strcmp(node_name, orig) != 0)) {
		strcpy(other_node, orig);
		cl_log(LOG_DEBUG, "[They are %s]", other_node);
		wake_up(private);
	}
	if ((node_name[0] == 0) && (orig[0] != '?')) {
		strcpy(node_name, orig);
		cl_log(LOG_DEBUG, "[We are %s]", node_name);
	}


	/* If this is a join message from ipfail on a different node.... */
	if (!strcmp(ha_msg_value(msg, F_STATUS), JOINSTATUS) &&
	    !strcmp(ha_msg_value(msg, F_FROMID), "ipfail")   && 
	    strcmp(ha_msg_value(msg, F_ORIG),   node_name)) {
		cl_log(LOG_DEBUG, 
		       "Got join message from another ipfail client. (%s)",
		       ha_msg_value(msg, F_ORIG));
		ask_ping_nodes(private, ping_node_status(private));
	}
}

void
msg_resources(const struct ha_msg *msg, void *private)
{
	/* msg_resources: Catch T_RESOURCES messages, so that we can
	 * find out when stability is achieved among the cluster
	 */

	/* Right now there are two stable messages sent out, we are
	 * only concerned with the one that has no info= line on it.
	 */
	if (!strcmp(ha_msg_value(msg, F_ORIG), other_node) &&
	    !ha_msg_value(msg, F_COMMENT) &&
	    !strcmp(ha_msg_value(msg, F_ISSTABLE), "1")) {

		cl_log(LOG_DEBUG, "Other side is now stable.");
		node_stable = 1;
		
		/* There may be a pending standby */
		if (need_standby) {
			/* Gratuitious ARPs take some time, is there a
			 * way to know when they're finished?  I don't
			 * want this sleep here, even if it only is during
			 * startup.
			 */

			/* This value is prone to be wrong for different
			 * situations.  We need the resource stability
			 * message to be delayed until the resource scripts
			 * finish, and then we can stop waiting.
			 */
			sleep(10);


			/* If the resource message stuff is solved, we could
			 * safely giveup() here.  However, since we're waiting
			 * for arbitrary amounts of time it may be wise to
			 * recheck the assumptions of the cluster and count
			 * ping nodes.
			 */
			ask_ping_nodes(private, ping_node_status(private));
			//giveup(private);
		}
	}

	else if (!strcmp(ha_msg_value(msg, F_ORIG), other_node) &&
		 !strcmp(ha_msg_value(msg, F_ISSTABLE), "0")) {

		cl_log(LOG_DEBUG, "Other side is unstable.");
		node_stable = 0;
	
	}
}

void
ask_ping_nodes(ll_cluster_t *hb, int num_ping)
{
	/* ask_ping_nodes: Takes the heartbeat cluster and the number of
	 * ping nodes we can see alive as input, returning nothing.
	 * It asks the other node for the number of ping nodes it can see.
	 */

	struct ha_msg *msg;
	char pid[10], np[5];

	cl_log(LOG_DEBUG, "Asking other side for num_ping.");
	memset(pid, 0, sizeof(pid));
	snprintf(pid, sizeof(pid), "%ld", (long)getpid());
	memset(np, 0, sizeof(np));
	snprintf(np, sizeof(np), "%d", num_ping);

	msg = ha_msg_new(3);
	ha_msg_add(msg, F_TYPE, "num_ping_nodes");
	ha_msg_add(msg, F_ORIG, node_name);
	ha_msg_add(msg, "num_ping", np);

	hb->llc_ops->sendnodemsg(hb, msg, other_node);
	cl_log(LOG_DEBUG, "Message [num_ping] sent.");
	ha_msg_del(msg);
}

void
msg_ping_nodes(const struct ha_msg *msg, void *private)
{
	/* msg_ping_nodes: Takes the message and heartbeat cluster as input;
	 * returns nothing.  Callback for the num_ping_nodes message.
	 */

	int num_nodes=0;

	cl_log(LOG_DEBUG, "Got asked for num_ping.");
	num_nodes = ping_node_status(private);
	if (num_nodes > atoi(ha_msg_value(msg, "num_ping"))) {
		you_are_dead(private);
	}
	else if (num_nodes < atoi(ha_msg_value(msg, "num_ping"))) {
		giveup(private);
	}
}

void
you_are_dead(ll_cluster_t *hb)
{
	/* you_are_dead: Takes the heartbeat cluster as input; returns nothing.
	 * Sends the you_are_dead message to the dead node.
	 */

	struct ha_msg *msg;
	char pid[10];

	cl_log(LOG_DEBUG, "Sending you_are_dead.");

	memset(pid, 0, sizeof(pid));
	snprintf(pid, sizeof(pid), "%ld", (long)getpid());

	msg = ha_msg_new(1);
	ha_msg_add(msg, F_TYPE, "you_are_dead");

	hb->llc_ops->sendnodemsg(hb, msg, other_node);
	cl_log(LOG_DEBUG, "Message [you_are_dead] sent.");
	ha_msg_del(msg);
}

void
i_am_dead(const struct ha_msg *msg, void *private)
{
	/* i_am_dead: Takes the you_are_dead message and the heartbeat cluster
	 * as input; returns nothing.
	 * Callback for the you_are_dead message.
	 */

	cl_log(LOG_DEBUG, "Got you_are_dead.");
	giveup(private);
}

void
wake_up(ll_cluster_t *hb)
{
	/* wake_up: Takes the heartbeat cluster as input; returns nothing.
	 * Used for initial syncing of cluster names.  Sending this message 
	 * forces the other side to see your node name.
	 */

	struct ha_msg *msg;

	msg = ha_msg_new(1);
	ha_msg_add(msg, F_TYPE, "wake_up");
	hb->llc_ops->sendnodemsg(hb, msg, other_node);
	ha_msg_del(msg);
}

void
got_wake_up(const struct ha_msg *msg, void *private)
{
	/* got_wake_up: When we see a wake_up message, we can 
	 * record the other node's name.
	 */

	const char *orig;

	if ((orig = ha_msg_value(msg, F_ORIG)) != NULL) {
		strcpy(other_node, orig);
		cl_log(LOG_DEBUG, "[They are %s]", other_node);
	}	
}

int quitnow = 0;

void
gotsig(int nsig)
{
	(void)nsig;
	quitnow = 1;
}


#define	DEFAULT_FACILITY	LOG_DAEMON

int
main(int argc, char **argv)
{
	struct ha_msg *reply;
	unsigned fmask;
	ll_cluster_t *hb;
	const char *node;
/*	const char *intf;  --Out until ifwalk is fixed */
	char pid[10];
	char *bname;
	int	facility;

	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;
	cl_log_enable_stderr(TRUE);
	bname = strdup(argv[0]);
	cl_log_set_entity(basename(bname));
	cl_log_set_facility(DEFAULT_FACILITY);

	hb = ll_cluster_new("heartbeat");

	memset(node_name, 0, sizeof(node_name));
	memset(other_node, 0, sizeof(other_node));
	node_stable = 0;
	need_standby = 0;

	memset(pid, 0, sizeof(pid));
	snprintf(pid, sizeof(pid), "%ld", (long)getpid());
	cl_log(LOG_DEBUG, "PID=%s", pid);

	cl_log(LOG_DEBUG, "Signing in with heartbeat");
	if (hb->llc_ops->signon(hb, "ipfail")!= HA_OK) {
		cl_log(LOG_ERR, "Cannot sign on with heartbeat");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(1);
	}
	if ((facility = hb->llc_ops->get_logfacility(hb)) <= 0) {
		facility = DEFAULT_FACILITY;
	}
	cl_log_set_facility(facility);

	if (hb->llc_ops->set_msg_callback(hb, T_APICLISTAT, 
					  msg_ipfail_join, hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set msg_ipfail_join callback");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(2);
	}

	if (hb->llc_ops->set_msg_callback(hb, T_RESOURCES, 
					  msg_resources, hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set msg_resources callback");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(18);
	}

	if (hb->llc_ops->set_msg_callback(hb, "num_ping_nodes", 
					  msg_ping_nodes, hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set msg callback");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(3);
	}

	if (hb->llc_ops->set_msg_callback(hb, "wake_up", 
					  got_wake_up, hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set msg callback");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(4);
	}

	if (hb->llc_ops->set_msg_callback(hb, "you_are_dead", 
					  i_am_dead, hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set i_am_dead callback");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(5);
	}

	if (hb->llc_ops->set_nstatus_callback(hb, NodeStatus, hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set node status callback");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(6);
	}

	if (hb->llc_ops->set_ifstatus_callback(hb, LinkStatus, hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set if status callback");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(7);
	}

	fmask = LLC_FILTER_DEFAULT;

	cl_log(LOG_DEBUG, "Setting message filter mode");
	if (hb->llc_ops->setfmode(hb, fmask) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set filter mode");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(8);
	}

	cl_log(LOG_DEBUG, "Starting node walk");
	if (hb->llc_ops->init_nodewalk(hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot start node walk");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(9);
	}
	while((node = hb->llc_ops->nextnode(hb))!= NULL) {
		cl_log(LOG_DEBUG, "Cluster node: %s: status: %s", node
		,	hb->llc_ops->node_status(hb, node));

		/* ifwalking is broken for ping nodes.  I don't think we even
		   need it at this point.

		if (hb->llc_ops->init_ifwalk(hb, node) != HA_OK) {
			cl_log(LOG_ERR, "Cannot start if walk");
			cl_log(LOG_ERR, "REASON: %s"
			,	hb->llc_ops->errmsg(hb));
			exit(10);
		}
		while ((intf = hb->llc_ops->nextif(hb))) {
			cl_log(LOG_DEBUG, "\tnode %s: intf: %s ifstatus: %s"
			,	node, intf
			,	hb->llc_ops->if_status(hb, node, intf));
		}
		if (hb->llc_ops->end_ifwalk(hb) != HA_OK) {
			cl_log(LOG_ERR, "Cannot end if walk");
			cl_log(LOG_ERR, "REASON: %s"
			,	hb->llc_ops->errmsg(hb));
			exit(11);
		}
		-END of ifwalkcode */
	}
	if (hb->llc_ops->end_nodewalk(hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot end node walk");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(12);
	}

	CL_SIGINTERRUPT(SIGINT, 1);
	CL_SIGNAL(SIGINT, gotsig);

	cl_log(LOG_DEBUG, "Setting message signal");
	if (hb->llc_ops->setmsgsignal(hb, 0) != HA_OK) {
		cl_log(LOG_ERR, "Cannot set message signal");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(13);
	}

	cl_log(LOG_DEBUG, "Waiting for messages...");
	errno = 0;
	cl_log_enable_stderr(FALSE);


	for(; !quitnow && (reply=hb->llc_ops->readmsg(hb, 1)) != NULL;) {
		ha_log_message(reply);
		ha_msg_del(reply); reply=NULL;
	}

	if (!quitnow) {
		cl_perror("read_hb_msg returned NULL");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
	}
	if (hb->llc_ops->signoff(hb) != HA_OK) {
		cl_log(LOG_ERR, "Cannot sign off from heartbeat.");
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		exit(14);
	}
	if (hb->llc_ops->delete(hb) != HA_OK) {
		cl_log(LOG_ERR, "REASON: %s", hb->llc_ops->errmsg(hb));
		cl_log(LOG_ERR, "Cannot delete API object.");
		exit(15);
	}

	return 0;
}


