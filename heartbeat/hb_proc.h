/*
 * hb_proc.h: definitions of heartbeat child process info
 *
 * These are the things that let us manage our child processes well.
 *
 * Copyright (C) 2001 Alan Robertson <alanr@unix.sh>
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

#ifndef _HB_PROC_H
#	define _HB_PROC_H 1

/* Need to use autoconf tricks to do this really right... */

#ifndef PAGE_SIZE
#	include <sys/param.h>
#endif

#ifndef PAGE_SIZE
#	include <asm/page.h>	/* This is where Linux puts it */
#endif


enum process_type {
	PROC_UNDEF,
	PROC_CONTROL,
	PROC_MST_STATUS,
	PROC_HBREAD,
	PROC_HBWRITE,
	PROC_PPP
};

enum process_status { 
	FORKED=1,	/* This process is forked, but not yet really running */
	RUNNING=2,	/* This process is fully active, and open for business*/
};

struct process_info {
	enum process_type	type;
	enum process_status	pstat;
	pid_t			pid;
	unsigned long		totalmsgs;
	unsigned long		allocmsgs;
	unsigned long		numalloc;	/* # of ha_malloc calls */
	unsigned long		numfree;	/* # of ha_free calls */
	unsigned long		nbytes_req;	/* # malloc bytes req'd */
	unsigned long		nbytes_alloc;	/* # bytes allocated  */
	unsigned long		mallocbytes;	/* # bytes malloc()ed  */
	time_t			lastmsg;
};

/*
 * The point of MXPROCS being defined this way is that it's nice (but
 * not essential) to have the number of processes we can manage fit
 * neatly in a page.  Nothing bad happens if it's too large or too small.
 *
 * It just appealed to me to do it this way because the shared memory
 * limitations are naturally organized in pages.
 */

/* This figure contains a couple of probably unnecessary fudge factors */

#define	MXPROCS	((PAGE_SIZE-2*sizeof(int))/sizeof(struct process_info)-1)

struct pstat_shm {
	int	nprocs;
	struct process_info info [MXPROCS];
};

/* These are volatile because they're in shared memory */
volatile extern struct pstat_shm *	procinfo;
volatile extern struct process_info *	curproc;

#endif /*_HB_PROC_H*/
