#include <stdio.h>
#include <upmls/MLModule.h>

#define MOD	"/home/alanr/modules"

int
main(int argc, char ** argv)
{
	MLModuleUniv *	u;


	u = NewMLModuleUniv(MOD);
	MLSetDebugLevel(100);

	printf("Load of foo: %d\n", MLLoadModule(u, "Plugin", "test", NULL));
	printf("Error = [%s]\n", lt_dlerror());
	DelMLModuleUniv(u); u = NULL;

	return 0;
}
