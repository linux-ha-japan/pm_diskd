#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <sys/wait.h>
#include <stonith.h>

extern Stonith *	__baytech_new(void);

Stonith * stonith_new(const char * type)
{
	if (strcasecmp(type, "baytech") == 0) {
		return(__baytech_new());
	}
	return(NULL);

}
