#include <stdio.h>
#include <upmls/MLModule.h>

#define MOD	"/home/alanr/modules"

int
main(int argc, char ** argv)
{
	MLModuleUniv *	u;


	u = NewMLModuleUniv(MOD);
	MLsetdebuglevel(100);

	printf("Load of foo: %d\n", MLLoadModule(u, PLUGIN_PLUGIN, "sha1"));

	return 0;
}
