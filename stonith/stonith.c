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
#include <portability.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#ifdef HAVE_LIBINTL_H
#    include <libintl.h>
#endif
#include <sys/wait.h>
#include <sys/param.h>
#include <dlfcn.h>
#include <dirent.h>
#include <stonith/stonith.h>

#define MAX_FUNC_NAME 20

#define	MALLOC(n)	malloc(n)
#define MALLOCT(t)	(t*)(malloc(sizeof(t)))
#define FREE(p)		{free(p); (p) = NULL;}

#ifndef RTLD_GLOBAL
#	define RTLD_GLOBAL	0
#endif
#ifndef RTLD_LAZY
#	define RTLD_LAZY	0
#endif

struct symbol_str {
    char name[MAX_FUNC_NAME];
    void** function;
};

/* BSD wants us to cast the select parameter to scandir */
#ifdef BSD
#	define SCANSEL_C	(void *)
#else
#	define SCANSEL_C	/* Nothing */
#endif


static int so_select (const struct dirent *dire);

static int symbol_load(struct symbol_str symbols[], int len, void **handle);

static int
symbol_load(struct symbol_str symbols[], int len, void **handle)
{
	int  a;
	const char *error;

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

	s = MALLOCT(Stonith);

	if (s == NULL) {
		return(NULL);
	}

	s->s_ops = MALLOCT(struct stonith_ops);

	if (s->s_ops == NULL) {
		FREE(s);
		return(NULL);
	}

	obj_path = (char*) MALLOC((strlen(STONITH_MODULES) + strlen(type) + 5)
				* sizeof(char));

	if (obj_path == NULL) {
		FREE(s->s_ops);
		FREE(s);
		return(NULL);
	}
	
	sprintf(obj_path,"%s/%s.so", STONITH_MODULES, type);

	if ((s->dlhandle = dlopen(obj_path, RTLD_LAZY|RTLD_GLOBAL)) == NULL) {
		syslog(LOG_ERR, "%s: %s\n", __FUNCTION__, dlerror());
		FREE(s->s_ops);
		FREE(s);
		FREE(obj_path);
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
	
	if (ret != 0) {
		FREE(s->s_ops);
		FREE(s);
		FREE(obj_path);
		return(NULL);
	}

	s->pinfo = s->s_ops->new();

	return s;
}

/*
 *	Return the list of Stonith types which can be given to stonith_new()
 */
char **
stonith_types(void)
{
	char ** list;
	struct dirent **namelist;
	int n, i;
	static char **	lastret = NULL;
	static int	lastcount = 0;


	n = scandir(STONITH_MODULES, &namelist, SCANSEL_C &so_select, 0);
	if (n < 0) {
		syslog(LOG_ERR, "%s: scandir failed.", __FUNCTION__);
		return(NULL);
	}

	/* Clean up from the last time we got called. */
	if (lastret != NULL) {
		char **	cp = lastret;
		for (;*cp; ++cp) {
			FREE(*cp)
		}
		if (lastcount != n) {
			FREE(lastret);
			lastret = NULL;
		}
	}
	if (lastret) {
		list = lastret;
	}else{
		list = (char **)MALLOC((n+1)*sizeof(char *));
	}

	if (list == NULL) {
		syslog(LOG_ERR, "%s: malloc failed.", __FUNCTION__);
		return(NULL);
	}

	for(i=0; i<n; i++) { 
		int len = strlen(namelist[i]->d_name);

		list[i] = (char*)  MALLOC(len * sizeof(char));
		if (list[i] == NULL) {
			syslog(LOG_ERR, "%s: malloc/1 failed.", __FUNCTION__);
			return(NULL);
		}
		strcpy(list[i], namelist[i]->d_name);

		/* strip ".so" */
		list[i][len - 3] = '\0';

		FREE(namelist[i]);
	}

	list[i] = NULL;
	lastret = list;
	lastcount = n;

	return list;
}

static int so_select (const struct dirent *dire) {

	const char *end = &dire->d_name[strlen(dire->d_name) - 3];
	const char *obj_end = ".so";

	if (strcmp(end, obj_end) == 0){
		return 1;
	}

	return 0;
}

