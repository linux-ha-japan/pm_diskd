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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <heartbeat.h>
#include <hb_api_core.h>
#include <hb_api.h>

/*
 * A heartbeat API test program...
 */

void NodeStatus(const char * node, const char * status, void * private);
void LinkStatus(const char * node, const char *, const char *, void*);
void gotsig(int nsig);

void
NodeStatus(const char * node, const char * status, void * private)
{
	fprintf(stderr, "Status update: Node %s now has status %s\n"
	,	node, status);
}

void
LinkStatus(const char * node, const char * lnk, const char * status
,	void * private)
{
	fprintf(stderr, "Link Status update: Link %s/%s now has status %s\n"
	,	node, lnk, status);
}

int quitnow = 0;
void gotsig(int nsig)
{
	(void)nsig;
	quitnow = 1;
}



int
main(int argc, char ** argv)
{
	struct ha_msg*	reply;
	struct ha_msg*	pingreq = NULL;
	unsigned	fmask;
	ll_cluster_t*	hb;
	const char *	node;
	const char *	intf;
	int		msgcount=0;

	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	hb = ll_cluster_new("heartbeat");
	fprintf(stderr, "PID=%ld\n", (long)getpid());
	fprintf(stderr, "Signing in with heartbeat\n");
	if (hb->llc_ops->signon(hb, "ping")!= HA_OK) {
		fprintf(stderr, "Cannot sign on with heartbeat\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(1);
	}

	if (hb->llc_ops->set_nstatus_callback(hb, NodeStatus, NULL) !=HA_OK){
		fprintf(stderr, "Cannot set node status callback\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(2);
	}

	if (hb->llc_ops->set_ifstatus_callback(hb, LinkStatus, NULL)!=HA_OK){
		fprintf(stderr, "Cannot set if status callback\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(3);
	}

#if 0
	fmask = LLC_FILTER_RAW;
#else
	fmask = LLC_FILTER_DEFAULT;
#endif
	fprintf(stderr, "Setting message filter mode\n");
	if (hb->llc_ops->setfmode(hb, fmask) != HA_OK) {
		fprintf(stderr, "Cannot set filter mode\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(4);
	}

	fprintf(stderr, "Starting node walk\n");
	if (hb->llc_ops->init_nodewalk(hb) != HA_OK) {
		fprintf(stderr, "Cannot start node walk\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(5);
	}
	while((node = hb->llc_ops->nextnode(hb))!= NULL) {
		fprintf(stderr, "Cluster node: %s: status: %s\n", node
		,	hb->llc_ops->node_status(hb, node));
		if (hb->llc_ops->init_ifwalk(hb, node) != HA_OK) {
			fprintf(stderr, "Cannot start if walk\n");
			fprintf(stderr, "REASON: %s\n"
			,	hb->llc_ops->errmsg(hb));
			exit(6);
		}
		while ((intf = hb->llc_ops->nextif(hb))) {
			fprintf(stderr, "\tnode %s: intf: %s ifstatus: %s\n"
			,	node, intf
			,	hb->llc_ops->if_status(hb, node, intf));
		}
		if (hb->llc_ops->end_ifwalk(hb) != HA_OK) {
			fprintf(stderr, "Cannot end if walk\n");
			fprintf(stderr, "REASON: %s\n"
			,	hb->llc_ops->errmsg(hb));
			exit(7);
		}
	}
	if (hb->llc_ops->end_nodewalk(hb) != HA_OK) {
		fprintf(stderr, "Cannot end node walk\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(8);
	}

	siginterrupt(SIGINT, 1);
	signal(SIGINT, gotsig);

#if 0
	fprintf(stderr, "Setting message signal\n");
	if (hb->llc_ops->setmsgsignal(hb, 0) != HA_OK) {
		fprintf(stderr, "Cannot set message signal\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(9);
	}

#endif
	pingreq = ha_msg_new(0);
	ha_msg_add(pingreq, F_TYPE, "ping");
	fprintf(stderr, "Sleeping...\n");
	sleep(5);
	if (hb->llc_ops->sendclustermsg(hb, pingreq) == HA_OK) {
		fprintf(stderr, "Sent ping request to cluster\n");
	}else{
		fprintf(stderr, "PING request FAIL to cluster\n");
	}
	fprintf(stderr, "Waiting for messages...\n");
	errno = 0;
	for(; !quitnow && (reply=hb->llc_ops->readmsg(hb, 1)) != NULL;) {
		const char *	type;
		const char *	orig;
		++msgcount;
		if ((type = ha_msg_value(reply, F_TYPE)) == NULL) {
			type = "?";
		}
		if ((orig = ha_msg_value(reply, F_ORIG)) == NULL) {
			orig = "?";
		}
		fprintf(stderr, "Got message %d of type [%s] from [%s]\n"
		,	msgcount, type, orig);
		ha_log_message(reply);
		if (strcmp(type, "ping") ==0) {
			struct ha_msg*	pingreply = ha_msg_new(4);
			int	count;

			ha_msg_add(pingreply, F_TYPE, "pingreply");

			for (count=0; count < 10; ++count) {
				if (hb->llc_ops->sendnodemsg(hb, pingreply, orig)
				==	HA_OK) {
					fprintf(stderr, "Sent ping reply(%d) to [%s]\n"
					,	count, orig);
				}else{
					fprintf(stderr, "PING %d FAIL to [%s]\n"
					,	count, orig);
				}
			}
			ha_msg_del(pingreply); pingreply=NULL;
		}
		ha_msg_del(reply); reply=NULL;
	}

	if (!quitnow) {
		perror("read_hb_msg returned NULL");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
	}
	if (hb->llc_ops->signoff(hb) != HA_OK) {
		fprintf(stderr, "Cannot sign off from heartbeat.\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(10);
	}
	if (hb->llc_ops->delete(hb) != HA_OK) {
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		fprintf(stderr, "Cannot delete API object.\n");
		fprintf(stderr, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(11);
	}
	return 0;
}
