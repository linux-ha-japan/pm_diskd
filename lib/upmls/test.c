/*
 *	Sample Plugin manager.
 */
#define	ML_MODULETYPE	Plugin
#define	ML_MODULE	test
#define	MODTYPE	"Plugin"
#define	MODNAME	"test"

/* We are a plugin manager... */
#define ENABLE_PLUGIN_MANAGER_PRIVATE

#include <upmls/MLPlugin.h>
 
ML_MODULE_BOILERPLATE("1.0", DebugFlag, Ourclose)

static void
Ourclose	(MLModule* us)
{
}

/*
 *	Places to store information gotten during registration.
 */
static const MLModuleImports*	OurModImports;	/* Imported module fcns */
static MLModule*		OurModule;	/* Our module info */
static MLPluginImports*		OurPiImports;	/* Plugin imported fcns */
static MLPlugin*		OurPi;		/* Pointer to plugin info */

/*
 *	Our Plugin Manager interfaces - exported to the universe!
 *
 *	(or at least the plugin management universe ;-).
 *
 */
static MLPluginOps		OurPiOps = {
	/* FIXME -- put some in here !! */
};

ML_rc ML_MODULE_INIT(MLModule*us, MLModuleImports* imports, void*);


ML_rc
ML_MODULE_INIT(MLModule*us, MLModuleImports* imports, void *user_ptr)
{
	ML_rc		ret;
	/*
	 * Force compiler to check our parameters...
	 */
	MLModuleInitFun	fun = &ML_MODULE_INIT; (void)fun;


	OurModImports = imports;
	OurModule = us;

	imports->log(ML_DEBUG, "Module %s: user_ptr = %lx"
	,	MODNAME, (unsigned long)user_ptr);

	imports->log(ML_DEBUG, "Registering ourselves as a module");

	/* Register as a module */
	imports->register_module(us, &OurModExports);
 
	imports->log(ML_DEBUG, "Registering our plugins");

	/*  Register our plugins */
	ret = imports->register_plugin
	(	us
	,	MODTYPE
	,	MODNAME
	,	&OurPiOps	/* Exported plugin operations */
	,	NULL		/* Plugin Close function */
	,	&OurPi
	,	(void**)&OurPiImports
	,	NULL);
	imports->log(ML_DEBUG, "Returning %d", ret);

	return ret;
}

