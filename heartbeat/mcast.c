static const char _mcast_Id [] = "$Id: mcast.c,v 1.2 2001/03/11 03:16:12 alan Exp $";
/*
 * mcast.c: implements hearbeat API for UDP multicast communication
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 * Copyright (C) 2000 Chris Wright <chris@wirex.com>
 *
 * Thanks to WireX for providing hardware to test on.
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
#include <net/if.h>
#include <sys/ioctl.h>

#include "heartbeat.h"

struct mcast_private {
	char *  interface;      /* Interface name */
	struct  in_addr mcast;  /* multicast address */
	struct  sockaddr_in   addr;   /* multicast addr */
	u_short port;
	int     rsocket;        /* Read-socket */
	int     wsocket;        /* Write-socket */
	u_char	ttl;		/* TTL value for outbound packets */
	u_char	loop;		/* boolean, loop back outbound packets */
};


/* external interface */
int hb_dev_parse(const char *line);
int hb_dev_init(void);
/* not used right now 
 * struct hb_media* hb_dev_new(const char* interface);
 */
struct hb_media* hb_dev_new(const char* interface, const char *addr, 
							u_short port, u_char ttl, u_char loop);
int hb_dev_open(struct hb_media* hbm);
int hb_dev_close(struct hb_media* hbm);
struct ha_msg* hb_dev_read(struct hb_media* hbm);
int hb_dev_write(struct hb_media* hbm, struct ha_msg* msg);
int hb_dev_descr (char** buffer);
int hb_dev_mtype (char** buffer);
int hb_dev_isping (void);

/* helper functions */
static int HB_make_receive_sock(struct hb_media* hbm);
static int HB_make_send_sock(struct hb_media * hbm);
static struct mcast_private *
new_mcast_private(const char *ifn, const char *mcast, u_short port,
		u_char ttl, u_char loop);
static int set_mcast_if(int sockfd, char *ifname);
static int set_mcast_loop(int sockfd, u_char loop);
static int set_mcast_ttl(int sockfd, u_char ttl);
static int join_mcast_group(int sockfd, struct in_addr *addr, char *ifname);
static int if_NameToIndex(const char *ifname);
static int is_valid_dev(const char *dev);
static int is_valid_mcast_addr(const char *addr);
static int get_port(const char *port, u_short *p);
static int get_ttl(const char *ttl, u_char *t);
static int get_loop(const char *loop, u_char *l);

/* config items */
extern int	udpport;

/* taken out, not used (these were to be global settings, but now they
 * are set per mcast entry in the config file...
 *
 * extern char	*mcast_ip_addr;
 * extern u_char	mcast_ttl;
 * extern u_char	mcast_loop;
 */

#define		UDPASSERT(hbm)

int hb_dev_mtype (char** buffer)
{ 
	
	*buffer = ha_malloc((strlen("mcast") * sizeof(char)) + 1);

	strcpy(*buffer, "mcast");

	return strlen("mcast");
}

int hb_dev_descr (char **buffer)
{ 

	const char* str = "UDP/IP multicast";	

	*buffer = ha_malloc((strlen(str) * sizeof(char)) + 1);

	strcpy(*buffer, str);

	return strlen(str);
}

int hb_dev_isping (void)
{
	/* nope, this is not a ping device */
	return 0;
}

/* hb_dev_parse will parse the line in the config file that is 
 * associated with the media's type (hb_dev_mtype).  It should 
 * receive the rest of the line after the mtype.  And it needs
 * to call hb_dev_new, update the global sysmedia array, and
 * increment the global nummedia counter.  (sample code from
 * ppp-udp.c)
 *
 * So in this case, the config file line should look like
 * mcast [device] [mcast group] [port] [mcast ttl] [mcast loop]
 * for example:
 * mcast eth0 225.0.0.1 694 1 0
 */
int hb_dev_parse(const char *line)
{
	const char *	bp = line;
	char		dev[MAXLINE];
	char		mcast[MAXLINE];
	char		token[MAXLINE];
	u_short		port;
	u_char		ttl;
	u_char		loop;
	int		toklen;
	extern struct hb_media* sysmedia[];
	extern int		nummedia;
	struct hb_media *	mp;

	/* Skip over white space, then grab the device */
	bp += strspn(bp, WHITESPACE);
	toklen = strcspn(bp, WHITESPACE);
	strncpy(dev, bp, toklen);
	bp += toklen;
	dev[toklen] = EOS;

	if (*dev != EOS)  {
		if (!is_valid_dev(dev)) {
			ha_log(LOG_ERR, "mcast bad device [%s]", dev);
			return HA_FAIL;
		}
		/* Skip over white space, then grab the multicast group */
		bp += strspn(bp, WHITESPACE);
		toklen = strcspn(bp, WHITESPACE);
		strncpy(mcast, bp, toklen);
		bp += toklen;
		mcast[toklen] = EOS;
	
		if (*mcast == EOS)  {
			ha_log(LOG_ERR, "mcast [%s] missing mcast address",
				dev);
			return(HA_FAIL);
		}
		if (!is_valid_mcast_addr(mcast)) {
			ha_log(LOG_ERR, "mcast [%s] bad addr [%s]", dev, mcast);
			return(HA_FAIL);
		}

		/* Skip over white space, then grab the port */
		bp += strspn(bp, WHITESPACE);
		toklen = strcspn(bp, WHITESPACE);
		strncpy(token, bp, toklen);
		bp += toklen;
		token[toklen] = EOS;

		if (*token == EOS)  {
			ha_log(LOG_ERR, "mcast [%s] missing port", dev);
			return(HA_FAIL);
		}
		if (get_port(token, &port) == -1) {
			ha_log(LOG_ERR, " mcast [%s] bad port [%s]", dev, port);
			return HA_FAIL;
		}

		/* Skip over white space, then grab the ttl */
		bp += strspn(bp, WHITESPACE);
		toklen = strcspn(bp, WHITESPACE);
		strncpy(token, bp, toklen);
		bp += toklen;
		token[toklen] = EOS;

		if (*token == EOS)  {
			ha_log(LOG_ERR, "mcast [%s] missing ttl", dev);
			return(HA_FAIL);
		}
		if (get_ttl(token, &ttl) == -1) {
			ha_log(LOG_ERR, " mcast [%s] bad ttl [%s]", dev, ttl);
			return HA_FAIL;
		}

		/* Skip over white space, then grab the loop */
		bp += strspn(bp, WHITESPACE);
		toklen = strcspn(bp, WHITESPACE);
		strncpy(token, bp, toklen);
		bp += toklen;
		token[toklen] = EOS;

		if (*token == EOS)  {
			ha_log(LOG_ERR, "mcast [%s] missing loop", dev);
			return(HA_FAIL);
		}
		if (get_loop(token, &loop) == -1) {
			ha_log(LOG_ERR, " mcast [%s] bad loop [%s]", dev, loop);
			return HA_FAIL;
		}

		if ((mp = hb_dev_new(dev, mcast, port, ttl, loop)) == NULL)  {
			return(HA_FAIL);
		}
		sysmedia[nummedia] = mp;
		++nummedia;
	}

	return(HA_OK);
}

/* Initialize global stuff, this should only be called once at,
 * module load time
 */
int hb_dev_init(void)
{
	struct servent*	service;

	(void)_heartbeat_h_Id;
	(void)_mcast_Id;
	(void)_ha_msg_h_Id;

	/* If our service name is in /etc/services, then use it */

	if ((service=getservbyname(HA_SERVICENAME, "udp")) != NULL) {
		udpport = ntohs(service->s_port);
	}else{
		udpport = UDPPORT;
	}

/* taken out, not used (these were to be global settings, but now they
 * are set per mcast entry in the config file...
 *	mcast_ip_addr=DEFAULT_MCAST_IPADDR;
 *	mcast_ttl=DEFAULT_MCAST_TTL;
 *	mcast_loop=DEFAULT_MCAST_LOOP;
 */
	return(HA_OK);
}

/*
 * Create new UDP/IP multicast heartbeat object 
 * pass in name of interface, multicast address, port, multicast
 * ttl, and multicast loopback value as parameters.
 * This should get called from hb_dev_parse().
 */
struct hb_media *
hb_dev_new(const char * intf, const char *mcast, u_short port,
			u_char ttl, u_char loop)
{
	struct mcast_private*	mcp;
	struct hb_media *	ret;

	/* create new mcast_private struct...hmmm...who frees it? */
	mcp = new_mcast_private(intf, mcast, port, ttl, loop);
	if (mcp == NULL) {
		ha_perror("Error creating mcast_private(%s, %s, %d, %d, %d)",
			 intf, mcast, port, ttl, loop);
		return(NULL);
	}
	ret = MALLOCT(struct hb_media);
	if (ret != NULL) {
		char * name;
		ret->pd = (void*)mcp;
		name = ha_malloc(strlen(intf)+1);
		strcpy(name, intf);
		ret->name = name;

	}else{
		ha_free(mcp->interface);
		ha_free(mcp);
	}
	return(ret);
}

/*
 *	Open UDP/IP multicast heartbeat interface
 */
int hb_dev_open(struct hb_media* hbm)
{
	struct mcast_private * mcp;

	UDPASSERT(hbm);
	mcp = (struct mcast_private *) hbm->pd;

	if ((mcp->wsocket = HB_make_send_sock(hbm)) < 0) {
		return(HA_FAIL);
	}
	if ((mcp->rsocket = HB_make_receive_sock(hbm)) < 0) {
		hb_dev_close(hbm);
		return(HA_FAIL);
	}

	ha_log(LOG_NOTICE, "UDP multicast heartbeat started for group %s "
		"port %d interface %s (ttl=%d loop=%d)" , inet_ntoa(mcp->mcast),
		mcp->port, mcp->interface, mcp->ttl, mcp->loop);

	return(HA_OK);
}

/*
 *	Close UDP/IP multicast heartbeat interface
 */
int hb_dev_close(struct hb_media* hbm)
{
	struct mcast_private * mcp;
	int	rc = HA_OK;

	UDPASSERT(hbm);
	mcp = (struct mcast_private *) hbm->pd;

	if (mcp->rsocket >= 0) {
		if (close(mcp->rsocket) < 0) {
			rc = HA_FAIL;
		}
	}
	if (mcp->wsocket >= 0) {
		if (close(mcp->wsocket) < 0) {
			rc = HA_FAIL;
		}
	}
	return(rc);
}
/*
 * Receive a heartbeat multicast packet from UDP interface
 */

struct ha_msg * hb_dev_read(struct hb_media* hbm)
{
	struct mcast_private *	mcp;
	char			buf[MAXLINE];
	int			addr_len = sizeof(struct sockaddr);
   	struct sockaddr_in	their_addr; /* connector's addr information */
	int	numbytes;

	UDPASSERT(hbm);
	mcp = (struct mcast_private *) hbm->pd;

	if ((numbytes=recvfrom(mcp->rsocket, buf, MAXLINE-1, 0
	,	(struct sockaddr *)&their_addr, &addr_len)) == -1) {
		ha_perror("Error receiving from socket");
	}
	buf[numbytes] = EOS;

	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "got %d byte packet from %s"
		,	numbytes, inet_ntoa(their_addr.sin_addr));
	}
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, buf);
	}
	return(string2msg(buf));
}

/*
 * Send a heartbeat packet over multicast UDP/IP interface
 */

int hb_dev_write(struct hb_media* hbm, struct ha_msg * msgptr)
{
	struct mcast_private *	mcp;
	int			rc;
	char*			pkt;
	int			size;

	UDPASSERT(hbm);
	mcp = (struct mcast_private *) hbm->pd;

	if ((pkt = msg2string(msgptr)) == NULL)  {
		return(HA_FAIL);
	}
	size = strlen(pkt)+1;

	if ((rc=sendto(mcp->wsocket, pkt, size, 0
	,	(struct sockaddr *)&mcp->addr
	,	sizeof(struct sockaddr))) != size) {
		ha_perror("Error sending packet");
		ha_free(pkt);
		return(HA_FAIL);
	}

	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "sent %d bytes to %s"
		,	rc, inet_ntoa(mcp->addr.sin_addr));
   	}
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, pkt);
   	}
	ha_free(pkt);
	return(HA_OK);
}

/*
 * Set up socket for sending multicast UDP heartbeats
 */

int HB_make_send_sock(struct hb_media * hbm)
{
	int sockfd;
	struct mcast_private * mcp;
	UDPASSERT(hbm);
	mcp = (struct mcast_private *) hbm->pd;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		ha_perror("Error getting socket");
		return(sockfd);
   	}

	if (set_mcast_if(sockfd, mcp->interface) < 0) {
		ha_perror("Error setting outbound mcast interface");
	}

	if (set_mcast_loop(sockfd, mcp->loop) < 0) {
		ha_perror("Error setting outbound mcast loopback value");
	}

	if (set_mcast_ttl(sockfd, mcp->ttl) < 0) {
		ha_perror("Error setting outbound mcast TTL");
	}

	if (fcntl(sockfd,F_SETFD, FD_CLOEXEC)) {
		ha_perror("Error setting the close-on-exec flag");
	}
	return(sockfd);
}

/*
 * Set up socket for listening to heartbeats (UDP multicasts)
 */

#define	MAXBINDTRIES	10
int HB_make_receive_sock(struct hb_media * hbm)
{

	struct mcast_private * mcp;
	int	sockfd;
	int	bindtries;
	int	boundyet=0;
	int	one=1;
	int	rc;
	int	binderr;

	UDPASSERT(hbm);
	mcp = (struct mcast_private *) hbm->pd;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		ha_log(LOG_ERR, "Error getting socket");
		return -1;
	}
	/* set REUSEADDR option on socket so you can bind a multicast */
	/* reader to multiple interfaces */
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0){
		ha_log(LOG_ERR, "Error setsockopt(SO_REUSEADDR)");
	}        

	/* ripped off from udp.c, if we all use SO_REUSEADDR */
	/* this shouldn't be necessary  */
	/* Try binding a few times before giving up */
	/* Sometimes a process with it open is exiting right now */

	for(bindtries=0; !boundyet && bindtries < MAXBINDTRIES; ++bindtries) {
		rc=bind(sockfd, (struct sockaddr *)&mcp->addr, sizeof(mcp->addr));
		binderr=errno;
		if (rc==0) {
			boundyet=1;
		} else if (rc == -1) {
			if (binderr == EADDRINUSE) {
				ha_log(LOG_ERR, "Can't bind (EADDRINUSE), "
					"retrying");
				sleep(1);
			} else	{ 
			/* don't keep trying if the error isn't caused by */
			/* the address being in use already...real error */
				break;
			}
		}
	}
	if (!boundyet) {
		if (binderr == EADDRINUSE) {
			/* This happens with multiple udp or ppp interfaces */
			ha_log(LOG_NOTICE
			,	"Someone already listening on port %d [%s]"
			,	mcp->port
			,	mcp->interface);
			ha_log(LOG_NOTICE, "multicast read process exiting");
			close(sockfd);
			cleanexit(0);
		} else {
			ha_perror("Unable to bind socket. Giving up");
			close(sockfd);
			return(-1);
		}
	}
	/* join the multicast group...this is what really makes this a */
	/* multicast reader */
	if (join_mcast_group(sockfd, &mcp->mcast, mcp->interface) == -1) {
		ha_log(LOG_ERR, "Can't join multicast group %s on interface %s",
			inet_ntoa(mcp->mcast), mcp->interface);
		ha_log(LOG_NOTICE, "multicast read process exiting");
		close(sockfd);
		cleanexit(0);
	}
	if (ANYDEBUG) 
		ha_log(LOG_DEBUG, "Successfully joined multicast group %s on"
			"interface %s", inet_ntoa(mcp->mcast), mcp->interface);
		
	if (fcntl(sockfd,F_SETFD, FD_CLOEXEC)) {
		ha_perror("Error setting the close-on-exec flag");
	}
	return(sockfd);
}

static struct mcast_private *
new_mcast_private(const char *ifn, const char *mcast, u_short port,
		u_char ttl, u_char loop)
{
	struct mcast_private *mcp;

	mcp = MALLOCT(struct mcast_private);
	if (mcp == NULL)  {
		return NULL;
	}

	mcp->interface = (char *)ha_malloc(strlen(ifn)+1);
	if(mcp->interface == NULL) {
		ha_free(mcp);
		return NULL;
	}
	strcpy(mcp->interface, ifn);

	/* set up multicast address */
	if (inet_aton(mcast, &mcp->mcast) == -1) {
		ha_free(mcp->interface);
		ha_free(mcp);
		return NULL;
	}

	
	bzero(&mcp->addr, sizeof(mcp->addr));	/* zero the struct */
	mcp->addr.sin_family = AF_INET;		/* host byte order */
	mcp->addr.sin_port = htons(port);	/* short, network byte order */
	mcp->addr.sin_addr = mcp->mcast;
	mcp->port = port;
	mcp->wsocket = -1;
	mcp->rsocket = -1;
	mcp->ttl=ttl;
	mcp->loop=loop;
	return(mcp);
}

/* set_mcast_loop takes a boolean flag, loop, which is useful on
 * a writing socket.  with loop enabled (the default on a multicast socket)
 * the outbound packet will get looped back and received by the sending
 * interface, if it is listening for the multicast group and port that the
 * packet was sent to.  Returns 0 on success -1 on failure.
 */
static int set_mcast_loop(int sockfd, u_char loop)
{
	return setsockopt(sockfd, SOL_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
}

/* set_mcast_ttl will set the time-to-live value for the writing socket.
 * the socket default is TTL=1.  The TTL is used to limit the scope of the
 * packet and can range from 0-255.  
 * TTL     Scope
 * ----------------------------------------------------------------------
 *    0    Restricted to the same host. Won't be output by any interface.
 *    1    Restricted to the same subnet. Won't be forwarded by a router.
 *  <32    Restricted to the same site, organization or department.
 *  <64    Restricted to the same region.
 * <128    Restricted to the same continent.
 * <255    Unrestricted in scope. Global.
 *
 * Returns 0 on success -1 on failure.
 */
static int set_mcast_ttl(int sockfd, u_char ttl)
{
	return setsockopt(sockfd, SOL_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
}

/* set_mcast_if takes the name of an interface (i.e. eth0) and then
 * sets that as the interface to use for outbound multicast traffic.
 * If ifname is NULL, then it the OS will assign the interface.
 * Returns 0 on success -1 on faliure.
 */
static int set_mcast_if(int sockfd, char *ifname)
{
	int idx;
	struct ip_mreqn	mreq;

	idx=if_NameToIndex(ifname);
	if (idx == -1) {
		return -1;
	}

	// zero out the struct...only care about the index...
	memset(&mreq, 0, sizeof(mreq));
	mreq.imr_ifindex=idx;
	return setsockopt(sockfd, SOL_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq));
}

/* join_mcast_group is used to join a multicast group.  the group is
 * specified by a class D multicast address 224.0.0.0/8 in the in_addr
 * structure passed in as a parameter.  The interface name can be used
 * to "bind" the multicast group to a specific interface (or any
 * interface if ifname is NULL);
 * returns 0 on success, -1 on failure.
 */
static int join_mcast_group(int sockfd, struct in_addr *addr, char *ifname)
{
	struct ip_mreqn mreq_add;

	memset(&mreq_add, 0, sizeof(mreq_add));
	memcpy(&mreq_add.imr_multiaddr, addr, sizeof(struct in_addr));

	if (ifname) {
		int idx;
		idx=if_NameToIndex(ifname);
		if (idx != -1)
			mreq_add.imr_ifindex=idx;
	}
	return setsockopt(sockfd, SOL_IP, IP_ADD_MEMBERSHIP, &mreq_add, sizeof(mreq_add));
}

/* if_NameToIndex takes an interface name as input and returns
 * the interface index number, 0 if ifname is NULL, or -1 on error
 */
static int if_NameToIndex(const char *ifname)
{
	int	fd;
	struct ifreq	if_info;

	memset(&if_info, 0, sizeof(if_info));
	if (ifname) {
		strncpy(if_info.ifr_name, ifname, IFNAMSIZ-1);
	} else {	/* ifname is NULL, so index will be zero */
		return 0;
	}

	if ((fd=socket(AF_INET, SOCK_DGRAM, 0)) == -1)	{
		ha_log(LOG_ERR, "Error getting socket");
		return -1;
	}
	if (ANYDEBUG) {
		ha_log(LOG_DEBUG, 
			"looking up index for %s",
			if_info.ifr_name);
	}
	if (ioctl(fd, SIOCGIFINDEX, &if_info) == -1) {
		ha_log(LOG_ERR, "Error ioctl(SIOCGIFINDEX)");
		close(fd);
		return -1;
	}
	close(fd);
	return if_info.ifr_ifindex;
}

/* returns true or false */
static int is_valid_dev(const char *dev)
{
	int rc=0;
	if (dev) 
		if (if_NameToIndex(dev) != -1)
			rc=1;
	return rc;
}

/* returns true or false */
static int is_valid_mcast_addr(const char *addr)
{
	/* not implemented yet */
	return 1;
}

/* return port number on success, 0 on failure */
static int get_port(const char *port, u_short *p)
{
	/* not complete yet */
	*p=(u_short)atoi(port);
	return 0;
}

/* returns ttl on succes, -1 on failure */
static int get_ttl(const char *ttl, u_char *t)
{
	/* not complete yet */
	*t=(u_char)atoi(ttl);
	return 0;
}

/* returns loop on success, -1 on failure */
static int get_loop(const char *loop, u_char *l)
{
	/* not complete yet */
	*l=(u_char)atoi(loop);
	return 0;
}

/*
 * $Log: mcast.c,v $
 * Revision 1.2  2001/03/11 03:16:12  alan
 * Fixed the problem with mcast not incrementing nummedia.
 * Installed mcast module in the makefile.
 * Made the code for printing things a little more cautious about data it is
 * passed as a parameter.
 *
 * Revision 1.1  2001/01/16 21:53:39  alan
 * Added mcast comm code and APC stonith code and updated version #, etc.
 *
 * Revision 1.1  2000/12/23 22:01:11  chris
 * Initial revision
 *
 */
