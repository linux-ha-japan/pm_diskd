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
 
/*
 *	These functions are standard exported interfaces from all modules.
 */
static const char*	Ourmoduleversion(void);
static const char*	Ourmodulename	(void);
static int		Ourgetdebuglevel(void);
static void		Oursetdebuglevel(int);
static void		Ourclose	(MLModule*);

static MLModuleOps OurModExports =
{	Ourmoduleversion
,	Ourmodulename
,	Ourgetdebuglevel
,	Oursetdebuglevel
,	Ourclose
};

/*
 *	Places to store information gotten during registration.
 */
static const MLModuleImports*	OurModImports;	/* Module Imported functions */
static MLModule*		OurModule;	/* Pointer to our module info */
static MLPluginImports*		OurPiImports;	/* Plugin imported functions */
static MLPlugin*		OurPi;		/* Pointer to our plugin info */

/*
 *	Our Plugin Manager interfaces - exported to the universe!
 *
 *	(or at least the plugin management universe ;-).
 *
 */
static MLPluginOps		OurPiOps = {
};

ML_rc ML_MODULE_INIT(MLModule*us, const MLModuleImports* imports);

ML_rc
ML_MODULE_INIT(MLModule*us, const MLModuleImports* imports)
{
	ML_rc		ret;
	OurModImports = imports;
	OurModule = us;
 

	imports->log(ML_DEBUG, "Registering ourselves as a module");
	/* Register ourself as a module */
	imports->register_module(us, &OurModExports);
 
	imports->log(ML_DEBUG, "Registering our plugins");
	/*  Register our plugins */
	ret = imports->register_plugin(us
	,	MODTYPE
	,	MODNAME
	,	&OurPiOps
	,	(void **)&OurPi
	,	(const void**)&OurPiImports
	,	NULL);
	imports->log(ML_DEBUG, "Returning %d", ret);
	return ret;
}

static const char*
Ourmoduleversion	(void)
{
	return "1.0";
}

static const char*
Ourmodulename	(void)
{
	return "foo";
}

static int debuglevel = 0;
static int	
Ourgetdebuglevel(void)
{
	return debuglevel;
}

static void
Oursetdebuglevel(int level)
{
	debuglevel = level;
}

static void
Ourclose	(MLModule* us)
{
}

