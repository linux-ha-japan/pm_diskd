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
#include <signal.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <syslog.h>
#include <heartbeat.h>
#include <hb_api_core.h>
#include <hb_api.h>

#ifndef LOG_PERROR
#define LOG_PERROR 0x0 /* Some syslogs don't allow you to log messages
                        * to stderr as well as to a log facility */
			* (Solaris, perhaps others)
			*/
#endif /* sun */

/*
 * A heartbeat API test program...
 */

void NodeStatus(const char * node, const char * status, void * private);
void LinkStatus(const char * node, const char *, const char *, void*);
void gotsig(int nsig);

void
NodeStatus(const char * node, const char * status, void * private)
{
	syslog(LOG_NOTICE, "Status update: Node %s now has status %s\n"
	,	node, status);
}

void
LinkStatus(const char * node, const char * lnk, const char * status
,	void * private)
{
	syslog(LOG_NOTICE, "Link Status update: Link %s/%s now has status %s\n"
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

	openlog("ping-test", LOG_PERROR|LOG_PID, LOG_LOCAL7);
	hb = ll_cluster_new("heartbeat");
	fprintf(stderr, "PID=%ld\n", (long)getpid());
	fprintf(stderr, "Signing in with heartbeat\n");
	if (hb->llc_ops->signon(hb, "ping")!= HA_OK) {
		syslog(LOG_ERR, "Cannot sign on with heartbeat\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(1);
	}

	if (hb->llc_ops->set_nstatus_callback(hb, NodeStatus, NULL) !=HA_OK){
		syslog(LOG_ERR, "Cannot set node status callback\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(2);
	}

	if (hb->llc_ops->set_ifstatus_callback(hb, LinkStatus, NULL)!=HA_OK){
		syslog(LOG_ERR, "Cannot set if status callback\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(3);
	}

#if 0
	fmask = LLC_FILTER_RAW;
#else
	fmask = LLC_FILTER_DEFAULT;
#endif
	fprintf(stderr, "Setting message filter mode\n");
	if (hb->llc_ops->setfmode(hb, fmask) != HA_OK) {
		syslog(LOG_ERR, "Cannot set filter mode\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(4);
	}

	syslog(LOG_INFO, "Starting node walk\n");
	if (hb->llc_ops->init_nodewalk(hb) != HA_OK) {
		syslog(LOG_ERR, "Cannot start node walk\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(5);
	}
	while((node = hb->llc_ops->nextnode(hb))!= NULL) {
		syslog(LOG_INFO, "Cluster node: %s: status: %s\n", node
		,	hb->llc_ops->node_status(hb, node));
		if (hb->llc_ops->init_ifwalk(hb, node) != HA_OK) {
			syslog(LOG_ERR, "Cannot start if walk\n");
			syslog(LOG_ERR, "REASON: %s\n"
			,	hb->llc_ops->errmsg(hb));
			exit(6);
		}
		while ((intf = hb->llc_ops->nextif(hb))) {
			syslog(LOG_ERR, "\tnode %s: intf: %s ifstatus: %s\n"
			,	node, intf
			,	hb->llc_ops->if_status(hb, node, intf));
		}
		if (hb->llc_ops->end_ifwalk(hb) != HA_OK) {
			syslog(LOG_ERR, "Cannot end if walk\n");
			syslog(LOG_ERR, "REASON: %s\n"
			,	hb->llc_ops->errmsg(hb));
			exit(7);
		}
	}
	if (hb->llc_ops->end_nodewalk(hb) != HA_OK) {
		syslog(LOG_ERR, "Cannot end node walk\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(8);
	}

	siginterrupt(SIGINT, 1);
	signal(SIGINT, gotsig);

#if 0
	syslog(LOG_INFO, "Setting message signal\n");
	if (hb->llc_ops->setmsgsignal(hb, 0) != HA_OK) {
		syslog(LOG_ERR, "Cannot set message signal\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(9);
	}

#endif
	pingreq = ha_msg_new(0);
	ha_msg_add(pingreq, F_TYPE, "ping");
	syslog(LOG_INFO, "Sleeping...\n");
	sleep(5);
	if (hb->llc_ops->sendclustermsg(hb, pingreq) == HA_OK) {
		syslog(LOG_ERR, "Sent ping request to cluster\n");
	}else{
		syslog(LOG_ERR, "PING request FAIL to cluster\n");
	}
	syslog(LOG_ERR, "Waiting for messages...\n");
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
		syslog(LOG_NOTICE, "Got message %d of type [%s] from [%s]\n"
		,	msgcount, type, orig);
#if 0
		ha_log_message(reply);
		syslog(LOG_NOTICE, "%s", hb->llc_ops->errmsg(hb));
#endif
		if (strcmp(type, "ping") ==0) {
			struct ha_msg*	pingreply = ha_msg_new(4);
			int	count;

			ha_msg_add(pingreply, F_TYPE, "pingreply");

			for (count=0; count < 10; ++count) {
				if (hb->llc_ops->sendnodemsg(hb, pingreply, orig)
				==	HA_OK) {
					syslog(LOG_INFO
					,	"Sent ping reply(%d) to [%s]\n"
					,	count, orig);
				}else{
					syslog(LOG_ERR, "PING %d FAIL to [%s]\n"
					,	count, orig);
				}
			}
			ha_msg_del(pingreply); pingreply=NULL;
		}
		ha_msg_del(reply); reply=NULL;
	}

	if (!quitnow) {
		syslog(LOG_ERR, "read_hb_msg returned NULL");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
	}
	if (hb->llc_ops->signoff(hb) != HA_OK) {
		syslog(LOG_ERR, "Cannot sign off from heartbeat.\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(10);
	}
	if (hb->llc_ops->delete(hb) != HA_OK) {
		syslog(LOG_ERR, "Cannot delete API object.\n");
		syslog(LOG_ERR, "REASON: %s\n", hb->llc_ops->errmsg(hb));
		exit(11);
	}
	return 0;
}
