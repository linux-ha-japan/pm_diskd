#include <portability.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

/* Dumbness... */
#define time FooTimeParameter
#define index FooIndexParameter
#	include <glib.h>
#undef time
#undef index


#include <upmls/MLPlugin.h>
#include "../../libltdl/config.h"

#ifdef BSD
#	define SCANSEL_C	(void *)
#else
#	define SCANSEL_C	/* Nothing */
#endif


#define MODULESUFFIX	LTDL_SHLIB_EXT

static int	ModuleDebugLevel = 0;

#define DEBUGMODULE	(ModuleDebugLevel > 0)

static const char * ml_module_version(void);
static const char * ml_module_name(void);


static ML_rc PluginPlugin_module_init(MLModuleUniv* univ);

static int ml_GetDebugLevel(void);
static void ml_SetDebugLevel (int level);
static void ml_close (MLModule*);
static char** MLModTypeListModules(MLModuleType* mtype, int* modcount);


void	DelMLModuleUniv(MLModuleUniv*);
static void	DelAMLModuleType
(	gpointer modtname	/* Name of this module type */
,	gpointer modtype	/* MLModuleType* */
,	gpointer NotUsed
);

static MLModuleType* NewMLModuleType
(	MLModuleUniv* moduleuniv
,	const char *	moduletype
);
static void	DelMLModuleType(MLModuleType*);
static void	DelAMLModule
(	gpointer modname	/* Name of this module  */
,	gpointer module		/* MLModule* */
,	gpointer NotUsed
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
static void		DelAMLPluginType
(	gpointer typename	/* Name of this plugin type  */
,	gpointer pitype		/* MLPluginType* */
,	gpointer hash		/* Hash table */
);

static MLPluginType*	NewMLPluginType
(	MLPluginUniv*
,	const char * typename
,	void* pieports, void* user_data
);
static void		DelMLPluginType(MLPluginType*);
static void		DelAMLPlugin
(	gpointer piname		/* Name of this plugin */
,	gpointer module		/* MLPlugin* */
,	gpointer hash
);

static MLPlugin*	NewMLPlugin
(	MLPluginType*	plugintype
,	const char*	pluginname
,	void *		exports
,	void*		ud_plugin
);
static void		DelMLPlugin(MLPlugin*);



static void close_a_plugin
(	gpointer pluginname	/* Name of this plugin (not used) */
,	gpointer vplugin	/* MLPlugin we want to close */
,	gpointer NotUsed
);


static MLModuleOps ModExports =
{	ml_module_version
,	ml_module_name
,	ml_GetDebugLevel
,	ml_SetDebugLevel
,	ml_close
};

static ML_rc MLregister_module(MLModule* modinfo, MLModuleOps* commonops);
static ML_rc MLunregister_module(MLModule* modinfo);
static ML_rc
MLRegisterAPlugin
(	MLModule*	modinfo
,	const char *	plugintype	/* Type of plugin	*/
,	const char *	pluginname	/* Name of plugin	*/
,	void*		Ops		/* Info (functions) exported
					   by this plugin	*/
,	void**		pluginid	/* Plugin id 	(OP)	*/
,	void**		Imports		/* Functions imported by
					 this plugin	(OP)	*/
,	void*		ud_plugin	/* plugin user data */
);

static ML_rc	MLunregister_plugin(void* pluginid);
static void	MLLog(MLLogLevel priority, const char * fmt, ...);


/* FIXME!! */
static MLModuleImports MLModuleImportSet =
{	MLregister_module	/* register_module */
,	MLunregister_module	/* unregister_module */
,	MLRegisterAPlugin	/* register_plugin */
,	MLunregister_plugin	/* unregister_plugin */
,	MLLoadModule		/* load_module */
,	MLLog			/* Logging function */
};

static MLPlugin*	pipi_register_plugin(MLPluginType* env
				,	const char * pluginname
				,	void * exports, void * ud_plugin
				,	void** imports);
static ML_rc		pipi_unregister_plugin(MLPlugin* plugin);
static ML_rc		pipi_close_plugin(MLPlugin* plugin
				,	MLPlugin* pi2);

static MLPluginType*	pipi_new_plugintype(MLPluginUniv*);
static void		pipi_del_plugintype(MLPluginType*);

static void		pipi_del_while_walk(gpointer key, gpointer value
,				gpointer user_data);

/*
 *	Functions exported by the Plugin plugins whose name is plugin
 *	(The PluginPlugin plugin)
 */

static MLPluginOps  PiExports =
{		pipi_register_plugin
	,	pipi_unregister_plugin	/* Whack!! (?) FIXME(?) */
	,	pipi_close_plugin
	,	pipi_new_plugintype
	,	pipi_del_plugintype
};

static int PiRefCount(MLPlugin * pih);
static int PiModRefCount(MLPlugin*epiinfo,int plusminus);
static void PiUnloadIfPossible(MLPlugin *epiinfo);


static MLPluginImports PIHandlerImports = {
	PiRefCount,
	PiModRefCount,
	PiUnloadIfPossible,
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
	MLModule*	ret = g_new(MLModule, 1);
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
	g_free(mod->module_name);
	mod->module_name=NULL;

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

	g_free(mod); mod=NULL;
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
	MLModuleType*	ret = g_new(MLModuleType, 1);

	*ret = dummymlmtype;

	ret->moduletype = g_strdup(moduletype);
	ret->moduniv = moduleuniv;
	ret->Modules = g_hash_table_new(g_str_hash, g_str_equal);
	return ret;
}
static void
DelMLModuleType(MLModuleType*mtype)
{
	g_free(mtype->moduletype);
	mtype->moduletype=NULL;

	if (g_hash_table_size(mtype->Modules) > 0) {
		MLLog(ML_CRIT, "DelMLModuleType: Modules not empty");
	}
	g_hash_table_foreach(mtype->Modules, DelAMLModule, mtype->Modules);
	g_hash_table_destroy(mtype->Modules);
	mtype->Modules = NULL;
	mtype->moduniv = NULL;
	g_free(mtype);	mtype=NULL;
}
static void
DelAMLModule	/* IsA GHFunc: required for g_hash_table_foreach() */
(	gpointer modname	/* Name of this module  */
,	gpointer module		/* MLModule* */
,	gpointer hash
)
{
	g_hash_table_remove(hash, modname);
	DelMLModule(module);
	g_free(modname);
}

MLModuleUniv*
NewMLModuleUniv(const char * basemoduledirectory)
{
	MLModuleUniv*	ret = g_new(MLModuleUniv, 1);

	if (!g_path_is_absolute(basemoduledirectory)) {
		g_free(ret); ret = NULL;
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

	g_free(moduniv->rootdirectory);
	moduniv->rootdirectory = NULL;

	if (g_hash_table_size(moduniv->ModuleTypes) > 0) {
		MLLog(ML_CRIT, "DelMLModuleUniv: ModuleTypes not empty");
	}
	g_hash_table_foreach(moduniv->ModuleTypes
	,	DelAMLModuleType, moduniv->ModuleTypes);
	g_hash_table_destroy(moduniv->ModuleTypes);
	moduniv->ModuleTypes = NULL;
	DelMLPluginUniv(moduniv->piuniv);
	moduniv->piuniv = NULL;
	moduniv->imports = NULL;
	g_free(moduniv);	moduniv=NULL;
}

static void	/* IsA GHFunc: required for g_hash_table_foreach() */
DelAMLModuleType
(	gpointer modtname	/* Name of this module type */
,	gpointer modtype	/* MLModuleType* */
,	gpointer hash
)
{
	g_hash_table_remove(hash, modtname);
	DelMLModuleType(modtype);
	g_free(modtname);
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

	modtype = NewMLModuleType(univ, PLUGIN_PLUGIN);

	pipi_module= NewMLModule(modtype, PLUGIN_PLUGIN, NULL, NULL);

	/* We can call register_module, since it doesn't depend on us... */
	rc = imports->register_module(pipi_module, &ModExports);
	if (rc != ML_OK) {
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

	/* The first argument is the MLPluginType */
	piinfo = pipi_register_plugin(pitype, PLUGIN_PLUGIN, &PiExports
	,	NULL, &dontcare);

	/* FIXME (unfinished module) */
	return(ML_OK);
}/*PluginPlugin_module_init*/


/* Return current PiPi "module" version (not very interesting for us) */
static const char *
ml_module_version(void)
{
	return("1.0");
}

/* Return current PiPi "module" name (not very interesting for us) */
static const char *
ml_module_name(void)
{
	return(PLUGIN_PLUGIN);
}

/* Return current PiPi debug level */
static int
ml_GetDebugLevel(void)
{
	return(ModuleDebugLevel);
}

/* Set current PiPi debug level */
static void
ml_SetDebugLevel (int level)
{
	ModuleDebugLevel = level;
}

/* Close/shutdown the Module */
static void
ml_close (MLModule* module)
{
	/* Need to find all the plugins associated with this Module...  */
	GHashTable*	pi = module->Plugins;

	/* Try to close each plugin associated with this module */
	g_hash_table_foreach(pi, close_a_plugin, NULL);

	/* FIXME:  There is no doubt more cleanup work to do... */

	/*
	 * In particular, we need to check our reference count
	 * and unload our module if we can (?)
	 */
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
		g_hash_table_insert(plugintype->plugins
		,	g_strdup(ret->pluginname), ret);
		ret->refcnt = 0;
	}
	return ret;
}
static void
DelMLPlugin(MLPlugin* pi)
{
	g_free(pi->pluginname);
	pi->pluginname = NULL;
	if (pi->refcnt > 0) {
		MLLog(ML_CRIT, "DelMLPlugin: refcnt not zero");
	}
	g_free(pi); pi = NULL;
}

static MLPluginType*
NewMLPluginType(MLPluginUniv*univ, const char * typename
,	void* pieports, void* user_data)
{
	MLPluginType*	pipi_types;
	MLPlugin*	pipi_ref;
	MLPluginType*	ret = g_new(MLPluginType, 1);
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
	g_hash_table_foreach(pit->plugins, DelAMLPlugin, pit->plugins);
	g_hash_table_destroy(pit->plugins);
	pit->ud_pi_type = NULL;
	pit->universe = NULL;
	pit->pipi_ref = NULL;

	g_free(pit); pit = NULL;
}

static void	/* IsAGHFunc: required for g_hash_table_foreach() */
DelAMLPlugin
(	gpointer piname	/* Name of this plugin */
,	gpointer pi	/* MLPlugin* */
,	gpointer hash	/* hash table */
)
{
	g_hash_table_remove(hash, piname);
	g_free(piname);
	DelMLPlugin(pi);
}

/*
 * close_a_plugin:	(forcibly) close (shutdown) a plugin if possible
 *
 * Although this code deals with plugins, strangely enough its
 * part of the module code...
 *
 * The observant reader will note that this code looks pretty indirect.
 *
 * The reason why it's so indirect is that we have no knowledge of how to
 * shut down a plugin of a given type.  However, every PluginPlugin
 * (plugin manager) exports an interface which will close down any plugin
 * of the type it manages.
 *
 * It is required for it to understand the Ops which plugins of the type it
 * manages export so it will be able to know how to do that.
 *
 * The interface of this module is dicated by what g_hash_table_foreach()
 * expects.
 */

static void	/* IsA GHFunc: required for g_hash_table_foreach() */
close_a_plugin
(	gpointer pluginname	/* Name of this plugin (not used) */
,	gpointer vplugin	/* MLPlugin we want to close */
,	gpointer NotUsed
)
{
	MLPlugin*	plugin = vplugin;
	MLPluginType*	pitype;		/* Our plugin type */
	MLPlugin*	pipi_ref;	/* Pointer to our plugin handler */
	MLPluginOps*	exports;	/* PluginPlugin operations  for the
					 * type of plugin we are
					 */

	pitype =  plugin->plugintype;	/* Find our base plugin type */
	g_assert(pitype != NULL);

	/* Find the PluginPlugin that manages us */
	pipi_ref = pitype->pipi_ref;

	g_assert(pipi_ref != NULL);

	/* Find the exported functions from that PIPI */
	exports =  pipi_ref->exports;

	g_assert(exports != NULL && exports->CloseOurPI != NULL);

	/* Now, ask that function to shut us down properly and close us up */
	exports->CloseOurPI(pipi_ref, plugin);
}


/* Register a Plugin Plugin */
static MLPlugin*
pipi_register_plugin(MLPluginType* pitype
	,	const char * pluginname, void * exports, void * user_data
	,	void**		imports)
{
	MLPlugin* ret;

	if (g_hash_table_lookup(pitype->plugins, pluginname) != NULL) {
		return NULL;
	}
	ret = NewMLPlugin(pitype, pluginname, exports, user_data);
	g_hash_table_insert(pitype->plugins, g_strdup(pluginname), ret);
	*imports = &PIHandlerImports;
	return ret;
}

/* Unregister a Plugin Plugin - whack it (is this right? FIXME?)*/
static ML_rc
pipi_unregister_plugin(MLPlugin* plugin)
{
	MLPluginType*	pitype = plugin->plugintype;
	/* HOW DO WE FREE THE KEY? FIXME!! */
	g_hash_table_remove(pitype->plugins, plugin->pluginname);
	DelMLPlugin(plugin);
	return ML_OK;
}

/* I'm a little confused: FIXME (ALR) */

/* Close / UnRegister a Plugin Plugin of type Plugin */
static ML_rc
pipi_close_plugin(MLPlugin* basepi, MLPlugin*plugin)
{
	/* Basepi and plugin ought to be the same for us... */
	g_assert(basepi == plugin);
	return pipi_unregister_plugin(basepi);
}

/* Create a new MLPluginType object for PLUGINPLUGIN */
static MLPluginType*
pipi_new_plugintype(MLPluginUniv* pluginuniv)
{
	MLPluginType*	ret = g_new(MLPluginType, 1);

	ret->ud_pi_type = NULL;
	ret->universe = pluginuniv;

	/* FIXME: Need to set pipi_ref - our managing plugin plugin */
	/* (kind of a pain) */
	ret->pipi_ref = NULL;

	ret->plugins = g_hash_table_new(g_str_hash, g_str_equal);

	if (!ret->plugins) {
		DELETE(ret);
		ret = NULL;
	}
	return(ret);
}

/* Destroy a MLPluginType object - Whack it! */
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
	DelMLPlugin((MLPlugin*)value); value = NULL;
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
	if (epiinfo->refcnt < 0) {
		epiinfo = 0;
	}
	return epiinfo->refcnt;
}

static void
PiUnloadIfPossible(MLPlugin *epiinfo)
{
	/*FIXME!*/
}

static ML_rc
MLregister_module(MLModule* modinfo, MLModuleOps* commonops)
{
	/*FIXME!*/
	return ML_OOPS;
}

static ML_rc
MLunregister_module(MLModule* modinfo)
{
	/*FIXME!*/
	return ML_OOPS;
}

static ML_rc
MLunregister_plugin(void* pluginid)
{
	/*FIXME!*/
	return ML_OOPS;
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
,	const char * modulename)
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

	if (access(ModulePath, R_OK|X_OK) != 0) {
		g_free(ModulePath); ModulePath=NULL;
		return ML_NOMODULE;
	}
	if((mtype=g_hash_table_lookup(universe->ModuleTypes, moduletype))
	!= NULL) {
		if ((modinfo = g_hash_table_lookup
		(	mtype->Modules, modulename)) != NULL) {
			g_free(ModulePath); ModulePath=NULL;
			return ML_EXIST;
		}

	}else{
		/* Create a new MLModuleType object */
		mtype = NewMLModuleType(universe, moduletype);
	}

	g_assert(mtype != NULL);

	/*
	 * At this point, we have a MlModuleType object and our
	 * module name is not listed in it.
	 */

	dlhand = lt_dlopen(ModulePath);
	g_free(ModulePath); ModulePath=NULL;

	if (!dlhand) {
		return ML_NOMODULE;
	}
	/* Construct the magic init function symbol name */
	ModuleSym = g_strdup_printf(ML_FUNC_FMT
	,	moduletype, modulename);

	initfun = lt_dlsym(dlhand, ModuleSym);
	g_free(ModuleSym); ModuleSym=NULL;

	if (initfun == NULL) {
		lt_dlclose(dlhand); dlhand=NULL;
		return ML_NOMODULE;
	}
	/*
	 *	Construct the new MLModule object
	 */
	modinfo = NewMLModule(mtype, modulename, dlhand, initfun);
	g_assert(modinfo != NULL);
	g_hash_table_insert(mtype->Modules, modinfo->module_name, modinfo);

	return ML_OK;
}/*MLLoadModule*/

#define REPORTERR(msg)	MLLog(ML_CRIT, "ERROR: %s", msg)

/*
 *	It may be the case that this function needs to be split into
 *	a couple of functions in order to avoid code duplication
 */

static ML_rc
MLRegisterAPlugin(MLModule* modinfo
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
	MLModuleUniv*	moduniv;	/* Universe this module is in */
	MLModuleType*	modtype;	/* Type of this module */
	MLPluginUniv*	piuniv;		/* Universe this plugin is in */
	MLPluginType*	pitype;		/* Type of this plugin */
	MLPlugin*	piinfo;		/* Info about this Plugin */

	MLPluginType*	pipitype;	/* MLPluginType for PLUGIN_PLUGIN */
	MLPlugin*	pipiinfo;	/* Plugin info for "plugintype" */
	MLPluginOps*	piops;		/* Ops vector for PluginPlugin */
					/* of type "plugintype" */

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
		(void)MLLoadModule(moduniv, PLUGIN_PLUGIN, plugintype);

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
		REPORTERR("No " PLUGIN_PLUGIN " plugin for given type!");
		return ML_BADTYPE;
	}

	piops = pipiinfo->exports;

	/* Now we have all the information anyone could possibly want ;-) */

	/* Call the registration function for our plugin type */
	piinfo = piops->RegisterPlugin(pitype, pluginname, Ops
	,	ud_plugin
	,	Imports);
	*pluginid = piinfo;

	/* FIXME! need to increment the ref count for plugin type */
	return (piinfo == NULL ? ML_OOPS : ML_OK);
}

static MLPluginUniv*
NewMLPluginUniv(MLModuleUniv* moduniv)
{
	MLPluginUniv*	ret = g_new(MLPluginUniv, 1);

	ret->moduniv = moduniv;
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
	g_hash_table_foreach(piuniv->pitypes
	,	DelAMLPluginType, piuniv->pitypes);
	g_hash_table_destroy(piuniv->pitypes);
	piuniv->pitypes = NULL;
	piuniv->moduniv = NULL;
}

static void	/* IsA GHFunc: required for g_hash_table_foreach() */
DelAMLPluginType
(	gpointer typename	/* Name of this plugin type  */
,	gpointer pitype		/* MLPluginType* */
,	gpointer hash		/* Hash table */
)
{
	g_hash_table_remove(hash, typename);
	g_free(typename);
	DelMLPluginType(pitype);
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

