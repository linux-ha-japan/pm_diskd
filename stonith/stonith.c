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
#include <dlfcn.h>
#include <stonith.h>

#define MAX_FUNC_NAME 20

struct symbol_str {
    char name[MAX_FUNC_NAME];
    void** function;
};

int symbol_load(struct symbol_str symbols[], int len, void **handle);

int symbol_load(struct symbol_str symbols[], int len, void **handle)
{
	int  a;
	char *error;

	for(a = 0; a < len; a++) {
		struct symbol_str *sym = &symbols[a];

		*sym->function = dlsym(*handle, sym->name);

		if ((error = dlerror()) != NULL)  {
			syslog(LOG_ERR, "%s", error);
			dlclose(*handle); 
			return 1;
		}
	}
	return 0;
}

/*
 *	Create a new Stonith object of the requested type.
 */

Stonith *
stonith_new(const char * type)
{
	int	ret;
	Stonith *s;
	struct symbol_str syms[NR_STONITH_FNS];
	char *obj_path;

	bindtextdomain(ST_TEXTDOMAIN, LOCALEDIR);

	s = malloc(sizeof(Stonith));

	if(s == NULL)
		return(NULL);

	s->s_ops = malloc(sizeof(struct stonith_ops));

	if(s->s_ops == NULL) {
		free(s);
		return(NULL);
	}

	obj_path = malloc((strlen(STONITH_MODULES) + strlen(type) + 4) 
				* sizeof(char));

	if(obj_path == NULL) {
		free(s->s_ops);
		free(s);
		return(NULL);
	}
	
	sprintf(obj_path,"%s/%s.so", STONITH_MODULES, type);

	if((s->dlhandle = dlopen(obj_path, RTLD_LAZY|RTLD_GLOBAL)) == NULL) {
		syslog(LOG_ERR, "%s: %s\n", __FUNCTION__, dlerror());
		free(s->s_ops);
		free(s);
		free(obj_path);
		return(NULL);
	}

    strcpy(syms[0].name, "st_new");
	syms[0].function = (void **) &s->s_ops->new;
    strcpy(syms[1].name, "st_destroy");
	syms[1].function = (void **) &s->s_ops->destroy;
    strcpy(syms[2].name, "st_setconffile");
	syms[2].function = (void **) &s->s_ops->set_config_file;
    strcpy(syms[3].name, "st_setconfinfo");
	syms[3].function = (void **) &s->s_ops->set_config_info;
    strcpy(syms[4].name, "st_getinfo");
	syms[4].function = (void **) &s->s_ops->getinfo;
    strcpy(syms[5].name, "st_status");
	syms[5].function = (void **) &s->s_ops->status;
    strcpy(syms[6].name, "st_reset");
	syms[6].function = (void **) &s->s_ops->reset_req;
    strcpy(syms[7].name, "st_hostlist");
	syms[7].function = (void **) &s->s_ops->hostlist;
    strcpy(syms[8].name, "st_freehostlist");
	syms[8].function = (void **) &s->s_ops->free_hostlist;

	ret = symbol_load(syms, NR_STONITH_FNS, &s->dlhandle);
	
	if(ret != 0) {
		free(s->s_ops);
		free(s);
		free(obj_path);
		return(NULL);
	}

	s->pinfo = s->s_ops->new();

	return s;
}
