static const char * _ha_malloc_c_id = "$Id: ha_malloc.c,v 1.11 2002/10/30 17:15:42 alan Exp $";
#include <portability.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_MALLOC_H
#	include <malloc.h>
#endif
#include <heartbeat.h>
#include <hb_proc.h>

#include <ltdl.h>

/*
 *
 *	Malloc wrapper functions
 *
 *	I wrote these so we can better track memory leaks, etc. and verify
 *	that the system is stable in terms of memory usage.
 *
 *	For our purposes, these functions are a somewhat faster than using
 *	malloc directly (although they use a bit more memory)
 *
 *	The general strategy is loosely related to the buddy system, 
 *	except very simple, well-suited to our continuous running
 *	nature, and the constancy of the requests and messages.
 *
 *	We keep an array of linked lists, each for a different size
 *	buffer.  If we need a buffer larger than the largest one provided
 *	by the list, we go directly to malloc.
 *
 *	Otherwise, we keep return them to the appropriate linked list
 *	when we're done with them, and reuse them from the list.
 *
 *	We never coalesce buffers on our lists, and we never free them.
 *
 *	It's very simple.  We get usage stats.  It makes me happy.
 *
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 *
 * This software licensed under the GNU LGPL.
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

#define	HA_MALLOC_MAGIC	0xFEEDBEEFUL
#define	HA_MALLOC_GUARD	0xACE00ECAUL		/* Not yet used */

struct ha_mhdr {
	unsigned long	magic;	/* Must match HA_MALLOC_MAGIC */
	size_t		reqsize;
	int		bucket;
};

struct ha_bucket {
	struct ha_mhdr		hdr;
	struct ha_bucket *	next;
};


#define	NUMBUCKS	8
#define	NOBUCKET	(NUMBUCKS)

struct ha_bucket*	ha_malloc_buckets[NUMBUCKS];
size_t	ha_bucket_sizes[NUMBUCKS];

static int ha_malloc_inityet = 0;
static int ha_malloc_hdr_offset = sizeof(struct ha_mhdr);

void*		ha_malloc(size_t size);
static void*	ha_new_mem(size_t size, int numbuck);
void*		ha_calloc(size_t nmemb, size_t size);
void		ha_free(void *ptr);
static void	ha_malloc_init(void);

/*
 * ha_malloc: malloc clone
 */

void *
ha_malloc(size_t size)
{
	int			j;
	int			numbuck = NOBUCKET;
	struct ha_bucket*	buckptr = NULL;
	void*			ret;

	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;
	(void)_ha_malloc_c_id;

	if (!ha_malloc_inityet) {
		ha_malloc_init();
	}

	/*
	 * Find which bucket would have buffers of the requested size
	 */
	for (j=0; j < NUMBUCKS; ++j) {
		if (size <= ha_bucket_sizes[j]) {
			numbuck = j;
			buckptr = ha_malloc_buckets[numbuck];
			break;
		}
	}

	/*
	 * Pull it out of the linked list of free buffers if we can...
	 */

	if (buckptr == NULL) {
		ret = ha_new_mem(size, numbuck);
	}else{
		ha_malloc_buckets[numbuck] = buckptr->next;
		buckptr->hdr.reqsize = size;
		ret = (((char*)buckptr)+ha_malloc_hdr_offset);
		if (curproc) {
			curproc->nbytes_req += size;
			curproc->nbytes_alloc+=ha_bucket_sizes[numbuck];
		}
		
	}

	if (ret && curproc) {
#ifdef HAVE_MALLINFO
		struct mallinfo	i = mallinfo();
		curproc->arena = i.arena;
#endif
		curproc->numalloc++;
	}
	return(ret);
}

/*
 * ha_free: "free" clone
 */

void
ha_free(void *ptr)
{
	char*			cptr;
	int			bucket;
	struct ha_bucket*	bhdr;

	if (!ha_malloc_inityet) {
		ha_malloc_init();
	}

	ASSERT(ptr != NULL);

	if (ptr == NULL) {
		ha_log(LOG_ERR, "attempt to free NULL pointer in ha_free()");
		return;
	}

	/* Find the beginning of our "hidden" structure */

	cptr = ptr;
	cptr -= ha_malloc_hdr_offset;
	ptr = cptr;

	bhdr = (struct ha_bucket*) ptr;

	if (bhdr->hdr.magic != HA_MALLOC_MAGIC) {
		ha_log(LOG_ERR, "Bad magic number in ha_free()");
		return;
	}
	bucket = bhdr->hdr.bucket;

	/*
	 * Return it to the appropriate bucket (linked list), or just free
	 * it if it didn't come from one of our lists...
	 */

	if (bucket >= NUMBUCKS) {
		if (curproc) {
			if (curproc->nbytes_alloc >= bhdr->hdr.reqsize) {
				curproc->nbytes_req   -= bhdr->hdr.reqsize;
				curproc->nbytes_alloc -= bhdr->hdr.reqsize;
				curproc->mallocbytes  -= bhdr->hdr.reqsize;
			}
		}
		free(bhdr);
	}else{
		ASSERT(bhdr->hdr.reqsize <= ha_bucket_sizes[bucket]);
		if (curproc) {
			if (curproc->nbytes_alloc >= bhdr->hdr.reqsize) {
				curproc->nbytes_req  -= bhdr->hdr.reqsize;
				curproc->nbytes_alloc-= ha_bucket_sizes[bucket];
			}
		}
		bhdr->next = ha_malloc_buckets[bucket];
		ha_malloc_buckets[bucket] = bhdr;
	}
	if (curproc) {
		curproc->numfree++;
	}
}

/*
 * ha_new_mem:	use the real malloc to allocate some new memory
 */

static void*
ha_new_mem(size_t size, int numbuck)
{
	struct ha_bucket*	hdrret;
	size_t			allocsize;

	if (numbuck < NUMBUCKS) {
		allocsize = ha_bucket_sizes[numbuck];
	}else{
		allocsize = size;
	}

	if ((hdrret = malloc(sizeof(*hdrret)+allocsize)) == NULL) {
		return(NULL);
	}

	hdrret->hdr.reqsize = size;
	hdrret->hdr.bucket = numbuck;
	hdrret->hdr.magic = HA_MALLOC_MAGIC;

	if (curproc) {
		curproc->nbytes_alloc += allocsize;
		curproc->nbytes_req += size;
		curproc->mallocbytes += allocsize;
	}
	return(((char*)hdrret)+ha_malloc_hdr_offset);
}


/*
 * ha_malloc: calloc clone
 */

void *
ha_calloc(size_t nmemb, size_t size)
{
	void *	ret = ha_malloc(nmemb*size);

	if (ret != NULL) {
		memset(ret, 0, nmemb*size);
	}
		
	return(ret);
}

/*
 * ha_malloc_init():	initialize our malloc wrapper things
 */

static void
ha_malloc_init()
{
	int	j;
	size_t	cursize = 16;

	ha_malloc_inityet = 1;
	for (j=0; j < NUMBUCKS; ++j) {
		ha_malloc_buckets[j] = NULL;

		ha_bucket_sizes[j] = cursize;
		cursize <<= 1;
	}
}
