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

#ifndef PAGE_SIZE
#	include <sys/param.h>
#endif

#ifndef PAGE_SIZE
# ifdef HAVE_ASM_PAGE_H
#	include <asm/page.h>	/* This is where Linux puts it */
# endif /*HAVE_ASM_PAGE_H*/
#endif

#if !defined(PAGE_SIZE)

/*
 * Still don't have PAGE_SIZE?
 *
 * At least on Solaris, we have PAGESIZE.
 * So in theory, could:
 *      #if !defined(PAGE_SIZE) && defined(PAGESIZE)
 *      #define PAGE_SIZE PAGESIZE
 *      #endif
 *
 * Unfortunately, PAGESIZE (Solaris) is not a constant, rather a function call.
 * So its later use in "struct pstat_shm { ... array[fn(PAGESIZE)]}"
 * would be faulty.  Sigh...
 *
 * Accordingly, define it to 4096, since and it's a reasonable guess
 * (which is good enough for our purposes - as described below)
 */

#	define PAGE_SIZE 4096
#endif


enum process_type {
	PROC_UNDEF=0,		/* OOPS! ;-) */
	PROC_CONTROL,		/* Control process */
	PROC_MST_STATUS,	/* Master status process */
	PROC_HBREAD,		/* Read process */
	PROC_HBWRITE,		/* Write process */
	PROC_PPP		/* (Obsolescent) PPP process */
};

enum process_status { 
	FORKED=1,	/* This process is forked, but not yet really running */
	RUNNING=2,	/* This process is fully active, and open for business*/
};

struct process_info {
	enum process_type	type;		/* Type of process */
	enum process_status	pstat;		/* Is it running yet? */
	pid_t			pid;		/* Process' PID */
	unsigned long		totalmsgs;	/* Total # of messages */
						/* ever handled */
	unsigned long		allocmsgs;	/* # Msgs currently allocated */
	unsigned long		numalloc;	/* # of ha_malloc calls */
	unsigned long		numfree;	/* # of ha_free calls */
	unsigned long		nbytes_req;	/* # malloc bytes req'd */
	unsigned long		nbytes_alloc;	/* # bytes currently allocated 
						 */
	unsigned long		mallocbytes;	/* total # bytes malloc()ed  */
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

#define	MXPROCS	((PAGE_SIZE-3*sizeof(int))/sizeof(struct process_info)-1)

struct pstat_shm {
	int	nprocs;
	int	restart_after_shutdown;
	struct process_info info [MXPROCS];
};

/* These are volatile because they're in shared memory */
volatile extern struct pstat_shm *	procinfo;
volatile extern struct process_info *	curproc;

#endif /*_HB_PROC_H*/
