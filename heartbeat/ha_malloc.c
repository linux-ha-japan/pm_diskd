static const char * _ha_malloc_c_id = "$Id: ha_malloc.c,v 1.2 1999/10/10 20:11:51 alanr Exp $";
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/utsname.h>
#include <heartbeat.h>

#define	HA_MALLOC_MAGIC	0xFEEDBEEFUL
#define	HA_MALLOC_GUARD	0xACE00ECAUL

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

#define	MALLOC_BEGIN_CRITICAL()	BeginCritical()
#define	MALLOC_END_CRITICAL()	EndCritical()

struct ha_bucket*	ha_malloc_buckets[NUMBUCKS];
size_t	ha_bucket_sizes[NUMBUCKS];

static int ha_malloc_inityet = 0;
static int ha_malloc_hdr_offset = sizeof(struct ha_mhdr);

void*		ha_malloc(size_t size);
static void*	ha_new_mem(size_t size, int numbuck);
void*		ha_calloc(size_t nmemb, size_t size);
void		ha_free(void *ptr);
static void	ha_malloc_init(void);
static void	remember_signal(int sig);
static void	BeginCritical(void);
static void	EndCritical(void);

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

	MALLOC_BEGIN_CRITICAL();
		for (j=0; j < NUMBUCKS; ++j) {
			if (size <= ha_bucket_sizes[j]) {
				numbuck = j;
				buckptr = ha_malloc_buckets[numbuck];
				break;
			}
		}

		if (buckptr == NULL) {
			ret = ha_new_mem(size, numbuck);
		}else{
			ha_malloc_buckets[numbuck] = buckptr->next;
			buckptr->hdr.reqsize = size;
			ret = (((char*)buckptr)+ha_malloc_hdr_offset);
			curproc->nbytes_req += size;
			
		}
	MALLOC_END_CRITICAL();
	if (ret && curproc) {
		curproc->numalloc++;
	}
	return(ret);
}


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

	curproc->nbytes_alloc += allocsize;
	curproc->nbytes_req += size;
	return(((char*)hdrret)+ha_malloc_hdr_offset);
}

void *
ha_calloc(size_t nmemb, size_t size)
{
	void *	ret = ha_malloc(nmemb*size);

	if (ret != NULL) {
		bzero(ret, nmemb*size);
	}
		
	return(ret);
}

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
		return;
	}

	cptr = ptr;
	cptr -= ha_malloc_hdr_offset;

	bhdr = (struct ha_bucket*) cptr;

	ASSERT(bhdr->hdr.magic == HA_MALLOC_MAGIC);

	if (bhdr->hdr.magic != HA_MALLOC_MAGIC) {
		return;
	}
	bucket = bhdr->hdr.bucket;
	if (bucket >= NUMBUCKS) {
		curproc->nbytes_alloc -= bhdr->hdr.reqsize;
		curproc->nbytes_req   -= bhdr->hdr.reqsize;
		MALLOC_BEGIN_CRITICAL();
			free(bhdr);
		MALLOC_END_CRITICAL();
	}else{
		ASSERT(bhdr->hdr.reqsize <= ha_bucket_sizes[bucket]);
		curproc->nbytes_req   -= bhdr->hdr.reqsize;
		MALLOC_BEGIN_CRITICAL();
			bhdr->next = ha_malloc_buckets[bucket];
			ha_malloc_buckets[bucket] = bhdr;
		MALLOC_END_CRITICAL();
	}
	if (curproc) {
		curproc->numfree++;
	}
}

void
ha_malloc_report()
{

}
static void
ha_malloc_init()
{
	int	j;
	size_t	cursize = 16;

	for (j=0; j < NUMBUCKS; ++j) {
		ha_malloc_buckets[j] = NULL;

		ha_bucket_sizes[j] = cursize;
		cursize <<= 1;
	}
}


static int	signals_to_block[] = {SIGALRM};

#define	MAXSIGS	(DIMOF(signals_to_block))
#define	SIGMAX	64
static void	(*ha_signal_handlers[SIGMAX])(int);
static int	ha_signal_memory[MAXSIGS*4];
static int	num_mem_sigs = 0;

static void
remember_signal(int sig)
{
	if (num_mem_sigs >= DIMOF(ha_signal_memory)) {
		/* You win some, you lose some... */
		ha_log(LOG_ERR, "Lost signal %d (!)", sig);
		return;
	}
	ha_signal_memory[num_mem_sigs] = sig;
	++num_mem_sigs;
}

static void
BeginCritical()
{
	int	j;

	num_mem_sigs = 0;
	/* Set up alternate signal handlers to remember signals */

	for (j=0; j < DIMOF(signals_to_block); ++j) {
		int	sig = signals_to_block[j];
		ha_signal_handlers[sig] = signal(sig, remember_signal);
	}
}

static void
EndCritical()
{
	int	j;

	/* Restore original signal handlers ...*/

	for (j=0; j < DIMOF(signals_to_block); ++j) {
		int	sig = signals_to_block[j];
		signal(sig, ha_signal_handlers[sig]);
	}

	/* Play back any delayed signals... */

	for (j=0; j < num_mem_sigs; ++j) {
		int	sig = ha_signal_memory[j];
		ha_signal_handlers[sig](sig);
	}
	num_mem_sigs = 0;
}
