static const char _module_c_Id [] = "$Id: module.c,v 1.26 2001/06/23 04:30:26 alan Exp $";
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
/*
 *	WARNING!! THIS CODE LARGELY / COMPLETELY UNTESTED!!
 *
 *	Not only that, but it isn't finished ;-)
 */

/*****************************************************************************
 *	UPMLS - Universal Plugin and Module Loading System
 *****************************************************************************
 *
 * An Overview of UPMLS...
 *
 * UPMLS is fairly general and reasonably interesting module loading system.
 * These modules are sometimes referred to as plugins.  Here, we use the
 * two terms to mean two different things.
 *
 * This plugin and module loading system is quite general, and should be
 * directly usable by basically any project on any platform on which it runs
 * - which should be many, since everything is build with automake.
 *
 * Some terminology...
 *
 * There are two basic kinds of objects we deal with here:
 *
 * Modules: dynamically loaded chunks of code which implement one or more
 *		plugins.  The system treats all modules as the same.
 *		In UNIX, these are dynamically loaded ".so" files.
 *
 * Plugin: A set of functions which implement a particular plug-in capability.
 * 	Generally plugins are dynamically loaded as part of a module.
 * 	The system treats all plugins of the same type the same.
 * 	It is common to have one plugin inside of each module.  In this case,
 * 	the plugin name should match the module name.
 *
 * Each plugin exports certain interfaces which it exports for its clients
 * to use.   We refer to these those "Ops".  Every plugin of the same type
 * "imports" the same interfaces, and exports the same "Ops".
 *
 * Each plugin is provided certain interfaces which it imports when it
 * is loaded.  We refer to these as "Imports".  Every plugin of a given
 * type imports the same interfaces.
 *
 * Every module exports a certain set of interfaces, regardless of what type
 * of plugins it may implement.  These are described in the MLModuleOps
 * structure.
 *
 * Every module imports a certain set of interfaces, regardless of what type
 * of plugins it may implement.  These are described by the
 * MLModuleImports structure.
 *
 * In the function parameters below, the following notation will
 * sometimes appear:
 *
 * (OP) == Output Parameter - a parameter which is modified by the
 * 	function being called
 *
 *
 *****************************************************************************
 *
 * The basic structures we maintain about modules are as follows:
 *
 *	MLModule		The data which represents a module.
 *	MlModuleType		The data common to all modules of a given type
 *	MlModuleUniv		The set of all module types in the Universe
 *					(well... at least *this* universe)
 *
 * The basic structures we maintain about plugins are as follows:
 * 	MLPlugin		The data which represents a plugin
 * 	MLPluginType		The data which is common to all plugins of
 * 					a given type
 *	MlModuleUniv		The set of all plugin types in the Universe
 *					(well... at least *this* universe)
 *
 * Regarding "Universe"s.  It is our intent that a given program can deal
 * with modules in more than one universe.  This might occur if you have two
 * independent libraries each of which uses the module loading environment
 * to manage their own independent plugin components.  There should be
 * no restriction in creating a program which uses both of these libraries. 
 * At least that's what we hope ;-)
 *
 *
 ***************************************************************************
 * SOME MORE DETAILS ABOUT MODULES...
 ***************************************************************************
 *
 * Going back to more detailed data structures about modules...
 *
 *	MLModuleImports		The set of standard functions all modules
 *				import.
 *				This includes:
 *					register_module()
 *					unregister_module()
 *					register_plugin()
 *					unregister_plugin()
 *					load_module()
 *					log()	Preferred logging function
 *
 *	MLModuleOps		The set of standard operations all modules
 *				export.
 *				This includes:
 *					moduleversion()
 *					modulename()
 *					getdebuglevel()
 *					setdebuglevel()
 *					close()	    Prepare for unloading...
 *
 *	Although we treat modules pretty much the same, they are still
 *	categorized into "types" - one type per directory.  These types
 *	generally (but not necessarily) correspond to plugin types.
 *
 *	One can only cause a module to be loaded - not a plugin.  But it is
 *	common to assume that loading a module named foo of type bar will
 *	cause a plugin named foo of type bar to be registered.  If one
 *	wants to implement automatic module loading in a given plugin type,
 *	this assumption is necessary.
 *
 *	Automatic plugin loading isn't necessary everywhere, but it's nice
 *	for some plugin types.
 *
 *	The general way this works is...
 *
 *	- A request is made to load a particular module of a particular type.
 *
 *	- The module is loaded from the appropriate directory for modules
 *		of that type.
 *
 *	- The ml_module_init() function is called once when the module is
 *		loaded.
 *
 *	The ml_module_init() function is passed a vector of functions which
 *		point to functions it can call to register itself, etc.
 *		(it's of type MLModuleImports)
 *
 * 	The ml_module_init function then uses this set of imported functions
 * 	to register itself and its plugins.
 *
 * 	The mechanism of registering a plugin is largely the same for
 * 	every plugin.  However, the semantics of registering a plugins is
 * 	determined by the plugin loader for the particular type of plugin
 * 	being discussed.
 *
 ***************************************************************************
 * SOME MORE DETAILS ABOUT PLUGINS...
 ***************************************************************************
 *
 *	There is only one built in type of plugin.  That's the Plugin plugin.
 *	The plugin loader for the plugin of type "Plugin", named "Plugin"
 *	inserts itself into the system in order to bootstrap things...
 *
 *	When an attempt is made to register a plugin of an unknown type, then
 *	the Plugin module of the appropriate name is loaded automatically.
 *	The plugins it registers then handle requests to register
 *	plugins whose type is the same as its plugin name.
 *
 * 	Types associated with plugins of type Plugin
 *
 *	MLPluginOps	The set of interfaces that every plugin
 *				handler exports
 *	MLPluginImports	The set of interfaces which are supplied to
 *				(imported by) every plugin of type Plugin.
 */

/*
 *	Begin MLModule.h (or something like that)
 *
 *
 *****************************************************************************
 *
 * Each module has only one entry point which is exported directly, regardless
 * of what kind of plugin(s) it implements...
 *
 * This entrypoint is named ml_module_init().
 *
 * The ml_module_init() function is called once when the module is loaded.
 *
 *
 * All other entry points are exported through parameters passed to
 * ml_module_init()
 *
 * Ml_module_init() then registers the module, and all the plugins which
 * this module implements.  The registration function is in the parameters
 * which are passed to ml_module_init().
 *
 *****************************************************************************
 *
 * THINGS IN THIS DESIGN WHICH ARE PROBABLY BROKEN...
 *
 * Not sufficient thought has been given to return codes, and error
 * indications similar to errno.
 *
 * Each of the plugin handlers needs to be able to get some kind of
 * user data passed to it - at least if it has been loaded manually...
 *
 * It may also be the case that the module loading environment needs
 * to be able to have some kind of user_data passed to it which it can
 * also pass along to any plugin handlers...
 *
 * Does this mean that the plugin handlers don't need their own user_data?
 *
 * I dunno...  Probably not...
 *
 * This is all so that these nice pristene, beautiful concepts can come out
 * and work well in the real world where plugins need to interact with
 * some kind of global system view, and with each other...
 *
 *****************************************************************************
 */
typedef int			ML_rc;	/* Return code from Module fns*/

typedef struct MLModuleImports_s	MLModuleImports;
typedef struct MLModuleOps_s		MLModuleOps;
typedef struct MLModule_s		MLModule;
typedef struct MLModuleUniv_s		MLModuleUniv;
typedef struct MLModuleType_s		MLModuleType;


/*
 * struct MLModule_s (typedef MLModule) is the structure which
 * represents/defines a module, and is used to identify which module is
 * being referred to in various function calls.
 *
 * NOTE: It may be the case that this definition should be moved to
 * another header file - since no one ought to be messing with them anyway ;-)
 *
 * I'm not sure that we're putting the right stuff in here, either...
 */

struct MLModule_s {
	const char*	module_name;
	MLModuleType*	moduletype;	/* Parent structure */
	GHashTable*	Plugins;	/* Plugins registered by this module*/
	int		refcnt;		/* Reference count for this module */

	void*		ud_module;	/* Data needed by module-common code*/
	/* Other stuff goes here ...  (?) */
};
/*
 * struct MLModuleOps_s (typedef MLModuleOps) defines the set of functions
 * exported by all modules...
 */
struct MLModuleOps_s {
	const char*	(*moduleversion) (void);
	const char*	(*modulename)	 (void);
	int		(*getdebuglevel) (void);

	void		(*setdebuglevel) (int);
	void		(*close) (MLModule*);
};

/*
 * struct MLModuleImports_s (typedef MLModuleImports) defines
 * the functions and capabilities that every module imports when it is loaded.
 */

struct MLModuleImports_s {
	ML_rc	(*register_module)(MLModule* modinfo, MLModuleOps* commonops);
	ML_rc	(*unregister_module)(MLModule* modinfo);
	ML_rc	(*register_plugin)(MLModule* modinfo
	,	const char *	plugintype	/* Type of plugin	*/
	,	const char *	pluginname	/* Name of plugin	*/
	,	void*		Ops		/* Info (functions) exported
						   by this plugin	*/
	,	void**		pluginid	/* Plugin id 	(OP)	*/
	,	void**		Imports);	/* Functions imported by
						   this plugin	(OP)	*/

	ML_rc	(*unregister_plugin)(void* pluginid);
	ML_rc	(*load_module)(const char * moduletype, const char * modulname);

	void	(*log)	(int priority, const char * fmt, ...);
					/* Logging function		*/
};

MLModuleUniv*	NewMLModuleUniv(const char * basemoduledirectory);

/***************************************************************************
 *
 * Start of MLModuleType.h or something like that ;-)
 *
 ***************************************************************************/

/*
 * MLModuleType is the "class" for the basic module loading mechanism.
 *
 * To enable loading of modules from a particular module type
 * one calls NewMLModuleType with the module type name, the module
 * base directory, and the set of functions to be imported to the module.
 *
 *
 * The general idea of these structures is as follows:
 *
 * The MLModuleUniv object contains information about all modules of all types.
 * The MLModuleType object contains information about all the modules of a
 * specific type.
 *
 * Note: for modules which implement a single plugin, the module type name
 * should be the same as the plugin type name.
 *
 * For other modules that implement more than one plugin, one of the plugin
 * names should match the module name.
 */


extern MLModuleType* NewMLModuleType(MLModuleUniv* moduleuniv
	,	const char * moduletype
	,	const char* moduledirectory
	,	MLModuleImports * imports);

/*
 * MLForEachModType calls 'fun2call' once for each module type in
 * a MLModuleUniverse
 */
extern void	MLForEachModType(MLModuleUniv* universe
		,	void (*fun2call)(MLModuleType*, void* userdata)
		,	void *userdata);

/*
 *	MLModuleType		Information about all modules of a given type.
 *					(i.e.,  in a given directory)
 *				(AKA struct MLModuleType_s)
 */
struct MLModuleType_s {
	const char *		moduletype;
	const char *		basemoduledirectory;
	MLModuleUniv*		moduniv; /* The universe to which we belong */
	GHashTable*		Modules;
			/* Key is module type, value is MLModule */

	int	(*IsLoaded)	(MLModuleType*, const char * modulename);
	ML_rc	(*Load)		(MLModuleType*, const char * modulename);
	ML_rc	(*UnLoad)	(MLModuleType*, const char * modulename);
	int	(*refcount)	(MLModuleType*, const char * modulename);
	int	(*modrefcount)	(MLModuleType*, const char * modulename
	,			int plusminus);
	void	(*UnloadUnRef)	(MLModuleType*);
	void	(*setdebuglevel)(MLModuleType*, const char *  modulename);
					/* modulename may be NULL */
	int	(*getdebuglevel)(MLModuleType*, const char *  modulename);
	char**	(*listmodules)(MLModuleType*);
};
/*
 *	MLModuleUniv (aka struct MLModuleUniv_s) is the structure which
 *	represents the universe of all MLModuleType objects.
 *	There is one MLModuleType object for each Module type.
 */

struct MLModuleUniv_s {
			/* key is module type, data is MLModuleType* struct */
	GHashTable*		ModuleEnvs;
	struct MLPluginUniv_s*	piuniv; /* Parallel Universe of plugins */
	MLModuleImports*	imports;
};

/*****************************************************************************
 *	Begin MLPlugin.h    Or something like that...
 *****************************************************************************
 *
 * The most basic plugin type is the "PIHandler" plugin.
 * Each plugin handler registers and deals with plugins of a given type.
 *
 * Such a plugin must be loaded before any modules of it's type can be loaded.
 *
 * In order to load any module of type "foo", we must load a plugin of type
 * "Plugin" named "foo".  This plugin then handles the registration of
 * all plugins of type foo.
 *
 * To bootstrap, we load a plugin of type "Plugin" named "Plugin"
 * during the initialization of the module system.
 *
 * PIHandlers will be autoloaded if certain conditions are met...
 *
 * If a PIHandler is to be autoloaded, it must be one plugin handler
 * per file, and the file named according to the type of the plugin it
 * implements, and loaded in the directory named "PIHandler".
 * 
 */
typedef struct MLPluginUniv_s		MLPluginUniv;
typedef struct MLPluginType_s		MLPluginType;
typedef struct MLPlugin_s		MLPlugin;
typedef struct MLPluginOps_s		MLPluginOps;
typedef struct MLPluginImports_s	MLPluginImports;

/* Interfaces exported by a Plugin plugin */
struct MLPluginOps_s{
/*
 *	These are the interfaces exported by a PluginPlugin to the
 *	plugin management infrastructure.  These are not imported
 *	by plugins - only the plugin management infrastructure.
 */

	/* RegisterPlugin - Returns unique id for plugin or NULL (fail) */
 	MLPlugin* (*RegisterPlugin)(MLPluginType* pienv
		,	const char * pluginname, void * exports
		,	void *	ud_plugin
		,	void**	imports);

	ML_rc	(*UnRegisterPlugin)(MLPlugin*ipiinfo); /* Unregister PI*/
				/* And destroy MLPlugin object */

	/* Create a new MLPluginType object */
	MLPluginType*	(*NewPluginEnv)(MLPluginUniv* universe);

	/* Destroy a MLPluginType object */
	void			(*DelPluginEnv)(MLPluginType*);
};

/* Interfaces imported by a PIHandler plugin */
struct MLPluginImports_s { 

		/* Return current reference count */
	int (*RefCount)(MLPlugin * epiinfo);

		/* Incr/Decr reference count */
	int (*ModRefCount)(MLPlugin*epiinfo, int plusminus);

		/* Unload module associated with this plugin -- if possible */
	void (*UnloadIfPossible)(MLPlugin *epiinfo);

};
/*
 *	MLPlugin.c starts here I think ;-)
 *
 */

#define	PLUGIN_PLUGIN	"Plugin"

#define NEW(type)		(g_new(type,1))
#define DELETE(obj)	{g_free(obj); obj = NULL;}

/*
 *	MLPlugin (AKA struct MLPlugin_s) holds the information
 *	we use to track a single plugin handler.
 */

struct MLPlugin_s {
	MLPluginType*		plugintype;
	char *			pluginname;
	void*			exports;	/* Exported Functions	*/
						/* for this plugin	*/
	void*			ud_plugin;	/* per-plugin user data */
	int			refcnt;		/* Reference count for module */
};

static MLPlugin* MLPlugin_new(MLPluginType*	plugintype
	,	const char*	pluginname
	,	void *		exports
	,	void*		ud_plugin);

static void MLPlugin_del(MLPlugin*	plugintype);


/*
 *	MLPluginType (AKA struct MLPluginType_s) holds the info
 *	we use to track the set of all plugin handlers of a single kind.
 */
struct MLPluginType_s {
	GHashTable*		plugins;
	void*			ud_pi_type;	/* per-plugin-type user data*/
	MLPluginUniv*		universe;	/* Pointer to parent (up) */
};
struct MLPluginUniv_s{
	GHashTable*		pitypes;	/* containing
						 * MLPluginType objects
						 */
	struct MLModuleUniv_s*	modenv;	/* parallel universe of modules */
	MLModuleImports*	imports;
};

static int	debuglevel = 0;

static const char * ml_module_version(void);
static const char * ml_module_name(void);
/* Cannot be static -- needed for bootstrapping! */
ML_rc PluginPlugin_module_init(MLModuleImports* imports, MLModuleUniv* env
		);
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

static MLPlugin*		pipi_register_plugin(MLPluginType* env
				,	const char * pluginname
				,	void * exports, void * ud_plugin
				,	void** imports);
static ML_rc			pipi_unregister_plugin(MLPlugin* plugin);

static MLPluginType*	pipi_new_plugintype(MLPluginUniv*);
static void			pipi_del_plugintype(MLPluginType*);
static void pipi_del_while_walk(gpointer key, gpointer value
,				gpointer user_data);

static MLPluginOps  PiExports =
{		pipi_register_plugin
	,	pipi_unregister_plugin
	,	pipi_new_plugintype
	,	pipi_del_plugintype
};

static MLModule PluginPluginModinfo ={
	PLUGIN_PLUGIN
};

static int PiRefCount(MLPlugin * pih);
static int PiModRefCount(MLPlugin*epiinfo,int plusminus);
static void PiUnloadIfPossible(MLPlugin *epiinfo);


static MLPluginImports PIHandlerImports = {
	PiRefCount,
	PiModRefCount,
	PiUnloadIfPossible,
};

/*
 *	PluginPlugin_module_init: Initialize the handling of "Plugin" plugins.
 *
 *	There are a few potential bootstrapping problems here ;-)
 *
 */
ML_rc
PluginPlugin_module_init(MLModuleImports* imports, MLModuleUniv* univ)
{
	MLPlugin*	piinfo;
	MLPluginType*	piuniv;
	void*		dontcare;

	if (univ->piuniv) {
		return 0;
	}

	/* We are the creator of the MLPluginType object */

	piuniv = NEW(MLPluginType);
	memset(piuniv, 0, sizeof(*piuniv));

	/* We can call register_module, since it doesn't depend on us... */
	if (imports->register_module(&PluginPluginModinfo, &ModExports) == 0) {
		return(0);
	}
	/*
	 * Now, we're registering plugins, and are into some deep
	 * Catch-22 if do it the "easy" way, since our code is
	 * needed in order to support plugin loading for the type of plugin
	 * we are (a Plugin plugin).
	 *
	 * So, instead of calling imports->register_plugin(), we have to do
	 * the work ourselves here...
	 *
	 * Since no one should yet be registered to handle Plugin plugins, we
	 * need to bypass the hash table handler lookup that register_plugin
	 * would do and call the function that register_plugin would call...
	 *
	 * The function to call is pipi_register_plugin()...
	 */

	/* The first argument is the MLPluginType */
	piinfo = pipi_register_plugin(piuniv, PLUGIN_PLUGIN, &PiExports
	,	NULL, &dontcare);

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
	return(PLUGIN_PLUGIN);
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
	/* Need to find all the plugins associated with this Module...
	 * Probably need "real" (GHashTable) support for this
	 * FIXME!
	 */
}


static MLPlugin*
pipi_register_plugin(MLPluginType* univ
	,	const char * pluginname, void * exports, void * user_data
	,	void**		imports)
{
	MLPlugin* ret;

	if (g_hash_table_lookup(univ->plugins, pluginname) != NULL) {
		return NULL;
	}
	ret = MLPlugin_new(univ, pluginname, exports, user_data);
	g_hash_table_insert(univ->plugins, g_strdup(pluginname), ret);
	*imports = &PIHandlerImports;
	return ret;
}

static ML_rc
pipi_unregister_plugin(MLPlugin* plugin)
{
	MLPluginType*	univ = plugin->plugintype;
	g_hash_table_remove(univ->plugins, plugin->pluginname);
	MLPlugin_del(plugin);
	return 0;
}


/* Create a new MLPluginType object */
static MLPluginType*
pipi_new_plugintype(MLPluginUniv* pluginuniv)
{
	MLPluginType*	ret;

	ret = NEW(MLPluginType);

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

/* Destroy a MLPluginType object */
static void
pipi_del_plugintype(MLPluginType* univ)
{
	GHashTable*	t = univ->plugins;
	g_hash_table_foreach(t, &pipi_del_while_walk, t);
	g_hash_table_destroy(t);
	univ->plugins = NULL;
	DELETE(univ);
}

static void
pipi_del_while_walk(gpointer key, gpointer value, gpointer user_data)
{
	GHashTable* t = user_data;
	g_hash_table_remove(t, key);
	DELETE(key);
	MLPlugin_del((MLPlugin*)value); value = NULL;
}

static MLPlugin*
MLPlugin_new(MLPluginType*	plugintype
	,	const char*	pluginname
	,	void *		exports
	,	void*		ud_plugin)
{
	MLPlugin*	ret = NULL;
	MLPlugin*	look = NULL;


	if ((look = g_hash_table_lookup(plugintype->plugins, pluginname))
	!=		NULL) {
		MLPlugin_del(look);
	}
	ret = NEW(MLPlugin);

	if (ret) {
		ret->plugintype = plugintype;
		ret->exports = exports;
		ret->ud_plugin = ud_plugin;
		ret->pluginname = g_strdup(pluginname);
		g_hash_table_insert(plugintype->plugins, ret->pluginname, ret);
		ret->refcnt = 1;
	}
	return ret;
}

static void
MLPlugin_del(MLPlugin*	plugin)
{
	if (plugin != NULL) {
		/* FIXME!! Log warning if reference count isn't zero */
		if (plugin->pluginname != NULL) {
			DELETE(plugin->pluginname);
		}
		memset(plugin, 0, sizeof(*plugin));
		DELETE(plugin);
	}
}

static int
PiRefCount(MLPlugin * epiinfo)
{
	return epiinfo->refcnt;
}
static int
PiModRefCount(MLPlugin*epiinfo, int plusminus)
{
	epiinfo->refcnt += plusminus;
	return epiinfo->refcnt;
}
static void
PiUnloadIfPossible(MLPlugin *epiinfo)
{
}
/*****************************************************************************
 *
 * PluginMgmt.c
 *
 * This code is for managing plugins, and interacting with them...
 *
 ****************************************************************************/
 
#ifdef __GNUC__
 
#define REPORTERR(expr)                  G_STMT_START{		\
       g_log (G_LOG_DOMAIN,                                     \
	      G_LOG_LEVEL_ERROR,                                \
	      "ERROR: file %s: line %d (%s): %s",  			\
	      __FILE__,                                         \
	      __LINE__,                                         \
	      __PRETTY_FUNCTION__,                              \
	      #expr);                   }G_STMT_END
 
#else /* !__GNUC__ */
 
#define REPORTERR(expr)                  G_STMT_START{           \
       g_log (G_LOG_DOMAIN,                                     \
	      G_LOG_LEVEL_ERROR,                                \
	      "ERROR: file %s: line %d: %s",       \
	      __FILE__,                                         \
	      __LINE__,                                         \
	      #expr);                   }G_STMT_END
 
#endif /* __GNUC__ */
 

ML_rc
RegisterAPlugin(MLModule* modinfo
,	const char *	plugintype	/* Type of plugin	*/
,	const char *	pluginname	/* Name of plugin	*/
,	void*		Ops		/* Info (functions) exported
					   by this plugin	*/
,	void**		pluginid	/* Plugin id 	(OP)	*/
,	void**		Imports		/* Functions imported by
					 this plugin	(OP)	*/
,	void*		ud_plugin	/* plugin user data */
);

ML_rc
RegisterAPlugin(MLModule* modinfo
,	const char *	plugintype	/* Type of plugin	*/
,	const char *	pluginname	/* Name of plugin	*/
,	void*		Ops		/* Info (functions) exported
					   by this plugin	*/
,	void**		pluginid	/* Plugin id 	(OP)	*/
,	void**		Imports		/* Functions imported by
					 this plugin	(OP)	*/
,	void*		ud_plugin	/* Optional user_data */
)
{
	MLModuleUniv*	moduniv;
	MLModuleType*	modtype;
	MLPluginUniv*	piuniv;
	MLPluginType*	pitype;
	MLPlugin*	piinfo;

	MLPluginType*	pipitype;
	MLPlugin*	pipiinfo;
	MLPluginOps*	piops;

	if (modinfo == NULL
	||	(modtype = modinfo->moduletype)	== NULL
	||	(moduniv = modtype->moduniv)	== NULL
	||	(piuniv = moduniv->piuniv)	== NULL
	) {
		REPORTERR("bad parameters");
		return 0;
	}

	/* Now we have lots of info, but not quite enough... */

	if ((pitype = g_hash_table_lookup(piuniv->pitypes, plugintype))
	==	NULL) {
		/* Really ought to try and autoload this plugin module */
		return 0;
	}
	if ((piinfo = g_hash_table_lookup(pitype->plugins, pluginname))
	!=	NULL) {
		g_warning("Attempt to register duplicate plugin: %s/%s"
		,	plugintype, pluginname);
		return 0;
	}
	/*
	 * OK...  Now we know it is valid, and isn't registered...
	 * let's look up the PluginPlugin hander for this type
	 */
	if ((pipitype = g_hash_table_lookup(piuniv->pitypes, PLUGIN_PLUGIN))
	==	NULL) {
		REPORTERR("No " PLUGIN_PLUGIN " type!");
		return 0;
	}
	if ((pipiinfo = g_hash_table_lookup(pipitype->plugins, PLUGIN_PLUGIN))
	==	NULL) {
		REPORTERR("No " PLUGIN_PLUGIN " plugin!");
		return 0;
	}

	/* Now we have all the information anyone could possibly want ;-) */

	piops = pipiinfo->exports;
	piops->RegisterPlugin(pitype, pluginname, Ops
	,	ud_plugin
	,	Imports);

	return 1;
}
/*
 * We need the following functions:
 *
 * Module functions:
 *
 * MLloadModule()	- loads a module into memory and calls the
 * 				ml_module_init() entry point in the module.
 *
 * MLModulePath()	- returns path name for a given module
 *
 * MLModuleTypeList()	- returns list of modules of a given type
 * 
 *
 * PluginPlugin functions:
 *
 *
 */

#endif /* NEWMODULECODE */
