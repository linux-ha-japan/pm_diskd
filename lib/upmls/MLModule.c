#include <unistd.h>
#include <upmls/MLPlugin.h>
#include "../../libltdl/config.h"

static MLModuleImports MLModuleImportSet;/* UNINITIALIZED: FIXME! */

static void MLPlugin_del(MLPlugin*	plugintype);


static int	debuglevel = 0;

static const char * ml_module_version(void);
static const char * ml_module_name(void);
/* Cannot be static -- needed for bootstrapping! */
ML_rc PluginPlugin_module_init(MLModuleImports* imports, MLModuleUniv* env
		);
static int ml_GetDebugLevel(void);
static void ml_SetDebugLevel (int level);
static void ml_close (MLModule*);

extern MLModule* NewMLModule(MLModuleType* mtype
	,	const char *	module_name
	,	lt_dlhandle	dlhand
	,	MLModuleInitFun ModuleSym);

extern MLModuleType* NewMLModuleType(MLModuleUniv* moduleuniv
	,	const char *	moduletype
	,	MLModuleImports * imports
	
);
static MLPlugin* NewMLPlugin(MLPluginType*	plugintype
	,	const char*	pluginname
	,	void *		exports
	,	void*		ud_plugin
);

MLPluginType* NewMLPluginType(MLPluginUniv*, const char * typename
,	void* pieports, void* user_data);


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
static ML_rc			pipi_close_plugin(MLPlugin* plugin
				,	MLPlugin* pi2);

static MLPluginType*	pipi_new_plugintype(MLPluginUniv*);
static void			pipi_del_plugintype(MLPluginType*);
static void pipi_del_while_walk(gpointer key, gpointer value
,				gpointer user_data);

static MLPluginOps  PiExports =
{		pipi_register_plugin
	,	pipi_unregister_plugin
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

/*
 *	PluginPlugin_module_init: Initialize the handling of "Plugin" plugins.
 *
 *	There are a few potential bootstrapping problems here ;-)
 *
 */
ML_rc
PluginPlugin_module_init(MLModuleImports* imports, MLModuleUniv* univ)
{
	MLModuleType*	modtype;
	MLPlugin*	piinfo;
	MLPluginType*	pitype;
	void*		dontcare;
	MLModule*	pipi_module;
	ML_rc		rc;

	if (univ->piuniv) {
		return ML_INVAL;
	}

	pitype = NewMLPluginType(univ->piuniv, PLUGIN_PLUGIN, &PiExports, NULL);
	modtype = NewMLModuleType(univ, PLUGIN_PLUGIN, imports);
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
	 * The function to call is pipi_register_plugin()...
	 */

	/* The first argument is the MLPluginType */
	piinfo = pipi_register_plugin(pitype, PLUGIN_PLUGIN, &PiExports
	,	NULL, &dontcare);

	/* FIXME (unfinished module) */
	return(ML_OK);
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

static void
ml_SetDebugLevel (int level)
{
	debuglevel = level;
}

/*
 * close_a_plugin:	(forcibly) close (shutdown) a plugin if possible
 *
 * The observant reader will note that this code looks pretty indirect.
 *
 * The reason why it's so indirect is that we have no knowledge of how to shut down
 * a plugin of a given type.  However, every PluginPlugin (plugin manager) exports
 * an interface which will close down any plugin of the type it manages.
 *
 * It is required for it to understand the Ops which plugins of the type it
 * manages export so it will be able to know how to do that.
 */

static void	/* Interface is a GHashTable: required for g_hash_table_foreach() */
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

	pipi_ref = pitype->pipi_ref;	/* Find the PluginPlugin that manages us */
	g_assert(pipi_ref != NULL);

	exports =  pipi_ref->exports;	/* Find the exported functions from that PIPI */

	g_assert(exports != NULL && exports->CloseOurPI != NULL);

	/* Now, ask that interface to shut us down properly and close us up */
	exports->CloseOurPI(pipi_ref, plugin);
}
static void
ml_close (MLModule* module)
{
	/* Need to find all the plugins associated with this Module...  */
	GHashTable*	pi = module->Plugins;

	/* Close every plugin associated with this module */
	g_hash_table_foreach(pi, close_a_plugin, NULL);
	/* FIXME:  There is no doubt more cleanup work to do... */
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
	ret = NewMLPlugin(univ, pluginname, exports, user_data);
	g_hash_table_insert(univ->plugins, g_strdup(pluginname), ret);
	*imports = &PIHandlerImports;
	return ret;
}

static ML_rc
pipi_close_plugin(MLPlugin* basepi, MLPlugin*plugin)
{
	/* Basepi and plugin ought to be the same for us... */
	g_assert(basepi == plugin);
	return pipi_unregister_plugin(basepi);
}
static ML_rc
pipi_unregister_plugin(MLPlugin* plugin)
{
	MLPluginType*	univ = plugin->plugintype;
	g_hash_table_remove(univ->plugins, plugin->pluginname);
	MLPlugin_del(plugin);
	return ML_OK;
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
NewMLPlugin(MLPluginType*	plugintype
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
		g_hash_table_insert(plugintype->plugins
		,	g_strdup(ret->pluginname), ret);
		ret->refcnt = 0;
	}
	return ret;
}

MLPluginType*
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
		mtype = NewMLModuleType(universe, moduletype
		,	&MLModuleImportSet);
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
}

/*
 *	It may be the case that this function needs to be split into
 *	a couple of functions in order to avoid code duplication
 */

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

	/* FIXME! Probably need to do something with rc from RegisterPlugin */
	/* FIXME! need to increment the ref count for plugin type */
	return (piinfo == NULL ? ML_OOPS : ML_OK);
}

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

/*
 * We need to write lots more functions:  These include...
 *
 * Module functions:
 *
 * MLModulePath()	- returns path name for a given module
 *
 * MLModuleTypeList()	- returns list of modules of a given type
 *
 * MLLoadPluginModule()	- Loads the PluginPlugin Module for the given type
 * 
 * PluginPlugin functions:
 *
 *
 */
