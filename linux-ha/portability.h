#ifdef HAVE_CONFIG_H
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
#endif /* HAVE_CONFIG_H */
