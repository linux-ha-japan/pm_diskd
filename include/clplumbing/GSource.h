/* $Id: GSource.h,v 1.14 2006/01/30 18:41:59 alan Exp $ */
/*
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _CLPLUMBING_GSOURCE_H
#	define _CLPLUMBING_GSOURCE_H
#	include <clplumbing/ipc.h>

typedef	struct GFDSource_s	GFDSource;
typedef struct GCHSource_s	GCHSource;
typedef struct GWCSource_s	GWCSource;
typedef struct GSIGSource_s	GSIGSource;
typedef struct GTRIGSource_s	GTRIGSource;


void G_main_setmaxdispatchdelay(GSource* s, unsigned long delayms);
void G_main_setmaxdispatchtime(GSource* s, unsigned long dispatchms);


/***********************************************************************
 *	Functions for interfacing input to the mainloop
 ***********************************************************************/

GSource*
G_main_add_input(int priority, 
		 gboolean can_recurse,
		 GSourceFuncs* funcs);

/***********************************************************************
 *	Functions for interfacing "raw" file descriptors to the mainloop
 ***********************************************************************/
/*
*	Add a file descriptor to the gmainloop world...
 */
GFDSource* G_main_add_fd(int priority, int fd, gboolean can_recurse
,	gboolean (*dispatch)(int fd, gpointer user_data)
,	gpointer userdata
,	GDestroyNotify notify);

/*
 *	Delete a file descriptor from the gmainloop world...
 *	Note: destroys the GFDSource object.
 */
gboolean G_main_del_fd(GFDSource* fdp);

/*
 *	Notify us that a file descriptor is blocked on output.
 *	(i.e., we should poll for output events)
 */
void g_main_output_is_blocked(GFDSource* fdp);


/**************************************************************
 *	Functions for interfacing IPC_Channels to the mainloop
 **************************************************************/
/*
 *	Add an IPC_channel to the gmainloop world...
 */
GCHSource* G_main_add_IPC_Channel(int priority, IPC_Channel* ch
,	gboolean can_recurse
,	gboolean (*dispatch)(IPC_Channel* source_data
,		gpointer        user_data)
,	gpointer userdata
,	GDestroyNotify notify);

/*
 *	the events in this source is paused/resumed
 */

void	G_main_IPC_Channel_pause(GCHSource* chp);
void	G_main_IPC_Channel_resume(GCHSource* chp);


/*
 *	Delete an IPC_channel from the gmainloop world...
 *	Note: destroys the GCHSource object, and the IPC_Channel
 *	object automatically.
 */
gboolean G_main_del_IPC_Channel(GCHSource* chp);


/*
 *	Set the destroy notify function
 *
 */
void	set_IPC_Channel_dnotify(GCHSource* chp,
				GDestroyNotify notify);


/*********************************************************************
 *	Functions for interfacing IPC_WaitConnections to the mainloop
 ********************************************************************/
/*
 *	Add an IPC_WaitConnection to the gmainloop world...
 *	Note that the dispatch function is called *after* the
 *	connection is accepted.
 */
GWCSource* G_main_add_IPC_WaitConnection(int priority, IPC_WaitConnection* ch
,	IPC_Auth* auth_info
,	gboolean can_recurse
,	gboolean (*dispatch)(IPC_Channel* source_data
,		gpointer user_data)
,	gpointer userdata
,	GDestroyNotify notify);

/*
 *	Delete an IPC_WaitConnection from the gmainloop world...
 *	Note: destroys the GWCSource object, and the IPC_WaitConnection
 *	object automatically.
 */
gboolean G_main_del_IPC_WaitConnection(GWCSource* wcp);


/**************************************************************
 *	Functions for interfacing Signals to the mainloop
 **************************************************************/
/*
 *	Add an Signal to the gmainloop world...
 */
GSIGSource* G_main_add_SignalHandler(
	int priority, int signal,
	gboolean (*dispatch)(int nsig, gpointer user_data),
	gpointer userdata, GDestroyNotify notify);

/*
 *	Delete an signal from the gmainloop world...
 *	Note: destroys the GSIGSource object, and the removes the
 *	Signal Handler automatically.
 */
gboolean G_main_del_SignalHandler(GSIGSource* chp);


/*
 *	Set the destroy notify function
 *
 */
void	set_SignalHandler_dnotify(GSIGSource* chp, GDestroyNotify notify);


/*	manage child process death using sig source*/
void	set_sigchld_proctrack(int priority);
     


/**************************************************************
 *	Functions for interfacing Manual triggers to the mainloop
 **************************************************************/
/*
 *	Add an Trigger to the gmainloop world...
 */
GTRIGSource* G_main_add_TriggerHandler(
	int priority, gboolean (*dispatch)(gpointer user_data),
	gpointer userdata, GDestroyNotify notify);

/*
 *	Delete an signal from the gmainloop world...
 *	Note: destroys the GTRIGSource object, and the removes the
 *	Trigger Handler automatically.
 */
gboolean G_main_del_TriggerHandler(GTRIGSource* chp);


/*
 *	Set the destroy notify function
 *
 */
void	set_TriggerHandler_dnotify(GTRIGSource* chp, GDestroyNotify notify);


void G_main_set_trigger(GTRIGSource* man_src);

#endif
