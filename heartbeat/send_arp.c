const static char * _send_arp_c = "$Id: send_arp.c,v 1.13 2003/01/31 10:02:09 lars Exp $";
/* 
 * send_arp
 * 
 * This program sends out one ARP packet with source/target IP and Ethernet
 * hardware addresses suuplied by the user.  It uses the libnet libary from
 * Packet Factory (http://www.packetfactory.net/libnet/ ). It has been tested
 * on Linux, FreeBSD, and on Solaris.
 * 
 * This inspired by the sample application supplied by Packet Factory.

 * Matt Soffen

 * Copyright (C) 2001 Matt Soffen <matt@soffen.com>
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
 *
 */

#include <portability.h>

#include <libnet.h>
#include <syslog.h>

#ifdef HAVE_LIBNET_1_0_API
#	define	LTYPE	struct libnet_link_int
#endif
#ifdef HAVE_LIBNET_1_1_API
#	define	LTYPE	libnet_t
#endif

int send_arp(LTYPE* l, u_long ip, u_char *device, u_char *macaddr, u_char *broadcast, u_char *netmask, u_short arptype);

char print_usage[]={"send_arp: sends out custom ARP packet. packetfactory.net\n\
\tusage: send_arp dev src_ip_addr src_hw_addr targ_ip_addr tar_hw_addr\n\n"};

void convert_macaddr (u_char *macaddr, u_char enet_src[6]);


#ifndef LIBNET_ERRBUF_SIZE
#	define LIBNET_ERRBUF_SIZE 256
#endif
int
main(int argc, char *argv[])
{
    int c;
    char errbuf[LIBNET_ERRBUF_SIZE];
    char *device = NULL;
    char *macaddr = NULL;
    char *broadcast = NULL;
    char *netmask = NULL;
    u_long ip;
#ifdef HAVE_LIBNET_1_0_API
    struct libnet_link_int *l;
#endif

#ifdef HAVE_LIBNET_1_1_API
    libnet_t*		l;

    if ((l=libnet_init(LIBNET_LINK, device, errbuf)) == NULL) {
	syslog(LOG_ERR, "libnet_init failure:");
        exit(EXIT_FAILURE);
    }
#endif

    (void)_send_arp_c;
    if (argc != 6) {
        printf("%s", print_usage);
	exit (-1);
    }

    /*
    argv[1] DEVICE	dc0,eth0:0,hme0:0,
    argv[2] IP		192.168.195.186
    argv[3] MAC ADDR	00a0cc34a878
    argv[4] BROADCAST	192.168.195.186
    argv[5] NETMASK	ffffffffffff
    */

    device = argv[1];
    macaddr = argv[3];
    broadcast = argv[4];
    netmask = argv[5];
    
#ifdef HAVE_LIBNET_1_0_API
    if ((ip = libnet_name_resolve(argv[2], 1)) == -1UL) {
        syslog(LOG_ERR, "Cannot resolve IP address\n");
        exit(EXIT_FAILURE);
    }

    l = libnet_open_link_interface(device, errbuf);
    if (!l) {
        syslog(LOG_ERR, "libnet_open_link_interface: %s\n", errbuf);
        exit(EXIT_FAILURE);
    }
#endif

#ifdef HAVE_LIBNET_1_1_API
    if ((ip = libnet_name2addr4(l, argv[2], 1)) == -1) {
        syslog(LOG_ERR, "Cannot resolve IP address\n");
        exit(EXIT_FAILURE);
    }
#endif

/*
 * We need to send both a broadcast ARP request as well as the ARP response we
 * were already sending.  All the interesting research work for this fix was
 * done by Masaki Hasegawa <masaki-h@pp.iij4u.or.jp> and his colleagues.
 */
    c = send_arp(l, ip, device, macaddr, broadcast, netmask, ARPOP_REQUEST);
    c = send_arp(l, ip, device, macaddr, broadcast, netmask, ARPOP_REPLY);
    printf("\n");
    return (c == -1 ? EXIT_FAILURE : EXIT_SUCCESS);
}


void convert_macaddr (u_char *macaddr, u_char enet_src[6])
{
	int i, pos;
	u_char bits[3];

	pos = 0;
	for (i = 0; i < 6; i++) {
	    bits[0] = macaddr[pos++];
	    bits[1] = macaddr[pos++];
	    bits[2] = '\0';

	    enet_src[i] = strtol(bits, (char **)NULL, 16);
	}

}

#ifdef HAVE_LIBNET_1_0_API
int
send_arp(struct libnet_link_int *l, u_long ip, u_char *device, u_char *macaddr, u_char *broadcast, u_char *netmask, u_short arptype)
{
    int n;
    u_char *buf;
    u_char enet_src[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    u_char enet_dst[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


    if (libnet_init_packet(LIBNET_ARP_H + LIBNET_ETH_H, &buf) == -1) {
	syslog(LOG_ERR, "libnet_init_packet memory:");
        exit(EXIT_FAILURE);
    }

    /* Convert ASCII Mac Address to 6 Hex Digits. */
    convert_macaddr (macaddr, enet_src);

    /* Ethernet header */
    libnet_build_ethernet(enet_dst, enet_src, ETHERTYPE_ARP, NULL, 0, buf);

    /*
     *  ARP header
     */
    libnet_build_arp(ARPHRD_ETHER,	/* Hardware address type */
        ETHERTYPE_IP,			/* Protocol address type */
        6,				/* Hardware address length */
        4,				/* Protocol address length */
        arptype,			/* ARP operation */
        enet_src,			/* Source hardware addr */
        (u_char *)&ip,			/* Target hardware addr */
        enet_dst,			/* Destination hw addr */
        (u_char *)&ip,			/* Target protocol address */
        NULL,				/* Payload */
        0,				/* Payload length */
        buf + LIBNET_ETH_H);

    n = libnet_write_link_layer(l, device, buf, LIBNET_ARP_H + LIBNET_ETH_H);

    libnet_destroy_packet(&buf);
    return (n);
}
#endif /* HAVE_LIBNET_1_0_API */




#ifdef HAVE_LIBNET_1_1_API
int
send_arp(libnet_t* lntag, u_long ip, u_char *device, u_char *macaddr, u_char *broadcast, u_char *netmask, u_short arptype)
{
    int n;
    u_char enet_src[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    u_char enet_dst[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


    /* Convert ASCII Mac Address to 6 Hex Digits. */
    convert_macaddr (macaddr, enet_src);

    /*
     *  ARP header
     */
    libnet_build_arp(ARPHRD_ETHER,	/* hardware address type */
        ETHERTYPE_IP,	/* protocol address type */
        6,		/* Hardware address length */
        4,		/* protocol address length */
        arptype,	/* ARP operation type */
        enet_src,	/* sender Hardware address */
        (u_char *)&ip,	/* sender protocol address */
        enet_dst,	/* target hardware address */
        (u_char *)&ip,	/* target protocol address */
        NULL,		/* Payload */
        0,		/* Length of payload */
	lntag,		/* libnet context pointer */
	0		/* packet id */
	);

    /* Ethernet header */
    libnet_build_ethernet(enet_dst, enet_src, ETHERTYPE_ARP, NULL, 0
    ,	lntag, 0);

    n = libnet_write(lntag);
    libnet_clear_packet(lntag);

    return (n);
}
#endif /* HAVE_LIBNET_1_1_API */

/*
 * $Log: send_arp.c,v $
 * Revision 1.13  2003/01/31 10:02:09  lars
 * Various small code cleanups:
 * - Lots of "signed vs unsigned" comparison fixes
 * - time_t globally replaced with TIME_T
 * - All seqnos moved to "seqno_t", which defaults to unsigned long
 * - DIMOF() definition centralized to portability.h and typecast to int
 * - EOS define moved to portability.h
 * - dropped inclusion of signal.h from stonith.h, so that sigignore is
 *   properly defined
 *
 * Revision 1.12  2002/09/12 14:06:18  msoffen
 * Removed a write to stderr of ".", really served no purpose and always ran.
 * It was a carryover from the old send_arp.c.
 *
 * Revision 1.11  2002/09/05 06:12:42  alan
 * Put in code to recover a bug fix from Japan, plus
 * make the code hopefully work with both the old and new libnet APIs.
 *
 * Revision 1.10  2002/08/16 14:18:41  msoffen
 * Changes to get IP takeover working properly on OpenBSD
 * Changed how *BSD deletes an alias.
 * Create get_hw_addr (uses libnet package).
 *
 * Revision 1.9  2002/07/08 04:14:12  alan
 * Updated comments in the front of various files.
 * Removed Matt's Solaris fix (which seems to be illegal on Linux).
 *
 * Revision 1.8  2002/06/06 04:43:40  alan
 * Got rid of a warning (error) about an unused RCS version string.
 *
 * Revision 1.7  2002/05/28 18:25:48  msoffen
 * Changes to replace send_arp with a libnet based version.  This works accross
 * all operating systems we currently "support" (Linux, FreeBSD, Solaris).
 *
 * Revision 1.6  2001/10/24 00:21:58  alan
 * Fix to send both a broadcast ARP request as well as the ARP response we
 * were already sending.  All the interesting research work for this fix was
 * done by Masaki Hasegawa <masaki-h@pp.iij4u.or.jp> and his colleagues.
 *
 * Revision 1.5  2001/06/07 21:29:44  alan
 * Put in various portability changes to compile on Solaris w/o warnings.
 * The symptoms came courtesy of David Lee.
 *
 * Revision 1.4  2000/12/04 20:33:17  alan
 * OpenBSD fixes from Frank DENIS aka Jedi/Sector One <j@c9x.org>
 *
 * Revision 1.3  1999/10/05 06:17:29  alanr
 * Fixed various uninitialized variables
 *
 * Revision 1.2  1999/09/30 18:34:27  alanr
 * Matt Soffen's FreeBSD changes
 *
 * Revision 1.1.1.1  1999/09/23 15:31:24  alanr
 * High-Availability Linux
 *
 * Revision 1.4  1999/09/08 03:46:27  alanr
 * Changed things so they work when rearranged to match the FHS :-)
 *
 * Revision 1.3  1999/08/17 03:49:09  alanr
 * *** empty log message ***
 *
 */
