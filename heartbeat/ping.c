static const char _udp_Id [] = "$Id: ping.c,v 1.9 2001/06/07 21:29:44 alan Exp $";
/*
 * ping.c: ICMP-echo-based heartbeat code for heartbeat.
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 *
 * The checksum code in this file code was borrowed from the ping program.
 *
 * SECURITY NOTE:  It would be very easy for someone to masquerade as the
 * device that you're pinging.  If they don't know the password, all they can
 * do is echo back the packets that you're sending out, or send out old ones.
 * This does mean that if you're using such an approach, that someone could
 * make you think you have quorum when you don't during a cluster partition.
 * The danger in that seems small, but you never know ;-)
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

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/param.h>

#ifdef HAVE_NETINET_IN_SYSTM_H
#	include <netinet/in_systm.h>
#endif /* HAVE_NETINET_IN_SYSTM_H */

#ifdef HAVE_NETINET_IP_VAR_H
#	include <netinet/ip_var.h>
#endif /* HAVE_NETINET_IP_VAR_H */

#ifdef HAVE_NETINET_IP_H
#	include <netinet/ip.h>
#endif /* HAVE_NETINET_IP_H */

#ifdef HAVE_NETINET_IP_COMPAT_H
#	include <netinet/ip_compat.h>
#endif /* HAVE_NETINET_IP_COMPAT_H */

#ifdef HAVE_NETINET_IP_FW_H
#	include <netinet/ip_fw.h>
#endif /* HAVE_NETINET_IP_FW_H */

#include <sys/socket.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include "heartbeat.h"

#ifdef linux
#	define	ICMP_HDR_SZ	sizeof(struct icmphdr)	/* 8 */
#else
#	define	ICMP_HDR_SZ	8
#endif

#define	EOS	'\0'

#define MODULE ping
#include <hb_module.h>

struct ping_private {
        struct sockaddr_in      addr;   	/* ping addr */
        int    			sock;		/* ping socket */
	int			ident;		/* heartbeat pid */
	int			iseq;		/* sequence number */
};


int	EXPORT(hb_dev_init) (void);
struct hb_media* EXPORT(hb_dev_new) (const char* interface);
int	EXPORT(hb_dev_open) (struct hb_media* mp);
int	EXPORT(hb_dev_close) (struct hb_media* mp);
struct ha_msg* EXPORT(hb_dev_read) (struct hb_media* mp);
int	EXPORT(hb_dev_write) (struct hb_media* mp, struct ha_msg* msg);

static struct ping_private *
		new_ping_interface(const char * host);
static int in_cksum (u_short * buf, int nbytes);

int EXPORT(hb_dev_mtype) (char **buffer);
int EXPORT(hb_dev_descr) (char **buffer);
int EXPORT(hb_dev_isping) (void);


#define		ISPINGOBJECT(mp)	((mp) && ((mp)->vf == (void*)&ping_media_fns))
//#define		PINGASSERT(mp)	ASSERT(ISPINGOBJECT(mp))
#define		PINGASSERT(mp)

int
EXPORT(hb_dev_mtype) (char **buffer) { 
	
	*buffer = ha_malloc((strlen("ping") * sizeof(char)) + 1);

	strcpy(*buffer, "ping");

	return strlen("ping");
}

int
EXPORT(hb_dev_descr) (char **buffer) { 

	const char *str = "ping membership";	

	*buffer = ha_malloc((strlen(str) * sizeof(char)) + 1);

	strcpy(*buffer, str);

	return strlen(str);
}

/* Yes, a ping device */

int
EXPORT(hb_dev_isping) (void) {
	return 1;
}

int
EXPORT(hb_dev_init) (void)
{
	(void)_heartbeat_h_Id;
	(void)_udp_Id;
	(void)_ha_msg_h_Id;
	return(HA_OK);
}

static struct ping_private *
new_ping_interface(const char * host)
{
	struct ping_private*	ppi;
	struct sockaddr_in *to;

	if ((ppi = MALLOCT(struct ping_private)) == NULL) {
		return NULL;
	}
	memset(ppi, 0, sizeof (*ppi));
  	to = &ppi->addr;

#ifdef HAVE_SOCKADDR_IN_SIN_LEN
	ppi->addr.sin_len = sizeof(struct sockaddr_in);
#endif
	ppi->addr.sin_family = AF_INET;

	if (inet_aton(host, &ppi->addr.sin_addr) == 0) {
		struct hostent *hep;
		hep = gethostbyname(host);
		if (hep == NULL) {
			ha_perror("unknown host: %s", host);
			ha_free(ppi); ppi = NULL;
			return NULL;
		}
		ppi->addr.sin_family = hep->h_addrtype;
		memcpy(&ppi->addr.sin_addr, hep->h_addr, hep->h_length);
	}
	ppi->ident = getpid() & 0xFFFF;
	return(ppi);
}

/*
 *	Create new ping heartbeat object 
 *	Name of host is passed as a parameter
 */
struct hb_media *
EXPORT(hb_dev_new) (const char * host)
{
	struct ping_private*	ipi;
	struct hb_media *	ret;

	ipi = new_ping_interface(host);
	if (ipi == NULL) {
		return(NULL);
	}

	ret = MALLOCT(struct hb_media);
	if (ret != NULL) {
		char * name;
		ret->pd = (void*)ipi;
		name = ha_malloc(strlen(host)+1);
		strcpy(name, host);
		ret->name = name;
		add_node(host, PINGNODE);

	}else{
		ha_free(ipi); ipi = NULL;
	}
	return(ret);
}

/*
 *	Close UDP/IP broadcast heartbeat interface
 */

int
EXPORT(hb_dev_close) (struct hb_media* mp)
{
	struct ping_private * ei;
	int	rc = HA_OK;

	PINGASSERT(mp);
	ei = (struct ping_private *) mp->pd;

	if (ei->sock >= 0) {
		if (close(ei->sock) < 0) {
			rc = HA_FAIL;
		}
	}
	return(rc);
}

/*
 * Receive a heartbeat ping reply packet.
 */

struct ha_msg *
EXPORT(hb_dev_read) (struct hb_media* mp)
{
	struct ping_private *	ei;
	union {
		char		cbuf[MAXLINE+ICMP_HDR_SZ];
		struct ip	ip;
	}buf;
	int			addr_len = sizeof(struct sockaddr);
   	struct sockaddr_in	their_addr; /* connector's addr information */
	struct ip *		ip;
	struct icmp *		icp;
	int			numbytes;
	int			hlen;

	PINGASSERT(mp);
	ei = (struct ping_private *) mp->pd;

	if ((numbytes=recvfrom(ei->sock, &buf.cbuf, sizeof(buf.cbuf)-1, 0
	,	(struct sockaddr *)&their_addr, &addr_len)) == -1) {
		ha_perror("Error receiving from socket");
		return NULL;
	}
	buf.cbuf[numbytes] = EOS;

	/* Check the IP header */
	ip = &buf.ip;
	hlen = ip->ip_hl * 4;

	if (numbytes < hlen + ICMP_MINLEN) {
		ha_log(LOG_WARNING, "ping packet too short (%d bytes) from %s"
		,	numbytes
		,	inet_ntoa(*(struct in_addr *)
		&		their_addr.sin_addr.s_addr));
		return NULL;
	}

	/* Now the ICMP part */	/* (there may be a better way...) */
	icp = (struct icmp *)(buf.cbuf + hlen);

	if (icp->icmp_type != ICMP_ECHOREPLY || icp->icmp_id != ei->ident) {
		return NULL;
	}

	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "got %d byte packet from %s"
		,	numbytes, inet_ntoa(their_addr.sin_addr));
	}
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, "%s", &icp->icmp_data[0]);
	
	}
	return(string2msg(&icp->icmp_data[0]));
}

/*
 * Send a heartbeat packet over broadcast UDP/IP interface
 *
 * The peculiar thing here is that we don't send the packet we're given at all
 *
 * Instead, we send out the packet we want to hear back from them, just
 * as though we were they ;-)  That's what comes of having such a dumb
 * device as a "member" of our cluster...
 *
 * We ignore packets we're given to write that aren't "status" packets.
 *
 */

int
EXPORT(hb_dev_write) (struct hb_media* mp, struct ha_msg * msg)
{
	struct ping_private *	ei;
	int			rc;
	char*			pkt;
	union{
		char*			buf;
		struct icmp		ipkt;
	}*icmp_pkt;
	int			size;
	struct icmp *		icp;
	int			pktsize;
	const char *		type;
	const char *		ts;
	struct ha_msg *		nmsg;

	PINGASSERT(mp);
	ei = (struct ping_private *) mp->pd;
	type = ha_msg_value(msg, F_TYPE);

	if (type == NULL || strcmp(type, T_STATUS) != 0 
	|| ((ts = ha_msg_value(msg, F_TIME)) == NULL)) {
		return HA_OK;
	}

	/*
	 * We populate the following fields in the packet we create:
	 *
	 * F_TYPE:	T_NS_STATUS
	 * F_STATUS:	ping
	 * F_ORIG:	destination name
	 * F_TIME:	local timestamp (from "msg")
	 * F_AUTH:	added by add_msg_auth()
	 */
	if ((nmsg = ha_msg_new(5)) == NULL) {
		ha_log(LOG_ERR, "cannot create new message");
		return(HA_FAIL);
	}

	if (ha_msg_add(nmsg, F_TYPE, T_NS_STATUS) != HA_OK
	||	ha_msg_add(nmsg, F_STATUS, "ping") != HA_OK
	||	ha_msg_add(nmsg, F_ORIG, mp->name) != HA_OK
	||	ha_msg_add(nmsg, F_TIME, ts) != HA_OK) {
		ha_msg_del(nmsg); nmsg = NULL;
		ha_log(LOG_ERR, "cannot add fields to message");
		return HA_FAIL;
	}

	if (add_msg_auth(nmsg) != HA_OK) {
		ha_log(LOG_ERR, "cannot add auth field to message");
		ha_msg_del(nmsg); nmsg = NULL;
		return HA_FAIL;
	}

	if ((pkt = msg2string(nmsg)) == NULL)  {
		ha_log(LOG_ERR, "cannot convert message to string");
		return HA_FAIL;
	}
	ha_msg_del(nmsg); nmsg = NULL;

	size = strlen(pkt)+1;

	pktsize = size + ICMP_HDR_SZ;

	if ((icmp_pkt = ha_malloc(size + ICMP_HDR_SZ)) == NULL) {
		ha_log(LOG_ERR, "out of memory");
		ha_free(pkt);
		return HA_FAIL;
	}

	memcpy(icmp_pkt + ICMP_HDR_SZ, pkt, size);
	ha_free(pkt); pkt = NULL;

	icp = &(icmp_pkt->ipkt);
	icp->icmp_type = ICMP_ECHO;
	icp->icmp_code = 0;
	icp->icmp_cksum = 0;
	icp->icmp_seq = htons(ei->iseq);
	icp->icmp_id = ei->ident;
	++ei->iseq;

	/* Compute the ICMP checksum */
	icp->icmp_cksum = in_cksum((u_short *)icp, pktsize);

	if ((rc=sendto(ei->sock, icmp_pkt, pktsize, 0
	,	(struct sockaddr *)&ei->addr
	,	sizeof(struct sockaddr))) != pktsize) {
		ha_perror("Error sending packet");
		ha_free(icmp_pkt);
		return(HA_FAIL);
	}

	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "sent %d bytes to %s"
		,	rc, inet_ntoa(ei->addr.sin_addr));
   	}
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, pkt);
   	}
	ha_free(icmp_pkt);
	return HA_OK;
}

/*
 *	Open ping socket.
 */
int
EXPORT(hb_dev_open) (struct hb_media* mp)
{
	struct ping_private * ei;
	int sockfd;
	struct protoent *proto;

	PINGASSERT(mp);
	ei = (struct ping_private *) mp->pd;


	if ((proto = getprotobyname("icmp")) == NULL) {
		ha_perror("protocol ICMP is unknown");
		return HA_FAIL;
	}
	if ((sockfd = socket(AF_INET, SOCK_RAW, proto->p_proto)) < 0) {
		ha_perror("Can't open RAW socket.");
		return HA_FAIL;
    	}

	if (fcntl(sockfd, F_SETFD, FD_CLOEXEC)) {
		ha_perror("Error setting the close-on-exec flag");
	}
	ei->sock = sockfd;

	ha_log(LOG_NOTICE, "ping heartbeat started.");
	return HA_OK;
}

/*
 * in_cksum --
 *	Checksum routine for Internet Protocol family headers (C Version)
 *	This function taken from Mike Muuss' ping program.
 */
static int
in_cksum (u_short *addr, int len)
{
	int		nleft = len;
	u_short *	w = addr;
	int		sum = 0;
	u_short		answer = 0;

	/*
	 * The IP checksum algorithm is simple: using a 32 bit accumulator (sum)
	 * add sequential 16 bit words to it, and at the end, folding back all
	 * the carry bits from the top 16 bits into the lower 16 bits.
	 */
	while (nleft > 1) {
		sum += *w++;
		nleft -= 2;
	}

	/* Mop up an odd byte, if necessary */
	if (nleft == 1) {
		sum += *(u_char*)w;
	}

	/* Add back carry bits from top 16 bits to low 16 bits */

	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	answer = ~sum;				/* truncate to 16 bits */

	return answer;
}
