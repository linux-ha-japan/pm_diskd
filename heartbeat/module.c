static const char _module_c_Id [] = "$Id: module.c,v 1.34 2001/08/15 16:17:12 alan Exp $";
/*
 * module: Dynamic module support code
 *
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 * Copyright (C) 2000 Marcelo Tosatti <marcelo@conectiva.com.br>
 * 
 * Thanks to Conectiva S.A. for sponsoring Marcelo Tosatti work
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "portability.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/param.h>
#include <unistd.h>
#include <dirent.h>
#include <ltdl.h>
#include "heartbeat.h"
#include <ha_msg.h>
#include <hb_module.h>
#include <pils/generic.h>
#include "../libltdl/config.h"
#include <HBcomm.h>
#include "lock.h"

/* BSD wants us to cast the select parameter to scandir */
#ifdef BSD
#	define SCANSEL_C	(void *)
#else
#	define SCANSEL_C	/* Nothing */
#endif
#ifndef RTLD_NOW
#	define RTLD_NOW 0
#endif

extern struct hb_media_fns** hbmedia_types;
extern int num_hb_media_types;


PILPluginUniv*	PluginLoadingSystem = NULL;
GHashTable*	AuthFunctions = NULL;
GHashTable*	CommFunctions = NULL;
GHashTable*	StonithFuncs = NULL;
static void	RegisterNewMedium(struct hb_media* mp);
static const char *	ParameterValue(const char * name);
struct hb_media_imports	CommImports =
{	ParameterValue
,	RegisterNewMedium
,	ttylock
,	ttyunlock
};

extern struct hb_media* sysmedia[];
extern int              nummedia;

static PILGenericIfMgmtRqst RegistrationRqsts [] =
{	{"HBauth",	&AuthFunctions,	NULL,		NULL, NULL}
,	{"HBcomm",	&CommFunctions,	&CommImports,	NULL, NULL}
,	{"stonith",	&StonithFuncs,	NULL,		NULL, NULL}
,	{NULL,		NULL,		NULL,		NULL, NULL}
};


int 
module_init(void)
{ 
	static int initialised = 0;

	int errors = 0;
	PIL_rc	rc;

	(void)_module_c_Id;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	/* Perform the init only once */
	if (initialised) {
		return HA_FAIL;
	}
	/* Initialize libltdl's list of preloaded modules */
	LTDL_SET_PRELOADED_SYMBOLS();

	/* Initialize ltdl */
	if ((errors = lt_dlinit())) {
		return HA_FAIL;
	}

	if ((PluginLoadingSystem = NewPILPluginUniv(HA_PLUGIN_D))
	==	NULL) {
    		return(HA_FAIL);
	}

 	PILSetDebugLevel(PluginLoadingSystem, NULL, NULL, 10);

	if ((rc = PILLoadPlugin(PluginLoadingSystem, "InterfaceMgr", "generic"
	,	&RegistrationRqsts)) != PIL_OK) {
	
		ha_log(LOG_ERR
		,	"ERROR: cannot load generic interface manager plugin"
	       " [%s/%s]: %s"
	      	,	"InterfaceMgr", "generic"
		,	PIL_strerror(rc));
		return HA_FAIL;
	}
	PILSetDebugLevel(PluginLoadingSystem, "InterfaceMgr", "generic", 10);

	/* init completed */
	++initialised;

	return HA_OK;
}

static void
RegisterNewMedium(struct hb_media* mp)
{
 
	sysmedia[nummedia] = mp;
	++nummedia;
}
static const char *
ParameterValue(const char * name)
{
	return NULL;
}
