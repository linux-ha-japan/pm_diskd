#include <stdlib.h>
#include <stdio.h>

int setenv(const char *name, const char * value, int why);

/*
 *	Small replacement function for setenv()
 */
int
setenv(const char *name, const char * value, int why)
{
	if ( name && value ) {
		char * envp = NULL;
		envp = malloc(strlen(name)+strlen(value)+2);
		if (envp) {
			/*
			 * Unfortunately, the putenv API guarantees memory leaks when
			 * changing environment variables repeatedly...   :-(
			 */

			sprintf(envp, "%s=%s", name, value);

			/* Cannot free envp (!) */
			putenv(envp);

			return(0);
		}
	
	}
	return(-1);
}
