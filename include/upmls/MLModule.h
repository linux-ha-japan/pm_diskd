#ifndef UPMLS_MLMODULE_H
#  define UPMLS_MLMODULE_H
#include <glib.h>
/*
 *	WARNING!! THIS CODE LARGELY / COMPLETELY UNTESTED!!
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
typedef enum {
	ML_OK=0,		/* Success */
	ML_INVAL,	/* Invalid Parameters */
	ML_BADTYPE,	/* Bad module/plugin type */
	ML_EXIST,	/* Duplicate Module/Plugin name */
	ML_OOPS,	/* Internal Error */
	ML_NOMODULE,	/* No such module or Plugin */
}ML_rc;			/* Return code from Module fns*/

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
#endif /*UPMLS_MLMODULE_H */
