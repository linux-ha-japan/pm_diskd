#ifndef HB_MODULE_H
#define HB_MODULE_H 1

#include <ltdl.h>

/* *************************************************************
 *      SPECIAL NOTE:
 *      Must define MODULE *before* including this header if you
 *      want to use the EXPORT macro.
 * *************************************************************/

/*
 * Portability stuff - I believe that this should be some day split off 
 * from the generic code
 */

#define	MODPREFIX	_LTX_
/*
 * No doubt fancy 'C' tricks can define MODPREFIXSTR in terms of MODPREFIX
 * but, it isn't worth my figuring it out right now ;-)
 */
#define	MODPREFIXSTR	"_LTX_"


#ifdef __STDC__
#  define EXPORTHELP1(module, prefix, function)	module##prefix##function
#else
#  define EXPORTHELP1(module, prefix, function)	module/**/prefix/**/function
#endif
#define EXPORTHELP2(a, b, c)	EXPORTHELP1(a, b, c)
#define EXPORT(function)	EXPORTHELP2(MODULE, MODPREFIX, function)
/* portability end */

#define MAX_FUNC_NAME 64

struct symbol_str { 
	char     name[MAX_FUNC_NAME];	
        lt_ptr_t function;
	int      mandatory; 
};


int module_init(void);
int auth_module_init(void);

#endif
