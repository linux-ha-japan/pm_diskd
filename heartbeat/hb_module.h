#include <dirent.h>
#include <dlfcn.h>

#define MAX_FUNC_NAME 15

struct symbol_str { 
	char name[MAX_FUNC_NAME];	
	void** function;
	int mandatory; 
};

int module_init(void);
int auth_module_init(void);

