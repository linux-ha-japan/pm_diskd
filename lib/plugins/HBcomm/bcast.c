static const char _bcast_Id [] = "$Id: bcast.c,v 1.3 2001/08/15 02:14:49 alan Exp $";
/*
 * bcast.c: UDP/IP broadcast-based communication code for heartbeat.
 *
 * Copyright (C) 1999, 2000,2001 Alan Robertson <alanr@unix.sh>
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

#include <portability.h>
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
#include <net/if.h>
#include <arpa/inet.h>

#include <heartbeat.h>
#include <HBcomm.h>

#if defined(SO_BINDTODEVICE)
#	include <net/if.h>
#endif

#define	EOS	'\0'

 
#include <HBcomm.h>
 
#define PIL_PLUGINTYPE          HB_COMM_TYPE
#define PIL_PLUGINTYPE_S        HB_COMM_TYPE_S
#define PIL_PLUGIN              bcast
#define PIL_PLUGIN_S            "bcast"
#include <pils/plugin.h>

struct ip_private {
        char *  interface;      /* Interface name */
	struct in_addr bcast;   /* Broadcast address */
        struct sockaddr_in      addr;   /* Broadcast addr */
        int     port;
        int     rsocket;        /* Read-socket */
        int     wsocket;        /* Write-socket */
};


static int		bcast_init(void);
struct hb_media*	bcast_new(const char* interface);
static int		bcast_open(struct hb_media* mp);
static int		bcast_close(struct hb_media* mp);
struct ha_msg*		bcast_read(struct hb_media* mp);
static int		bcast_write(struct hb_media* mp, struct ha_msg* msg);
static int		bcast_make_receive_sock(struct hb_media* ei);
static int		bcast_make_send_sock(struct hb_media * mp);
static struct ip_private *
			new_ip_interface(const char * ifn, int port);
static int		bcast_descr(char** buffer);
static int		bcast_mtype(char** buffer);
static int		bcast_isping(void);

static int		udpport = -1;

int if_get_broadaddr(const char *ifn, struct in_addr *broadaddr);

/*
 * bcastclose is called as part of unloading the bcast HBcomm plugin.
 * If there was any global data allocated, or file descriptors opened, etc.
 * which is associated with the plugin, and not a single interface
 * in particular, here's our chance to clean it up.
 */

static void
bcastclosepi(PILPlugin*pi)
{
}


/*
 * bcastcloseintf called as part of shutting down the bcast HBcomm interface.
 * If there was any global data allocated, or file descriptors opened, etc.
 * which is associated with the bcast implementation, here's our chance
 * to clean it up.
 */
static PIL_rc
bcastcloseintf(PILInterface* pi, void* pd)
{
	return PIL_OK;
}

static struct hb_media_fns bcastOps ={
	bcast_new,	/* Create single object function */
	NULL,		/* whole-line parse function */
	bcast_open,
	bcast_close,
	bcast_read,
	bcast_write,
	bcast_mtype,
	bcast_descr,
	bcast_isping,
};

PIL_PLUGIN_BOILERPLATE("1.0", Debug, bcastclosepi);
static const PILPluginImports*  PluginImports;
static PILPlugin*               OurPlugin;
static PILInterface*		OurInterface;
static struct hb_media_imports*	OurImports;
static void*			interfprivate;

PIL_rc
PIL_PLUGIN_INIT(PILPlugin*us, const PILPluginImports* imports);

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
	,	&bcastOps
	,	bcastcloseintf		/*close */
	,	&OurInterface
	,	(void*)&OurImports
	,	interfprivate); 
}

#define		ISBCASTOBJECT(mp) ((mp) && ((mp)->vf == (void*)&bcastOps))
#define		BCASTASSERT(mp)	g_assert(ISBCASTOBJECT(mp))

static int 
bcast_mtype(char** buffer) { 
	
	*buffer = ha_malloc(sizeof(PIL_PLUGIN_S));

	strcpy(*buffer, PIL_PLUGIN_S);

	return STRLEN(PIL_PLUGIN_S);
}

static int
bcast_descr(char **buffer) { 

	const char* str = "UDP/IP broadcast";	

	*buffer = ha_malloc((strlen(str) * sizeof(char)) + 1);

	strcpy(*buffer, str);

	return strlen(str);
}

static int
bcast_isping(void) {
    return 0;
}

static int
bcast_init(void)
{
	struct servent*	service;

	(void)_heartbeat_h_Id;
	(void)_bcast_Id;
	(void)_ha_msg_h_Id;

	g_assert(OurImports != NULL);
	if (udpport <= 0) {
		const char *	chport;
		if ((chport  = OurImports->ParamValue("port")) != NULL) {
			sscanf(chport, "%d", &udpport);
		}
	}

	/* No port specified in the configuration... */

	if (udpport <= 0) {
		/* If our service name is in /etc/services, then use it */
		if ((service=getservbyname(HA_SERVICENAME, "bcast")) != NULL){
			udpport = ntohs(service->s_port);
		}else{
			udpport = UDPPORT;
		}
	}
	return(HA_OK);
}

/*
 *	Create new UDP/IP broadcast heartbeat object 
 *	Name of interface is passed as a parameter
 */
struct hb_media *
bcast_new(const char * intf)
{
	char	msg[MAXLINE];
	struct ip_private*	ipi;
	struct hb_media *	ret;

	bcast_init();
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
		ha_free(ipi->interface);
		ha_free(ipi);
	}
	return(ret);
}

/*
 *	Open UDP/IP broadcast heartbeat interface
 */
static int
bcast_open(struct hb_media* mp)
{
	struct ip_private * ei;

	BCASTASSERT(mp);
	ei = (struct ip_private *) mp->pd;

	if ((ei->wsocket = bcast_make_send_sock(mp)) < 0) {
		return(HA_FAIL);
	}
	if ((ei->rsocket = bcast_make_receive_sock(mp)) < 0) {
		bcast_close(mp);
		return(HA_FAIL);
	}
	ha_log(LOG_NOTICE, "UDP Broadcast heartbeat started on port %d interface %s"
	,	udpport, mp->name);
	return(HA_OK);
}

/*
 *	Close UDP/IP broadcast heartbeat interface
 */
static int
bcast_close(struct hb_media* mp)
{
	struct ip_private * ei;
	int	rc = HA_OK;

	BCASTASSERT(mp);
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
 * Receive a heartbeat broadcast packet from BCAST interface
 */

struct ha_msg *
bcast_read(struct hb_media* mp)
{
	struct ip_private *	ei;
	char			buf[MAXLINE];
	int			addr_len = sizeof(struct sockaddr);
   	struct sockaddr_in	their_addr; /* connector's addr information */
	int	numbytes;

	BCASTASSERT(mp);
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

int
bcast_write(struct hb_media* mp, struct ha_msg * msgptr)
{
	struct ip_private *	ei;
	int			rc;
	char*			pkt;
	int			size;

	BCASTASSERT(mp);
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

static int
bcast_make_send_sock(struct hb_media * mp)
{
	int sockfd, one = 1;
	struct ip_private * ei;
	BCASTASSERT(mp);
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
static int
bcast_make_receive_sock(struct hb_media * mp) {

	struct ip_private * ei;
	struct sockaddr_in my_addr;    /* my address information */
	int	sockfd;
	int	bindtries;
	int	boundyet=0;
	int	j;

	BCASTASSERT(mp);
	ei = (struct ip_private *) mp->pd;
	bzero(&(my_addr), sizeof(my_addr));	/* zero my address struct */
	my_addr.sin_family = AF_INET;		/* host byte order */
	my_addr.sin_port = htons(ei->port);	/* short, network byte order */
	my_addr.sin_addr.s_addr = INADDR_ANY;	/* auto-fill with my IP */

	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		ha_perror("Error getting socket");
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
		ha_perror("Error setting option SO_REUSEADDR");
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
			/* This happens with multiple bcast or ppp interfaces */
			ha_log(LOG_NOTICE
			,	"Someone already listening on port %d [%s]"
			,	ei->port
			,	ei->interface);
			ha_log(LOG_NOTICE, "BCAST read process exiting");
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



static struct ip_private *
new_ip_interface(const char * ifn, int port)
{
	struct ip_private * ep;
	struct in_addr broadaddr;

	/* Fetch the broadcast address for this interface */
	if (if_get_broadaddr(ifn, &broadaddr) < 0) {
		/* this function whines about problems... */
		return (NULL);
	}
	
	/*
	 * We now have all the information we need.  Populate our
	 * structure with the information we've gotten.
	 */

	ep = MALLOCT(struct ip_private);
	if (ep == NULL)  {
		return(NULL);
	}

	ep->bcast = broadaddr;

	ep->interface = (char *)ha_malloc(strlen(ifn)+1);
	if(ep->interface == NULL) {
		ha_free(ep);
		return(NULL);
	}
	strcpy(ep->interface, ifn);
	
	bzero(&ep->addr, sizeof(ep->addr));	/* zero the struct */
	ep->addr.sin_family = AF_INET;		/* host byte order */
	ep->addr.sin_port = htons(port);	/* short, network byte order */
	ep->port = port;
	ep->wsocket = -1;
	ep->rsocket = -1;
	ep->addr.sin_addr = ep->bcast;
	return(ep);
}


/*
 * ha_if.c - code that extracts information about a network interface
 *
 * See the linux ifconfig source code for more examples.
 *
 * Works on HP_UX 10.20, freebsd, linux rh6.2
 * Works on solaris or Unixware (SVR4) with:
 *   gcc -DBSD_COMP -c ha_if.c
 * Doesn't seem to work at all on Digital Unix (???)
 *
 * Author: Eric Z. Ayers <eric.ayers@compgen.com>
 *
 * Copyright (C) 2000 Computer Generation Incorporated
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
 *
 */



#include <sys/ioctl.h>


#ifdef HAVE_SYS_SOCKIO_H
#	include <sys/sockio.h>
#endif


/*
  if_get_broadaddr
     Retrieve the ipv4 broadcast address for the specified network interface.

  Inputs:  ifn - the name of the network interface:
                 e.g. eth0, eth1, ppp0, plip0, plusb0 ...
  Outputs: broadaddr - returned broadcast address.

  Returns: 0 on success
           -1 on failure - sets errno.
 */
int
if_get_broadaddr(const char *ifn, struct in_addr *broadaddr)
{
	int
		return_val,
		fd = -1;
	struct ifreq
		ifr; /* points to one interface returned from ioctl */

	/* get rid of compiler warnings about unreferenced variables */
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;
	
	fd = socket (PF_INET, SOCK_DGRAM, 0);
	
	if (fd < 0) {
		ha_perror("Error opening socket for interface %s", ifn);
		return -1;
	}
	
	strncpy (ifr.ifr_name, ifn, sizeof(ifr.ifr_name));

	/* Fetch the broadcast address of this interface by calling ioctl() */
	return_val = ioctl(fd,SIOCGIFBRDADDR, &ifr);
	
	if (return_val == 0 ) {
		if (ifr.ifr_broadaddr.sa_family == AF_INET) {
			struct sockaddr_in
				*sin_ptr = (struct sockaddr_in *)
				&ifr.ifr_broadaddr;
			
			*broadaddr = sin_ptr->sin_addr;
			
			/* wanna see it? */
			/* printf ("ifr_broadaddr %s\n",
			   inet_ntoa(sin_ptr->sin_addr));*/
			
			/* leave return_val set to 0 to return success! */
		} else {
			ha_perror ("Wrong family for broadcast interface %s",
				   ifn);
			return_val = -1;
		}
		
	} else {
		ha_perror ("Get broadcast for interface %s failed", ifn);
		return_val = -1;
	}
	
	close (fd);

	return return_val;
	
} /* end if_get_broadaddr() */

/*
 * $Log: bcast.c,v $
 * Revision 1.3  2001/08/15 02:14:49  alan
 * Put in #include net/if.h needed for FreeBSD and solaris
 *
 * Revision 1.2  2001/08/11 01:40:54  alan
 * Removed the old copy of the heartbeat.h file, and removed a
 * blank line from a file... ;-)
 *
 * Revision 1.1  2001/08/10 17:16:44  alan
 * New code for the new plugin loading system.
 *
 * Revision 1.20  2001/06/19 13:56:28  alan
 * FreeBSD portability patch from Matt Soffen.
 * Mainly added #include "portability.h" to lots of files.
 * Also added a library to Makefile.am
 */
