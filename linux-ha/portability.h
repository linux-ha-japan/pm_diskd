#ifndef PORTABILITY_H
#  define PORTABILITY_H

/*
 * Copyright (C) 2001 Alan Robertson <alanr@unix.sh>
 * This software licensed under the GNU LGPL.
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA *
 */

#define	EOS			'\0'
#define	DIMOF(a)		((int) (sizeof(a)/sizeof(a[0])) )
#define	STRLEN(conststr)	((int)(sizeof(conststr)/sizeof(char))-1)

/* This is consistent with OpenBSD, and is a good choice anyway */
#define	TIME_T	unsigned long
#define	TIME_F	"%lu"
#define	TIME_X	"%lx"

/* Needs to be defined before any other includes, otherwise some system
 * headers do not behave as expected! Major black magic... */
#define _GNU_SOURCE

#ifdef __STDC__
#       define  MKSTRING(s)     #s
#else
#       define  MKSTRING(s)     "s"
#endif


#include <sys/param.h>
#ifdef BSD
#	define SCANSEL_CAST	(void *)
#else
#	define SCANSEL_CAST	/* Nothing */
#endif

#if	__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define G_GNUC_PRINTF( format_idx, arg_idx )	\
  __attribute__((format (printf, format_idx, arg_idx)))
#else	/* !__GNUC__ */
#define G_GNUC_PRINTF( format_idx, arg_idx )
#endif	/* !__GNUC__ */

#  ifdef HAVE_CONFIG_H
#	include <config.h>

#ifndef HAVE_SETENV
  /* We supply a replacement function, but need a prototype */

int setenv(const char *name, const char * value, int why);

#endif /* HAVE_SETENV */

#ifndef HAVE_STRERROR
  /* We supply a replacement function, but need a prototype */
const char * strerror(int errnum);

#endif /* HAVE_STRERROR */
int setenv(const char *name, const char * value, int why);


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

#ifndef HAVE_ALPHASORT
#  include <dirent.h>
int
alphasort(const void *dirent1, const void *dirent2);
#endif /* HAVE_ALPHASORT */

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
 * NOTE:  We're moving away from times - to the longclock_t types
 * which solve this problem much more nicely...
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
#  endif /* HAVE_CONFIG_H */


#ifndef HAVE_STRNLEN
#	define	strnlen(a,b) strlen(a)
#else
#	define USE_GNU
#endif

#ifndef HAVE_NFDS_T 
	typedef unsigned int nfds_t;
#endif
#endif /* !PORTABILITY_H */
