static const char * _ha_msg_c_Id = "$Id: ha_msg_internal.c,v 1.20 2002/08/02 22:44:00 alan Exp $";
/*
 * ha_msg_internal: heartbeat internal messaging functions
 *
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

#include <portability.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/utsname.h>
#include <heartbeat.h>
#include <ha_msg.h>

#define		MINFIELDS	20
#define		CRNL		"\r\n"

/* Return the next message found in the stream and copies */
/* the iface in "iface"  */

struct ha_msg *
if_msgfromstream(FILE * f, char *iface)
{
	char		buf[MAXLINE];
	const char *	bufmax = buf + sizeof(buf);
	char *		getsret;
	struct ha_msg*	ret;
	

	(void)_ha_msg_c_Id;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;
	clearerr(f);

	if(!(getsret=fgets(buf, MAXLINE, f))) { 
		if (!ferror(f) || errno != EINTR) 
			ha_error("if_msgfromstream: cannot get message");
		return(NULL);
	}

	/* Try to find the interface in the message. */

	if (!strcmp(buf, IFACE)) {
		/* Found interface name header, get interface name. */
		if(!(getsret=fgets(buf, MAXLINE, f))) { 
			if (!ferror(f) || errno != EINTR)
				ha_error("if_msgfromstream: cannot get message");
			return(NULL);
		}
		if (iface) { 
			int len = strlen(buf);
			if(len < MAXIFACELEN) {
				strncpy(iface, buf, len);
				iface[len -1] = EOS;
			}
		}
	}

	if (strcmp(buf, MSG_START)) { 	
		/* Skip until we find a MSG_START (hopefully we skip nothing) */
		while ((getsret=fgets(buf, MAXLINE, f)) != NULL
		&&	strcmp(buf, MSG_START) != 0) {
			/* Nothing */
		}
	}

	if (getsret == NULL || (ret = ha_msg_new(0)) == NULL) {
		/* Getting an error with EINTR is pretty normal */
		if (!ferror(f) || errno != EINTR) {
			ha_error("if_msgfromstream: cannot get message");
		}
		return(NULL);
	}

	/* Add Name=value pairs until we reach MSG_END or EOF */
	while ((getsret=fgets(buf, MAXLINE, f)) != NULL
	&&	strcmp(buf, MSG_END) != 0) {

		/* Add the "name=value" string on this line to the message */
		if (ha_msg_add_nv(ret, buf, bufmax) != HA_OK) {
			ha_log(LOG_INFO
			,	"NV failure (if_msgfromsteam)(%s): [%s]"
			,	iface, buf);
			ha_msg_del(ret);
			return(NULL);
		}
	}
	return(ret);
}

/*
 *	Output string encoding both message and interface it came in on.
 */
char *
msg2if_string(const struct ha_msg *m, const char *iface) 
{

	int	j;
	char *	buf;
	char *	bp;	/* current position in output string (buf)
			 * Maintaining this makes this code lots faster because
			 * otherwise strcat is pretty slow
			 */
	int	ifaceLen;
	int	mlen;

	if (m->nfields <= 0) {
		ha_error("msg2if_string: Message with zero fields");
		return(NULL);
	}

	ifaceLen = strlen(iface);

	/* Note: m->stringlen is # of chars to convert "m" to a plain string */
	mlen = STRLEN(IFACE) + ifaceLen + STRLEN("\n") + m->stringlen;

	buf = ha_malloc(mlen * sizeof(char ));

	if (buf == NULL) {
		ha_error("msg2if_string: no memory for string");
	}else{
		/* Prepend information indicating incoming "interface" */
		strcpy(buf, IFACE);
		bp = buf + STRLEN(IFACE);

		strcat(bp, iface);
		bp += ifaceLen;

		strcat(bp, "\n");
		bp += STRLEN("\n");

		/* Append the normal (plain) string representation of the message */
		strcat(buf, MSG_START);
		for (j=0; j < m->nfields; ++j) {

			strcat(bp, m->names[j]);
			bp += m->nlens[j];

			strcat(bp, "=");
			bp += STRLEN("=");

			strcat(bp, m->values[j]);
			bp += m->vlens[j];

			strcat(bp, "\n");
			bp += STRLEN("\n");
		}
		strcat(bp, MSG_END);
	}
	return(buf);
}


#define	SEQ	"seq"
#define	LOAD1	"load1"

/* The value functions are expected to return pointers to static data */
struct default_vals {
	const char *	name;
	const char * 	(*value)(void);
	int		seqfield;
};

STATIC	const char * ha_msg_seq(void);
STATIC	const char * ha_msg_timestamp(void);
STATIC	const char * ha_msg_loadavg(void);
STATIC	const char * ha_msg_from(void);
STATIC	const char * ha_msg_ttl(void);
STATIC	const char * ha_msg_hbgen(void);

/* Each of these functions returns static data requiring copying */
struct default_vals defaults [] = {
	{F_ORIG,	ha_msg_from,	0},
	{F_SEQ,		ha_msg_seq,	1},
	{F_HBGENERATION,ha_msg_hbgen,	0},
	{F_TIME,	ha_msg_timestamp,0},
	{F_LOAD,	ha_msg_loadavg, 1},
	{F_TTL,		ha_msg_ttl, 0},
};

/* Reads from control fifo, and creates a new message from it */
/* (this adds a few default fields with timestamp, sequence #, etc.) */
struct ha_msg *
controlfifo2msg(FILE * f)
{
	char		buf[MAXLINE];
	const char *	bufmax = buf + sizeof(buf);
	char *		getsret;
	const char*	type;
	struct ha_msg*	ret;
	int		j;
	int		noseqno;

	/* Skip until we find a MSG_START (hopefully we skip nothing) */
	while ((getsret=fgets(buf, MAXLINE, f)) != NULL
	&&	strcmp(buf, MSG_START) != 0) {
		/* Nothing */
	}

	if (getsret == NULL || (ret = ha_msg_new(0)) == NULL) {
		return(NULL);
	}

	/* Add Name=value pairs until we reach MSG_END or EOF */
	while ((getsret=fgets(buf, MAXLINE, f)) != NULL
	&&	strcmp(buf, MSG_END) != 0) {

		/* Add the "name=value" string on this line to the message */
		if (ha_msg_add_nv(ret, buf, bufmax) != HA_OK) {
			ha_error("NV failure (controlfifo2msg):");
			ha_log(LOG_INFO, "%s", buf);
			ha_msg_del(ret);
			return(NULL);
		}
	}
	if ((type = ha_msg_value(ret, F_TYPE)) == NULL) {
		ha_log(LOG_ERR, "No type (controlfifo2msg)");
		ha_msg_del(ret);
		return(NULL);
	}

	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, "controlfifo2msg: input packet");
		ha_log_message(ret);
	}

	noseqno = (strncmp(type, NOSEQ_PREFIX, sizeof(NOSEQ_PREFIX)-1) == 0);

	/* Add our default name=value pairs */
	for (j=0; j < DIMOF(defaults); ++j) {

		/*
		 * Should we skip putting a sequence number on this packet?
		 *
		 * We don't want requests for retransmission to be subject
		 * to being retransmitted according to the protocol.  They
		 * need to be outside the normal retransmission protocol.
		 * To accomplish that, we avoid giving them sequence numbers.
		 */
		if (noseqno && defaults[j].seqfield) {
			continue;
		}

		/* Don't put in duplicate values already gotten */
		if (noseqno && ha_msg_value(ret, defaults[j].name) != NULL) {
			/* This keeps us from adding another "from" field */
			continue;
		}

		if (ha_msg_mod(ret, defaults[j].name, defaults[j].value())
		!=	HA_OK)  {
			ha_msg_del(ret);
			return(NULL);
		}
	}
	if (!add_msg_auth(ret)) {
		ha_msg_del(ret);
		ret = NULL;
	}
	if (DEBUGPKTCONT) {
		ha_log(LOG_DEBUG, "controlfifo2msg: packet returned");
		ha_log_message(ret);
	}

	return(ret);
}

int
add_msg_auth(struct ha_msg * m)
{
	char	msgbody[MAXMSG];
	char	authstring[MAXLINE];
	char	authtoken[MAXLINE];
	char *	bp = msgbody;
	int	j;

	{
		const char *	from;
		const char *	ts;
		const char *	type;

		/* Extract message type, originator, timestamp, auth */
		type = ha_msg_value(m, F_TYPE);
		from = ha_msg_value(m, F_ORIG);
		ts = ha_msg_value(m, F_TIME);

		if (from == NULL || ts == NULL || type == NULL) {
			ha_log(LOG_ERR
			,	"add_msg_auth: %s:  from %s"
			,	"missing from/ts/type"
			,	(from? from : "<?>"));
			ha_log_message(m);
		}
	}

	check_auth_change(config);
	msgbody[0] = EOS;
	for (j=0; j < m->nfields; ++j) {
		/* Skip over any F_AUTH fields we find... */
		if (strcmp(m->names[j], F_AUTH) == 0) {
			continue;
		}
		strcat(bp, m->names[j]);
		bp += m->nlens[j];
		strcat(bp, "=");
		bp++;
		strcat(bp, m->values[j]);
		bp += m->vlens[j];
		strcat(bp, "\n");
		bp++;
	}


	if (!config->authmethod->auth->auth(config->authmethod, msgbody
	,	authtoken, DIMOF(authtoken))) {
		ha_log(LOG_ERR 
		,	"Cannot compute message authentication [%s/%s/%s]"
		,	config->authmethod->authname
		,	config->authmethod->key
		,	msgbody);
		return(HA_FAIL);
	}

	sprintf(authstring, "%d %s", config->authnum, authtoken);

	/* It will add it if it's not there yet, or modify it if it is */

	return(ha_msg_mod(m, F_AUTH, authstring));
}

int
isauthentic(const struct ha_msg * m)
{
	char	msgbody[MAXMSG];
	char	authstring[MAXLINE];
	char	authbuf[MAXLINE];
	const char *	authtoken = NULL;
	char *	bp = msgbody;
	int	j;
	int	authwhich = 0;
	struct HBauth_info*	which;
	
	if (m->stringlen >= sizeof(msgbody)) {
		return(0);
	}

	/* Reread authentication? */
	check_auth_change(config);

	msgbody[0] = EOS;
	for (j=0; j < m->nfields; ++j) {
		if (strcmp(m->names[j], F_AUTH) == 0) {
			authtoken = m->values[j];
			continue;
		}
		strcat(bp, m->names[j]);
		bp += m->nlens[j];
		strcat(bp, "=");
		bp++;
		strcat(bp, m->values[j]);
		bp += m->vlens[j];
		strcat(bp, "\n");
		bp++;
	}
	
	if (authtoken == NULL
	||	sscanf(authtoken, "%d %s", &authwhich, authstring) != 2) {
		ha_log(LOG_WARNING, "Bad/invalid auth token");
		return(0);
	}
	which = config->auth_config + authwhich;

	if (authwhich < 0 || authwhich >= MAXAUTH || which->auth == NULL) {
		ha_log(LOG_WARNING
		,	"Invalid authentication type [%d] in message!"
		,	authwhich);
		return(0);
	}
		
	
	if (!which->auth->auth(which, msgbody, authbuf, DIMOF(authbuf))) {
		ha_log(LOG_ERR, "Failed to compute message authentication");
		return(0);
	}
	if (strcmp(authstring, authbuf) == 0) {
		if (DEBUGAUTH) {
			ha_log(LOG_DEBUG, "Packet authenticated");
		}
		return(1);
	}
	if (DEBUGAUTH) {
		ha_log(LOG_INFO, "Packet failed authentication check");
	}
	return(0);
}


/* Add field to say who this packet is from */
STATIC	const char *
ha_msg_from(void)
{
	static struct utsname u;
	static int uyet = 0;
	if (!uyet) {
		uname(&u);
		uyet++;
	}
	return(u.nodename);
}

/* Add sequence number field */
STATIC	const char *
ha_msg_seq(void)
{
	static char seq[32];
	static int seqno = 1;
	sprintf(seq, "%x", seqno);
	++seqno;
	return(seq);
}

/* Add local timestamp field */
STATIC	const char *
ha_msg_timestamp(void)
{
	static char ts[32];
	sprintf(ts, "%lx", time(NULL));
	return(ts);
}

/* Add load average field */
STATIC	const char *
ha_msg_loadavg(void)
{
	static char	loadavg[64];
	static int 		fd = -1;

	/*
	 * NOTE:  We never close 'fd'
	 * We keep it open to avoid touching the real filesystem once we
	 * are running, and avoid realtime problems.  I don't know that
	 * this was a significant problem, but if updates were being made
	 * to the / or /proc directories, then we could get blocked,
	 * and this was a very simple fix.
	 *
	 * We should probably get this information once every few seconds
	 * and use that, but this is OK for now...
	 * get blocked.
	 */

	if (fd < 0 && (fd=open(LOADAVG, O_RDONLY)) < 0 ) {
		strcpy(loadavg, "n/a");
	}else{
		lseek(fd, 0, SEEK_SET);
		read(fd, loadavg, sizeof(loadavg));
		loadavg[sizeof(loadavg)-1] = EOS;
	}
	loadavg[strlen(loadavg)-1] = EOS;
	return(loadavg);
}

STATIC	const char *
ha_msg_ttl(void)
{
	static char	ttl[8];
	snprintf(ttl, sizeof(ttl), "%d", config->hopfudge + config->nodecount);
	return(ttl);
}

STATIC	const char *
ha_msg_hbgen(void)
{
	static char	hbgen[32];
	snprintf(hbgen, sizeof(hbgen), "%lx", config->generation);
	return(hbgen);
}


#ifdef TESTMAIN_MSGS
int
main(int argc, char ** argv)
{
	struct ha_msg*	m;
	while (!feof(stdin)) {
		if ((m=controlfifo2msg(stdin)) != NULL) {
			fprintf(stderr, "Got message!\n");	
			if (msg2stream(m, stdout) == HA_OK) {
				fprintf(stderr, "Message output OK!\n");
			}else{
				fprintf(stderr, "Could not output Message!\n");
			}
		}else{
			fprintf(stderr, "Could not get message!\n");
		}
	}
	return(0);
}
#endif
/*
 * $Log: ha_msg_internal.c,v $
 * Revision 1.20  2002/08/02 22:44:00  alan
 * Enhanced an error message when we get a name/value (NV) failure.
 *
 * Revision 1.19  2002/07/08 04:14:12  alan
 * Updated comments in the front of various files.
 * Removed Matt's Solaris fix (which seems to be illegal on Linux).
 *
 * Revision 1.18  2002/04/13 22:35:08  alan
 * Changed ha_msg_add_nv to take an end pointer to make it safer.
 * Added a length parameter to string2msg so it would be safer.
 * Changed the various networking plugins to use the new string2msg().
 *
 * Revision 1.17  2002/04/07 13:54:06  alan
 * This is a pretty big set of changes ( > 1200 lines in plain diff)
 *
 * The following major bugs have been fixed
 *  - STONITH operations are now a precondition for taking over
 *    resources from a dead machine
 *
 *  - Resource takeover events are now immediately terminated when shutting
 *    down - this keeps resources from being held after shutting down
 *
 *  - heartbeat could sometimes fail to start due to how it handled its
 *    own status through two different channels.  I restructured the handling
 *    of local status so that it's now handled almost exactly like handling
 *    the status of remote machines
 *
 * There is evidence that all these serious bugs have been around a long time,
 * even though they are rarely (if ever) seen.
 *
 * The following minor bugs have been fixed:
 *
 *  - the standby test now retries during transient conditions...
 *
 *  - the STONITH code for the test method "ssh" now uses "at" to schedule
 *    the stonith operation on the other node so it won't hang when using
 *    newer versions of ssh.
 *
 * The following new test was added:
 *  - SimulStart - starting all nodes ~ simultaneously
 *
 * The following significant restructuring of the code occurred:
 *
 *  - Completely rewrote the process management and death-of-child code to
 *    be uniform, and be based on a common semi-object-oriented approach
 *    The new process tracking code is very general, and I consider it to
 *    be part of the plumbing for the OCF.
 *
 *  - Completely rewrote the event handling code to be based on the Glib
 *    mainloop paradigm. The sets of "inputs" to the main loop are:
 *     - "polled" events like signals, and once-per-loop occurrances
 *     - messages from the cluster and users
 *     - API registration requests from potential clients
 *     - API calls from clients
 *
 *
 * The following minor changes were made:
 *
 *  - when nice_failback is taking over resources, since we always negotiate for
 *    taking them over, so we no longer have a timeout waiting for the other
 *    side to reply.  As a result, the timeout for waiting for the other
 *    side is now much longer than it was.
 *
 *  - transient errors for standby operations now print WARN instead of EROR
 *
 *  - The STONITH and standby tests now don't print funky output to the
 *    logs.
 *
 *  - added a new file TESTRESULTS.out for logging "official" test results.
 *
 * Groundwork was laid for the following future changes:
 *  - merging the control and master status processes
 *
 *  - making a few other things not wait for process completion in line
 *
 *  - creating a comprehensive asynchronous action structure
 *
 *  - getting rid of the "interface" kludge currently used for tracking
 *    activity on individual interfaces
 *
 * The following things still need to be tested:
 *
 *  - STONITH testing (including failures)
 *
 *  - clock jumps
 *
 *  - protocol retransmissions
 *
 *  - cross-version compatability of status updates (I added a new field)
 *
 * Revision 1.16  2002/03/27 02:10:22  alan
 * Finished (hopefully) the last bug fix.  Now it won't complain
 * if it authenticates a packet without a sequence number.  This was kinda
 * dumb anyway.  I know packets go out w/o seq numbers...
 *
 * Revision 1.15  2002/03/15 14:26:36  alan
 * Added code to help debug the current missing to/from/ts/,etc. problem...
 *
 * Revision 1.14  2001/10/25 14:17:28  alan
 * Changed a few of the errors into warnings.
 *
 * Revision 1.13  2001/10/24 20:46:28  alan
 * A large number of patches.  They are in these categories:
 * 	Fixes from Matt Soffen
 * 	Fixes to test environment things - including changing some ERRORs to
 * 		WARNings and vice versa.
 * 	etc.
 *
 * Revision 1.12  2001/09/29 19:08:24  alan
 * Wonderful security and error correction patch from Emily Ratliff
 * 	<ratliff@austin.ibm.com>
 * Fixes code to have strncpy() calls instead of strcpy calls.
 * Also fixes the number of arguments to several functions which were wrong.
 * Many thanks to Emily.
 *
 * Revision 1.11  2001/07/18 03:12:52  alan
 * Put in a couple of minor security fixes from Emily Ratliff.
 * The ttl value put in the messages is now checked for overflow, and the
 * hopfudge value it is based on is now bounded to 255...
 *
 * Revision 1.10  2001/07/17 15:00:04  alan
 * Put in Matt's changes for findif, and committed my changes for the new module loader.
 * You now have to have glib.
 *
 * Revision 1.9  2001/06/19 13:56:28  alan
 * FreeBSD portability patch from Matt Soffen.
 * Mainly added #include "portability.h" to lots of files.
 * Also added a library to Makefile.am
 *
 * Revision 1.8  2001/06/06 23:10:10  alan
 * Comment clarification as a result of Emily's code audit.
 *
 * Revision 1.7  2001/06/06 23:07:44  alan
 * Put in some code clarifications suggested by Emily Ratliff.  Thanks Emily!
 *
 * Revision 1.6  2001/04/19 13:41:54  alan
 * Removed the two annoying "error" messages that occur when heartbeat
 * is shut down.  They are: "controlfifo2msg: cannot create message"
 * and "control_process: NULL message"
 *
 * Revision 1.5  2000/09/10 03:48:52  alan
 * Fixed a couple of bugs.
 * - packets that were already authenticated didn't get reauthenticated correctly.
 * - packets that were irretrievably lost didn't get handled correctly.
 *
 * Revision 1.4  2000/08/11 00:30:07  alan
 * This is some new code that does two things:
 * 	It has pretty good replay attack protection
 * 	It has sort-of-basic recovery from a split partition.
 *
 * Revision 1.3  2000/07/26 05:17:19  alan
 * Added GPL license statements to all the code.
 *
 * Revision 1.2  2000/07/19 23:03:53  alan
 * Working version of most of the API code.  It still has the security bug...
 *
 * Revision 1.1  2000/07/11 00:25:52  alan
 * Added a little more API code.  It looks like the rudiments are now working.
 *
 *
 */
