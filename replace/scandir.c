/* Scan a directory, collecting all (selected) items into a an array.  */

/* This code borrowed from the GNU project at:
 *
 * http://www.iro.umontreal.ca/~pinard/libit/dist/scandir/
 *
 * It has been slightly modified to get rid of warnings, etc.
 *
 */

#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <stddef.h>

#ifndef NULL
# define NULL ((void *) 0)
#endif

/* Initial guess at directory allocated.  */
#define INITIAL_ALLOCATION 20

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

int
scandir (const char *directory_name,
	struct dirent ***array_pointer,
	int (*select_function) (const struct dirent *),
#ifdef USE_SCANDIR_COMPARE_STRUCT_DIRENT
	/* This is what the linux man page says */
	int (*compare_function) (const struct dirent**, const struct dirent**)
#else
	/* This is what the linux header file says ... */
	int (*compare_function) (const void *, const void *)
#endif
	)
{
  DIR *directory;
  struct dirent **array;
  struct dirent *entry;
  struct dirent *copy;
  int allocated = INITIAL_ALLOCATION;
  int counter = 0;

  /* Get initial list space and open directory.  */

  if (directory = opendir (directory_name), directory == NULL)
    return -1;

  if (array = (struct dirent **) malloc (allocated * sizeof (struct dirent *)),
      array == NULL)
    return -1;

  /* Read entries in the directory.  */

  while (entry = readdir (directory), entry)
    if (select_function == NULL || (*select_function) (entry))
      {
	/* User wants them all, or he wants this one.  Copy the entry.  */

	if (copy = (struct dirent *) malloc (sizeof (struct dirent)),
	    copy == NULL)
	  {
	    closedir (directory);
	    free (array);
	    return -1;
	  }
	copy->d_ino = entry->d_ino;
	copy->d_reclen = entry->d_reclen;
	strcpy (copy->d_name, entry->d_name);

	/* Save the copy.  */

	if (counter + 1 == allocated)
	  {
	    allocated <<= 1;
	    array = (struct dirent **)
	      realloc ((char *) array, allocated * sizeof (struct dirent *));
	    if (array == NULL)
	      {
		closedir (directory);
		free (array);
		free (copy);
		return -1;
	      }
	  }
	array[counter++] = copy;
      }

  /* Close things off.  */

  array[counter] = NULL;
  *array_pointer = array;
  closedir (directory);

  /* Sort?  */

  if (counter > 1 && compare_function)
    qsort ((char *) array, counter, sizeof (struct dirent)
  	,	(int (*)(const void *, const void *))(compare_function));

  return counter;
}
