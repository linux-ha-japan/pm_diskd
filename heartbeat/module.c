static const char _module_c_Id [] = "$Id: module.c,v 1.19 2001/06/06 17:50:46 alan Exp $";
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

#include <config.h>
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
 *	Herein lie many fragments and pieces of the new module loading scheme...
 *
 *	Ultimately most of the things in here will go into separate files, and probably
 *	into separate directories as well...
 *
 *	This stuff is all pretty cool if we can make it work ;-)
 *
 */
/* Gotta love the glib folks ... whose prototype parameters shadow system functions  */
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
	return(strcmp(*(const char **)a, *(const char **)b));
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

/*
 *	Begin MLModule.h (or something like that)
 *
 * This of a fairly general and reasonably interesting module loading system.
 * These modules are sometimes referred to as plugins.  Here, we use the two terms
 * to mean slightly different things.
 *
 * It is not at all specific to our application (high-availability) and should be
 * directly usable by basically any project.
 *
 *
 *
 * Some terminology...
 *
 * There are kinds of objects we deal with here:
 *
 * Modules: dynamically loaded chunks of code which implement one or more
 *		plugins.
 *
 * Plugin: A set of functions which implement a particular plug-in capability which
 * 	can by dynamically loaded.
 *
 * Each plugin exports certain interfaces which it exports for its clients to use.
 * We refer to these those "Ops".
 *
 * Each plugin is provided certain interfaces which it imports when it is loaded.
 * We refer to these as "Imports".
 *
 * In the function parameters below, the following notation will sometimes appear:
 *
 * (OP) == Output Parameter - a parameter which is modified by the function being called
 *
 *************************************************************************************
 *
 * Each module has only one entry point which is exported directly, regardless
 * of what kind of plugin(s) it implements...
 *
 * This entrypoint is named ml_module_init().
 *
 * The ml_module_init() function is called once when the module is loaded.
 *
 *
 * All other entry points are exported through parameters passed to ml_module_init()
 *
 * Ml_module_init() then registers the module, and all the plugins which this module
 * implements.  The registration function is in the parameters which are passed
 * as a parameter to ml_module_init().
 *
 *************************************************************************************
 *
 * THINGS IN THIS DESIGN WHICH ARE KNOWN TO BE BROKEN...
 *
 * I think the MLModuleEnv/MLModuleSet environment may still has some
 * unnecessary global variables implied by it's interfaces...  It needs to
 * be improved/finished.
 *
 *************************************************************************************
 */
typedef int				ML_rc;	/* Return code from Module functions */

typedef struct mlModuleImports_s	MLModuleImports;
typedef struct mlModuleOps_s		MLModuleOps;
typedef struct mlModule_s		MLModule;
typedef struct mlModuleSet_s		MLModuleSet;


/*
 * struct mlModule_s (typedef MLModule) is the structure which represents/defines a
 * module, and is used to identify which module is being referred to in
 * various function calls.
 *
 * NOTE: It may be the case that this definition should be moved to another header
 * file - since no one ought to be messing with them anyway ;-)
 */

struct mlModule_s {
	const char*	module_name;
	void*		user_data;
	/* Other stuff goes here ... */
};
/*
 * struct mlModuleOps_s (typedef MLModuleOps) defines the set of functions
 * exported by all modules...
 */
struct mlModuleOps_s {
	const char*	(*moduleversion) (void);
	const char*	(*modulename)	 (void);
	int		(*getdebuglevel) (void);

	void		(*setdebuglevel) (int);
	void		(*close) (MLModule*);
};

/*
 * struct mlModuleImports_s (typedef MLModImport) defines
 * the functions and capabilities that every module imports when it is loaded.
 */

struct mlModuleImports_s {
	ML_rc	(*register_module)(MLModule* modinfo, MLModuleOps* commonops);
	ML_rc	(*unregister_module)(MLModule* modinfo);
	ML_rc	(*register_plugin)(MLModule* modinfo
	,	const char *	plugintype	/* Type of plugin			*/
	,	const char *	pluginname	/* Name of plugin			*/
	,	void*		Ops		/* Info (functions) exported by plugin	*/
	,	void**		pluginid	/* Plugin id (OP)			*/
	,	void**		Imports);	/* Functions imported by plugin (OP)	*/

	ML_rc	(*unregister_plugin)(void* pluginid);
	ML_rc	(*load_module)(const char * moduletype, const char * modulename);

	void*	(*pmalloc)(size_t);	/* Preferred malloc method...		*/
	void	(*pfree)(void *);	/* Preferred free method...		*/
	void	(*log)	(int priority, const char * fmt, ...); // Logging function
};

MLModuleSet*	NewMLModuleSet(const char * basemoduledirectory);

/************************************************************************************
 *
 * Start of MLModEnv.h or something like that ;-)
 *
 *************************************************************************************/

/*
 * MLModEnv is the "class" for the basic module loading mechanism.
 *
 * To enable loading of modules from a particular environment (module type), 
 * one calls NewMLModuleEnvironment with the module type name, the module
 * base directory, and the set of functions to be imported to the module.
 */

typedef struct mlModEnv_s		MLModEnv;

extern MLModEnv* NewMLModuleEnvironment(MLModuleSet* globalenv
	,	const char * moduletype
	,	const char* moduledirectory
	,	MLModuleImports * imports);

extern MLModEnv*	MLModuleEnvironment(MLModEnv* env, const char * moduletype);

/*
 * MLForEachEnv calls 'fun2call' once for each environment in
 * the autoload module environment.
 */
extern void	MLForEachEnv(MLModuleSet* envset
		,	void (*fun2call)(MLModEnv*, void*private)
		,	void *private);

struct mlModEnv_s {
	const char *		moduletype;
	const char *		basemoduledirectory;
	MLModuleSet*		globalenv;
	MLModuleImports*	imports;
	void*			moduleinfo;	/* Private data */

	int	(*IsLoaded)	(MLModEnv*, const char * modulename);
	ML_rc	(*Load)		(MLModEnv*, const char * modulename);
	ML_rc	(*UnLoad)	(MLModEnv*, const char * modulename);
	int	(*refcount)	(MLModEnv*, const char * modulename);
	int	(*modrefcount)	(MLModEnv*, const char * modulename, int plusminus);
	void	(*UnloadUnRef)	(MLModEnv*);
	void	(*setdebuglevel)(MLModEnv*, const char *  modulename);
					/* modulename may be NULL */
	int	(*getdebuglevel)(MLModEnv*, const char *  modulename);
	char**	(*listmodules)(MLModEnv*);
};

/************************************************************************************
 *	Begin MLPluginHandler.h    Or something like that...
 ************************************************************************************/

/*
 * The most basic plugin type is the "PluginHandler" plugin.
 * Each plugin handler registers and deals with plugins of a given type.
 *
 * Such a plugin must be loaded before any modules of it's type can be loaded.
 *
 * In order to load any module of type "foo", we must load a plugin of type
 * "PluginHandler" named "foo".  This plugin then handles the registration of
 * all plugins of type foo.
 *
 * To bootstrap, we load a plugin of type "PluginHandler" named "PluginHandler"
 * during the initialization of the module system.
 *
 * PluginHandlers will be autoloaded if certain conditions are met...
 *
 * If a PluginHandler is to be autoloaded, it must be one plugin handler per file,
 * and the file named according to the type of the plugin it implements, and loaded
 * in the directory named "PluginHandler".
 * 
 */
typedef struct MLPluginHandlerOps_s	MLPluginHandlerOps;
typedef struct MLPluginHandlerImports_s	MLPluginHandlerImports;
typedef struct MLPluginHandlerEnv_s	MLPluginHandlerEnv;
typedef struct MLPluginHandler_s	MLPluginHandler;

/* Interfaces exported by a PluginHandler plugin */
struct MLPluginHandlerOps_s{
	/* RegisterPlugin - Returns unique id info for plugin or NULL on failure */
 	MLPluginHandler* (*RegisterPlugin)(MLPluginHandlerEnv* env
		,	const char * pluginname, void * epiinfo);
	ML_rc	(*UnRegisterPlugin)(MLPluginHandler*ipiinfo);	/* Unregister plugin */
				/* And destroy MLPluginHandler object */
	int	(*IsKnown)(MLPluginHandlerEnv*	env
		,	const char*);	/* True if the given plugin is known */
	MLPluginHandlerEnv*	(*NewPluginEnv)(void);
	void			(*DelPluginEnv)(MLPluginHandlerEnv*);
};

/* Interfaces imported by a PluginHandler plugin */
struct MLPluginHandlerImports_s { 
		/* Return current reference count */
	int (*RefCount)(void * epiinfo);
		/* Incr/Decr reference count */
	int (*ModRefCount)(MLPluginHandler*epiinfo,int plusminus);
		/* Unload module associated with this plugin -- if possible */
	void (*UnloadIfPossible)(MLPluginHandler *epiinfo);
};
/*
 *	MLPluginHandler.c starts here I think ;-)
 *
 */

#define NEW(type)	((type *)ml_pmalloc(sizeof(type)))
#define DELETE(obj)	{ml_pfree(obj); (obj) = NULL;}

struct MLPluginHandler_s {
	MLPluginHandlerEnv*	pluginenv;
	char *			pluginname;
	void*			exportfuns;
};

static MLPluginHandler* MLPluginHandler_new(MLPluginHandlerEnv*	pluginenv
	,	const char*	pluginname
	,	void *		exports);

static void MLPluginHandler_del(MLPluginHandler*	pluginenv);

struct MLPluginHandlerEnv_s {
	GHashTable*	plugins;
};

static void*	(*ml_pmalloc)(size_t) = malloc;
static void	(*ml_pfree)(void *) = free;
static void	(*ml_log)(int priority, const char * fmt, ...) = NULL;
static MLModuleImports*	imp;
static int	debuglevel = 0;

static const char * ml_module_version(void);
static const char * ml_module_name(void);
ML_rc ml_module_init(MLModuleImports* imports);
static int ml_GetDebugLevel(void);
static void ml_SetDebugLevel (int level);
static void ml_close (MLModule*);

static MLModuleOps ModExports =
{	ml_module_version
,	ml_module_name
,	ml_GetDebugLevel
,	ml_SetDebugLevel
,	ml_close
};

static MLPluginHandler*		pipi_register_plugin(MLPluginHandlerEnv* env
				,	const char * pluginname, void * epiinfo);
static ML_rc			pipi_unregister_plugin(MLPluginHandler* plugin);
static int			pipi_isknown(MLPluginHandlerEnv* env, const char *);

static MLPluginHandlerEnv*	pipi_new_pluginenv(void);
static void			pipi_del_pluginenv(MLPluginHandlerEnv*);
static void pipi_del_while_walk(gpointer key, gpointer value, gpointer user_data);

static MLPluginHandlerOps  PiExports =
{		pipi_register_plugin
	,	pipi_unregister_plugin
	,	pipi_isknown
	,	pipi_new_pluginenv
	,	pipi_del_pluginenv
};

static MLModule modinfo ={
	"PluginPlugin"
};
	/* Other stuff goes here ... */


static void * ML_our_pluginid	= NULL;
static MLPluginHandlerImports*	pipimports;

ML_rc
ml_module_init(MLModuleImports* imports)
{
	void *		piimports;

	imp	= imports;
	ml_pmalloc = imp->pmalloc;
	ml_pfree = imp->pfree;
	ml_log = imp->log;

	if (imp->register_module(&modinfo, &ModExports) == 0) {
		return(0);
	}
	if (imp->register_plugin(&modinfo, "Plugin", "PluginPlugin"
		,	&PiExports
		,	&ML_our_pluginid, &piimports) == 0) {

		imp->unregister_module(&modinfo);
		return(0);
	}
	modinfo.user_data = ML_our_pluginid; 	/* THIS IS PROBABLY WRONG! */
						/* Need to read this again after sleep */
	pipimports = piimports;
	return(1);
}



static const char *
ml_module_version(void)
{
	return("1.0");
}
static const char *
ml_module_name(void)
{
	return("PluginPlugin");
}

static int
ml_GetDebugLevel(void)
{
	return(debuglevel);
}

static void ml_SetDebugLevel (int level)
{
	debuglevel = level;
}
static void ml_close (MLModule* module)
{
	/* Need to find all the plugins associated with this Module... */
	/* Probably need "real" support for this */
}


static MLPluginHandler*
pipi_register_plugin(MLPluginHandlerEnv* env
	,	const char * pluginname, void * epiinfo)
{
	MLPluginHandler* ret;

	ret = MLPluginHandler_new(env, pluginname, epiinfo);
	return ret;
}

static ML_rc
pipi_unregister_plugin(MLPluginHandler* plugin)
{
	MLPluginHandlerEnv*	env = plugin->pluginenv;
	g_hash_table_remove(env->plugins, plugin->pluginname);
	MLPluginHandler_del(plugin);
	return 0;
}

static int
pipi_isknown(MLPluginHandlerEnv* env, const char * pluginname)
{
	return 0;
}

/* Create new PluginEnvironment */
static MLPluginHandlerEnv*
pipi_new_pluginenv(void)
{
	MLPluginHandlerEnv*	ret;

	ret = NEW(MLPluginHandlerEnv);

	if (!ret) {
		return ret;
	}
	ret->plugins = g_hash_table_new(g_str_hash, g_str_equal);
	if (!ret->plugins) {
		DELETE(ret);
		ret = NULL;
	}
	return(ret);
}

/* Throw away PluginEnvironment object */
static void
pipi_del_pluginenv(MLPluginHandlerEnv* env)
{
	GHashTable*	t = env->plugins;
	g_hash_table_foreach(t, &pipi_del_while_walk, NULL);
}
static void
pipi_del_while_walk(gpointer	key, gpointer value, gpointer user_data)
{
	MLPluginHandler_del((MLPluginHandler*)value);
}

static MLPluginHandler*
MLPluginHandler_new(MLPluginHandlerEnv*	pluginenv
	,	const char*	pluginname
	,	void *		exports)
{
	MLPluginHandler*	ret = NULL;
	MLPluginHandler*	look = NULL;


	if ((look = g_hash_table_lookup(pluginenv->plugins, pluginname)) != NULL) {
		MLPluginHandler_del(look);
	}
	ret = NEW(MLPluginHandler);

	if (ret) {
		ret->pluginenv = pluginenv;
		ret->exportfuns = exports;
		ret->pluginname = g_strdup(pluginname);
		g_hash_table_insert(pluginenv->plugins, ret->pluginname, ret);
	}
	return ret;
}

static void
MLPluginHandler_del(MLPluginHandler*	plugin)
{
	if (plugin != NULL) {
		if (plugin->pluginname != NULL) {
			DELETE(plugin->pluginname);
		}
		memset(plugin, 0, sizeof(*plugin));
		DELETE(plugin);
	}
}

#endif /* NEWMODULECODE */
