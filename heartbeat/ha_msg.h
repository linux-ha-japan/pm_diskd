#ifndef _HA_MSG_H
#	define _HA_MSG_H

static const char * _ha_msg_h_Id = "$Id: ha_msg.h,v 1.1 1999/09/23 15:31:24 alanr Exp $";
#include <stdio.h>
/*
 *	Intracluster message object (struct ha_msg)
 */

struct ha_msg {
	int	nfields;
	int	nalloc;
	char **	names;
	char **	values;
};

#define	MSG_START	">>>\n"
#define	MSG_END		"<<<\n"
#define	EQUAL		"="

	/* Common field names for our messages */
#define	F_TYPE		"t"		/* Message type */
#define	F_ORIG		"src"		/* Originator */
#define	F_TO		"dest"		/* Destination (optional) */
#define	F_STATUS	"st"		/* New status (type = status) */
#define	F_TIME		"ts"		/* Timestamp */
#define F_SEQ		"seq"		/* Sequence number */
#define	F_LOAD		"ld"		/* Load average */
#define	F_COMMENT	"info"		/* Comment */
#define	F_TTL		"ttl"		/* Time To Live */

#define	T_STATUS	"status"	/* Message type = Status */


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

/* Writes a message into a stream */
int		msg2stream(struct ha_msg* m, FILE * f);

/* Converts a string gotten via UDP into a message */
struct ha_msg *	string2msg(const char * s);

/* Converts a message into a string for sending out UDP interface */
char *		msg2string(const struct ha_msg *m);

/* Reads from control fifo, and creates a new message from it */
/* This adds the default sequence#, load avg, etc. to the message */
struct ha_msg *	controlfifo2msg(FILE * f);
void		ha_log_message(const struct ha_msg* msg);
#endif
