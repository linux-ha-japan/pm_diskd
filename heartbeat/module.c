static const char _module_c_Id [] = "$Id: module.c,v 1.27 2001/06/23 15:52:06 alan Exp $";
/*
 * module: Dynamic module support code
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 * Copyright (C) 2000 Marcelo Tosatti <marcelo@conectiva.com.br>
 * 
 * Thanks to Conectiva S.A. for sponsoring Marcelo Tosatti work
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

#include "portability.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/param.h>
#include <unistd.h>
#include <dirent.h>
#include <ltdl.h>
#include "heartbeat.h"
#include <ha_msg.h>
#include <hb_module.h>
#include "../libltdl/config.h"

/* BSD wants us to cast the select parameter to scandir */
#ifdef BSD
#	define SCANSEL_C	(void *)
#else
#	define SCANSEL_C	/* Nothing */
#endif
#ifndef RTLD_NOW
#	define RTLD_NOW 0
#endif

extern struct hb_media_fns** hbmedia_types;
extern int num_hb_media_types;

extern struct auth_type** ValidAuths;
extern int num_auth_types;


static const char *module_error (void);
static int generic_symbol_load(const char * module
				,	struct symbol_str symbols[]
				,	int len, lt_dlhandle handle);
static int comm_module_init(void);


static char multi_init_error[] = "module loader initialised more than once";
/* static char module_not_unloaded_error[] = "module not unloaded"; */

static const char *_module_error = NULL;

const char *
module_error (void)
{
	return _module_error;
}

/* #define MODULESUFFIX	".so" */
#define MODULESUFFIX	LTDL_SHLIB_EXT
#define	STRLEN(s)	(sizeof(s)-1)

static int
so_select (const struct dirent *dire)
{ 
    
	const char obj_end [] = MODULESUFFIX;
	const char *end = &dire->d_name[strlen(dire->d_name) - (STRLEN(obj_end))];
	
	
	if (DEBUGMODULE) {
		ha_log(LOG_DEBUG
		,	"In so_select: %s.", dire->d_name);
	}
	if (obj_end < dire->d_name) {
			return 0;
	}
	if (strcmp(end, obj_end) == 0) {
		if (DEBUGMODULE) {
			ha_log(LOG_DEBUG
			,	"FILE %s looks like a module name."
			,	dire->d_name);
		}
		return 1;
	}
	if (DEBUGMODULE) {
		ha_log(LOG_DEBUG
		,	"FILE %s Doesn't look like a module name [%s], %d %d %s."
		,	dire->d_name, end, sizeof(obj_end), strlen(dire->d_name)
		,	&dire->d_name[strlen(dire->d_name) - (sizeof(obj_end)-1)]);
	}
	
	return 0;
}


/* 
 * Generic function to load symbols from a module.
 */
static int
generic_symbol_load(const char * module
,	struct symbol_str symbols[], int len, lt_dlhandle handle)
{ 
	int  a;
#ifdef MODPREFIXSTR
	char symbolname[MAX_FUNC_NAME + sizeof(MODPREFIXSTR)];
#else
	char symbolname[MAX_FUNC_NAME + sizeof(MODPREFIXSTR)];
#endif
	char modlen = strlen(module);
	char modulename[MAX_FUNC_NAME];

	strncpy(modulename, module, sizeof(modulename));
	if (modlen > STRLEN(MODULESUFFIX)
	&&	strcmp(MODULESUFFIX
		,	modulename+modlen-(STRLEN(MODULESUFFIX)+1))) {
		modulename[modlen-STRLEN(MODULESUFFIX)] = EOS;
	}
		
	
	for (a = 0; a < len; a++) {

		struct symbol_str *sym = &symbols[a];

#if defined(MODPREFIXSTR)
		strncpy(symbolname, modulename, sizeof(symbolname));
		strncat(symbolname, MODPREFIXSTR, sizeof(symbolname));
#endif
		strncat(symbolname, sym->name, sizeof(symbolname));
		
		if ((sym->function = lt_dlsym(handle, symbolname)) == NULL) {
			if (sym->mandatory) {
				ha_log(LOG_ERR
				,	"%s: Plugin does not have [%s] symbol [%s]."
				,	__FUNCTION__, sym->name
				,	symbolname);
				lt_dlclose(handle); handle = NULL;
				return(HA_FAIL);
			}
		}
	}

	return(HA_OK);
}

static int
comm_module_init(void)
{ 
	struct symbol_str comm_symbols[NR_HB_MEDIA_FNS]; 
	int a, n;
	struct dirent **namelist;

	strcpy(comm_symbols[0].name, "hb_dev_init");
	comm_symbols[0].mandatory = 1;

	strcpy(comm_symbols[1].name, "hb_dev_new");
	comm_symbols[1].mandatory = 1;

	strcpy(comm_symbols[2].name, "hb_dev_parse");
	comm_symbols[2].mandatory = 0;

	strcpy(comm_symbols[3].name, "hb_dev_open");
	comm_symbols[3].mandatory = 1;

	strcpy(comm_symbols[4].name, "hb_dev_close");
	comm_symbols[4].mandatory = 1;

	strcpy(comm_symbols[5].name, "hb_dev_read");
	comm_symbols[5].mandatory = 1;

	strcpy(comm_symbols[6].name, "hb_dev_write");
	comm_symbols[6].mandatory = 1;

	strcpy(comm_symbols[7].name, "hb_dev_mtype");
	comm_symbols[7].mandatory = 1;

	strcpy(comm_symbols[8].name, "hb_dev_descr");
	comm_symbols[8].mandatory = 1;

	strcpy(comm_symbols[9].name, "hb_dev_isping");
	comm_symbols[9].mandatory = 1;

	if (DEBUGMODULE) {
		ha_log(LOG_DEBUG
		,	"Scanning directory %s for modules."
		,	COMM_MODULE_DIR);
	}
	n = scandir(COMM_MODULE_DIR, &namelist, SCANSEL_C &so_select, NULL);
	if (DEBUGMODULE) {
		ha_log(LOG_DEBUG
		,	"scandir on %s returned %d."
		,	COMM_MODULE_DIR, n);
	}

	if (n < 0) { 
		ha_log(LOG_ERR, "%s: scandir failed.", __FUNCTION__);
		return (HA_FAIL);
	}

	for (a = 0; a < n; a++) {
		char *mod_path           = NULL;
		struct hb_media_fns*	fns;
		int			ret;
		int			pathlen;

		
		pathlen = (strlen(COMM_MODULE_DIR) 
		+	strlen(namelist[a]->d_name) + 2) * sizeof(char);

		mod_path = (char*) ha_malloc(pathlen);
		if (!mod_path) { 
			ha_log(LOG_ERR, "%s: Failed to alloc module path."
			,	__FUNCTION__);
			for (a=0; a < n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}

		snprintf(mod_path, pathlen, "%s/%s", COMM_MODULE_DIR, 
			namelist[a]->d_name);

		if (DEBUGMODULE) {
			ha_log(LOG_DEBUG
			,	"Examining module %s:"
			,	mod_path);
		}
		fns = MALLOCT(struct hb_media_fns);
		
		if (fns == NULL) { 
			ha_log(LOG_ERR, "%s: fns alloc failed.", __FUNCTION__);
			ha_free(mod_path); 
			for (a=0; a < n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}

		if (!(fns->dlhandler = lt_dlopen(mod_path))) {
			ha_log(LOG_ERR, "%s: %s", __FUNCTION__, lt_dlerror());
			ha_free(mod_path); mod_path = NULL;
			ha_free(fns); fns = NULL;
			for (a=0; a < n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}
		if (DEBUGMODULE) {
			ha_log(LOG_DEBUG
			,	"Loading module %s:"
			,	mod_path);
		}

		ret = generic_symbol_load(namelist[a]->d_name
		,	comm_symbols, NR_HB_MEDIA_FNS
		,	fns->dlhandler);

		fns->init = comm_symbols[0].function;
		fns->new  = comm_symbols[1].function;
		fns->parse = comm_symbols[2].function;
		fns->open = comm_symbols[3].function;
		fns->close = comm_symbols[4].function;
		fns->read = comm_symbols[5].function;
		fns->write = comm_symbols[6].function;
		fns->mtype = comm_symbols[7].function;
		fns->descr = comm_symbols[8].function;
		fns->isping = comm_symbols[9].function;
		
		if (ret == HA_FAIL) {
			ha_free(mod_path); mod_path=NULL;
			ha_free(fns);
			for (a=0; a < n; a++) {
				free(namelist[a]);
			} 
			return ret;
		}
		hbmedia_types[num_hb_media_types] = fns;
		num_hb_media_types++;

		fns->type_len = fns->mtype(&fns->type);
		fns->desc_len = fns->descr(&fns->description);
		fns->ref = 0;
		ha_free(mod_path); mod_path = NULL;
	}

	for (a=0; a < n; a++) {
		free(namelist[a]);
	} 

	return (HA_OK);
}


int
auth_module_init() 
{ 
	struct symbol_str auth_symbols[NR_AUTH_FNS]; 
	int a, n;
	struct dirent **namelist;
	
	strcpy(auth_symbols[0].name, "hb_auth_calc");
	auth_symbols[0].mandatory = 1;
	
	strcpy(auth_symbols[1].name, "hb_auth_atype");
	auth_symbols[1].mandatory = 1;
	
	strcpy(auth_symbols[2].name, "hb_auth_nkey");
	auth_symbols[2].mandatory = 1;

	if (DEBUGMODULE) {
		ha_log(LOG_DEBUG
		,	"Scanning directory %s for modules."
		,	AUTH_MODULE_DIR);
	}
	n = scandir(AUTH_MODULE_DIR, &namelist, SCANSEL_C &so_select, 0);

	if (DEBUGMODULE) {
		ha_log(LOG_DEBUG
		,	"scandir on %s returned %d."
		,	AUTH_MODULE_DIR, n);
	}
	if (n < 0) { 
		ha_log(LOG_ERR, "%s: scandir failed", __FUNCTION__);
		return (HA_FAIL);
	}
	
	for (a = 0; a < n; a++) {
		char *mod_path         = NULL; 
		struct auth_type* auth;
		int			pathlen;
		int ret;
		
		pathlen = (strlen(AUTH_MODULE_DIR) +
		      strlen(namelist[a]->d_name) + 2) * sizeof(char);

		mod_path = (char *) ha_malloc(pathlen);

		if (!mod_path) { 
			ha_log(LOG_ERR, "%s: Failed to alloc module path"
			,	__FUNCTION__);
			for (a=0; a < n; a++) {
				free(namelist[a]);
			}
			return(HA_FAIL);
		}

		snprintf(mod_path, pathlen, "%s/%s", AUTH_MODULE_DIR
		,	namelist[a]->d_name);
		
		auth = MALLOCT(struct auth_type);
		
		if (auth == NULL) { 
			ha_log(LOG_ERR, "%s: auth_type alloc failed"
			,	__FUNCTION__);
			ha_free(mod_path);
			for (a=0; a < n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}

		if ((auth->dlhandler = lt_dlopen(mod_path)) == NULL) {
			ha_log(LOG_ERR, "%s: dlopen failed", __FUNCTION__);
			ha_free(mod_path);
			ha_free(auth);
			for (a=0; a < n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}
		
		if (DEBUGMODULE) {
			ha_log(LOG_DEBUG
			,	"Loading authentication module %s:"
			,	namelist[a]->d_name);
		}
		ret = generic_symbol_load(namelist[a]->d_name
		,	auth_symbols, NR_AUTH_FNS
		,	auth->dlhandler);

		auth->auth = auth_symbols[0].function;
		auth->atype = auth_symbols[1].function;
		auth->needskey = auth_symbols[2].function;

		if (ret == HA_FAIL) {
			ha_free(mod_path); mod_path = NULL;
			ha_free(auth); auth=NULL;
			for (a=0; a < n; a++) {
				free(namelist[a]);
				namelist[a]=NULL;
			} 
			return ret;
		}
		
		ValidAuths[num_auth_types] = auth;
		num_auth_types++;
		
		auth->authname_len = auth->atype(&auth->authname);
		auth->ref = 0;
		
		ha_free(mod_path);
		mod_path = NULL;
	}
	
	for (a=0; a < n; a++) {
		free(namelist[a]);
		namelist[a] = NULL;
	} 
	
	return(HA_OK);

}

int 
module_init(void)
{ 
    static int initialised = 0;
    int errors = 0;
	
    (void)_module_c_Id;
    (void)_heartbeat_h_Id;
    (void)_ha_msg_h_Id;
    (void)module_error();

    /* perform the init only once */
    if (!initialised) {
	/* initialize libltdl's list of preloaded modules */
	LTDL_SET_PRELOADED_SYMBOLS();

	/* initialise global module loader error string */
	_module_error = NULL;

	/* init ltdl */
	errors = lt_dlinit();

	if (comm_module_init() == HA_FAIL) {
	    return(HA_FAIL);
	}

	if (auth_module_init() == HA_FAIL) {
	    return(HA_FAIL);
	}

	/* init completed */
	++initialised;

	return errors ? HA_FAIL : HA_OK;
    }
    
    _module_error = multi_init_error;
    return HA_FAIL;
}
#ifdef HAVE_LIBGLIB
#  define NEWMODULECODE 1
#endif

#ifdef NEWMODULECODE
/*
 *	Herein lie many fragments and pieces of the new module loading
 *	scheme...
 *
 *	Ultimately most of the things in here will go into separate files,
 *	and probably into separate directories as well...
 *
 *	This stuff is all pretty cool if we can make it work ;-)
 *
 */
/* Gotta love the glib folks ... whose prototype parameters shadow system
 * functions
 */
#define index _fooIndex
#define time _fooTime

#include <glib.h>
#undef index
#undef time


static void free_dirlist(struct dirent** dlist, int n);
/* Returns a char ** */
char** ml_get_module_list(const char * basedir, const char * modclass, int* count);

static int qsort_string_cmp(const void *a, const void *b);


static void
free_dirlist(struct dirent** dlist, int n)
{
	int	j;
	for (j=0; j < n; ++j) {
		if (dlist[j]) {
			free(dlist[j]);
			dlist[j] = NULL;
		}
	}
	free(dlist);
}

static int
qsort_string_cmp(const void *a, const void *b)
{
	return(strcmp(*(const char * const *)a, *(const char * const *)b));
}

#define FREE_DIRLIST(dlist, n)	{free_dirlist(dlist, n); dlist = NULL;}

/* Generalized module loading code */
char**
ml_get_module_list	/* Return (sorted) list of available module names */
(	const char *	basedir		/* Defaults to HA_MODULE_D */
,	const char *	modclass	/* Can be NULL if you want ... */
,	int *		modcount	/* Ditto... */
)
{
	GString*	path;
	char **		result = NULL;
	struct dirent**	files;
	int		modulecount;
	int		j;

	if (basedir == NULL) {
		basedir= HA_MODULE_D;
	}

	/* Base module directory must be a full path name */
	if (!g_path_is_absolute(basedir)) {
		return(NULL);
	}

	path = g_string_new(basedir);
	if (modclass) {
		if (g_string_append_c(path, G_DIR_SEPARATOR) == NULL
		||	g_string_append(path, modclass) == NULL) {
			g_string_free(path, 1); path = NULL;
			return(NULL);
		}
	}

	modulecount = scandir(path->str, &files, SCANSEL_C &so_select, NULL);
	g_string_free(path, 1); path=NULL;

	result = (char **) ha_malloc((modulecount+1)*sizeof(char *));

	for (j=0; j < modulecount; ++j) {
		char*	s;
		int	slen = strlen(files[j]->d_name) - STRLEN(MODULESUFFIX);

		s = ha_malloc(slen+1);
		strncpy(s, files[j]->d_name, slen);
		s[slen] = EOS;
		result[j] = s;
	}
	result[j] = NULL;

	/* Return them in sorted order... */
	qsort(result, modulecount, sizeof(char *), qsort_string_cmp);

	if (modcount != NULL) {
		*modcount = modulecount;
	}

	FREE_DIRLIST(files, modulecount);
	return(result);
}
/* Most of this moved to the lib/upmls directory */
#endif /* NEWMODULECODE */
