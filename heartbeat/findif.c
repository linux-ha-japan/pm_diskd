static const char _findif_c [] = "$Id: findif.c,v 1.12 2001/07/17 15:00:04 alan Exp $";
/*
 * findif.c:	Finds an interface which can route a given address
 *
 *	It's really simple to write in C, but hard to write in the shell...
 *
 *	This code is dependent on IPV4 addressing conventions...
 *		Sorry.
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 * Copyright (C) 2001 Matt Soffen <matt@soffen.com>
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
 *
 *
 ***********************************************************
 *
 *	Our single argument is of the form:
 *		address/CIDR-netmask/{interface/}broadcast address
 *	with the everything but the address being optional
 *
 *	So, the following forms are legal:
 *		135.9.216.100
 *		135.9.216.100/24		Implies a 255.255.255.0 netmask
 *		135.9.216.100/8/135.9.216.255
 *
 *
 *	If the CIDR netmask is omitted, we choose the netmask associated with
 *	the route we selected.
 *
 *	If the broadcast address was omitted, we assume the highest address
 *	in the subnet.
 *
 */
#include <portability.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>

#undef __OPTIMIZE__
/*
 * This gets rid of some silly -Wtraditional warnings on Linux
 * because the netinet header has some slightly funky constants
 * in it.
 */

#include <netinet/in.h>
#include <arpa/inet.h>

#define DEBUG 0

void ConvertQuadToInt (char *dest);

void ConvertBitsToMask (char *mask);

int SearchForProcRoute (char *address, struct in_addr *in, struct in_addr *addr_out
,	 char *best_if, unsigned long *best_netmask);

int SearchForRoute (char *address, struct in_addr *out, struct in_addr *addr_out	
,	char *best_if, unsigned long *best_netmask);

void GetAddress (char *inputaddress, char **address, char **netmaskbits
,	 char **bcast_arg, char **if_specified);

void ValidateNetmaskBits (char *netmaskbits, long *netmask);

const char *	cmdname = "findif";
void usage(void);

#define EOS	'\0'
#define DELIM	'/'
#define	BAD_NETMASK	(~0L)
#define	BAD_BROADCAST	(0L)
#define	MAXSTR	128

void
ConvertQuadToInt (char *dest)
{
	int ipquad[4] = { 0, 0, 0, 0 };
	unsigned long int intdest = 0;

	/*
	Convert a dotted quad into a value

	ex.  192.168.123.1 would be converted to
		1.123.168.192
		1.7B.A8.C0
		17BA8C0
		24881344

	*/

	while (strstr (dest, ".")) { *strstr(dest, ".") = ' '; }
	sscanf (dest, "%d%d%d%d", &ipquad[3], &ipquad[2], &ipquad[1], &ipquad[0]);
	intdest = (ipquad[0] * 0x1000000) + (ipquad[1] * 0x10000) + (ipquad[2] * 0x100) + ipquad[3];
	sprintf (dest, "%ld", intdest);
}

void
ConvertBitsToMask (char *mask)
{
	int maskbits;
	unsigned long int maskflag = 0x1;
	unsigned long int longmask = 0;
	int i;

	maskbits = atoi(mask);

	for (i = 0; i < 32; i++)
	{
		if (i <  maskbits)
			longmask |= maskflag;
		/* printf ("%d:%x:%x\n", i, longmask, maskflag); */
		maskflag = maskflag << 1;
	}

	/* printf ("longmask: %u (%X)\n", longmask, longmask); */
        sprintf (mask, "%ld", longmask);
}

int
SearchForProcRoute (char *address, struct in_addr *in, struct in_addr *addr_out
,	 char *best_if, unsigned long *best_netmask)
{
	long    dest, gw, flags, refcnt, use, metric, mask;
	int	best_metric = INT_MAX;
	
	char	buf[2048];
	char	interface[MAXSTR];
	FILE *routefd = NULL;

	if ((routefd = fopen(PROCROUTE, "r")) == NULL) {
		fprintf(stderr, "Cannot open %s for reading"
		,	PROCROUTE);
		return(1);
	}

	/* Skip first line */
	fgets(buf, sizeof(buf), routefd);
	while (fgets(buf, sizeof(buf), routefd) != NULL) {
		if (sscanf(buf, "%[^\t]\t%lx%lx%lx%lx%lx%lx%lx"
		,	interface, &dest, &gw, &flags, &refcnt, &use
		,	&metric, &mask)
		!= 8) {
			fprintf(stderr, "Bad line in %s: %s"
			,	PROCROUTE, buf);
			return(1);
		}
		if ((in->s_addr&mask) == (dest&mask)
		&&	metric < best_metric) {
			best_metric = metric;
			*best_netmask = mask;
			strcpy(best_if, interface);
		}
	}
	fclose(routefd);

	if (best_metric == INT_MAX) {
		fprintf(stderr, "No route to %s\n", address);
		return(1); 
	}

        return(0);
}

int
SearchForRoute (char *address, struct in_addr *in, struct in_addr *addr_out
,	 char *best_if, unsigned long *best_netmask)
{
	char	dest[20], mask[20];
	char	routecmd[MAXSTR];
	int	best_metric = INT_MAX;	
	char	buf[2048];
	char	interface[MAXSTR];
	char  *cp, *sp;
	int done = 0;
	FILE *routefd = NULL;

	
	/* Open route and get the information */
	sprintf (routecmd, "%s %s", ROUTE, address);
	routefd = popen (routecmd, "r");
	if (routefd == NULL)
		return (-1);
	mask[0] = EOS;

	while ((done < 3) && fgets(buf, sizeof(buf), routefd)) {
		cp = buf;

		sp = buf + strlen(buf);
		while (sp!=buf && isspace(*(sp-1))) --sp;
		*sp = '\0';

		buf[strlen(buf)] = EOS;
		if (strstr (buf, "mask:"))
		{
			/*strsep(&cp, ":");cp++;*/
			cp = strtok(buf, ":");
			cp = strtok(NULL, ":");cp++;
			strcpy(mask, cp);
                  	done++;
		}

		if (strstr (buf, "interface:"))
		{
			/*strsep(&cp, ":");cp++;*/
			cp = strtok(buf, ":");
			cp = strtok(NULL, ":");cp++;
			strcpy(interface, cp);
                  	done++;
		}

		if (strstr (buf, "destination:"))
		{
			/*strsep(&cp, ":");cp++;*/
			cp = strtok(buf, ":");
			cp = strtok(NULL, ":");cp++;
			strcpy(dest, cp);
                  	done++;
		}
	}

	/* Check to see if mask isn't available.  It may not bereturned if multiple IP's are defined.
	use 255.255.255.255 for mask then */
	if (!strlen(mask)) strcpy (mask, "255.255.255.255");
	ConvertQuadToInt  (dest);
	ConvertBitsToMask (mask);

	if (inet_pton(AF_INET, address, addr_out) <= 0) {
		fprintf(stderr, "IP address [%s] not valid.", address);
		usage();
		return(1);
	}

	if ((in->s_addr & atoi(mask)) == (addr_out->s_addr & atoi(mask))) {
		best_metric = 0;
		*best_netmask = atoi(mask);
		strcpy(best_if, interface);
	}

	fclose(routefd);
	if (best_metric == INT_MAX) {
		fprintf(stderr, "No route to %s\n", address);
		return(1);
	}

	return (0);
}

void
GetAddress (char *inputaddress, char **address, char **netmaskbits
,	 char **bcast_arg, char **if_specified)
{
	/*
	 *	Our argument is of the form:
	 *		address/CIDR-bitcount/broadcast address
	 *	with the last two portions being optional
	 *
	 *	So, the following forms are legal:
	 *		135.9.216.100
	 *		135.9.216.100/8
	 *		135.9.216.100/8/135.9.216.255
	 *
	 *	See http://www.doom.net/docs/netmask.html for a table
	 *	explaining CIDR address format and their relationship
	 *	to life, the universe and everything.
	 *
	 */
	*address = inputaddress;

	if ((*netmaskbits = strchr(*address, DELIM)) != NULL) {
		**netmaskbits = EOS;
		++(*netmaskbits);

		if ((*bcast_arg=strchr(*netmaskbits, DELIM)) != NULL) {
			**bcast_arg = EOS;
			++*bcast_arg;
			/* Did they specify the interface to use? */
			if (!isdigit(**bcast_arg)) {
				*if_specified = *bcast_arg;
				if ((*bcast_arg=strchr(*bcast_arg,DELIM))
				!=	NULL){
					**bcast_arg = EOS;
					++*bcast_arg;
				}else{
					*bcast_arg = NULL;
				}
				/* OK... Now we know the interface */
			}
		}
	}
}

void
ValidateNetmaskBits (char *netmaskbits, long *netmask)
{
	if (netmaskbits != NULL) {
		if ((strspn(netmaskbits, "0123456789")
		!=	strlen(netmaskbits)
		||	strlen(netmaskbits) == 0)) {
			fprintf(stderr, "Invalid netmask specification"
			" [%s]", netmaskbits);
			usage();
		}else{
			long	bits = atoi(netmaskbits);

			if (bits < 1 || bits > 32) {
				fprintf(stderr
				,	"Invalid netmask specification [%s]"
				,	netmaskbits);
				usage();
			}

			bits = 32 - bits;
			*netmask = (1L<<(bits))-1L;
			*netmask = ((~(*netmask))&0xffffffffUL);
			*netmask = htonl(*netmask);
		}
	}
}

int
main(int argc, char ** argv) {

	char *	address = NULL;
	char *	bcast_arg = NULL;
	char *	netmaskbits = NULL;
	struct in_addr	in = { 0 };
	struct in_addr	addr_out = { 0 };
	long	netmask = BAD_NETMASK;
	char	best_if[MAXSTR];
	char *	if_specified = NULL;
	unsigned long	best_netmask = INT_MAX;

	(void)_findif_c;
	cmdname=argv[0];

	if (argc < 2) {
		usage();
		return(1);
      }

	GetAddress (argv[1], &address, &netmaskbits, &bcast_arg, &if_specified);

	/* Is the IP address we're supposed to find valid? */
	 
	if (inet_pton(AF_INET, address, (void *)&in) <= 0) {
		fprintf(stderr, "IP address [%s] not valid.", address);
		usage();
		return(1);
	}

	/* Validate the netmaskbits field */
	ValidateNetmaskBits (netmaskbits, &netmask);

	if (if_specified != NULL) {
		strcpy(best_if, if_specified);
	}else{
#ifdef USE_ROUTE_GET
		SearchForRoute (address, &in, &addr_out, best_if, &best_netmask);
#else
		SearchForProcRoute (address, &in, &addr_out, best_if, &best_netmask);
#endif
      }

	if (netmask != BAD_NETMASK) {
		best_netmask = netmask;
	}else if (best_netmask == 0L) {
		fprintf(stderr
		,	"ERROR: Cannot use default route w/o netmask [%s]\n"
		,	 address);
		return(1);
	}
		


	/* Did they tell us the broadcast address? */

	if (bcast_arg) {
		best_netmask = htonl(best_netmask);
		/* Yes, they gave us a broadcast address */
		printf("%s\t netmask %d.%d.%d.%d\tbroadcast %s\n"
		,	best_if
                ,       (int)((best_netmask>>24) & 0xff)
                ,       (int)((best_netmask>>16) & 0xff)
                ,       (int)((best_netmask>>8) & 0xff)
                ,       (int)(best_netmask & 0xff)
		,	bcast_arg);
	}else{
		/* No, we use a common broadcast address convention */
		unsigned long	def_bcast;

			/* Common broadcast address */
		def_bcast = (in.s_addr | (~best_netmask));
#if DEBUG
		fprintf(stderr, "best_netmask = %08lx, def_bcast = %08lx\n"
		,	best_netmask,  def_bcast);
#endif

		/* Make things a bit more machine-independent */
		best_netmask = htonl(best_netmask);
		def_bcast = htonl(def_bcast);
                printf("%s\tnetmask %d.%d.%d.%d\tbroadcast %d.%d.%d.%d\n"
                ,       best_if
                ,       (int)((best_netmask>>24) & 0xff)
                ,       (int)((best_netmask>>16) & 0xff)
                ,       (int)((best_netmask>>8) & 0xff)
                ,       (int)(best_netmask & 0xff)
                ,       (int)((def_bcast>>24) & 0xff)
                ,       (int)((def_bcast>>16) & 0xff)
                ,       (int)((def_bcast>>8) & 0xff)
                ,       (int)(def_bcast & 0xff));
	}
	return(0);
}

void
usage()
{
	fprintf(stderr, "usage: %s "
	"ip-address[/CIDR-maskbits[/broadcast-addr]]\n"
	,	cmdname);
	exit(1);
}
		
/*
Iface	Destination	Gateway 	Flags	RefCnt	Use	Metric	Mask		MTU	Window	IRTT                                                       
eth0	33D60987	00000000	0005	0	0	0	FFFFFFFF	0	0	0                                                                               
eth0	00D60987	00000000	0001	0	0	0	00FFFFFF	0	0	0                                                                               
lo	0000007F	00000000	0001	0	0	0	000000FF	0	0	0                                                                                 
eth0	00000000	FED60987	0003	0	0	0	00000000	0	0	0                                                                               

netstat -rn outpug from RedHat Linux 6.0
Kernel IP routing table
Destination     Gateway         Genmask         Flags   MSS Window  irtt Iface
192.168.85.2    0.0.0.0         255.255.255.255 UH        0 0          0 eth1
10.0.0.2        0.0.0.0         255.255.255.255 UH        0 0          0 eth2
208.132.134.61  0.0.0.0         255.255.255.255 UH        0 0          0 eth0
208.132.134.32  0.0.0.0         255.255.255.224 U         0 0          0 eth0
192.168.85.0    0.0.0.0         255.255.255.0   U         0 0          0 eth1
10.0.0.0        0.0.0.0         255.255.255.0   U         0 0          0 eth2
127.0.0.0       0.0.0.0         255.0.0.0       U         0 0          0 lo
0.0.0.0         208.132.134.33  0.0.0.0         UG        0 0          0 eth0

|--------------------------------------------------------------------------------
netstat -rn output from FreeBSD 3.3
Routing tables

Internet:
Destination        Gateway            Flags     Refs     Use     Netif Expire
default            209.61.94.161      UGSc        3        8      pn0
192.168            link#1             UC          0        0      xl0
192.168.0.2        0:60:8:a4:91:fd    UHLW        0       38      lo0
192.168.0.255      ff:ff:ff:ff:ff:ff  UHLWb       1     7877      xl0
209.61.94.160/29   link#2             UC          0        0      pn0
209.61.94.161      0:a0:cc:26:c2:ea   UHLW        6    17265      pn0   1105
209.61.94.162      0:a0:cc:27:1c:fb   UHLW        1      568      pn0   1098
209.61.94.163      0:a0:cc:29:1f:86   UHLW        0     4749      pn0   1095
209.61.94.166      0:a0:cc:27:2d:e1   UHLW        0       12      lo0
209.61.94.167      ff:ff:ff:ff:ff:ff  UHLWb       0    10578      pn0

|--------------------------------------------------------------------------------
netstat -rn output from FreeBSD 4.2
Routing tables

Internet:
Destination        Gateway            Flags     Refs     Use     Netif Expire
default            64.65.195.1        UGSc        1       11      dc0
64.65.195/24       link#1             UC          0        0      dc0 =>
64.65.195.1        0:3:42:3b:0:dd     UHLW        2        0      dc0   1131
64.65.195.184      0:a0:cc:29:1f:86   UHLW        2    18098      dc0   1119
64.65.195.194      0:a0:cc:27:2d:e1   UHLW        3   335161      dc0    943
64.65.195.200      52:54:0:db:33:b3   UHLW        0       13      dc0    406
64.65.195.255      ff:ff:ff:ff:ff:ff  UHLWb       1      584      dc0
127.0.0.1          127.0.0.1          UH          0        0      lo0
192.168/16         link#2             UC          0        0      vx0 =>
192.168.0.1        0:20:af:e2:f0:36   UHLW        0        2      lo0
192.168.255.255    ff:ff:ff:ff:ff:ff  UHLWb       0        1      vx0

Internet6:
Destination                       Gateway                       Flags      Netif Expire
::1                               ::1                           UH          lo0
fe80::%dc0/64                     link#1                        UC          dc0
fe80::%vx0/64                     link#2                        UC          vx0
fe80::%lo0/64                     fe80::1%lo0                   Uc          lo0
ff01::/32                         ::1                           U           lo0
ff02::%dc0/32                     link#1                        UC          dc0
ff02::%vx0/32                     link#2                        UC          vx0
ff02::%lo0/32                     fe80::1%lo0                   UC          lo0
*/

/* 
 * $Log: findif.c,v $
 * Revision 1.12  2001/07/17 15:00:04  alan
 * Put in Matt's changes for findif, and committed my changes for the new module loader.
 * You now have to have glib.
 *
 * Revision 1.11  2001/06/23 04:30:26  alan
 * Changed the code to use inet_pton() when it's available, and
 * emulate it when it's not...  Patch was from Chris Wright.
 *
 * Revision 1.10  2001/06/07 21:29:44  alan
 * Put in various portability changes to compile on Solaris w/o warnings.
 * The symptoms came courtesy of David Lee.
 *
 * Revision 1.9  2001/05/10 22:36:37  alan
 * Deleted Makefiles from CVS and made all the warnings go away.
 *
 * Revision 1.8  2001/02/05 04:55:27  alan
 * Sparc fix from Uzi.
 *
 * Revision 1.7  2000/08/30 20:32:39  alan
 * Fixed a byte ordering problem in findif.c.  There's probably another one in the code yet.
 *
 * Revision 1.6  2000/08/13 20:37:49  alan
 * Fixed a bug related to byte-ordering in findif.c.  Thanks to
 *         Lars Kellogg-Stedman for the fix.  There are probably some
 * 	related to byte ordering in input that still need fixing...
 *
 * Revision 1.5  2000/07/26 05:17:19  alan
 * Added GPL license statements to all the code.
 *
 * Revision 1.4  2000/06/21 04:34:48  alan
 * Changed henge.com => linux-ha.org and alanr@henge.com => alanr@suse.com
 *
 * Revision 1.3  2000/01/26 15:16:48  alan
 * Added code from Michael Moerz <mike@cubit.at> to keep findif from
 * core dumping if /proc/route can't be read.
 *
 * Revision 1.2  1999/09/30 18:34:27  alanr
 * Matt Soffen's FreeBSD changes
 *
 * Revision 1.1.1.1  1999/09/23 15:31:24  alanr
 * High-Availability Linux
 *
 * Revision 1.5  1999/09/22 16:49:03  alanr
 * Put in the ability to explicitly specify the interface on the command line argument.
 * This was requested by Lars Marowsky-Bree.
 *
 * Revision 1.4  1999/09/16 15:03:24  alanr
 * fixed a glaring bug in CIDR style addresses...
 *
 * Revision 1.3  1999/09/12 06:23:00  alanr
 * Fixed calculation of the broadcast address.
 * Disallowed using default route to locate interface, unless a netmask is specified.
 *
 * Revision 1.2  1999/08/17 03:45:32  alanr
 * added RCS log to end of file...
 *
 */
