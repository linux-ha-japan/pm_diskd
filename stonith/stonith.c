#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <sys/wait.h>
#include <stonith.h>

#define	DIMOF(a)	(sizeof(a)/sizeof((a)[0]))

extern Stonith *	__baytech_new(void);

static struct StonithTypes {
	Stonith*	(*new)(void);
}TypeList[]	=
{
	{__baytech_new},
};

static const char *	TypeNames[] =
{
	"baytech",
	NULL,
};



Stonith *
stonith_new(const char * type)
{
	int	j;

	for (j=0; (j < DIMOF(TypeList) && TypeNames[j] != NULL); ++j) {
		if (strcasecmp(type, TypeNames[j]) == 0) {
			return TypeList[j].new();
		}
	}
	return(NULL);
}
const char **
stonith_types(void)
{
	return TypeNames;
}
