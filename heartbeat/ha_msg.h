/*
 * Intracluster message object (struct ha_msg)
 *
 * Copyright (C) 1999, 2000 Alan Robertson <alanr@unix.sh>
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

#ifndef _HA_MSG_H
#	define _HA_MSG_H 1
static const char * _ha_msg_h_Id = "$Id: ha_msg.h,v 1.12 2000/08/11 00:30:07 alan Exp $";
#include <stdio.h>

struct ha_msg {
	int	nfields;
	int	nalloc;
	int	stringlen;
	char **	names;
	int  *	nlens;
	char **	values;
	int  *	vlens;
};
#define	IFACE		"!^!\n"  
#define	MSG_START	">>>\n"
#define	MSG_END		"<<<\n"
#define	EQUAL		"="

#define	MAXMSG	1400	/* Maximum string length for a message */

	/* Common field names for our messages */
#define	F_TYPE		"t"		/* Message type */
#define	F_ORIG		"src"		/* Originator */
#define	F_NODE		"node"		/* Originator */
#define	F_TO		"dest"		/* Destination (optional) */
#define	F_STATUS	"st"		/* New status (type = status) */
#define	F_TIME		"ts"		/* Timestamp */
#define F_SEQ		"seq"		/* Sequence number */
#define	F_LOAD		"ld"		/* Load average */
#define	F_COMMENT	"info"		/* Comment */
#define	F_TTL		"ttl"		/* Time To Live */
#define F_AUTH          "auth"		/* Authentication string */
#define F_HBGENERATION	"hg"		/* Heartbeat generation number */
#define F_FIRSTSEQ      "firstseq"	/* Lowest seq # to retransmit */
#define F_LASTSEQ       "lastseq"	/* Highest seq # to retransmit */
#define F_RESOURCES	"rsc_hold"      /* What resources do we hold? */
#define F_FROMID	"from_id"	/* from Client id */
#define F_TOID		"to_id"		/* To client id */
#define F_PID		"pid"		/* PID of client */
#define F_ISSTABLE	"isstable"	/* true/false for RESOURCES */
#define F_APIREQ	"reqtype"	/* API request type for "hbapi" */
#define F_APIRESULT	"result"	/* API request result code */
#define F_IFNAME	"ifname"	/* Interface name */


#define	T_STATUS	"status"	/* Message type = Status */
#define	T_IFSTATUS	"ifstat"	/* Message type = Interface status */
#define	NOSEQ_PREFIX	"NS_"		/* Give no sequence number */
#define	T_REXMIT	"NS_rexmit"	/* Message type = Retransmit request */
#define	T_NAKREXMIT	"NS_nak_rexmit"	/* Message type = NAK Re-xmit rqst */
#define T_STARTING      "starting"      /* Message type = Starting Heartbeat */
#define T_RESOURCES	"resource"      /* Message type = Resources info */
#define T_APIREQ	"hbapi-req" 	/* Message type = Heartbeat API req */
#define T_APIRESP	"hbapi-resp" 	/* Message type = Heartbeat API req */


/* Allocate new (empty) message */
struct ha_msg *	ha_msg_new(int nfields);

/* Free message */
void		ha_msg_del(struct ha_msg *msg);

/* Add null-terminated name and a value to the message */
int		ha_msg_add(struct ha_msg * msg
		,	const char* name, const char* value);

/* Modify null-terminated name and a value to the message */
int		ha_msg_mod(struct ha_msg * msg
		,	const char* name, const char* value);

/* Add name, value (with known lengths) to the message */
int		ha_msg_nadd(struct ha_msg * msg, const char * name, int namelen
		,	const char * value, int vallen);

/* Add name=value string to a message */
int		ha_msg_add_nv(struct ha_msg* msg, const char * nvline);

/* Return value associated with particular name */
const char *	ha_msg_value(const struct ha_msg * msg, const char * name);

/* Reads a stream -- converts it into a message */
struct ha_msg *	msgfromstream(FILE * f);

/* Same as above plus copying the iface name to "iface" */
struct ha_msg * if_msgfromstream(FILE * f, char *iface);

/* Writes a message into a stream */
int		msg2stream(struct ha_msg* m, FILE * f);

/* Converts a message into a string and adds the iface name on start */
char *     msg2if_string(const struct ha_msg *m, const char * iface);

/* Converts a string gotten via UDP into a message */
struct ha_msg *	string2msg(const char * s);

/* Converts a message into a string for sending out UDP interface */
char *		msg2string(const struct ha_msg *m);

/* Reads from control fifo, and creates a new message from it */
/* This adds the default sequence#, load avg, etc. to the message */
struct ha_msg *	controlfifo2msg(FILE * f);
void		ha_log_message(const struct ha_msg* msg);
int		isauthentic(const struct ha_msg * msg);
#endif /* __HA_MSG_H */
