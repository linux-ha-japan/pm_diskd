/* 
 * ccmversion.c: routines that handle information while in the version 
 * request state
 *
 * Copyright (c) International Business Machines  Corp., 2002
 * Author: Ram Pai (linuxram@us.ibm.com)
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
#include <ccm.h>

#define TIMEOUT 3
#define MAXTRIES 3

#define VERSION_GET_TIMER(ver) (&(ver->time))
#define VERSION_GET_TRIES(ver) ver->numtries
#define VERSION_RESET_TRIES(ver) ver->numtries = 0
#define VERSION_INC_TRIES(ver) (ver->numtries)++


extern int global_debug;
//
// return true if we have waited long enough for a response
// for our version request.
//
static int
version_timeout_expired(ccm_version_t *ver)
{
	struct timeval tmp;

	ccm_get_time(&tmp);

	return(ccm_timeout(VERSION_GET_TIMER(ver), &tmp, TIMEOUT));
}

//
// reset all the data structures used to track the version request
// state.
//
void
version_reset(ccm_version_t *ver)
{
	(void)_heartbeat_h_Id; /* keeping compiler happy */
	(void)_ha_msg_h_Id; /* keeping compiler happy */
	ccm_get_time(VERSION_GET_TIMER(ver));
	VERSION_RESET_TRIES(ver);
}

//
// return true if version request has message has to be resent.
// else return false.
//
int
version_retry(ccm_version_t *ver)
{
	if(version_timeout_expired(ver)) {
		if(global_debug) {
			fprintf(stderr, "%d tries left\n", 
					3-VERSION_GET_TRIES(ver));
		}
		if(VERSION_GET_TRIES(ver) == MAXTRIES) {
			return FALSE;
		} else {
			VERSION_INC_TRIES(ver);
			ccm_get_time(VERSION_GET_TIMER(ver));
			return TRUE;
		}
	}
	return TRUE;
}

//
// The caller informs us:
// "please note that there is some activity going on in the cluster.
// Probably you may want to try for some more time"
//
void
version_some_activity(ccm_version_t *ver)
{
	VERSION_RESET_TRIES(ver);
}
