#ifndef UPMLS_MLPLUGIN_H
#  define UPMLS_MLPLUGIN_H
#  ifndef UPMLS_MLMODULE_H
#    include <upmls/MLModule.h>
#  endif

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
typedef struct MLPluginImports_s	MLPluginImports;



/*
 *	MLPlugin (AKA struct MLPlugin_s) holds the information
 *	we use to track a single plugin handler.
 */

struct MLPlugin_s {
	MLPluginType*		plugintype;
	char *			pluginname;
	const void*		exports;	/* Exported Functions	*/
						/* for this plugin	*/
	void*			ud_plugin;	/* per-plugin user data */
	int			refcnt;		/* Reference count for module */
};
/*
 *	MLPluginType (AKA struct MLPluginType_s) holds the info
 *	we use to track the set of all plugin handlers of a single kind.
 */
struct MLPluginType_s {
	GHashTable*		plugins;
	void*			ud_pi_type;	/* per-plugin-type user data*/
	MLPluginUniv*		universe;	/* Pointer to parent (up) */
	MLPlugin*		pipi_ref;	/* Pointer to our plugin plugin */
};
struct MLPluginUniv_s{
	GHashTable*		pitypes;	/* containing
						 * MLPluginType objects
						 */
	struct MLModuleUniv_s*	moduniv;	/* parallel universe of
						 * modules
						 */
};

/*
 *
 * From here to the end is specific to the PluginPlugin type of Plugin...
 *
 */
typedef struct MLPluginOps_s		MLPluginOps;


/* Interfaces imported by a PIHandler plugin */
struct MLPluginImports_s { 

		/* Return current reference count */
	int (*RefCount)(MLPlugin * epiinfo);

		/* Incr/Decr reference count */
	int (*ModRefCount)(MLPlugin*epiinfo, int plusminus);

		/* Unload module associated with this plugin -- if possible */
	void (*UnloadIfPossible)(MLPlugin *epiinfo);

};

/* Interfaces exported by a Plugin plugin */
struct MLPluginOps_s{
/*
 *	These are the interfaces exported by a PluginPlugin to the
 *	plugin management infrastructure.  These are not imported
 *	by plugins - only the plugin management infrastructure.
 */

	/* RegisterPlugin - Returns unique id for plugin or NULL (fail) */
 	MLPlugin* (*RegisterPlugin)(MLPluginType* pienv
		,	const char * pluginname, const void * exports
		,	void *	ud_plugin
		,	const void**	imports);

	ML_rc	(*UnRegisterPlugin)(MLPlugin*ipiinfo); /* Unregister PI-PI*/
				/* And destroy MLPlugin object */

	/* Close a Plugin of the type we manage (not a PIPI ...) */
	ML_rc	(*CloseOurPI)(MLPlugin*pipiinfo, MLPlugin* ourpiinfo);

	/* Create a new MLPluginType object */
	MLPluginType*	(*NewPluginEnv)(MLPluginUniv* universe);

	/* Destroy a MLPluginType object */
	void		(*DelPluginEnv)(MLPluginType*);
};

#define	PLUGIN_PLUGIN	"Plugin"

#define NEW(type)		(g_new(type,1))
#define DELETE(obj)	{g_free(obj); obj = NULL;}

#endif /* UPMLS_MLPLUGIN_H */
