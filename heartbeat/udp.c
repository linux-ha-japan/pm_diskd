static const char _udp_Id [] = "$Id: udp.c,v 1.12 2000/09/08 20:15:06 alan Exp $";
/*
 * udp.c: UDP-based heartbeat code for heartbeat.
 *
 * Copyright (C) 1999, 2000 Alan Robertson <alanr@unix.sh>
 *
 * About 150 lines of the code in this file originally borrowed in
 * 1999 from Tom Vogt's "Heart" program, and significantly mangled by
 *	Alan Robertson <alanr@unix.sh>
 *	
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
#include "heartbeat.h"

#if defined(SO_BINDTODEVICE)
#	include <net/if.h>
#endif

#define	EOS	'\0'

struct ip_private {
        char *  interface;      /* Interface name */
        char *  bcast_addr;     /* Broadcast address */
        struct hostent  bcast;  /* Broadcast address */
        struct sockaddr_in      addr;   /* Broadcast addr */
        int     port;
        int     rsocket;        /* Read-socket */
        int     wsocket;        /* Write-socket */
};


STATIC int	hb_dev_init(void);
STATIC struct hb_media*
		hb_dev_new(const char* interface);
STATIC int	hb_dev_open(struct hb_media* mp);
STATIC int	hb_dev_close(struct hb_media* mp);
STATIC struct ha_msg*
		hb_dev_read(struct hb_media* mp);
STATIC int	hb_dev_write(struct hb_media* mp, struct ha_msg* msg);
STATIC int	HB_make_receive_sock(struct hb_media* ei);
STATIC int	HB_make_send_sock(struct hb_media * mp);
STATIC struct ip_private *
		new_ip_interface(const char * ifn, int port);
STATIC int hb_dev_descr (char** buffer);
STATIC int hb_dev_mtype (char** buffer);
STATIC int hb_dev_isping (void);

extern int	udpport;

#define		ISUDPOBJECT(mp)	((mp) && ((mp)->vf == (void*)&ip_media_fns))
//#define		UDPASSERT(mp)	ASSERT(ISUDPOBJECT(mp))
#define		UDPASSERT(mp)

STATIC int hb_dev_mtype (char** buffer) { 
	
	*buffer = ha_malloc((strlen("udp") * sizeof(char)) + 1);

	strcpy(*buffer, "udp");

	return strlen("udp");
}

STATIC int hb_dev_descr (char **buffer) { 

	const char* str = "UDP/IP broadcast";	

	*buffer = ha_malloc((strlen(str) * sizeof(char)) + 1);

	strcpy(*buffer, str);

	return strlen(str);
}

STATIC int hb_dev_isping (void) {
    return 0;
}

STATIC int
hb_dev_init(void)
{
	(void)_heartbeat_h_Id;
	(void)_udp_Id;
	(void)_ha_msg_h_Id;
	udpport = UDPPORT;
	return(HA_OK);
}

/*
 *	Create new UDP/IP broadcast heartbeat object 
 *	Name of interface is passed as a parameter
 */
STATIC struct hb_media *
hb_dev_new(const char * intf)
{
	char	msg[MAXLINE];
	struct ip_private*	ipi;
	struct hb_media *	ret;

	ipi = new_ip_interface(intf, udpport);
	if (ipi == NULL) {
		sprintf(msg, "IP interface [%s] does not exist"
		,	intf);
		ha_error(msg);
		return(NULL);
	}
	ret = MALLOCT(struct hb_media);
	if (ret != NULL) {
		char * name;
		ret->pd = (void*)ipi;
		name = ha_malloc(strlen(intf)+1);
		strcpy(name, intf);
		ret->name = name;

	}else{
		ha_free(ipi);
	}
	return(ret);
}

/*
 *	Open UDP/IP broadcast heartbeat interface
 */
STATIC int
hb_dev_open(struct hb_media* mp)
{
	struct ip_private * ei;

	UDPASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((ei->wsocket = HB_make_send_sock(mp)) < 0) {
		return(HA_FAIL);
	}
	if ((ei->rsocket = HB_make_receive_sock(mp)) < 0) {
		hb_dev_close(mp);
		return(HA_FAIL);
	}
	ha_log(LOG_NOTICE, "UDP heartbeat started on port %d interface %s"
	,	udpport, mp->name);
	return(HA_OK);
}

/*
 *	Close UDP/IP broadcast heartbeat interface
 */
STATIC int
hb_dev_close(struct hb_media* mp)
{
	struct ip_private * ei;
	int	rc = HA_OK;

	UDPASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if (ei->rsocket >= 0) {
		if (close(ei->rsocket) < 0) {
			rc = HA_FAIL;
		}
	}
	if (ei->wsocket >= 0) {
		if (close(ei->rsocket) < 0) {
			rc = HA_FAIL;
		}
	}
	return(rc);
}
/*
 * Receive a heartbeat broadcast packet from UDP interface
 */

STATIC struct ha_msg *
hb_dev_read(struct hb_media* mp)
{
	struct ip_private *	ei;
	char			buf[MAXLINE];
	int			addr_len = sizeof(struct sockaddr);
   	struct sockaddr_in	their_addr; /* connector's addr information */
	int	numbytes;

	UDPASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((numbytes=recvfrom(ei->rsocket, buf, MAXLINE-1, 0
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
 * Send a heartbeat packet over broadcast UDP/IP interface
 */

STATIC int
hb_dev_write(struct hb_media* mp, struct ha_msg * msgptr)
{
	struct ip_private *	ei;
	int			rc;
	char*			pkt;
	int			size;

	UDPASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((pkt = msg2string(msgptr)) == NULL)  {
		return(HA_FAIL);
	}
	size = strlen(pkt)+1;

	if ((rc=sendto(ei->wsocket, pkt, size, 0
	,	(struct sockaddr *)&ei->addr
	,	sizeof(struct sockaddr))) != size) {
		ha_perror("Error sending packet");
		ha_free(pkt);
		return(HA_FAIL);
	}

	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "sent %d bytes to %s"
		,	rc, inet_ntoa(ei->addr.sin_addr));
   	}
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, pkt);
   	}
	ha_free(pkt);
	return(HA_OK);
}

/*
 * Set up socket for sending broadcast UDP heartbeats
 */

STATIC int
HB_make_send_sock(struct hb_media * mp)
{
	int sockfd, one = 1;
	struct ip_private * ei;
	UDPASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		ha_perror("Error getting socket");
		return(sockfd);
   	}

	/* Warn that we're going to broadcast */
	if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &one,sizeof(one))==-1){
		ha_perror("Error setting socket option SO_BROADCAST");
		close(sockfd);
		return(-1);
	}

#if defined(SO_DONTROUTE) && !defined(USE_ROUTING)
	/* usually, we don't want to be subject to routing. */
	if (setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE,&one,sizeof(int))==-1) {
		ha_perror("Error setting socket option SO_DONTROUTE");
		close(sockfd);
		return(-1);
	}
#endif
#if defined(SO_BINDTODEVICE)
	{
		/*
		 *  We want to send out this particular interface
		 *
		 * This is so we can have redundant NICs, and heartbeat on both
		 */
		struct ifreq i;
		strcpy(i.ifr_name,  mp->name);

		if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE
		,	&i, sizeof(i)) == -1) {
			ha_perror("Error setting socket option SO_BINDTODEVICE");
			close(sockfd);
			return(-1);
		}
	}
#endif
	if (fcntl(sockfd,F_SETFD, FD_CLOEXEC)) {
		ha_perror("Error setting the close-on-exec flag");
	}
	return(sockfd);
}

/*
 * Set up socket for listening to heartbeats (UDP broadcasts)
 */

#define	MAXBINDTRIES	10
int
HB_make_receive_sock(struct hb_media * mp) {

	struct ip_private * ei;
	struct sockaddr_in my_addr;    /* my address information */
	int	sockfd;
	int	bindtries;
	int	boundyet=0;

	UDPASSERT(mp);
	ei = (struct ip_private *) mp->pd;
	bzero(&(my_addr), sizeof(my_addr));	/* zero my address struct */
	my_addr.sin_family = AF_INET;		/* host byte order */
	my_addr.sin_port = htons(ei->port);	/* short, network byte order */
	my_addr.sin_addr.s_addr = INADDR_ANY;	/* auto-fill with my IP */

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		ha_perror("Error getting socket");
		return(-1);
	}
#if defined(SO_BINDTODEVICE)
	{
		/*
		 *  We want to receive packets only from this interface...
		 */
		struct ifreq i;
		strcpy(i.ifr_name,  ei->interface);

		if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE
		,	&i, sizeof(i)) == -1) {
			ha_perror("Error setting option SO_BINDTODEVICE(r)");
			ha_perror(i.ifr_name);
			close(sockfd);
			return(-1);
		}
		if (ANYDEBUG) {
			ha_log(LOG_DEBUG
			,	"SO_BINDTODEVICE(r) set for device %s"
			,	i.ifr_name);
		}
	}
#endif

	/* Try binding a few times before giving up */
	/* Sometimes a process with it open is exiting right now */

	for(bindtries=0; !boundyet && bindtries < MAXBINDTRIES; ++bindtries) {
		if (bind(sockfd, (struct sockaddr *)&my_addr
		,	sizeof(struct sockaddr)) < 0) {
			ha_perror("Error binding socket. Retrying");
			sleep(1);
		}else{
			boundyet = 1;
		}
	}
	if (!boundyet) {
#if !defined(SO_BINDTODEVICE)
		if (errno == EADDRINUSE) {
			/* This happens with multiple udp or ppp interfaces */
			ha_log(LOG_NOTICE
			,	"Someone already listening on port %d [%s]"
			,	ei->port
			,	ei->interface);
			ha_log(LOG_NOTICE, "UDP read process exiting");
			close(sockfd);
			cleanexit(0);
		}
#else
		ha_perror("Unable to bind socket. Giving up");
		close(sockfd);
		return(-1);
#endif
	}
	if (fcntl(sockfd,F_SETFD, FD_CLOEXEC)) {
		ha_perror("Error setting the close-on-exec flag");
	}
	return(sockfd);
}

/*
 *	Kludgy method of getting udp configuration information
 *	It works on Solaris and Linux and FreeBSD.  Maybe other places, too.
 */

#	define IFCONFIG	"/sbin/ifconfig"
#	define	FILTER "grep '[Bb][a-z]*cast' | "	\
	"sed -e 's%^.*[Bb][a-z]*cast[ :]*%%' -e 's% .*%%'"

STATIC struct ip_private *
new_ip_interface(const char * ifn, int port)
{
	FILE *	ifc;
	char*	bp;
	int	buflen;
	char	buf[MAXLINE];
	char	cmd[MAXLINE];
	struct ip_private * ep;
	struct hostent *he;

	sprintf(cmd, "%s %s | %s", IFCONFIG, ifn, FILTER);

	/*
	 *	Run ifconfig to get the broadcast addr for this interface
	 */
	if ((ifc = popen(cmd, "r")) == NULL
	||	fgets(buf, MAXLINE, ifc) == NULL
	||	strlen(buf) == 0) {
		return(NULL);
	}
	fclose(ifc);
	bp=buf;
	while (*bp && !isdigit(*bp))  {
		++bp;
	}
	buflen = strlen(bp);
	if (bp[buflen-1] == '\n' ) {
		bp[buflen-1] = '\0';
	}
	if (strlen(bp) <= 0) {
		return(NULL);
	}

	/*
	 * Get the "hostent" structure for the broadcast addr
	 */

	if ((he=gethostbyname(bp)) == NULL) {
		ha_perror("Error getting IP for broadcast address");
		return(NULL);
	}

	/*
	 * We now have all the information we need.  Populate our
	 * structure with the information we've gotten.
	 */

	ep = MALLOCT(struct ip_private);
	if (ep == NULL)  {
		return(NULL);
	}

	ep->bcast = *he;

	ep->interface = (char *)ha_malloc(strlen(ifn)+1);
	if(ep->interface == NULL) {
		ha_free(ep);
		return(NULL);
	}
	strcpy(ep->interface, ifn);
	ep->bcast_addr = ha_malloc(strlen(bp)+1);
	if(ep->bcast_addr == NULL) {
		ha_free(ep->interface);
		ha_free(ep);
		return(NULL);
	}
	strcpy(ep->bcast_addr, bp);
	bzero(&ep->addr, sizeof(ep->addr));	/* zero the struct */
	ep->addr.sin_family = AF_INET;		/* host byte order */
	ep->addr.sin_port = htons(port);	/* short, network byte order */
	ep->port = port;
	ep->wsocket = -1;
	ep->rsocket = -1;
	ep->addr.sin_addr = *((struct in_addr *)ep->bcast.h_addr);
	return(ep);
}
/*
 * $Log: udp.c,v $
 * Revision 1.12  2000/09/08 20:15:06  alan
 * Added code to retry the bind operation several times before giving up.
 *
 * Revision 1.11  2000/09/01 21:10:46  marcelo
 * Added dynamic module support
 *
 * Revision 1.10  2000/08/13 04:36:16  alan
 * Added code to make ping heartbeats work...
 * It looks like they do, too ;-)
 *
 * Revision 1.9  2000/07/26 05:17:19  alan
 * Added GPL license statements to all the code.
 *
 * Revision 1.8  2000/06/21 04:34:48  alan
 * Changed henge.com => linux-ha.org and alanr@henge.com => alanr@suse.com
 *
 * Revision 1.7  2000/05/17 13:39:55  alan
 * Added the close-on-exec flag to sockets and tty fds that we open.
 * Thanks to Christoph Jäger for noticing the problem.
 *
 * Revision 1.6  1999/10/10 20:12:58  alanr
 * New malloc/free (untested)
 *
 * Revision 1.5  1999/10/06 05:37:24  alanr
 * FreeBSD port - getting broadcast address
 *
 * Revision 1.4  1999/09/30 18:34:27  alanr
 * Matt Soffen's FreeBSD changes
 *
 * Revision 1.3  1999/09/30 16:04:22  alanr
 *
 * Minor comment change.
 *
 * Revision 1.2  1999/09/26 14:01:21  alanr
 * Added Mijta's code for authentication and Guenther Thomsen's code for serial locking and syslog reform
 *
 * Revision 1.1.1.1  1999/09/23 15:31:24  alanr
 * High-Availability Linux
 *
 * Revision 1.9  1999/09/18 02:56:36  alanr
 * Put in Matt Soffen's portability changes...
 *
 * Revision 1.8  1999/08/17 03:49:48  alanr
 * added log entry to bottom of file.
 *
 */
