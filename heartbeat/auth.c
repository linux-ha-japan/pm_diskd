
/*
 * auth.c: Authentication code for heartbeat
 *
 * Copyright (C) 1999,2000 Mitja Sarp <mitja@lysator.liu.se>
 *	Somewhat mangled by Alan Robertson <alanr@unix.sh>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>

#include "heartbeat.h"

unsigned char result[MAXLINE];

const unsigned char *	calc_crc	(const struct auth_info *, const char * text);

struct auth_type** ValidAuths;

int num_auth_types;

struct auth_type *	findauth(const char * type)
{
	int	j;

	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	for (j=0; j < num_auth_types; ++j) {
		if (strcmp(type, ValidAuths[j]->authname) == 0) {
			return (ValidAuths[j]);
		}
	}
	return(NULL);
}
