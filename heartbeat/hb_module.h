#include <dirent.h>
#include <dlfcn.h>

#define MAX_FUNC_NAME 15

struct symbol_str { 
	char name[MAX_FUNC_NAME];	
	void** function;
	int mandatory; 
};

int so_select (const struct dirent *);

int generic_symbol_load(struct symbol_str[], int len, void **);
int module_init(void);
int comm_module_init(void);
int auth_module_init(void);


