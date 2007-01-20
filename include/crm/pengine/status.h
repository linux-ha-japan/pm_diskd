/* $Id: status.h,v 1.3 2006/06/21 14:48:01 andrew Exp $ */
/* 
 * Copyright (C) 2004 Andrew Beekhof <andrew@beekhof.net>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef PENGINE_STATUS__H
#define PENGINE_STATUS__H

#include <glib.h>
#include <crm/common/iso8601.h>
#include <crm/pengine/common.h>

typedef struct node_s node_t;
typedef struct action_s action_t;
typedef struct resource_s resource_t;

typedef enum no_quorum_policy_e {
	no_quorum_freeze,
	no_quorum_stop,
	no_quorum_ignore
} no_quorum_policy_t;

enum node_type {
	node_ping,
	node_member
};

enum pe_restart {
	pe_restart_restart,
	pe_restart_ignore
};

typedef struct pe_working_set_s 
{
		crm_data_t *input;
		ha_time_t *now;

		/* options extracted from the input */
		char *transition_idle_timeout;
		char *dc_uuid;
		node_t *dc_node;
		gboolean have_quorum;
		gboolean stonith_enabled;
		const char *stonith_action;
		gboolean symmetric_cluster;
		gboolean is_managed_default;

		gboolean remove_after_stop;
		gboolean stop_rsc_orphans;
		gboolean stop_action_orphans;

		int default_resource_stickiness;
		int default_resource_fail_stickiness;
		no_quorum_policy_t no_quorum_policy;

		GHashTable *config_hash;
		
		GListPtr nodes;
		GListPtr resources;
		GListPtr placement_constraints;
		GListPtr ordering_constraints;
		
		GListPtr actions;

		/* stats */
		int num_synapse;
		int max_valid_nodes;
		int order_id;
		int action_id;

		/* final output */
		crm_data_t *graph;

} pe_working_set_t;

struct node_shared_s { 
		const char *id; 
		const char *uname; 
		gboolean online;
		gboolean standby;
		gboolean unclean;
		gboolean shutdown;
		gboolean expected_up;
		gboolean is_dc;
		int	 num_resources;
		GListPtr running_rsc;	/* resource_t* */
		GListPtr allocated_rsc;	/* resource_t* */
		
		GHashTable *attrs;	/* char* => char* */
		enum node_type type;
}; 

struct node_s { 
		int	weight; 
		gboolean fixed;
		int      count;
		struct node_shared_s *details;
};

#include <crm/pengine/complex.h>

struct resource_s { 
		char *id; 
		char *clone_name; 
		char *long_name; 
		crm_data_t *xml; 
		crm_data_t *ops_xml; 

		resource_t *parent;
		void *variant_opaque;
		enum pe_obj_types variant;
		resource_object_functions_t *fns;
 		resource_alloc_functions_t  *cmds;

		enum rsc_recovery_type recovery_type;
		enum pe_restart        restart_type;

		int	 priority; 
		int	 stickiness; 
		int	 fail_stickiness;
		int	 effective_priority; 

		gboolean notify;
		gboolean is_managed;
		gboolean can_migrate;
		gboolean starting;
		gboolean stopping;
		gboolean runnable;
		gboolean provisional;
		gboolean globally_unique;
		gboolean is_allocating;
		
		gboolean failed;
		gboolean start_pending;
		
		gboolean orphan;
		
		GListPtr rsc_cons;         /* rsc_colocation_t* */
		GListPtr rsc_location;     /* rsc_to_node_t*    */
		GListPtr actions;	   /* action_t*         */

		node_t *allocated_to;
		GListPtr running_on;       /* node_t*   */
		GListPtr known_on;	   /* node_t* */
		GListPtr allowed_nodes;    /* node_t*   */

		enum rsc_role_e role;
		enum rsc_role_e next_role;

		GHashTable *meta;	   
		GHashTable *parameters;	   
};

struct action_s 
{
		int         id;
		int         priority;
		resource_t *rsc;
		void       *rsc_opaque;
		node_t     *node;
		const char *task;

		char *uuid;
		crm_data_t *op_entry;
		
		gboolean pseudo;
		gboolean runnable;
		gboolean optional;
		gboolean failure_is_fatal;
		gboolean allow_reload_conversion;

		enum rsc_start_requirement needs;
		enum action_fail_response  on_fail;
		enum rsc_role_e fail_role;
		
		gboolean dumped;
		gboolean processed;

		action_t *pre_notify;
		action_t *pre_notified;
		action_t *post_notify;
		action_t *post_notified;
		
		int seen_count;

		GHashTable *meta;
		GHashTable *extra;
		GHashTable *notify_keys;  /* do NOT free */
		
		GListPtr actions_before; /* action_warpper_t* */
		GListPtr actions_after;  /* action_warpper_t* */
};

gboolean cluster_status(pe_working_set_t *data_set);
extern void set_working_set_defaults(pe_working_set_t *data_set);
extern void cleanup_calculations(pe_working_set_t *data_set);
extern resource_t *pe_find_resource(GListPtr rsc_list, const char *id_rh);
extern node_t *pe_find_node(GListPtr node_list, const char *uname);
extern node_t *pe_find_node_id(GListPtr node_list, const char *id);

#endif
