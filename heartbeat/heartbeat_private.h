/*
 * heartbeat_private.h: definitions for the Linux-HA heartbeat program
 * that are defined in heartbeat.c and are used by other .c files
 * that are only compiled into the heartbeat binary
 *
 * I evisage that eventually these funtions will be broken out
 * of heartbeat.c and that this heartbeat_private.h will no longer
 * be neccessary.
 *
 * Copyright (C) 2002 Horms <horms@verge.net.au>
 *
 * This file created from heartbeat.c
 * Copyright (C) 2000 Alan Robertson <alanr@unix.sh>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef _HEARTBEAT_PRIVATE_H
#define _HEARTBEAT_PRIVATE_H

#include <glib.h>

#include <hb_proc.h>

static const char * _heartbeat_private_h_Id = "$Id: heartbeat_private.h,v 1.2 2002/10/18 22:46:30 alan Exp $";

enum hb_rsc_state {
	HB_R_INIT,		/* Links not up yet */
	HB_R_STARTING,		/* Links up, start message issued */
	HB_R_BOTHSTARTING,	/* Links up, start msg received & issued  */
				/* BOTHSTARTING now equiv to STARTING (?) */
	HB_R_RSCRCVD,		/* Resource Message received */
	HB_R_STABLE,		/* Local resources acquired, too... */
	HB_R_SHUTDOWN,		/* We're in shutdown... */
};

/*
 *	Note that the _RSC defines below are bit fields!
 */
#define HB_NO_RESOURCES		"none"
#define HB_NO_RSC			0

#define HB_LOCAL_RESOURCES		"local"
#define HB_LOCAL_RSC		1

#define HB_FOREIGN_RESOURCES	"foreign"
#define	HB_FOREIGN_RSC		2

#define HB_ALL_RSC		(HB_LOCAL_RSC|HB_FOREIGN_RSC)
#define HB_ALL_RESOURCES	"all"


/* Used by signal handlers */
void hb_init_watchdog(void);
void hb_tickle_watchdog(void);
void hb_close_watchdog(void);

int  hb_send_resources_held(const char *str, int stable, const char * comment);

gboolean hb_msp_final_shutdown(gpointer p);
gboolean hb_send_local_status(gpointer p);
gboolean hb_dump_all_proc_stats(gpointer p);

void hb_force_shutdown(void);
void hb_emergency_shutdown(void);

void hb_versioninfo(void);
void hb_dump_proc_stats(volatile struct process_info * proc);
void hb_trigger_restart(int quickrestart);

void hb_giveup_resources(void);
void hb_kill_tracked_process(ProcTrack* p, void * data);

#endif /* _HEARTBEAT_PRIVATE_H */
