#ifndef HB_MODULE_H
#define HB_MODULE_H 1

#include <ltdl.h>

/* *************************************************************
 *      SPECIAL NOTE:
 *      Must define MODULE *before* including this header if you
 *      want to use the EXPORT macro.
 * *************************************************************/

/* portability stuff - I believe that this should be some day split off 
 * from the generic code */
#ifdef __STDC__
#define EXPORTHELP1( module, function )   module##_LTX_##function
#else
#define EXPORTHELP1( module, function )   module/**/_LTX_/**/function
#endif
/* portability end */
#define EXPORTHELP2( a, b ) EXPORTHELP1( a, b )
#define EXPORT( function ) EXPORTHELP2( MODULE, function )

#define MAX_FUNC_NAME 15

struct symbol_str { 
	char     name[MAX_FUNC_NAME];	
        lt_ptr_t function;
	int      mandatory; 
};

//#define EXPORT(name) CONC3(MODULE, _LTX_, name)

int module_init(void);
int auth_module_init(void);

#endif
