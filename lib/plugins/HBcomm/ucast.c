static const char _ucast_Id [] = "$Id: ucast.c,v 1.6 2002/10/07 04:34:23 alan Exp $";
/*
 * Adapted from alanr's UDP broadcast heartbeat bcast.c by Stéphane Billiart
 *	<stephane@reefedge.com>
 * (c) 2002  Stéphane Billiart <stephane@reefedge.com>
 * (c) 2002  Alan Robertson <alanr@unix.sh>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <portability.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <heartbeat.h>
#include <HBcomm.h>
#define PIL_PLUGINTYPE          HB_COMM_TYPE
#define PIL_PLUGINTYPE_S        HB_COMM_TYPE_S
#define PIL_PLUGIN              ucast
#define PIL_PLUGIN_S            "ucast"
#define PIL_PLUGINLICENSE	LICENSE_LGPL
#define PIL_PLUGINLICENSEURL	URL_LGPL
#include <pils/plugin.h>


#if defined(SO_BINDTODEVICE)
#	include <net/if.h>
#endif

#define		ISUCASTOBJECT(mp) ((mp) && ((mp)->vf == (void*)&ucastOps))
#define		UCASTASSERT(mp)	g_assert(ISUCASTOBJECT(mp))

struct ip_private {
        char *  interface;      /* Interface name */
	struct in_addr heartaddr;   /* Other node heartbeat address */
        struct sockaddr_in      addr;   /* Broadcast addr */
        int     port;
        int     rsocket;        /* Read-socket */
        int     wsocket;        /* Write-socket */
};


static int	ucast_parse(const char *line);
static struct hb_media*
		ucast_new(const char * intf, const char *addr);
static int	ucast_open(struct hb_media* mp);
static int	ucast_close(struct hb_media* mp);
static struct ha_msg*
		ucast_read(struct hb_media* mp);
static int	ucast_write(struct hb_media* mp, struct ha_msg* msg);
static int	HB_make_receive_sock(struct hb_media* ei);
static int	HB_make_send_sock(struct hb_media * mp);
static struct ip_private *
		new_ip_interface(const char * ifn, const char *hbaddr, int port);
static int ucast_descr (char** buffer);
static int ucast_mtype (char** buffer);
static int ucast_isping (void);


/*
 * ucastclose is called as part of unloading the ucast HBcomm plugin.
 * If there was any global data allocated, or file descriptors opened, etc.
 * which is associated with the plugin, and not a single interface
 * in particular, here's our chance to clean it up.
 */

static void
ucastclosepi(PILPlugin*pi)
{
}
/*
 * ucastcloseintf called as part of shutting down the ucast HBcomm interface.
 * If there was any global data allocated, or file descriptors opened, etc.
 * which is associated with the ucast implementation, here's our chance
 * to clean it up.
 */
static PIL_rc
ucastcloseintf(PILInterface* pi, void* pd)
{
	return PIL_OK;
}

static struct hb_media_fns ucastOps ={
	NULL,		
	ucast_parse,	/* Whole line parse function */
	ucast_open,
	ucast_close,
	ucast_read,
	ucast_write,
	ucast_mtype,
	ucast_descr,
	ucast_isping,
};


PIL_PLUGIN_BOILERPLATE("1.0", Debug, ucastclosepi);
static const PILPluginImports*  PluginImports;
static PILPlugin*               OurPlugin;
static PILInterface*		OurInterface;
static struct hb_media_imports*	OurImports;
static void*			interfprivate;
static int			localudpport;

#define LOG	PluginImports->log
#define MALLOC	PluginImports->alloc
#define FREE	PluginImports->mfree

PIL_rc PIL_PLUGIN_INIT(PILPlugin*us, const PILPluginImports* imports);

PIL_rc
PIL_PLUGIN_INIT(PILPlugin*us, const PILPluginImports* imports)
{
	/* Force the compiler to do a little type checking */
	(void)(PILPluginInitFun)PIL_PLUGIN_INIT;

	PluginImports = imports;
	OurPlugin = us;

	/* Register ourself as a plugin */
	imports->register_plugin(us, &OurPIExports);  

	/*  Register our interface implementation */
 	return imports->register_interface(us, PIL_PLUGINTYPE_S
	,	PIL_PLUGIN_S
	,	&ucastOps
	,	ucastcloseintf		/*close */
	,	&OurInterface
	,	(void*)&OurImports
	,	interfprivate); 
}

static int
ucast_parse (const char* line)
{ 
	const char *	bp = line;
	char		dev[MAXLINE];
	char		ucast[MAXLINE];
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
#ifdef NOTYET
		if (!is_valid_dev(dev)) {
			LOG(PIL_CRIT, "ucast bad device [%s]", dev);
			return HA_FAIL;
		}
#endif
		/* Skip over white space, then grab the IP address */
		bp += strspn(bp, WHITESPACE);
		toklen = strcspn(bp, WHITESPACE);
		strncpy(ucast, bp, toklen);
		bp += toklen;
		ucast[toklen] = EOS;
	
		if (*ucast == EOS)  {
			LOG(PIL_CRIT, "ucast [%s] missing IP address",
				dev);
			return(HA_FAIL);
		}
		if ((mp = ucast_new(dev, ucast)) == NULL)  {
			return(HA_FAIL);
		}
		sysmedia[nummedia] = mp;
		++nummedia;
	}

	return(HA_OK);
}

static int
ucast_mtype (char** buffer) { 
	
	*buffer = MALLOC((strlen(PIL_PLUGIN_S) * sizeof(char)) + 1);

	strcpy(*buffer, PIL_PLUGIN_S);

	return strlen(PIL_PLUGIN_S);
}

static int
ucast_descr (char **buffer) { 

	const char* str = "UDP/IP unicast";	

	*buffer = MALLOC((strlen(str) * sizeof(char)) + 1);

	strcpy(*buffer, str);

	return strlen(str);
}

static int
ucast_isping (void) {
    return 0;
}

static int
ucast_init(void)
{
	struct servent*	service;

	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;
	(void)_ucast_Id;

	g_assert(OurImports != NULL);

	if (localudpport <= 0) {
		const char *	chport;
		if ((chport  = OurImports->ParamValue("udpport")) != NULL) {
			sscanf(chport, "%d", &localudpport);
			if (localudpport <= 0) {
				PILCallLog(LOG, PIL_CRIT
				,	"bad port number %s"
				,	chport);
				return HA_FAIL;
			}
		}
	}

	/* No port specified in the configuration... */

	if (localudpport <= 0) {
		/* If our service name is in /etc/services, then use it */
		if ((service=getservbyname(HA_SERVICENAME, "udp")) != NULL){
			localudpport = ntohs(service->s_port);
		}else{
			localudpport = UDPPORT;
		}
	}
	return(HA_OK);
}

/*
 *	Create new UDP/IP unicast heartbeat object 
 *	Name of interface and address are passed as parameters
 */
static struct hb_media *
ucast_new(const char * intf, const char *addr)
{
	char	msg[MAXLINE];
	struct ip_private*	ipi;
	struct hb_media *	ret;

	ucast_init();
	ipi = new_ip_interface(intf, addr, localudpport);
	if (ipi == NULL) {
		sprintf(msg, "IP interface [%s] does not exist"
		,	intf);
		ha_error(msg);
		return(NULL);
	}
	ret = (struct hb_media*)MALLOC(sizeof(struct hb_media));
	if (ret != NULL) {
		char * name;
		ret->pd = (void*)ipi;
		name = MALLOC(strlen(intf)+1);
		strcpy(name, intf);
		ret->name = name;

	}else{
		FREE(ipi->interface);
		FREE(ipi);
	}
	return(ret);
}

/*
 *	Open UDP/IP unicast heartbeat interface
 */
static int
ucast_open(struct hb_media* mp)
{
	struct ip_private * ei;

	UCASTASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((ei->wsocket = HB_make_send_sock(mp)) < 0) {
		return(HA_FAIL);
	}
	if ((ei->rsocket = HB_make_receive_sock(mp)) < 0) {
		ucast_close(mp);
		return(HA_FAIL);
	}
	LOG(PIL_INFO, "ucast heartbeat started on port %d interface %s to %s"
	,	localudpport, mp->name, inet_ntoa(ei->addr.sin_addr));
	return(HA_OK);
}

/*
 *	Close UDP/IP unicast heartbeat interface
 */
static int
ucast_close(struct hb_media* mp)
{
	struct ip_private * ei;
	int	rc = HA_OK;

	UCASTASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if (ei->rsocket >= 0) {
		if (close(ei->rsocket) < 0) {
			rc = HA_FAIL;
		}
	}
	if (ei->wsocket >= 0) {
		if (close(ei->wsocket) < 0) {
			rc = HA_FAIL;
		}
	}
	return(rc);
}
/*
 * Receive a heartbeat unicast packet from UDP interface
 */

static struct ha_msg *
ucast_read(struct hb_media* mp)
{
	struct ip_private *	ei;
	char			buf[MAXLINE];
	int			addr_len = sizeof(struct sockaddr);
   	struct sockaddr_in	their_addr; /* connector's addr information */
	int	numbytes;

	UCASTASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((numbytes=recvfrom(ei->rsocket, buf, MAXLINE-1, 0
	,	(struct sockaddr *)&their_addr, &addr_len)) < 0) {
		if (errno != EINTR) {
			LOG(PIL_CRIT, "Error receiving from socket: %s"
			,	strerror(errno));
		}
		return NULL;
	}
	buf[numbytes] = EOS;

	if (DEBUGPKT) {
		LOG(LOG_DEBUG, "got %d byte packet from %s"
		,	numbytes, inet_ntoa(their_addr.sin_addr));
	}
	if (DEBUGPKTCONT) {
		LOG(LOG_DEBUG, buf);
	}
	return(string2msg(buf, sizeof(buf)));
}

/*
 * Send a heartbeat packet over unicast UDP/IP interface
 */

static int
ucast_write(struct hb_media* mp, struct ha_msg * msgptr)
{
	struct ip_private *	ei;
	int			rc;
	char*			pkt;
	int			size;

	UCASTASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((pkt = msg2string(msgptr)) == NULL)  {
		return(HA_FAIL);
	}
	size = strlen(pkt)+1;

	if ((rc=sendto(ei->wsocket, pkt, size, 0
	,	(struct sockaddr *)&ei->addr
	,	sizeof(struct sockaddr))) != size) {
		LOG(PIL_CRIT, "Error sending packet: %s"
		,	strerror(errno));
		ha_free(pkt);
		return(HA_FAIL);
	}

	if (DEBUGPKT) {
		LOG(LOG_DEBUG, "sent %d bytes to %s"
		,	rc, inet_ntoa(ei->addr.sin_addr));
   	}
	if (DEBUGPKTCONT) {
		LOG(LOG_DEBUG, pkt);
   	}
	ha_free(pkt);
	return(HA_OK);
}

/*
 * Set up socket for sending unicast UDP heartbeats
 */

static int
HB_make_send_sock(struct hb_media * mp)
{
	int sockfd;
	struct ip_private * ei;
	UCASTASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		LOG(PIL_CRIT, "Error getting socket: %s"
		,	strerror(errno));
		return(sockfd);
   	}

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
			LOG(PIL_CRIT
			, "Error setting socket option SO_BINDTODEVICE: %s"
			,	strerror(errno));
			close(sockfd);
			return(-1);
		}
	}
#endif
	if (fcntl(sockfd,F_SETFD, FD_CLOEXEC)) {
		LOG(PIL_CRIT
		, "Error setting close-on-exec flag: %s"
		,	strerror(errno));
	}
	return(sockfd);
}

/*
 * Set up socket for listening to heartbeats (UDP unicast)
 */

#define	MAXBINDTRIES	10
int
HB_make_receive_sock(struct hb_media * mp) {

	struct ip_private * ei;
	struct sockaddr_in my_addr;    /* my address information */
	int	sockfd;
	int	bindtries;
	int	boundyet=0;
	int	j;

	UCASTASSERT(mp);
	ei = (struct ip_private *) mp->pd;
	memset(&(my_addr), 0, sizeof(my_addr));	/* zero my address struct */
	my_addr.sin_family = AF_INET;		/* host byte order */
	my_addr.sin_port = htons(ei->port);	/* short, network byte order */
	my_addr.sin_addr.s_addr = INADDR_ANY;	/* auto-fill with my IP */

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		LOG(PIL_CRIT, "Error getting socket: %s" , strerror(errno));
		return(-1);
	}
	/* 
 	 * Set SO_REUSEADDR on the server socket s. Variable j is used
 	 * as a scratch varable.
 	 *
 	 * 16th February 2000
 	 * Added by Horms <horms@vergenet.net>
 	 * with thanks to Clinton Work <work@scripty.com>
 	 */
	j = 1;
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR, (void *)&j, sizeof j) <0){
		/* Ignore it.  It will almost always be OK anyway. */
		LOG(PIL_CRIT, "Error setting option SO_REUSEADDR: %s"
		,	strerror(errno));
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
			LOG(PIL_CRIT
			, "Error setting option SO_BINDTODEVICE(r) on %s: %s"
			,	i.ifr_name
			,	strerror(errno));
			close(sockfd);
			return(-1);
		}
		if (ANYDEBUG) {
			LOG(LOG_DEBUG
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
			LOG(PIL_CRIT, "Error binding socket. Retrying: %s"
			,	strerror(errno));
			sleep(1);
		}else{
			boundyet = 1;
		}
	}
	if (!boundyet) {
#if !defined(SO_BINDTODEVICE)
		if (errno == EADDRINUSE) {
			/* This happens with multiple udp or ppp interfaces */
			LOG(PIL_INFO
			,	"Someone already listening on port %d [%s]"
			,	ei->port
			,	ei->interface);
			LOG(PIL_INFO, "UDP read process exiting");
			close(sockfd);
			cleanexit(0);
		}
#else
		LOG(PIL_CRIT, "Unable to bind socket. Giving up: %s"
		,	strerror(errno));
		close(sockfd);
		return(-1);
#endif
	}
	if (fcntl(sockfd,F_SETFD, FD_CLOEXEC)) {
		LOG(PIL_CRIT
		,	"Error setting close-on-exec flag: %s"
		,	strerror(errno));
	}
	return(sockfd);
}


static struct ip_private *
new_ip_interface(const char * ifn, const char *hbaddr, int port)
{
	struct ip_private * ep;
	struct in_addr heartadr;

	if (0 == inet_aton(hbaddr, &heartadr)) {
		return (NULL);
	}
	
	/*
	 * We now have all the information we need.  Populate our
	 * structure with the information we've gotten.
	 */

	ep = (struct ip_private*) MALLOC(sizeof(struct ip_private));
	if (ep == NULL)  {
		return(NULL);
	}

	ep->heartaddr = heartadr;

	ep->interface = (char *)MALLOC(strlen(ifn)+1);
	if(ep->interface == NULL) {
		FREE(ep);
		return(NULL);
	}
	strcpy(ep->interface, ifn);
	
	bzero(&ep->addr, sizeof(ep->addr));	/* zero the struct */
	ep->addr.sin_family = AF_INET;		/* host byte order */
	ep->addr.sin_port = htons(port);	/* short, network byte order */
	ep->port = port;
	ep->wsocket = -1;
	ep->rsocket = -1;
	ep->addr.sin_addr = ep->heartaddr;
	return(ep);
}
