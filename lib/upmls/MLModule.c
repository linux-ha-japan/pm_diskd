#include <portability.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

/* Dumbness... */
#define time FooTimeParameter
#define index FooIndexParameter
#	include <glib.h>
#undef time
#undef index


#define ENABLE_ML_DEFS_PRIVATE
#define ENABLE_PLUGIN_MANAGER_PRIVATE

#include <upmls/MLPlugin.h>
#include "../../libltdl/config.h"

#define NEW(type)		(g_new(type,1))
#define	ZAP(obj)	memset(obj, 0, sizeof(*obj))
#define DELETE(obj)	{g_free(obj); obj = NULL;}

#define MODULESUFFIX	LTDL_SHLIB_EXT

static int	ModuleDebugLevel = 5;

#define DEBUGMODULE	(ModuleDebugLevel > 0)



static ML_rc PluginPlugin_module_init(MLModuleUniv* univ);

static char** MLModTypeListModules(MLModuleType* mtype, int* modcount);


void	DelMLModuleUniv(MLModuleUniv*);
/*
 *	These RmA* functions primarily called from hash_table_foreach, but
 *	not necessarily, so they have gpointer arguments.  When calling
 *	them by hand, take special care to pass the right argument types.
 *
 *	They all follow the same calling sequence though.  It is:
 *		String name"*" type object
 *		"*" type object with the name given by 1st argument
 *		NULL
 *
 *	For example:
 *		RmAMLModuleType takes
 *			string name
 *			MLModuleType* object with the given name.
 */
static void	RmAMLModuleType
(	gpointer modtname	/* Name of this module type */
,	gpointer modtype	/* MLModuleType* */
,	gpointer notused
);

static MLModuleType* NewMLModuleType
(	MLModuleUniv* moduleuniv
,	const char *	moduletype
);
static void	DelMLModuleType(MLModuleType*);
/*
 *	These RmA* functions primarily called from hash_table_foreach, but
 *	not necessarily, so they have gpointer arguments.  When calling
 *	them by hand, take special care to pass the right argument types.
 */
static void	RmAMLModule
(	gpointer modname	/* Name of this module  */
,	gpointer module		/* MLModule* */
,	gpointer notused
);


static MLModule* NewMLModule(MLModuleType* mtype
	,	const char *	module_name
	,	lt_dlhandle	dlhand
	,	MLModuleInitFun ModuleSym);
static void	DelMLModule(MLModule*);




static int MLModrefcount(MLModuleType*, const char * modulename);
static int MLModmodrefcount(MLModuleType* mltype, const char * modulename
,	int plusminus);

static MLPluginUniv*	NewMLPluginUniv(MLModuleUniv*);
static void		DelMLPluginUniv(MLPluginUniv*);
/*
 *	These RmA* functions primarily called from hash_table_foreach, but
 *	not necessarily, so they have gpointer arguments.  When calling
 *	them by hand, take special care to pass the right argument types.
 */
static void		RmAMLPluginType
(	gpointer pitypename	/* Name of this plugin type  */
,	gpointer pitype		/* MLPluginType* */
,	gpointer notused
);

static MLPluginType*	NewMLPluginType
(	MLPluginUniv*
,	const char * typename
,	void* pieports, void* user_data
);
static void		DelMLPluginType(MLPluginType*);
/*
 *	These RmA* functions are designed to be  called from
 *	hash_table_foreach, so they have gpointer arguments.  When calling
 *	them by hand, take special care to pass the right argument types.
 *	They can be called from other places safely also.
 */
static void		RmAMLPlugin
(	gpointer piname		/* Name of this plugin */
,	gpointer module		/* MLPlugin* */
,	gpointer notused
);

static MLPlugin*	NewMLPlugin
(	MLPluginType*	plugintype
,	const char*	pluginname
,	void *		exports
,	MLPluginFun	closefun
,	void*		ud_plugin
);
static void		DelMLPlugin(MLPlugin*);
static ML_rc	close_pipi_plugin(MLPlugin*, void*);




/*
 *	For consistency, we show up as a module in our our system.
 *
 *	Here are our exports as a module.
 *
 */
static const char *	ML_MLModuleVersion(void);
static void		ML_MLModuleClose (MLModule*);

static const MLModuleOps ModExports =
{	ML_MLModuleVersion
,	MLGetDebugLevel		/* also directly exported */
,	MLSetDebugLevel		/* also directly exported */
,	ML_MLModuleClose
};

/*	Prototypes for the functions that we export to every module */
static ML_rc MLregister_module(MLModule* modinfo, const MLModuleOps* mops);
static ML_rc MLunregister_module(MLModule* modinfo);
static ML_rc
MLRegisterPlugin
(	MLModule*	modinfo
,	const char *	plugintype	/* Type of plugin		*/
,	const char *	pluginname	/* Name of plugin		*/
,	void*		Ops		/* Ops exported by this plugin	*/
,	MLPluginFun	closefunc	/* Ops exported by this plugin	*/
,	MLPlugin**	pluginid	/* Plugin id 	(OP)		*/
,	void**		Imports		/* Functions imported by
					 this plugin	(OP)		*/
,	void*		ud_plugin	/* plugin user data 		*/
);
static ML_rc	MLunregister_plugin(MLPlugin* pluginid);
static void	MLLog(MLLogLevel priority, const char * fmt, ...);


/*
 *	This is the set of functions that we export to every module
 *
 *	That also makes it the set of functions that every module imports.
 *
 */

static MLModuleImports MLModuleImportSet =
{	MLregister_module	/* register_module */
,	MLunregister_module	/* unregister_module */
,	MLRegisterPlugin	/* register_plugin */
,	MLunregister_plugin	/* unregister_plugin */
,	MLLoadModule		/* load_module */
,	MLLog			/* Logging function */
};

static ML_rc	pipi_register_plugin(MLPlugin* newpi
		,		void** imports);
static ML_rc	pipi_unregister_plugin(MLPlugin* plugin);

/*
 *	For consistency, the master plugin manager is a plugin in the system
 *	Below is our set of exported Plugin functions.
 *
 *	Food for thought:  This is the plugin manager whose name is plugin.
 *	This makes it the Plugin Plugin plugin ;-)
 *		(or the Plugin/Plugin plugin if you prefer)
 */

static MLPluginOps  PiExports =
{	pipi_register_plugin
,	pipi_unregister_plugin
};



/*
 * Below is the set of functions we export to every plugin manager.
 */

static int	PiRefCount(MLPlugin * pih);
static int	PiModRefCount(MLPlugin*epiinfo,int plusminus);
static void	PiForceUnregister(MLPlugin *epiinfo);
static void	PiForEachClient(MLPlugin* manangerpi
	,	void(*f)(MLPlugin* clientpi, void * other)
	,	void* other);

static MLPluginImports PIHandlerImports =
{	PiRefCount
,	PiModRefCount
,	PiForceUnregister
,	PiForEachClient
};

/*****************************************************************************
 *
 * This code is for managing modules, and interacting with them...
 *
 ****************************************************************************/

MLModule*
NewMLModule(	MLModuleType* mtype
	,	const char *	module_name
	,	lt_dlhandle	dlhand
	,	MLModuleInitFun ModuleSym)
{
	MLModule*	ret = NEW(MLModule);
	ret->module_name = g_strdup(module_name);
	ret->moduletype = mtype;
	ret->Plugins = g_hash_table_new(g_str_hash, g_str_equal);
	ret->refcnt = 0;
	ret->dlhandle = dlhand;
	ret->dlinitfun = ModuleSym;
	return ret;
}
static void
DelMLModule(MLModule*mod)
{
	DELETE(mod->module_name);

	mod->moduletype = NULL;
	if (g_hash_table_size(mod->Plugins) > 0) {
		MLLog(ML_CRIT, "DelMLModule: Plugins not empty");
	}
	g_hash_table_destroy(mod->Plugins);
	mod->Plugins = NULL;

	if (mod->refcnt > 0) {
		MLLog(ML_CRIT, "DelMLModule: Non-zero refcnt");
	}

	lt_dlclose(mod->dlhandle); mod->dlhandle=NULL;
	mod->dlhandle = NULL;

	mod->dlinitfun = NULL;
	mod->ud_module = NULL;

	ZAP(mod);
	DELETE(mod);
}


static MLModuleType dummymlmtype =
{	NULL			/*moduletype*/
,	NULL			/*moduniv*/
,	NULL			/*Modules*/
,	MLModrefcount		/* refcount */
,	MLModmodrefcount	/* modrefcount */
,	MLModTypeListModules	/* listmodules */
};

static MLModuleType*
NewMLModuleType(MLModuleUniv* moduleuniv
	,	const char *	moduletype
)
{
	MLModuleType*	ret = NEW(MLModuleType);

	*ret = dummymlmtype;

	ret->moduletype = g_strdup(moduletype);
	ret->moduniv = moduleuniv;
	ret->Modules = g_hash_table_new(g_str_hash, g_str_equal);
	return ret;
}
static void
DelMLModuleType(MLModuleType*mtype)
{
	DELETE(mtype->moduletype);

	if (g_hash_table_size(mtype->Modules) > 0) {
		MLLog(ML_CRIT, "DelMLModuleType: Modules not empty");
	}
	g_hash_table_foreach(mtype->Modules, RmAMLModule, NULL);
	g_hash_table_destroy(mtype->Modules);
	ZAP(mtype);
	DELETE(mtype);
}
/*
 *	These RmA* functions primarily called from hash_table_foreach, 
 *	so they have gpointer arguments.  This *not necessarily* clause
 *	is why they do the g_hash_table_lookup_extended call instead of
 *	just deleting the key.  When called from outside, the key *
 *	may not be pointing at the key to actually free, but a copy
 *	of the key.
 */
static void
RmAMLModule	/* IsA GHFunc: required for g_hash_table_foreach() */
(	gpointer modname	/* Name of this module  */
,	gpointer module		/* MLModule* */
,	gpointer notused	
)
{
	MLModule*	Module = module;
	MLModuleType*	Mtype = Module->moduletype;
	gpointer	key;

	/* Normally (but not always) called from g_hash_table_forall */

	if (g_hash_table_lookup_extended(Mtype->Modules
	,	modname, &key, &module)) {
		g_hash_table_remove(Mtype->Modules, key);
		DelMLModule(module);
		DELETE(key);
	}else{
		g_assert_not_reached();
	}
}

MLModuleUniv*
NewMLModuleUniv(const char * basemoduledirectory)
{
	MLModuleUniv*	ret = NEW(MLModuleUniv);

	if (!g_path_is_absolute(basemoduledirectory)) {
		DELETE(ret);
		return(ret);
	}
	ret->rootdirectory = g_strdup(basemoduledirectory);

	ret->ModuleTypes = g_hash_table_new(g_str_hash, g_str_equal);
	ret->imports = &MLModuleImportSet;
	ret->piuniv = NewMLPluginUniv(ret);
	return ret;
}

void
DelMLModuleUniv(MLModuleUniv* moduniv)
{

	DELETE(moduniv->rootdirectory);

	if (g_hash_table_size(moduniv->ModuleTypes) > 0) {
		MLLog(ML_CRIT, "DelMLModuleUniv: ModuleTypes not empty");
	}
	g_hash_table_foreach(moduniv->ModuleTypes, RmAMLModuleType, NULL);
	g_hash_table_destroy(moduniv->ModuleTypes);
	moduniv->ModuleTypes = NULL;
	DelMLPluginUniv(moduniv->piuniv);
	moduniv->piuniv = NULL;
	moduniv->imports = NULL;
	DELETE(moduniv);
}

/*
 *	These RmA* functions primarily called from hash_table_foreach, 
 *	so they have gpointer arguments.  This *not necessarily* clause
 *	is why they do the g_hash_table_lookup_extended call instead of
 *	just deleting the key.  When called from outside, the key *
 *	may not be pointing at the key to actually free, but a copy
 *	of the key.
 */
static void	/* IsA GHFunc: required for g_hash_table_foreach() */
RmAMLModuleType
(	gpointer modtname	/* Name of this module type */
,	gpointer modtype	/* MLModuleType* */
,	gpointer notused
)
{
	MLModuleType*	Modtype = modtype;
	MLModuleUniv*	Moduniv = Modtype->moduniv;
	gpointer	key;

	/*
	 * This function is usually but not always called by
	 * g_hash_table_foreach()
	 */

	if (g_hash_table_lookup_extended(Moduniv->ModuleTypes
	,	modtname, &key, &modtype)) {

		g_hash_table_remove(Moduniv->ModuleTypes, key);
		DelMLModuleType(modtype);
		DELETE(key);
	}else{
		g_assert_not_reached();
	}
}

/*
 *	PluginPlugin_module_init: Initialize the handling of "Plugin" plugins.
 *
 *	There are a few potential bootstrapping problems here ;-)
 *
 */
static ML_rc
PluginPlugin_module_init(MLModuleUniv* univ)
{
	MLModuleImports* imports = univ->imports;
	MLModuleType*	modtype;
	MLPlugin*	piinfo;
	MLPluginType*	pitype;
	void*		dontcare;
	MLModule*	pipi_module;
	ML_rc		rc;


	pitype = NewMLPluginType(univ->piuniv, PLUGIN_PLUGIN, &PiExports
	,	NULL);

	g_hash_table_insert(univ->piuniv->pitypes
	,	g_strdup(PLUGIN_PLUGIN), pitype);

	modtype = NewMLModuleType(univ, PLUGIN_PLUGIN);

	g_hash_table_insert(univ->ModuleTypes
	,	g_strdup(PLUGIN_PLUGIN), modtype);

	pipi_module= NewMLModule(modtype, PLUGIN_PLUGIN, NULL, NULL);

	g_hash_table_insert(modtype->Modules
	,	g_strdup(PLUGIN_PLUGIN), pipi_module);

	/* We can call register_module, since it doesn't depend on us... */
	rc = imports->register_module(pipi_module, &ModExports);
	if (rc != ML_OK) {
		MLLog(ML_CRIT, "register_module() failed in init: %s"
		,	ML_strerror(rc));
		return(rc);
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
	 */

	/* The first argument is the MLPluginType* */
	piinfo = NewMLPlugin(pitype, PLUGIN_PLUGIN, &PiExports
	,	close_pipi_plugin, NULL);
	pipi_register_plugin(piinfo, &dontcare);

	return(ML_OK);
}/*PluginPlugin_module_init*/


/* Return current PiPi "module" version (not very interesting for us) */
static const char *
ML_MLModuleVersion(void)
{
	return("1.0");
}

/* Return current PiPi debug level */
int
MLGetDebugLevel(void)
{
	return(ModuleDebugLevel);
}

/* Set current PiPi debug level */
void
MLSetDebugLevel (int level)
{
	ModuleDebugLevel = level;
}

/* Close/shutdown our MLModule (the plugin manager plugin module) */
/* All our plugins will have already been shut down and unregistered */
static void
ML_MLModuleClose (MLModule* module)
{
}

/*****************************************************************************
 *
 * This code is for managing plugins, and interacting with them...
 *
 ****************************************************************************/


static MLPlugin*
NewMLPlugin(MLPluginType*	plugintype
	,	const char*	pluginname
	,	void *		exports
	,	MLPluginFun	closefun
	,	void*		ud_plugin)
{
	MLPlugin*	ret = NULL;
	MLPlugin*	look = NULL;


	if ((look = g_hash_table_lookup(plugintype->plugins, pluginname))
	!=		NULL) {
		DelMLPlugin(look);
	}
	ret = NEW(MLPlugin);

	if (ret) {
		ret->plugintype = plugintype;
		ret->exports = exports;
		ret->ud_plugin = ud_plugin;
		ret->pluginname = g_strdup(pluginname);
		ret->pimanager = plugintype->pipi_ref;
		g_hash_table_insert(plugintype->plugins
		,	g_strdup(ret->pluginname), ret);
		ret->pi_close = closefun;
		ret->refcnt = 0;
	}
	return ret;
}
static void
DelMLPlugin(MLPlugin* pi)
{
	DELETE(pi->pluginname);
	if (pi->refcnt > 0) {
		MLLog(ML_CRIT, "DelMLPlugin: refcnt not zero");
	}
	DELETE(pi);
}

static MLPluginType*
NewMLPluginType(MLPluginUniv*univ, const char * typename
,	void* pieports, void* user_data)
{
	MLPluginType*	pipi_types;
	MLPlugin*	pipi_ref;
	MLPluginType*	ret = NEW(MLPluginType);
	ret->typename = g_strdup(typename);
	ret->plugins = g_hash_table_new(g_str_hash, g_str_equal);
	ret->ud_pi_type = user_data;
	ret->universe = univ;
	ret->pipi_ref = NULL;
	/* Now find the pointer to our plugin type in the Plugin Universe*/
	if ((pipi_types = g_hash_table_lookup(univ->pitypes, PLUGIN_PLUGIN))
	!= NULL) {
		if ((pipi_ref=g_hash_table_lookup(pipi_types->plugins
		,	typename)) != NULL) {
			ret->pipi_ref = pipi_ref;
		}else {
		      g_assert(strcmp(typename, PLUGIN_PLUGIN) == 0);
		}
	}
	return ret;
}
static void
DelMLPluginType(MLPluginType*pit)
{
	if (g_hash_table_size(pit->plugins) > 0) {
		MLLog(ML_CRIT, "DelMLPluginType: plugins not empty");
	}
	g_hash_table_foreach(pit->plugins, RmAMLPlugin, NULL);
	DELETE(pit->typename);
	g_hash_table_destroy(pit->plugins);
	ZAP(pit);
	DELETE(pit); pit = NULL;
}

/*
 *	These RmA* functions primarily called from hash_table_foreach, 
 *	so they have gpointer arguments.  This *not necessarily* clause
 *	is why they do the g_hash_table_lookup_extended call instead of
 *	just deleting the key.  When called from outside, the key *
 *	may not be pointing at the key to actually free, but a copy
 *	of the key.
 */
static void	/* IsAGHFunc: required for g_hash_table_foreach() */
RmAMLPlugin
(	gpointer piname	/* Name of this plugin */
,	gpointer pi	/* MLPlugin* */
,	gpointer notused
)
{
	MLPlugin*	Pi = pi;
	MLPluginType*	Pitype = Pi->plugintype;
	gpointer	key;

	/*
	 * This function is usually but not always called by
	 * g_hash_table_foreach()
	 */

	if (g_hash_table_lookup_extended(Pitype->plugins
	,	piname, &key, &pi)) {
		g_hash_table_remove(Pitype->plugins, key);
		DelMLPlugin(Pi);
		DELETE(key);
	}else{
		g_assert_not_reached();
	}
}


/* Register a Plugin Plugin (Plugin manager) */
static ML_rc
pipi_register_plugin(MLPlugin* pi
,		void**	imports)
{
	if (DEBUGMODULE) {
		MLLog(ML_DEBUG
		, 	"Registering Plugin manager for type '%s'"
		,	pi->pluginname);
	}
	*imports = &PIHandlerImports;
	return ML_OK;
}

/* Unconditionally unregister a plugin manager (Plugin Plugin) */
static ML_rc
pipi_unregister_plugin(MLPlugin* plugin)
{
	gpointer	origkey;
	gpointer	value;
	MLPluginType*	pitype = plugin->plugintype;

	/* Call g_hash_table_lookup_extended to get the key pointer */
	if (g_hash_table_lookup_extended(pitype->plugins
	,	pitype->plugins, &origkey, &value)) {
		g_hash_table_remove(pitype->plugins, plugin->pluginname);
		DELETE(origkey);
	}
	DelMLPlugin(plugin);
	return ML_OK;
}

/*	Called to close the Plugin manager for type Plugin */
static ML_rc
close_pipi_plugin(MLPlugin* us, void* ud_plugin)
{
	/* Nothing much to do */
	return ML_OK;
}

/* Return the reference count for this plugin */
static int
PiRefCount(MLPlugin * epiinfo)
{
	return epiinfo->refcnt;
}
 
/* Return the reference count for this plugin */
static int
PiModRefCount(MLPlugin*epiinfo, int plusminus)
{
	epiinfo->refcnt += plusminus;
	if (epiinfo->refcnt <= 0) {
		/* Unregister this plugin? FIXME!! */
		epiinfo = 0;
	}
	return epiinfo->refcnt;
}

static void
PiForceUnregister(MLPlugin *id)
{
	RmAMLPlugin(id->pluginname, id, NULL);
}

struct f_e_c_helper {
	void(*fun)(MLPlugin* clientpi, void * passalong);
	void*	passalong;
};

static void PiForEachClientHelper(gpointer key
,	gpointer pitype, gpointer helper_v);

static void
PiForEachClientHelper(gpointer unused, gpointer pitype, gpointer v)
{
	struct f_e_c_helper*	s;
	s = (struct f_e_c_helper*)v;
	s->fun((MLPlugin*)pitype, s->passalong);
}


static void
PiForEachClient(MLPlugin* mgrpi
	,	void(*f)(MLPlugin* clientpi, void * passalong)
	,	void* passalong)
{
	MLPluginType*	mgrt;
	MLPluginUniv*	u;
	const char *	piname;
	MLPluginType*	clientt;
	struct f_e_c_helper	h;
		

	if (mgrpi == NULL || (mgrt = mgrpi->plugintype) == NULL
	||	(u = mgrt->universe) == NULL
	||	(piname = mgrpi->pluginname) == NULL) {
		MLLog(ML_WARN, "bad parameters to PiForEachClient");
		return;
	}

	if ((clientt = g_hash_table_lookup(u->pitypes, piname)) == NULL) {
		MLLog(ML_WARN, "cannot find PI type %s", piname);
		return;
	};
	if (clientt->pipi_ref != mgrpi) {
		MLLog(ML_WARN, "Bad pipi_ref ptr in MLPluginType");
		return;
	}

	
	g_hash_table_foreach(clientt->plugins, PiForEachClientHelper, &h);

}

static ML_rc
MLregister_module(MLModule* modinfo, const MLModuleOps* commonops)
{
	modinfo->moduleops = commonops;
	return ML_OK;
}

static ML_rc
MLunregister_module(MLModule* modinfo)
{
	RmAMLModule(modinfo->module_name, modinfo, NULL);
	return ML_OK;
}

/* General logging function (not really UPMLS-specific) */
static void
MLLog(MLLogLevel priority, const char * format, ...)
{
	va_list		args;
	GLogLevelFlags	flags;

	switch(priority) {
		case ML_FATAL:	flags = G_LOG_LEVEL_ERROR;
			break;
		case ML_CRIT:	flags = G_LOG_LEVEL_CRITICAL;
			break;

		default:	/* FALL THROUGH... */
		case ML_WARN:	flags = G_LOG_LEVEL_WARNING;
			break;

		case ML_INFO:	flags = G_LOG_LEVEL_INFO;
			break;
		case ML_DEBUG:	flags = G_LOG_LEVEL_DEBUG;
			break;
	};
	va_start (args, format);
	g_logv (G_LOG_DOMAIN, flags, format, args);
	va_end (args);
}

static const char * ML_strerrmsgs [] =
{	"Success"
,	"Invalid Parameters"
,	"Bad module/plugin type"
,	"Duplicate entry (module/plugin name/type)"
,	"Oops happens"
,	"No such module/plugin/plugin type"
};

const char *
ML_strerror(ML_rc rc)
{
	int	irc = (int) rc;
	static	char buf[128];

	if (irc < 0 || irc >= DIMOF(ML_strerrmsgs)) {
		snprintf(buf, sizeof(buf), "return code %d (?)", irc);
		return buf;
	}
	return ML_strerrmsgs[irc];
}


/*
 * MLLoadModule()	- loads a module into memory and calls the
 * 			initial() entry point in the module.
 *
 *
 * Method:
 *
 * 	Construct file name of module.
 * 	See if module exists.  If not, fail with ML_NOMODULE.
 *
 *	Search Universe for module type
 *		If found, search module type for modulename
 *			if found, fail with ML_EXIST.
 *		Otherwise,
 *			Create new Module type structure
 *	Use lt_dlopen() on module to get lt_dlhandle for it.
 *
 *	Construct the symbol name of the initialization function.
 *
 *	Use lt_dlsym() to find the pointer to the init function.
 *
 *	Call the initialization function.
 */
ML_rc
MLLoadModule(MLModuleUniv* universe, const char * moduletype
,	const char *	modulename
,	void*		module_user_data)
{
	char * ModulePath;
	char * ModuleSym;
	MLModuleType*	mtype;
	MLModule*	modinfo;
	lt_dlhandle	dlhand;
	MLModuleInitFun	initfun;

	ModulePath = g_strdup_printf("%s%s%s%s%s%s"
	,	universe->rootdirectory
	,	G_DIR_SEPARATOR_S
	,	moduletype
	,	G_DIR_SEPARATOR_S
	,	modulename
	,	LTDL_SHLIB_EXT);

	if (DEBUGMODULE) {
		MLLog(ML_DEBUG, "Module path for %s/%s => [%s]"
		,	moduletype, modulename, ModulePath);
	}

	/* Make sure we can read and execute the module file */
	/* This test is nice, because dlopen reasons aren't return codes */

	if (access(ModulePath, R_OK|X_OK) != 0) {
		if (DEBUGMODULE) {
			MLLog(ML_DEBUG, "Module file %s does not exist"
			,	ModulePath);
		}
		DELETE(ModulePath);
		return ML_NOMODULE;
	}

	if((mtype=g_hash_table_lookup(universe->ModuleTypes, moduletype))
	!= NULL) {
		if ((modinfo = g_hash_table_lookup
		(	mtype->Modules, modulename)) != NULL) {

			if (DEBUGMODULE) {
				MLLog(ML_DEBUG, "Module %s already loaded"
				,	ModulePath);
			}
			DELETE(ModulePath);
			return ML_EXIST;
		}
		if (DEBUGMODULE) {
			MLLog(ML_DEBUG, "ModuleType %s already present"
			,	moduletype);
		}
	}else{
		if (DEBUGMODULE) {
			MLLog(ML_DEBUG, "Creating ModuleType for %s"
			,	moduletype);
		}
		/* Create a new MLModuleType object */
		mtype = NewMLModuleType(universe, moduletype);
	}

	g_assert(mtype != NULL);

	/*
	 * At this point, we have a MLModuleType object and our
	 * module name is not listed in it.
	 */

	dlhand = lt_dlopen(ModulePath);
	DELETE(ModulePath);

	if (!dlhand) {
		if (DEBUGMODULE) {
			MLLog(ML_DEBUG
			,	"lt_dlopen() failure on module %s/%s."
			" Reason: [%s]"
			,	moduletype, modulename
			,	lt_dlerror());
		}
		return ML_NOMODULE;
	}
	/* Construct the magic init function symbol name */
	ModuleSym = g_strdup_printf(ML_FUNC_FMT
	,	moduletype, modulename);
	if (DEBUGMODULE) {
		MLLog(ML_DEBUG, "Module %s/%s  init function: %s"
		,	moduletype, modulename
		,	ModuleSym);
	}

	initfun = lt_dlsym(dlhand, ModuleSym);
	DELETE(ModuleSym);

	if (initfun == NULL) {
		if (DEBUGMODULE) {
			MLLog(ML_DEBUG, "Module %s/%s init function not found"
			,	moduletype, modulename);
		}
		lt_dlclose(dlhand); dlhand=NULL;
		return ML_NOMODULE;
	}
	/*
	 *	Construct the new MLModule object
	 */
	modinfo = NewMLModule(mtype, modulename, dlhand, initfun);
	g_assert(modinfo != NULL);
	g_hash_table_insert(mtype->Modules, modinfo->module_name, modinfo);
	if (DEBUGMODULE) {
		MLLog(ML_DEBUG, "Module %s/%s loaded and constructed."
		,	moduletype, modulename);
	}
	if (DEBUGMODULE) {
		MLLog(ML_DEBUG, "Calling init function in module %s/%s."
		,	moduletype, modulename);
	}
	/* Save away the user_data for later */
	modinfo->ud_module = module_user_data;
	initfun(modinfo, universe->imports, module_user_data);

	return ML_OK;
}/*MLLoadModule*/

#define REPORTERR(msg)	MLLog(ML_CRIT, "%s", msg)

/*
 *	Register a plugin.
 *
 *	This function is exported to modules for their use.
 */
static ML_rc
MLRegisterPlugin(MLModule* modinfo
,	const char *	plugintype	/* Type of plugin	*/
,	const char *	pluginname	/* Name of plugin	*/
,	void*		Ops		/* Info (functions) exported
					   by this plugin	*/
,	MLPluginFun	close_func	/* Close function for plugin */
,	MLPlugin**	pluginid	/* Plugin id 	(OP)	*/
,	void**		Imports		/* Functions imported by
					 this plugin	(OP)	*/
,	void*		ud_plugin	/* Optional user_data */
)
{
	MLModuleUniv*	moduniv;	/* Universe this module is in */
	MLModuleType*	modtype;	/* Type of this module */
	MLPluginUniv*	piuniv;		/* Universe this plugin is in */
	MLPluginType*	pitype;		/* Type of this plugin */
	MLPlugin*	piinfo;		/* Info about this Plugin */

	MLPluginType*	pipitype;	/* MLPluginType for PLUGIN_PLUGIN */
	MLPlugin*	pipiinfo;	/* Plugin info for "plugintype" */
	const MLPluginOps* piops;	/* Ops vector for PluginPlugin */
					/* of type "plugintype" */
	ML_rc		rc;

	if (	 modinfo == NULL
	||	(modtype = modinfo->moduletype)	== NULL
	||	(moduniv = modtype->moduniv)	== NULL
	||	(piuniv = moduniv->piuniv)	== NULL
	||	piuniv->pitypes	== NULL
	) {
		REPORTERR("bad parameters");
		return ML_INVAL;
	}

	/* Now we have lots of info, but not quite enough... */

	if ((pitype = g_hash_table_lookup(piuniv->pitypes, plugintype))
	==	NULL) {

		/* Try to autoload the needed plugin handler */
		(void)MLLoadModule(moduniv, PLUGIN_PLUGIN, plugintype, NULL);

		/* See if the plugin handler loaded like we expect */
		if ((pitype = g_hash_table_lookup(piuniv->pitypes
		,	plugintype)) ==	NULL) {
			return ML_BADTYPE;
		}
	}
	if ((piinfo = g_hash_table_lookup(pitype->plugins, pluginname))
	!=	NULL) {
		g_warning("Attempt to register duplicate plugin: %s/%s"
		,	plugintype, pluginname);
		return ML_EXIST;
	}
	/*
	 * OK...  Now we know it is valid, and isn't registered...
	 * Let's locate the PluginPlugin registrar for this type
	 */
	if ((pipitype = g_hash_table_lookup(piuniv->pitypes, PLUGIN_PLUGIN))
	==	NULL) {
		REPORTERR("No " PLUGIN_PLUGIN " type!");
		return ML_OOPS;
	}
	if ((pipiinfo = g_hash_table_lookup(pipitype->plugins, plugintype))
	==	NULL) {
		MLLog(ML_CRIT
		,	"No plugin manager for given type (%s) !"
		,	plugintype);
		return ML_BADTYPE;
	}

	piops = pipiinfo->exports;

	/* Now we have all the information anyone could possibly want ;-) */

	piinfo = NewMLPlugin(pitype, pluginname, Ops, close_func, ud_plugin);
	g_hash_table_insert(pitype->plugins, g_strdup(pluginname), piinfo);
	*pluginid = piinfo;

	/* Call the registration function for our plugin type */
	rc = piops->RegisterPlugin(piinfo, Imports);

	/* Increment reference count of plugin manager */
	PiModRefCount(pipiinfo, 1);
	return rc;
}

/*
 * Method:
 *
 *	Verify plugin is valid.
 *
 *	Call plugin close function.
 *
 *	Call plugin manager unregister function
 *
 *	Call RmAMLPlugin to remove from PluginType table, and
 *		free plugin object.
 *
 */

static ML_rc
MLunregister_plugin(MLPlugin* id)
{
	MLPluginType*	t;
	MLPluginUniv*	u;
	ML_rc		rc;
	MLPlugin*	pipi_info;	/* Pointer to our plugin handler */
	const MLPluginOps* exports;	/* PluginPlugin operations  for the
					 * type of plugin we are
					 */

	if (	 id == NULL
	||	(t = id->plugintype) == NULL
	||	(u = t->universe) == NULL
	|| 	id->pluginname == NULL
	||	id->pi_close == NULL) {
		MLLog(ML_WARN, "MLunregister_plugin: bad pluginid");
		return ML_INVAL;
	}


	/* Call the close function supplied by the plugin */

	if ((rc=id->pi_close(id, id->ud_plugin)) != ML_OK) {
		MLLog(ML_WARN, "PluginClose on %s/%s returned %s"
		,	t->typename, id->pluginname
		,	ML_strerror(rc));
	}

	/* Find the PluginPlugin that manages us */
	pipi_info = t->pipi_ref;

	g_assert(pipi_info != NULL);

	/* Find the exported functions from that PIPI */
	exports =  pipi_info->exports;

	g_assert(exports != NULL && exports->UnRegisterPlugin != NULL);

	/* Call the plugin manager unregister function */
	exports->UnRegisterPlugin(id);

	RmAMLPlugin(id->pluginname, id, NULL);

	/* Decrement reference count of plugin manager */
	PiModRefCount(pipi_info, -1);

	return rc;
}

static MLPluginUniv*
NewMLPluginUniv(MLModuleUniv* moduniv)
{
	MLPluginUniv*	ret = NEW(MLPluginUniv);

	/* Make the two universes point at each other */
	ret->moduniv = moduniv;
	moduniv->piuniv = ret;

	ret->pitypes = g_hash_table_new(g_str_hash, g_str_equal);

	PluginPlugin_module_init(moduniv);
	return ret;
}

static void
DelMLPluginUniv(MLPluginUniv* piuniv)
{
	g_assert(piuniv!= NULL && piuniv->pitypes != NULL);

	if (g_hash_table_size(piuniv->pitypes) > 0) {
		MLLog(ML_CRIT, "DelMLPluginUniv: pitypes not empty");
	}
	g_hash_table_foreach(piuniv->pitypes, RmAMLPluginType, NULL);
	g_hash_table_destroy(piuniv->pitypes);
	piuniv->pitypes = NULL;
	piuniv->moduniv = NULL;
}

/*
 *	These RmA* functions primarily called from hash_table_foreach, 
 *	so they have gpointer arguments.  This *not necessarily* clause
 *	is why they do the g_hash_table_lookup_extended call instead of
 *	just deleting the key.  When called from outside, the key
 *	may not be pointing at the key to actually free, but a copy
 *	of the key.
 */
static void	/* IsA GHFunc: required for g_hash_table_foreach() */
RmAMLPluginType
(	gpointer typename	/* Name of this plugin type  */
,	gpointer pitype		/* MLPluginType* */
,	gpointer notused
)
{
	gpointer	key;
	MLPluginType*	Pitype = pitype;
	MLPluginUniv*	Piuniv = Pitype->universe;

	/*
	 * We are not always called by g_hash_table_foreach()
	 */

	if (g_hash_table_lookup_extended(Piuniv->pitypes
	,	typename, &key, &pitype)) {

		g_hash_table_remove(Piuniv->pitypes, key);
		DelMLPluginType(pitype);
		DELETE(key);
	}else{
		g_assert_not_reached();
	}
}

static int
MLModrefcount(MLModuleType* mtype, const char * modulename)
{
	MLModule*	modinfo;

	if ((modinfo = g_hash_table_lookup(mtype->Modules, modulename))
	==	NULL) {
		return -1;
	}
	return modinfo->refcnt;
}
static int
MLModmodrefcount(MLModuleType* mtype, const char * modulename
,	int plusminus)
{
	MLModule*	modinfo;

	if ((modinfo = g_hash_table_lookup(mtype->Modules, modulename))
	==	NULL) {
		return -1;
	}
	if ((modinfo->refcnt += plusminus) < 0) {
		modinfo->refcnt = 0;
	}
	return modinfo->refcnt;
}


/*
 * We need to write more functions:  These include...
 *
 * Module functions:
 *
 * MLModulePath()	- returns path name for a given module
 *
 * MLModuleTypeList()	- returns list of modules of a given type
 *
 */
static void free_dirlist(struct dirent** dlist, int n);

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

static int
so_select (const struct dirent *dire)
{ 
    
	const char obj_end [] = MODULESUFFIX;
	const char *end = &dire->d_name[strlen(dire->d_name)
	-	(STRLEN(obj_end))];
	
	
	if (DEBUGMODULE) {
		MLLog(ML_DEBUG, "In so_select: %s.", dire->d_name);
	}
	if (obj_end < dire->d_name) {
			return 0;
	}
	if (strcmp(end, obj_end) == 0) {
		if (DEBUGMODULE) {
			MLLog(ML_DEBUG, "FILE %s looks like a module name."
			,	dire->d_name);
		}
		return 1;
	}
	if (DEBUGMODULE) {
		MLLog(ML_DEBUG
		,	"FILE %s Doesn't look like a module name [%s] "
		"%d %d %s."
		,	dire->d_name, end
		,	sizeof(obj_end), strlen(dire->d_name)
		,	&dire->d_name[strlen(dire->d_name)
		-	(sizeof(obj_end)-1)]);
	}
	
	return 0;
}

/* Return (sorted) list of available module names */
static char**
MLModTypeListModules(MLModuleType* mtype
,	int *		modcount	/* Can be NULL ... */)
{
	const char *	basedir = mtype->moduniv->rootdirectory;
	const char *	modclass = mtype->moduletype;
	GString*	path;
	char **		result = NULL;
	struct dirent**	files;
	int		modulecount;
	int		j;


	path = g_string_new(basedir);
	if (modclass) {
		if (g_string_append_c(path, G_DIR_SEPARATOR) == NULL
		||	g_string_append(path, modclass) == NULL) {
			g_string_free(path, 1); path = NULL;
			return(NULL);
		}
	}

	modulecount = scandir(path->str, &files
	,	SCANSEL_CAST &so_select, NULL);
	g_string_free(path, 1); path=NULL;

	result = (char **) g_malloc((modulecount+1)*sizeof(char *));

	for (j=0; j < modulecount; ++j) {
		char*	s;
		int	slen = strlen(files[j]->d_name)
		-	STRLEN(MODULESUFFIX);

		s = g_malloc(slen+1);
		strncpy(s, files[j]->d_name, slen);
		s[slen] = EOS;
		result[j] = s;
	}
	result[j] = NULL;
	FREE_DIRLIST(files, modulecount);

	/* Return them in sorted order... */
	qsort(result, modulecount, sizeof(char *), qsort_string_cmp);

	if (modcount != NULL) {
		*modcount = modulecount;
	}

	return(result);
}

void
MLFreeModuleList(char ** modulelist)
{
	char **	ml = modulelist;

	while (*ml != NULL) {
		DELETE(*ml);
	}
	DELETE(modulelist);
}

