static const char _findif_c [] = "$Id: findif.c,v 1.2 1999/09/30 18:34:27 alanr Exp $";
/*
 *  This code written by
 *	Alan Robertson <alanr@henge.com> (c) 1999
 *	Released under the GNU General Public License
 *
 *	findif.c:	Finds an interface which can route a given address
 *
 *	It's really simple to write in C, but hard to write in the shell...
 *
 *	This code is dependent on IPV4 addressing conventions...
 *		Sorry.
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
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define	PROCROUTE	"/proc/net/route"

const char *	cmdname = "findif";
void usage(void);

#define EOS	'\0'
#define DELIM	'/'
#define	BAD_NETMASK	(~0L)
#define	BAD_BROADCAST	(0L)
#define	MAXSTR	128

int
main(int argc, char ** argv) {

	char *	address = NULL;
	char *	bcast_arg = NULL;
	char *	netmaskbits = NULL;
	FILE *	routefd = fopen(PROCROUTE, "r");
	char	buf[1024];
	char	interface[MAXSTR];
	struct in_addr	in;
	long	dest, gw, flags, refcnt, use, metric, mask;
	long	netmask = BAD_NETMASK;
	char	best_if[MAXSTR];
	char *	if_specified = NULL;
	int	best_metric = INT_MAX;
	unsigned long	best_netmask = INT_MAX;

	(void)_findif_c;
	cmdname=argv[0];

	if (argc < 2) {
		usage();
		return(1);
	}
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
	address = argv[1];

	if ((netmaskbits = strchr(address, DELIM)) != NULL) {
		*netmaskbits = EOS;
		++netmaskbits;

			
		if ((bcast_arg=strchr(netmaskbits, DELIM)) != NULL) {
			*bcast_arg = EOS;
			++bcast_arg;
			/* Did they specify the interface to use? */
			if (!isdigit(*bcast_arg)) {
				if_specified = bcast_arg;
				if ((bcast_arg=strchr(bcast_arg,DELIM))!=NULL){
					*bcast_arg = EOS;
					++bcast_arg;
				}else{
					bcast_arg = NULL;
				}
				/* OK... Now we know the interface */
			}
		}
	}

	/* Is the IP address we're supposed to find valid? */
	 
	if (inet_aton(address, &in) == 0) {
		fprintf(stderr, "IP address [%s] not valid.", address);
		usage();
		return(1);
	}

	/* Validate the netmaskbits field */

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
			netmask = (1L<<(bits))-1L;
			netmask = ((~netmask)&0xffffffffUL);
			netmask = (netmask&0x000000ffUL) <<24
			|	  (netmask&0x0000ff00UL) <<8
			|	  (netmask&0x00ff0000UL) >>8
			|	  (netmask&0xff000000UL) >>24;
		}
	}


	if (if_specified != NULL) {
		strcpy(best_if, if_specified);
	}else{
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
			if ((in.s_addr&mask) == (dest&mask)
			&&	metric < best_metric) {
				best_metric = metric;
				best_netmask = mask;
				strcpy(best_if, interface);
			}
		}
		fclose(routefd);
		if (best_metric == INT_MAX) {
			fprintf(stderr, "No route to %s\n", address);
			return(1);
		}
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
		/* Yes, they gave us a broadcast address */
		printf("%s\t netmask %d.%d.%d.%d\tbroadcast %s\n"
		,	best_if
		,	(int)(best_netmask & 0xff)
		,	(int)((best_netmask>>8) & 0xff)
		,	(int)((best_netmask>>16) & 0xff)
		,	(int)((best_netmask>>24) & 0xff)
		,	bcast_arg);
	}else{
		/* No, we use a common broadcast address convention */
		unsigned long	def_bcast;

			/* Common broadcast address */
		def_bcast = (in.s_addr | (~best_netmask));
#if 0
		fprintf(stderr, "best_netmask = %08lx, def_bcast = %08lx\n"
		,	best_netmask,  def_bcast);
#endif

		printf("%s\tnetmask %d.%d.%d.%d\tbroadcast %d.%d.%d.%d\n"
		,	best_if
		,	(int)(best_netmask & 0xff)
		,	(int)((best_netmask>>8) & 0xff)
		,	(int)((best_netmask>>16) & 0xff)
		,	(int)((best_netmask>>24) & 0xff)
		,	(int)(def_bcast & 0xff)
		,	(int)((def_bcast>>8) & 0xff)
		,	(int)((def_bcast>>16) & 0xff)
		,	(int)((def_bcast>>24) & 0xff));
	}
	return(0);
}

void
usage()
{
	fprintf(stderr, "usage: %s ip-address[/CIDR-maskbits[/broadcast-addr]]\n"
	,	cmdname);
	exit(1);
}
		
/*
Iface	Destination	Gateway 	Flags	RefCnt	Use	Metric	Mask		MTU	Window	IRTT                                                       
eth0	33D60987	00000000	0005	0	0	0	FFFFFFFF	0	0	0                                                                               
eth0	00D60987	00000000	0001	0	0	0	00FFFFFF	0	0	0                                                                               
lo	0000007F	00000000	0001	0	0	0	000000FF	0	0	0                                                                                 
eth0	00000000	FED60987	0003	0	0	0	00000000	0	0	0                                                                               
*/
/* 
 * $Log: findif.c,v $
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
