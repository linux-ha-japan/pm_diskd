#ifndef PORTABILITY_H
#  define PORTABILITY_H


#define	EOS			'\0'
#define	DIMOF(a)		(sizeof(a)/sizeof(a[0]))
#define	STRLEN(conststr)	((sizeof(conststr)/sizeof(char))-1)


#ifdef BSD
#	define SCANSEL_CAST	(void *)
#else
#	define SCANSEL_CAST	/* Nothing */
#endif

#  ifdef HAVE_CONFIG_H
#	include <config.h>

#ifndef HAVE_SETENV
  /* We supply a replacement function, but need a prototype */

int setenv(const char *name, const char * value, int why);

#endif /* HAVE_SETENV */

#ifndef HAVE_SCANDIR
  /* We supply a replacement function, but need a prototype */

#  include <dirent.h>
int
scandir (const char *directory_name,
	struct dirent ***array_pointer,
	int (*select_function) (const struct dirent *),

#ifdef USE_SCANDIR_COMPARE_STRUCT_DIRENT
	/* This is what the Linux man page says */
	int (*compare_function) (const struct dirent**, const struct dirent**)
#else
	/* This is what the Linux header file says ... */
	int (*compare_function) (const void *, const void *)
#endif
	);

#endif /* HAVE_SCANDIR */

#ifndef HAVE_INET_PTON
  /* We supply a replacement function, but need a prototype */
int
inet_pton(int af, const char *src, void *dst);

#endif /* HAVE_INET_PTON */

/*
 * Special Note:  CLK_TCK is *not* the same as CLOCKS_PER_SEC.
 *
 * CLOCKS_PER_SEC is supposed to be 1000000 on every system.
 * This is most certainly *not* what we need.
 *
 * Older UNIX systems used to call this (CLK_TCK) HZ.
 *
 */
#ifdef CLK_TCK_IN_TIME_H
#  include <time.h>
#else
#  ifdef CLK_TCK_IN_LIMITS_H
#    include <limits.h>
#  endif
#endif
#ifndef CLK_TCK
#  include <unistd.h>
#  ifdef _SC_CLK_TCK
#    define CLK_TCK	((clock_t)sysconf(_SC_CLK_TCK))
#  else
#    error "No definition for CLK_TCK available"
#  endif
#endif

#ifdef HAVE_STRINGIZE
#	define	MKSTRING(s)	#s
#else
#	define	MKSTRING(s)	"s"
#endif

#  endif /* HAVE_CONFIG_H */
#endif /* !PORTABILITY_H */
