#include <upmls/MLModule.h>
#include <upmls/MLPlugin.h>

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
static ML_rc			pipi_close_plugin(MLPlugin* plugin, MLPlugin* pi2);

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
	ret = MLPlugin_new(univ, pluginname, exports, user_data);
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
 * We need to write lots more functions:  These include...
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
 * PluginPlugin functions:
 *
 *
 */
