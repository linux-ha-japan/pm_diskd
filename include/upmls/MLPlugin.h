#ifndef UPMLS_MLPLUGIN_H
#  define UPMLS_MLPLUGIN_H
#  ifndef UPMLS_MLMODULE_H
#    include <upmls/MLModule.h>
#  endif

/*****************************************************************************
 *
 * The most basic plugin type is the "PIManager" plugin.
 * Each plugin manager registers and deals with plugins of a given type.
 *
 * Such a plugin must be loaded before any modules of it's type can be loaded.
 *
 * In order to load any module of type "foo", we must load a plugin of type
 * "Plugin" named "foo".  This plugin then manages the registration of
 * all plugins of type foo.
 *
 * To bootstrap, we load a plugin of type "Plugin" named "Plugin"
 * during the initialization of the module system.
 *
 * PIManagers will be autoloaded if certain conditions are met...
 *
 * If a PIManager is to be autoloaded, it must be one plugin handler
 * per file, and the file named according to the type of the plugin it
 * implements, and loaded in the directory named PLUGIN_PLUGIN.
 * 
 */


/*
 *	I'm unsure exactly which of the following structures
 *	are needed to write a plugin, or a plugin manager.
 *	We'll get that figured out and scope the defintions accordingly...
 */

/*
 *	MLPlugin (AKA struct MLPlugin_s) holds the information
 *	we use to track a single plugin handler.
 */

struct MLPlugin_s {
	MLPluginType*		plugintype;	/* Parent pointer */
	char *			pluginname;	/* malloced plugin name */
	MLPlugin*		pimanager;
	void*			exports;	/* Exported Functions	*/
						/* for this plugin	*/
	MLPluginFun		pi_close;	/* Plugin close operation */
	void*			ud_plugin;	/* per-plugin user data */
	int			refcnt;		/* Reference count for module */
};
/*
 *	MLPluginType (AKA struct MLPluginType_s) holds the info
 *	we use to track the set of all plugins of a single kind.
 */
struct MLPluginType_s {
	char*			typename;	/* Our plugin type name */
	GHashTable*		plugins;	/* The set of plugins
						 * of our type.  The 
						 * "values" are all MLPlugin*
						 * objects */
	void*			ud_pi_type;	/* per-plugin-type user data*/
	MLPluginUniv*		universe;	/* Pointer to parent (up) */
	MLPlugin*		pipi_ref;	/* Pointer to our plugin
						   manager */
};

/*
 *	MLPluginUniv (AKA struct MLPluginUniv_s) holds the information
 *	for all plugins of all types.  From our point of view this is
 *	our universe ;-)
 */

struct MLPluginUniv_s{
	GHashTable*		pitypes;	/* 
						 * Set of Pluign Types
						 * The values are all
						 * MLPluginType objects
						 */
	struct MLModuleUniv_s*	moduniv;	/* parallel universe of
						 * modules
						 */
};

#ifdef ENABLE_PLUGIN_MANAGER_PRIVATE
/*
 *
 * From here to the end is specific to plugin managers.
 * This data is only needed by plugin managers, and the plugin management
 * system itself.
 *
 */
typedef struct MLPluginOps_s		MLPluginOps;


/* Interfaces imported by a PIManager plugin */
struct MLPluginImports_s { 

		/* Return current reference count */
	int (*RefCount)(MLPlugin * epiinfo);

		/* Incr/Decr reference count */
	int (*ModRefCount)(MLPlugin*epiinfo, int plusminus);

		/* Unregister us as a plugin */
	void (*ForceUnRegister)(MLPlugin *epiinfo);

		/* Unregister/shut down all our clients*/
	void (*ForEachClient)(MLPlugin* manangerpi
	,	void(*f)(MLPlugin* clientpi, void * other)
	,	void* other);

};

/* Interfaces exported by a Plugin plugin */
struct MLPluginOps_s{
/*
 *	These are the interfaces exported by a PluginPlugin to the
 *	plugin management infrastructure.  These are not imported
 *	by plugins - only the plugin management infrastructure.
 */

	/* RegisterPlugin - Returns unique id for plugin or NULL (fail) */
 	ML_rc (*RegisterPlugin)(MLPlugin* newpi
		,	void**	imports);

	ML_rc	(*UnRegisterPlugin)(MLPlugin*ipiinfo); /* Unregister PI*/
				/* And destroy MLPlugin object */
};

/*
 *      These functions are standard exported interfaces from all modules.
 */
#define ML_MODULE_BOILERPLATE(ModuleVersion, DebugName, CloseName) \
/*                                                              \
 * Prototypes for boilerplate functions                         \
 */                                                             \
static const char*      Ourmoduleversion(void);                 \
static int              GetOurDebugLevel(void);                 \
static void             SetOurDebugLevel(int);                  \
static void             CloseName(MLModule*);                   \
                                                                \
/*                                                              \
 * Initialize Module Exports structure                          \
 */                                                             \
static MLModuleOps OurModExports =                              \
{       Ourmoduleversion                                        \
,       GetOurDebugLevel                                        \
,       SetOurDebugLevel                                        \
,       CloseName                                               \
};                                                              \
/*                                                              \
 * Definitions of boilerplate functions                         \
 */                                                             \
static const char*                                              \
Ourmoduleversion(void)                                          \
{ return ModuleVersion; }                                       \
                                                                \
static int DebugName = 0;                                       \
                                                                \
static int                                                      \
GetOurDebugLevel(void)                                          \
{ return DebugName; }                                           \
                                                                \
static void                                                     \
SetOurDebugLevel(int level)                                     \
{ DebugName = level; }

#endif /*ENABLE_PLUGIN_MANAGER_PRIVATE*/

#endif /* UPMLS_MLPLUGIN_H */
