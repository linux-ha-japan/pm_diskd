const static char * _send_arp_c = "$Id: send_arp.c,v 1.6 2001/10/24 00:21:58 alan Exp $";
/* send_arp.c

This program sends out one ARP packet with source/target IP and Ethernet
hardware addresses suuplied by the user.  It compiles and works on Linux
and will probably work on any Unix that has SOCK_PACKET.

The idea behind this program is a proof of a concept, nothing more.  It
comes as is, no warranty.  However, you're allowed to use it under one
condition: you must use your brain simultaneously.  If this condition is
not met, you shall forget about this program and go RTFM immediately.

yuri volobuev'97
volobuev@t1.chem.umn.edu

*/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#if 0
#	include <linux/in.h>
#endif
#include <netinet/in.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#ifdef linux
#	define	NEWSOCKET()	socket(AF_INET, SOCK_PACKET, htons(ETH_P_RARP))
#else
#	define	NEWSOCKET()	socket(SOL_SOCKET, SOCK_RAW, ETHERTYPE_REVARP)
#endif

#define ETH_HW_ADDR_LEN 6
#define IP_ADDR_LEN 4
#define ARP_FRAME_TYPE 0x0806
#define ETHER_HW_TYPE 1
#define IP_PROTO_TYPE 0x0800
#define OP_ARP_REQUEST	1
#define OP_ARP_REPLY	2


char usage[]={"send_arp: sends out custom ARP packet. yuri volobuev'97\n\
\tusage: send_arp dev src_ip_addr src_hw_addr targ_ip_addr tar_hw_addr\n\n"};

struct arp_packet {
        u_char targ_hw_addr[ETH_HW_ADDR_LEN];
        u_char src_hw_addr[ETH_HW_ADDR_LEN];
        u_short frame_type;
        u_short hw_type;
        u_short prot_type;
        u_char hw_addr_size;
        u_char prot_addr_size;
        u_short op;
        u_char sndr_hw_addr[ETH_HW_ADDR_LEN];
        u_char sndr_ip_addr[IP_ADDR_LEN];
        u_char rcpt_hw_addr[ETH_HW_ADDR_LEN];
        u_char rcpt_ip_addr[IP_ADDR_LEN];
        u_char padding[18];
};

void die(const char *);
void get_ip_addr(struct in_addr*,char*);
void get_hw_addr(u_char*,char*);

#define	DIMOF(a)	(sizeof(a)/sizeof(a[0]))

int
main(int argc,char** argv)
{

	struct in_addr src_in_addr,targ_in_addr;
	struct arp_packet pkt;
	struct sockaddr sa;
	int	sock;
	int	optypes [] = { OP_ARP_REQUEST, OP_ARP_REPLY};
	int	j;


	(void)_send_arp_c;

	if (argc != 6) {
		die(usage);
	}

	sock=NEWSOCKET();
	if (sock < 0) {
		perror("socket");
		exit(1);
        }

	/* Most switches/routers respond to the ARP reply, a few only
	 * to an ARP request.  RFCs say they should respond
	 * to either.  Oh well... We'll try and work with all...
	 * So, we broadcast both an ARP request and a reply...
	 * See RFCs 2002 and 826.
	 *
	 * The observation about some only responding to ARP requests
	 * came from Masaki Hasegawa <masaki-h@pp.iij4u.or.jp>.
	 * So, this fix is due largely to him.
	 */

	for (j=0; j < DIMOF(optypes); ++j) {
		pkt.frame_type = htons(ARP_FRAME_TYPE);
		pkt.hw_type = htons(ETHER_HW_TYPE);
		pkt.prot_type = htons(IP_PROTO_TYPE);
		pkt.hw_addr_size = ETH_HW_ADDR_LEN;
		pkt.prot_addr_size = IP_ADDR_LEN;
		pkt.op=htons(optypes[j]);

		get_hw_addr(pkt.targ_hw_addr,argv[5]);
		get_hw_addr(pkt.rcpt_hw_addr,argv[5]);
		get_hw_addr(pkt.src_hw_addr,argv[3]);
		get_hw_addr(pkt.sndr_hw_addr,argv[3]);

		get_ip_addr(&src_in_addr,argv[2]);
		get_ip_addr(&targ_in_addr,argv[4]);

		memcpy(pkt.sndr_ip_addr,&src_in_addr,IP_ADDR_LEN);
		memcpy(pkt.rcpt_ip_addr,&targ_in_addr,IP_ADDR_LEN);

		memset(pkt.padding,0, 18);

		strcpy(sa.sa_data,argv[1]);
		if (sendto(sock, (const void *)&pkt,sizeof(pkt),0
		,	&sa,sizeof(sa)) < 0) {
			perror("sendto");
			exit(1);
		}
	}
	exit(0);
}

void die(const char* str)
{
	fprintf(stderr,"%s\n",str);
	exit(1);
}

void get_ip_addr(struct in_addr* in_addr,char* str)
{
	struct hostent *hostp;

	in_addr->s_addr=inet_addr(str);
	if (in_addr->s_addr == -1) {
		if ( (hostp = gethostbyname(str))) {
			memcpy(in_addr, hostp->h_addr, hostp->h_length);
		}else{
			fprintf(stderr,"send_arp: unknown host %s\n",str);
			exit(1);
		}
	}
}

void get_hw_addr(u_char* buf,char* str)
{

	int i;
	char c,val = 0;

	for(i=0;i<ETH_HW_ADDR_LEN;i++) {
		if ( !(c = tolower((unsigned int)(*str++)))) {
			die("Invalid hardware address");
		}
		if (isdigit((unsigned int)c)) {
			val = c-'0';
		}else if (c >= 'a' && c <= 'f') {
			val = c-'a'+10;
		}else{
			die("Invalid hardware address");
		}

		*buf = val << 4;

		if ( !(c = tolower(*str++))) {
			die("Invalid hardware address");
		}

		if (isdigit((unsigned int)c)) {
			val = c-'0';
		}else if (c >= 'a' && c <= 'f') {
			val = c-'a'+10;
		}else{
			die("Invalid hardware address");
		}

		*buf++ |= val;

		if (*str == ':')str++;
        }
}

/*
 * $Log: send_arp.c,v $
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
