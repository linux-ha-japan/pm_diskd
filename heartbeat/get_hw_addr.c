#include <portability.h>

#include <stdio.h>
#include <stdlib.h>
#include <libnet.h>
#include <string.h>
#include <syslog.h>

const char * ether_ntoa(struct ether_addr *macaddr);

#ifdef HAVE_LIBNET_1_0_API

int
main(int argc, char *argv[])
{
	struct ether_addr	*mac_address;
	struct libnet_link_int	*network;
	u_char			*device;
	char			err_buf[LIBNET_ERRBUF_SIZE];

	/* Get around bad prototype for libnet_error() */

	char errmess1 [] = "libnet_open_link_interface: %s\n";
	char errmess2 [] = "libnet_get_hwaddr: %s\n";

	if (argc != 2) {
		syslog(LOG_ERR,"usage: get_hw_addr <INTERFACE-NAME>\n");
		exit(1);
	}

	device = argv[1];

	if ( (network = libnet_open_link_interface(device, err_buf)) == NULL) {
		libnet_error(LIBNET_ERR_FATAL, errmess1, err_buf);
	}

       if ( (mac_address = libnet_get_hwaddr(network, device, err_buf)) == 0) {
		libnet_error(LIBNET_ERR_FATAL, errmess2, err_buf);
	}

	printf("%s\n", ether_ntoa(mac_address));

	exit(0);
}
#endif

#ifdef HAVE_LIBNET_1_1_API
int
main(int argc, char *argv[])
{
	struct libnet_ether_addr	*mac_address;
	libnet_t		*ln;
	u_char			*device;
	char			err_buf[LIBNET_ERRBUF_SIZE];

	/* Get around bad prototype for libnet_error() */


	if (argc != 2) {
		syslog(LOG_ERR,"usage: get_hw_addr <INTERFACE-NAME>\n");
		exit(1);
	}

	device = argv[1];

	if ( (ln = libnet_init(LIBNET_LINK, device, err_buf)) == NULL) {
		fprintf(stderr, "libnet_open_link_interface: %s\n", err_buf);
		exit(1);
	}

       if ( (mac_address = libnet_get_hwaddr(ln)) == 0) {
		fprintf(stderr,  "libnet_get_hwaddr: %s\n", err_buf);
		exit(1);
	}

	printf("%s\n", ether_ntoa((struct ether_addr*)mac_address));

	exit(0);
}
#endif
