static const char _module_c_Id [] = "$Id: module.c,v 1.3 2000/09/06 15:56:03 marcelo Exp $";
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include "heartbeat.h"
#include <ha_msg.h>
#include <hb_module.h>

extern struct hb_media_fns** hbmedia_types;
extern int num_hb_media_types;

extern struct auth_type** ValidAuths;
extern int num_auth_types;


static int so_select (const struct dirent *dire);
static int generic_symbol_load(struct symbol_str symbols[]
				, int len, void **handle);
static int comm_module_init(void);

static int so_select (const struct dirent *dire) { 

	const char *end = &dire->d_name[strlen(dire->d_name) - 3];

	const char *obj_end = ".so";

	if(strcmp(end, obj_end) == 0)
		return 1;
	
	return 0;
}

/* 
 * Generic function to load symbols from a module.
 */
static int generic_symbol_load(struct symbol_str symbols[], int len, void **handle)
{ 
	int  a;

		for(a = 0; a < len; a++) {
			struct symbol_str *sym = &symbols[a];

			if((*sym->function = dlsym(*handle, sym->name)) == NULL) {
				if(sym->mandatory) { 
					ha_log(LOG_ERR, "%s: Plugin [] does not have [%s] symbol."
									, __FUNCTION__, sym->name);
					dlclose(*handle); *handle = NULL;
					return(HA_FAIL);
				}
			}

		}

	return(HA_OK);
}

static int comm_module_init(void) { 

	struct symbol_str comm_symbols[NR_HB_MEDIA_FNS]; 
	int a, n;
	struct dirent **namelist;

	strcpy(comm_symbols[0].name, "hb_dev_init");
	comm_symbols[0].mandatory = 1;

	strcpy(comm_symbols[1].name, "hb_dev_new");
	comm_symbols[1].mandatory = 1;

	strcpy(comm_symbols[2].name, "hb_dev_parse");
	comm_symbols[2].mandatory = 0;

	strcpy(comm_symbols[3].name, "hb_dev_open");
	comm_symbols[3].mandatory = 1;

	strcpy(comm_symbols[4].name, "hb_dev_close");
	comm_symbols[4].mandatory = 1;

	strcpy(comm_symbols[5].name, "hb_dev_read");
	comm_symbols[5].mandatory = 1;

	strcpy(comm_symbols[6].name, "hb_dev_write");
	comm_symbols[6].mandatory = 1;

	strcpy(comm_symbols[7].name, "hb_dev_mtype");
	comm_symbols[7].mandatory = 1;

	strcpy(comm_symbols[8].name, "hb_dev_descr");
	comm_symbols[8].mandatory = 1;

	strcpy(comm_symbols[9].name, "hb_dev_isping");
	comm_symbols[9].mandatory = 1;

	n = scandir(COMM_MODULE_DIR, &namelist, &so_select, 0);

	if(n < 0) { 
		ha_log(LOG_ERR, "%s: scandir failed.", __FUNCTION__);
		return (HA_FAIL);
	}

	for(a = 0; a<n; a++) {
		char* obj_path; 
		struct hb_media_fns* fns;
		int ret;

		obj_path = ha_malloc((strlen(COMM_MODULE_DIR) 
							+ strlen(namelist[a]->d_name) + 1) * sizeof(char));
		if(!obj_path) { 
			ha_log(LOG_ERR, "%s: Failed to alloc object path.", __FUNCTION__);
			return(HA_FAIL);
		}

		sprintf(obj_path,"%s/%s", COMM_MODULE_DIR, namelist[a]->d_name);

		fns = ha_malloc(sizeof(struct hb_media_fns));
		
		if(fns == NULL) { 
			ha_log(LOG_ERR, "%s: fns alloc failed.", __FUNCTION__);
			ha_free(obj_path); 
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}

		if((fns->dlhandler = dlopen(obj_path, RTLD_NOW)) == NULL) {
			ha_log(LOG_ERR, "%s: %s", __FUNCTION__, dlerror());
			ha_free(obj_path); obj_path = NULL;
			ha_free(fns); fns = NULL;
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}

		hbmedia_types[num_hb_media_types] = ha_malloc(sizeof(struct hb_media_fns *));

		if(hbmedia_types[num_hb_media_types] == NULL) { 
			ha_log(LOG_ERR, "%s: hbmedia_types[%d] alloc failed"
						, __FUNCTION__, num_hb_media_types);
			ha_free(obj_path);
			ha_free(fns);
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			return (HA_FAIL);
		}

		comm_symbols[0].function = (void **)&fns->init;
		comm_symbols[1].function = (void **)&fns->new;
		comm_symbols[2].function = (void **)&fns->parse;
		comm_symbols[3].function = (void **)&fns->open;
		comm_symbols[4].function = (void **)&fns->close;
		comm_symbols[5].function = (void **)&fns->read;
		comm_symbols[6].function = (void **)&fns->write;
		comm_symbols[7].function = (void **)&fns->mtype;
		comm_symbols[8].function = (void **)&fns->descr;
		comm_symbols[9].function = (void **)&fns->isping;

		ret = generic_symbol_load(comm_symbols, NR_HB_MEDIA_FNS, 
		 &fns->dlhandler);

		if(ret == HA_FAIL) {
			ha_free(hbmedia_types[num_hb_media_types]);
			ha_free(obj_path);
			ha_free(fns);
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			return ret;
		}

		hbmedia_types[num_hb_media_types] = fns;
		num_hb_media_types++;

		fns->type_len = fns->mtype(&fns->type);
		fns->desc_len = fns->descr(&fns->description);
		fns->ref = 0;

		ha_free(obj_path); 
		obj_path = NULL;
	}

	for(a=0; a<n; a++) {
		free(namelist[a]);
	} 

	return (HA_OK);
}


int auth_module_init() 
{ 
	struct symbol_str auth_symbols[NR_AUTH_FNS]; 
	int a, n;
	struct dirent **namelist;

	strcpy(auth_symbols[0].name, "hb_auth_calc");
	auth_symbols[0].mandatory = 1;

	strcpy(auth_symbols[1].name, "hb_auth_atype");
	auth_symbols[1].mandatory = 1;

	strcpy(auth_symbols[2].name, "hb_auth_nkey");
	auth_symbols[2].mandatory = 1;

	n = scandir(AUTH_MODULE_DIR, &namelist, &so_select, 0);

	if(n < 0) { 
		ha_log(LOG_ERR, "%s: scandir failed", __FUNCTION__);
		return (HA_FAIL);
	}

	for(a = 0; a<n; a++) {
		char* obj_path; 
		struct auth_type* auth;
		int ret;

		obj_path = ha_malloc((strlen(AUTH_MODULE_DIR) 
							+ strlen(namelist[a]->d_name) + 1) * sizeof(char));
		if(!obj_path) { 
			ha_log(LOG_ERR, "%s: Failed to alloc object path", __FUNCTION__);
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}

		sprintf(obj_path,"%s/%s", AUTH_MODULE_DIR, namelist[a]->d_name);

		auth = ha_malloc(sizeof(struct auth_type));

		if(auth == NULL) { 
			ha_log(LOG_ERR, "%s: auth_type alloc failed", __FUNCTION__);
			ha_free(obj_path); 
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}

		auth_symbols[0].function = (void **)&auth->auth;
		auth_symbols[1].function = (void **)&auth->atype;
		auth_symbols[2].function = (void **)&auth->needskey;

		if((auth->dlhandler = dlopen(obj_path, RTLD_NOW)) == NULL) {
			ha_log(LOG_ERR, "%s: dlopen failed", __FUNCTION__);
			ha_free(obj_path); 
			ha_free(auth);
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			return(HA_FAIL);
		}
		
		ValidAuths[num_auth_types] = ha_malloc(sizeof(struct auth_type *));

		if(ValidAuths[num_auth_types] == NULL) {
			ha_log(LOG_ERR, "%s: alloc of ValidAuths[%d] failed."
						, __FUNCTION__, num_auth_types);
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			ha_free(obj_path);
			ha_free(auth);
			return (HA_FAIL);
		}

		ret = generic_symbol_load(auth_symbols, NR_AUTH_FNS, 
		 &auth->dlhandler);

		if(ret == HA_FAIL) {
			ha_free(ValidAuths[num_auth_types]);
			ha_free(obj_path);
			ha_free(auth);
			for(a=0; a<n; a++) {
				free(namelist[a]);
			} 
			return ret;
		}

		ValidAuths[num_auth_types] = auth;
		num_auth_types++;

		auth->authname_len = auth->atype(&auth->authname);
		auth->ref = 0;
	}

	for(a=0; a<n; a++) {
		free(namelist[a]);
	} 

	return(HA_OK);

}

int module_init(void) { 
	
    (void)_module_c_Id;
    (void)_heartbeat_h_Id;
    (void)_ha_msg_h_Id; 

	if(comm_module_init() == HA_FAIL) 
		return(HA_FAIL);

	if(auth_module_init() == HA_FAIL)
		return(HA_FAIL);

	return (HA_OK);
}
