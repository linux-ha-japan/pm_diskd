#include <stdlib.h>
#include <stdio.h>

void
setenv(const char *name, const char * value, int why)
{
  char * envp = NULL;
  if ( name && value ) {
    envp = malloc(strlen(name)+strlen(value)+2);
    sprintf(envp, "%s=%s", name, value);
    putenv(envp);
  }
}
