/*
 * Stonith API infrastructure.
 *
 * Copyright (c) 2000 Alan Robertson <alanr@unix.sh>
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <libintl.h>
#include <sys/wait.h>
#include <stonith.h>

#define	DIMOF(a)	(sizeof(a)/sizeof((a)[0]))

extern Stonith *	__baytech_new(void);
extern Stonith *	__NULL_new(void);

static struct StonithTypes {
	Stonith*	(*new)(void);
}TypeList[]	=
{
	{__baytech_new},
	{__NULL_new},
};

static const char *	TypeNames[] =
{
	"baytech",
	"null",
	NULL,
};


/*
 *	Create a new Stonith object of the requested type.
 */

Stonith *
stonith_new(const char * type)
{
	int	j;

	bindtextdomain(ST_TEXTDOMAIN, LOCALEDIR);
	for (j=0; (j < DIMOF(TypeList) && TypeNames[j] != NULL); ++j) {
		if (strcasecmp(type, TypeNames[j]) == 0) {
			return TypeList[j].new();
		}
	}
	return(NULL);
}
/*
 *	Return the list of Stonith types which can be given to stonith_new()
 */
const char **
stonith_types(void)
{
	return TypeNames;
}
