static const char _udp_Id [] = "$Id: udp.c,v 1.4 1999/09/30 18:34:27 alanr Exp $";
/*
   About 150 lines of the code in this file borrowed 1999 from Tom Vogt's
	"Heart" program, and significantly mangled by
	Alan Robertson <alanr@henge.com> (c) 1999
	Released under the GNU General Public License
	
	Tom's orignal copyright reproduced notice below...

   Written 1999 by Tom Vogt <tom@lemuria.org>
   
   this is GPL software. you should own a few hundred copies of the GPL
   by now. if not, get one at http://www.fsf.org
   
   a few lines of this code have been taken from "Beej's Guide to
   Network Programming", http://www.ecst.csuchico.edu/~beej/guide/net/

   thanks to the Linux-HA mailing list <linux-ha@muc.de> for helping me
   with several questions on broadcasting and for testing the early alpha
   versions.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
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


STATIC int	udp_init(void);
STATIC struct hb_media*
		udp_new(const char* interface);
STATIC int	udp_open(struct hb_media* mp);
STATIC int	udp_close(struct hb_media* mp);
STATIC struct ha_msg*
		udp_read(struct hb_media* mp);
STATIC int	udp_write(struct hb_media* mp, struct ha_msg* msg);
STATIC int	HB_make_receive_sock(struct hb_media* ei);
STATIC int	HB_make_send_sock(struct hb_media * mp);
STATIC struct ip_private *
		new_ip_interface(const char * ifn, int port);


const struct hb_media_fns	ip_media_fns =
{	"udp"			/* type */
,	"UDP/IP broadcast"	/* description */
,	udp_init		/* init */
,	udp_new			/* new */
,	NULL			/* parse */
,	udp_open		/* open */
,	udp_close		/* close */
,	udp_read		/* read */
,	udp_write		/* write */
};

int	udpport;

#define		ISUDPOBJECT(mp)	((mp) && ((mp)->vf == (void*)&ip_media_fns))
#define		UDPASSERT(mp)	ASSERT(ISUDPOBJECT(mp))


STATIC int
udp_init(void)
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
udp_new(const char * intf)
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
		name = malloc(strlen(intf)+1);
		strcpy(name, intf);
		ret->name = name;
		ret->vf = &ip_media_fns;

	}else{
		free(ipi);
	}
	return(ret);
}

/*
 *	Open UDP/IP broadcast heartbeat interface
 */
STATIC int
udp_open(struct hb_media* mp)
{
	struct ip_private * ei;

	UDPASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((ei->wsocket = HB_make_send_sock(mp)) < 0) {
		return(HA_FAIL);
	}
	if ((ei->rsocket = HB_make_receive_sock(mp)) < 0) {
		udp_close(mp);
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
udp_close(struct hb_media* mp)
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
udp_read(struct hb_media* mp)
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
udp_write(struct hb_media* mp, struct ha_msg * msgptr)
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
		free(pkt);
		return(HA_FAIL);
	}

	if (DEBUGPKT) {
		ha_log(LOG_DEBUG, "sent %d bytes to %s"
		,	rc, inet_ntoa(ei->addr.sin_addr));
   	}
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, pkt);
   	}
	free(pkt);
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
	return(sockfd);
}

/*
 * Set up socket for listening to heartbeats (UDP broadcasts)
 */

int
HB_make_receive_sock(struct hb_media * mp) {

	struct ip_private * ei;
	struct sockaddr_in my_addr;    /* my address information */
	int	sockfd;

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
		 *  We want to packets only from this PPP interface...
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

	if (bind(sockfd, (struct sockaddr *)&my_addr
	,	sizeof(struct sockaddr)) < 0) {
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
#endif
		ha_perror("Error binding socket");
		close(sockfd);
		return(-1);
	}
	return(sockfd);
}

/*
 *	Kludgy method of getting udp configuration information
 *	It works on Solaris and Linux.  Maybe other places, too.
 */

#	define IFCONFIG	"/sbin/ifconfig"
#	define	FILTER "grep '[Bb][a-z]*cast' | "	\
	"sed -e 's%^.*[Bb][a-z]*cast%%' -e 's% .*%%'"

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

	ep->interface = (char *)malloc(strlen(ifn)+1);
	if(ep->interface == NULL) {
		free(ep);
		return(NULL);
	}
	strcpy(ep->interface, ifn);
	ep->bcast_addr = malloc(strlen(bp)+1);
	if(ep->bcast_addr == NULL) {
		free(ep->interface);
		free(ep);
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
