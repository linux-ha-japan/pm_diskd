/* 
 * ccmmisc.c: Miscellaneous Consensus Cluster Service functions
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

//
// Convert a given string to a bitmap.
//
int
ccm_str2bitmap(const char *memlist, unsigned char **bitlist)
{
	size_t str_len =  strnlen(memlist, maxstrsz);
	int    outbytes = B64_maxbytelen(str_len);

	(void)_heartbeat_h_Id; /* Make compiler happy */
	(void)_ha_msg_h_Id; /* Make compiler happy */

	if (str_len == 0) {
	   	return bitmap_create(bitlist, MAXNODE);
	}

	while ((*bitlist = (char *)g_malloc(outbytes)) == NULL) {
		sleep(1);
	}
	memset(*bitlist,0,outbytes);

	outbytes = base64_to_binary(memlist, str_len, *bitlist, outbytes);

	return outbytes;
}


//
// Convert a given bitmap to a string.
//
int
ccm_bitmap2str(const unsigned char *bitmap, int numBytes, char **memlist)
{
	int maxstrsize;

	maxstrsize = B64_stringlen(numBytes)+1;
	/* we want memory and we want it now */
	while ((*memlist = (char *)g_malloc(maxstrsize)) == NULL) {
		sleep(1);
	}

	return binary_to_base64(bitmap, numBytes, *memlist, maxstrsize);
}

// 
//
// END OF GENERIC FUNCTION FOR BITMAP AND STRING CONVERSION.
//


							
//
// BEGIN OF FUNCTIONS THAT FACILITATE A MONOTONICALLY INCREASING
// LOCAL CLOCK. Useful for timeout manipulations.
//
// NOTE: gettimeofday() is generally helpful, but has the disadvantage
// of resetting to a earlier value(in case system administrator resets
// the clock)
// Similarly times() is a monotonically increasing clock, but has the
// disadvantage a of wrapping back on overflow.
//
//

//
// return the current time 
// 
void 
ccm_get_time(struct timeval *t1)
{
	struct tms tm;
	long 	cps;
	clock_t clk;

	cps = (long)sysconf(_SC_CLK_TCK);
	while((clk = times(&tm)) == -1) { 
		sleep(1); 
		continue; 
	}

	t1->tv_sec = clk/cps;
	t1->tv_usec = ((clk%cps)*1000000)/cps;
	return;
}


//
// given two times, and a timeout interval(in seconds), 
// return true if the timeout has occured, else return
// false.
// NOTE: 'timeout' is in seconds.
int
ccm_timeout(struct timeval *t1, struct timeval *t2, long timeout)
{
	clock_t t1cl, t2cl, t3cl, interval, timeoutclks;
	long cps;

	cps = sysconf(_SC_CLK_TCK);

	timeoutclks = timeout*cps;
	t1cl = (t1->tv_sec+(t1->tv_usec/1000000))*cps;
	t2cl = (t2->tv_sec+(t2->tv_usec/1000000))*cps;

	if (t1cl <= t2cl) {
		interval = t2cl - t1cl;
		if(interval > timeoutclks) 
			return TRUE;
		else
			return FALSE;
	} else {
		t3cl = t1cl+timeoutclks;
		if (t3cl > t1cl) 
			return TRUE;
		else if (t3cl < t2cl) 
			return TRUE;
		else 
			return FALSE;
	}
}
