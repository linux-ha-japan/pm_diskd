const static char * _ha_if_c_Id = "$Id: ha_if.c,v 1.1 2000/10/06 19:26:20 eric Exp $";

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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>

#include <sys/ioctl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include "heartbeat.h"
#include "ha_if.h"

/*
  if_get_broadaddr
     Retrieve the ipv4 broadcast address for the specified network interface.

  Inputs:  ifn - the name of the network interface:
                 e.g. eth0, eth1, ppp0, plip0, plusb0 ...
  Outputs: broadaddr - returned broadcast address.

  Returns: 0 on success
           -1 on failure - sets errno.
 */
int if_get_broadaddr(const char *ifn, struct in_addr *broadaddr)
{
	int
		return_val,
		fd = -1;
	struct ifreq
		ifr; /* points to one interface returned from ioctl */

	/* get rid of compiler warnings about unreferenced variables */
	(void)_ha_if_c_Id;
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
