#ifndef UPMLS_MLMODULE_H
#  define UPMLS_MLMODULE_H
#define index FOOindex
#define time FOOtime
#include <glib.h>
#undef index
#undef time
#include <ltdl.h>
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
 * Loadable modules are sometimes referred to as plugins.  Here, we use the
 * two terms to mean two different things.
 *
 */
#define time FOOtime
#include <glib.h>
#undef index
#undef time
#include <ltdl.h>
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
 * Loadable modules are sometimes referred to as plugins.  Here, we use the
 * two terms to mean two different things.
 *
 * This plugin and module loading system is quite general, and should be
 * directly usable by basically any project on any platform on which it runs
 * - which should be many, since everything is build with automake
 * and libtool.
 *
 * Some terminology...
 *
 * There are two basic kinds of objects we deal with here:
 *
 * Modules: dynamically loaded chunks of code which implement one or more
 *		plugins.  The system treats all modules as the same.
 *		In UNIX, these are dynamically loaded ".so" files.
 *
 * Plugin: A set of functions which implement a particular plug-in capability
 * 		(or interface)
 * 	Generally plugins are dynamically loaded as part of a module.
 * 	The system treats all plugins of the same type the same.
 * 	It is common to have exactly one plugin inside of each module.
 * 	In this case, the plugin name should match the module name.
 *
 * Each plugin exports certain interfaces which it exports for its clients
 * to use.   We refer to these those "Ops".  Every plugin of the same type
 * "imports" the same interfaces from its plugin manager,
 * and exports the same "Ops".
 *
 * Each plugin is provided certain interfaces which it imports when it
 * from its plugin manager when it is registered.  We refer to these as
 * "Imports".  Every plugin of a given type imports the same interfaces.
 *
 * The story with modules is a little different...
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
 *	MLModuleType		The data common to all modules of a given type
 *	MLModuleUniv		The set of all module types in the Universe
 *					(well... at least *this* universe)
 *
 * The basic structures we maintain about plugins are as follows:
 * 	MLPlugin		The data which represents a plugin
 * 	MLPluginType		The data which is common to all plugins of
 * 					a given type
 *	MLModuleUniv		The set of all plugin types in the Universe
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
 *	generally correspond to plugin types.
 *
 *	One can only cause a module to be loaded - not a plugin.  But it is
 *	common to assume that loading a module named foo of type bar will
 *	cause a plugin named foo of type bar to be registered.  If one
 *	wants to implement automatic module loading in a given plugin type,
 *	this assumption is necessary.
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
 * 	determined by the plugin manager for the particular type of plugin
 * 	being discussed.
 *
 ***************************************************************************
 * SOME MORE DETAILS ABOUT PLUGINS...
 ***************************************************************************
 *
 *	There is only one built in type of plugin.  That's the Plugin plugin.
 *	The plugin manager for the plugin of type "Plugin", named "Plugin"
 *	inserts itself into the system in order to bootstrap things...
 *
 *	When an attempt is made to register a plugin of an unknown type, then
 *	the appropriate Plugin manager is loaded automatically.
 *
 *	It handles requests to register plugins whose type is the same
 *	as its plugin name.  If the plugin manager's plugin name is foo,
 *	then it is the plugin manager for all plugins of type foo.
 *
 * 	Types associated with plugins of type Plugin
 *
 *	MLPluginOps	The set of interfaces that every plugin
 *				manager exports
 *	MLPluginImports	The set of interfaces which are supplied to
 *				(imported by) every plugin of type Plugin.
 *				(that is, every plugin manager).
 *
 *****************************************************************************
 *
 * Each module has only one entry point which is exported directly, regardless
 * of what kind of plugin(s) it implements...
 *
 * This entrypoint is named ml_module_init()	{more or less - see below}
 *
 * The ml_module_init() function is called once when the module is loaded.
 *
 *
 * All other entry points are registered (exported) through parameters
 * passed to ml_module_init()
 *
 * Ml_module_init() then registers the module, and all the plugins which
 * this module implements.  The registration function is in the parameters
 * which are passed to ml_module_init().
 *
 *****************************************************************************
 *
 * THINGS IN THIS DESIGN WHICH ARE PROBABLY BROKEN...
 *
 * Each of the plugin needs to be able to get some kind of
 * user data passed to it - at least if it has been loaded manually...
 *
 * It may also be the case that the module loading environment needs
 * to be able to have some kind of user_data passed to it which it can
 * also pass along to any plugin ...
 *
 * Maybe this should be handled by a sort of global user_data registration
 * structure, so globals can be passed to plugins when they're registered.
 *
 * A sort of "user_data" registry.  One for each plugin type and one
 * for each plugin...  Or maybe it could be even more flexible...
 *
 * This is all so that these nice pristene, beautiful concepts can come out
 * and work well in the real world where plugins need to interact with
 * some kind of global system view, and with each other...
 *
 * Probably need some better way of managing plugin versions, etc.
 *
 *****************************************************************************
 */

/*
 * If you want to use this funky export stuff, then you need to #define
 * ML_MODULETYPE and ML_MODULE *before* including this file.
 *
 * The way to use this stuff is to declare your primary entry point this way:
 *
 * This example is for an module of type "auth" named "sha1"
 *
 *	#define ML_MODULETYPE	auth
 *	#define ML_MODULE	sha1
 *	#include <upmls/MLModule.h>
 *
 *	static const char*	Ourmoduleversion	(void);
 *	static const char*	Ourmodulename	(void);
 *	static int		Ourgetdebuglevel(void);
 *	static void		Oursetdebuglevel(int);
 *	static void		Ourclose	(MLModule*);
 *
 *	static struct MLModuleOps our_exported_module_operations =
 *	{	Ourmoduleversion,
 *	,	Ourmodulename
 *	,	Ourgetdebuglevel
 *	,	Oursetdebuglevel
 *	,	Ourclose
 *	};
 *
 *	static const MLModuleImports*	ModuleOps;
 *	static MLModule*		OurModule;
 *
 *	// Our module initialization and registration function
 *	// It gets called when the module gets loaded.
 *	ML_rc
 *	ML_MODULE_INIT(MLModule*us, const MLModuleImports* imports)
 *	{
 *		ModuleOps = imports;
 *		OurModule = us;
 *
 *		// Register ourself as a module * /
 *		imports->register_module(us, &our_exported_module_operations);
 *
 *		// Register our plugins
 *		imports->register_plugin(us, "plugintype", "pluginname"
 *			// Be sure and define "OurExports" and OurImports
 *			// above...
 *		,	&OurExports
 *		,	&OurImports);
 *		// Repeat for all plugins in this module...
 *
 *	}
 *
 * Except for the ML_MODULETYPE and the ML_MODULE definitions, and changing
 * the names of various static variables and functions, every single module is
 * set up pretty much the same way
 *
 */

/*
 * No doubt there is a fancy preprocessor trick for avoiding these
 * duplications but I don't have time to figure it out.  Patches are
 * being accepted...
 */
#define	mlINIT_FUNC	_ml_module_init
#define mlINIT_FUNC_STR	"_ml_module_init"
#define ML_INSERT	_LTX_
#define ML_INSERT_STR	"_LTX_"

/*
 * snprintf-style format string for initialization entry point name:
 * 	arguments are: (moduletype, modulename)
 */
#define	ML_FUNC_FMT	"%s" ML_INSERT_STR "%s" mlINIT_FUNC_STR

#ifdef __STDC__
#  define EXPORTHELPER1(moduletype, insert, modulename, function)	moduletype##insert##modulename##function
#else
#  define EXPORTHELPER1(moduletype, insert, modulename, function)	\
 	moduletype/**/insert/**/modulename/**/function
#endif

#define EXPORTHELPER2(a, b, c, d)    EXPORTHELPER1(a, b, c, d)
#define ML_MODULE_INIT	EXPORTHELPER2(ML_MODULETYPE,ML_INSERT,ML_MODULE,mlINIT_FUNC)

/*
 *	Module loading return codes.  OK will always be zero.
 *
 *	There are many ways to fail, but only one kind of success ;-)
 */

typedef enum {
	ML_OK=0,	/* Success */
	ML_INVAL=1,	/* Invalid Parameters */
	ML_BADTYPE=2,	/* Bad module/plugin type */
	ML_EXIST=3,	/* Duplicate Module/Plugin name */
	ML_OOPS=4,	/* Internal Error */
	ML_NOMODULE=5,	/* No such module or Plugin */
}ML_rc;			/* Return code from Module fns*/

const char * ML_strerror(ML_rc rc);


typedef struct MLModuleImports_s	MLModuleImports;
typedef struct MLModuleOps_s		MLModuleOps;
typedef struct MLModule_s		MLModule;
typedef struct MLModuleUniv_s		MLModuleUniv;
typedef struct MLModuleType_s		MLModuleType;

typedef struct MLPlugin_s		MLPlugin;
typedef struct MLPluginImports_s	MLPluginImports;
typedef struct MLPluginUniv_s		MLPluginUniv;
typedef struct MLPluginType_s		MLPluginType;

typedef ML_rc(*MLPluginFun)(MLPlugin*, void* ud_plugin);

#define	ML_MAGIC_MODULE		0xFEEDBEEFUL
#define	ML_MAGIC_MODTYPE	0xFEEDCEEFUL
#define	ML_MAGIC_MODUNIV	0xFEEDDEEFUL
#define	ML_MAGIC_PLUGIN		0xFEEDEEEFUL
#define	ML_MAGIC_PLUGINTYPE	0xFEEDFEEFUL
#define	ML_MAGIC_PLUGINUNIV	0xFEED0EEFUL

#define IS_MLMODULE(s)		((s)->MagicNum == ML_MAGIC_MODULE)
#define IS_MLMODTYPE(s)		((s)->MagicNum == ML_MAGIC_MODTYPE)
#define IS_MLMODUNIV(s)		((s)->MagicNum == ML_MAGIC_MODUNIV)
#define IS_MLPLUGIN(s)		((s)->MagicNum == ML_MAGIC_PLUGIN)
#define IS_MLPLUGINTYPE(s)	((s)->MagicNum == ML_MAGIC_PLUGINTYPE)
#define IS_MLPLUGINUNIV(s)	((s)->MagicNum == ML_MAGIC_PLUGINUNIV)

/* The type of a Module Initialization Function */
typedef ML_rc (*MLModuleInitFun) (MLModule*us
,		MLModuleImports* imports
,		void*	module_user_data);

/*
 * struct MLModuleOps_s (typedef MLModuleOps) defines the set of functions
 * exported by all modules...
 */
struct MLModuleOps_s {
	const char*	(*moduleversion) (void);
	int		(*getdebuglevel) (void);
	void		(*setdebuglevel) (int);
	void		(*close) (MLModule*);
};

/*
 *	Logging levels for the "standard" log interface.
 */

typedef enum {
	ML_FATAL= 1,	/* BOOM! Causes program to stop */
	ML_CRIT	= 2,	/* Critical -- serious error */
	ML_WARN	= 3,	/* Warning */
	ML_INFO	= 4,	/* Informative message */
	ML_DEBUG= 5,	/* Debug message */
}MLLogLevel;

/*
 * struct MLModuleImports_s (typedef MLModuleImports) defines
 * the functions and capabilities that every module imports when it is loaded.
 */


struct MLModuleImports_s {
	ML_rc	(*register_module)(MLModule* modinfo
	,	const MLModuleOps* commonops);
	ML_rc	(*unregister_module)(MLModule* modinfo);
/*
 *	A little explanation of the close_func parameter to register_plugin
 *	is in order.
 *
 *	It is an exported operation function, just like the Ops structure.
 *	However, the Ops vector is exported to applications that
 *	are using the plugin. Unlike the Ops structure, close_func is
 *	exported only to the plugin system, since applications shouldn't
 *	call it directly, but should manage the reference counts for the
 *	plugins instead.
 *	The generic plugin system doesn't have any idea how to call
 *	any functions in the operations vector.  So, it's a separate
 *	parameter for two good reasons.
 */
	ML_rc	(*register_plugin)(MLModule* modinfo
	,	const char *	plugintype	/* Type of plugin	*/
	,	const char *	pluginname	/* Name of plugin	*/
	,	void*		Ops		/* Info (functions) exported
						   by this plugin	*/
		/* Function to call to shut down this plugin */
	,	MLPluginFun	close_func

	,	MLPlugin**	pluginid /* Plugin id 	(OP)	*/
	,	void**		Imports
	,	void*		ud_plugin);	/* plugin user data */

	ML_rc	(*unregister_plugin)(MLPlugin* pluginid);
	ML_rc	(*load_module)(MLModuleUniv* universe
	,	const char * moduletype, const char * modulename
	,	void*	module_private);

	void	(*log)	(MLLogLevel priority, const char * fmt, ...);
					/* Logging function		*/
};

/*
 * EXPORTED INTERFACES...
 */

/* Create a new module universe - start the module loading system up */
MLModuleUniv*	NewMLModuleUniv(const char * basemoduledirectory);

/* Delete a module universe - shut the module loading system down */
/*	Best if used carefully ;-) */
void		DelMLModuleUniv(MLModuleUniv*);

/* Return a list of modules of the given type */
char **		MLListModules(MLModuleUniv* u, const char *moduletype
,		int* modulecount /*can be NULL*/);

/* Free the module list returned by MLFreeListModules */
void		MLFreeModuleList(char ** modulelist);

/* Load the requested module */
ML_rc		MLLoadModule(MLModuleUniv* moduniv
,		const char *	moduletype
,		const char *	modulename
,		void *		mod_private);

void		MLSetDebugLevel(int level);
int		MLGetDebugLevel(void);

ML_rc		MLIncrPIRefCount(MLModuleUniv* moduniv
,		const char *	plugintype
,		const char *	pluginname
,		int	plusminus);

int		MLGetPIRefCount(MLModuleUniv* moduniv
,		const char *	plugintype
,		const char *	pluginname);

/* The module/plugin type of a plugin manager */

#define	PLUGIN_PLUGIN	"Plugin"

#ifdef ENABLE_ML_DEFS_PRIVATE
/* Probably these should be moved to a different header file */

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
 * The MLModuleUniv object contains information about all modules of
 * all types.
 *
 * The MLModuleType object contains information about all the modules of a
 * specific type.
 *
 * Note: for modules which implement a single plugin, the module type name
 * should be the same as the plugin type name.
 *
 * For other modules that implement more than one plugin, one of the plugin
 * names should match the module name.
 */


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
	unsigned long	MagicNum;	
	char*		module_name;
	MLModuleType*	moduletype;	/* Parent structure */
	GHashTable*	Plugins;	/* Plugins registered by this module*/
	int		refcnt;		/* Reference count for this module */
	lt_dlhandle	dlhandle;	/* Reference to D.L. object */
	MLModuleInitFun	dlinitfun;	/* Initialization function */
	const MLModuleOps* moduleops;	/* Exported module operations */

	void*		ud_module;	/* Module-Private data */
	/* Other stuff goes here ...  (?) */
};

/*
 *	MLModuleType		Information about all modules of a given type.
 *					(i.e.,  in a given directory)
 *				(AKA struct MLModuleType_s)
 */

struct MLModuleType_s {
	unsigned long		MagicNum;	
	char *			moduletype;
	MLModuleUniv*		moduniv; /* The universe to which we belong */
	GHashTable*		Modules;
				/* Key is module type, value is MLModule */

	int	(*refcount)	(MLModuleType*, const char * modulename);
	int	(*modrefcount)	(MLModuleType*, const char * modulename
	,			int plusminus);
	char**	(*listmodules)(MLModuleType*, int* listlen);
};

/*
 *	MLModuleUniv (aka struct MLModuleUniv_s) is the structure which
 *	represents the universe of all MLModuleType objects.
 *	There is one MLModuleType object for each Module type.
 */

struct MLModuleUniv_s {
	unsigned long		MagicNum;	
	char *			rootdirectory;
			/* key is module type, data is MLModuleType* struct */
	GHashTable*		ModuleTypes;
	struct MLPluginUniv_s*	piuniv; /* Parallel Universe of plugins */
	MLModuleImports*	imports;
};

#  endif /* ENABLE_ML_DEFS_PRIVATE */
#endif /*UPMLS_MLMODULE_H */
