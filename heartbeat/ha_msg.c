static const char * _ha_msg_c_Id = "$Id: ha_msg.c,v 1.9 1999/11/22 20:28:23 alan Exp $";
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/utsname.h>
#include <heartbeat.h>
#include <ha_msg.h>

#define		MINFIELDS	20
#define		CRNL		"\r\n"

/*
 *	Heartbeat messaging object.
 */

/* Create a new (empty) message */
struct ha_msg *
ha_msg_new(nfields)
{
	struct ha_msg *	ret;

	(void)_heartbeat_h_Id;
	(void)_ha_msg_c_Id;
	(void)_ha_msg_h_Id;
	ret = MALLOCT(struct ha_msg);
	if (ret) {
		ret->nfields = 0;
		ret->nalloc	= MINFIELDS;
		ret->names	= (char **)ha_malloc(sizeof(char *)*MINFIELDS);
		ret->nlens	= (int *)ha_malloc(sizeof(int)*MINFIELDS);
		ret->values	= (char **)ha_malloc(sizeof(char *)*MINFIELDS);
		ret->vlens	= (int *)ha_malloc(sizeof(int)*MINFIELDS);
		ret->stringlen	= sizeof(MSG_START)+sizeof(MSG_END)-1;
		if (ret->names == NULL || ret->values == NULL
		||	ret->nlens == NULL || ret->vlens == NULL) {
			ha_error("ha_msg_new: out of memory for ha_msg");
			ha_msg_del(ret);
			ret = NULL;
		}else if (curproc) {
			curproc->allocmsgs++;
			curproc->totalmsgs++;
			curproc->lastmsg = time(NULL);
		}
	}
	return(ret);
}

/* Delete (destroy) a message */
void
ha_msg_del(struct ha_msg *msg)
{
	if (msg) {
		int	j;
		if (curproc) {
			curproc->allocmsgs--;
		}
		if (msg->names) {
			for (j=0; j < msg->nfields; ++j) {
				if (msg->names[j]) {
					ha_free(msg->names[j]);
				}
			}
			ha_free(msg->names);
			msg->names = NULL;
		}
		if (msg->values) {
			for (j=0; j < msg->nfields; ++j) {
				if (msg->values[j]) {
					ha_free(msg->values[j]);
				}
			}
			ha_free(msg->values);
			msg->values = NULL;
		}
		if (msg->nlens) {
			ha_free(msg->nlens);
			msg->nlens = NULL;
		}
		if (msg->vlens) {
			ha_free(msg->vlens);
			msg->vlens = NULL;
		}
		msg->nfields = -1;
		msg->nalloc = -1;
		msg->stringlen = -1;
		ha_free(msg);
	}
}

/* Add a null-terminated name and value to a message */
int
ha_msg_add(struct ha_msg * msg, const char * name, const char * value)
{
	return(ha_msg_nadd(msg, name, strlen(name), value, strlen(value)));
}

/* Add a name/value pair to a message (with sizes for name and value) */
int
ha_msg_nadd(struct ha_msg * msg, const char * name, int namelen
		,	const char * value, int vallen)
{
	int	next;
	char *	cpname;
	char *	cpvalue;
	int	startlen = sizeof(MSG_START)-1;
	int	newlen = msg->stringlen + (namelen+vallen+2);	/* 2 == "=" + "\n" */

	if (!msg || (msg->nfields >= msg->nalloc)
	||	msg->names == NULL || msg->values == NULL) {
		ha_error("ha_msg_nadd: cannot add field to ha_msg");
		return(HA_FAIL);
	}
	if (name == NULL || value == NULL
	||	namelen <= 0 || vallen <= 0 || newlen >= MAXMSG) {
		ha_error("ha_msg_nadd: cannot add name/value to ha_msg");
		return(HA_FAIL);
	}

	if (namelen >= startlen && strncmp(name, MSG_START, startlen) == 0) {
		ha_error("ha_msg_nadd: illegal field");
		return(HA_FAIL);
	}
		

	if ((cpname = ha_malloc(namelen+1)) == NULL) {
		ha_error("ha_msg_nadd: no memory for string (name)");
		return(HA_FAIL);
	}
	if ((cpvalue = ha_malloc(vallen+1)) == NULL) {
		ha_free(cpname);
		ha_error("ha_msg_nadd: no memory for string (value)");
		return(HA_FAIL);
	}
	/* Copy name, value, appending EOS to the end of the strings */
	strncpy(cpname, name, namelen);		cpname[namelen] = EOS;
	strncpy(cpvalue, value, vallen);	cpvalue[vallen] = EOS;

	next = msg->nfields;
	msg->values[next] = cpvalue;
	msg->vlens[next] = vallen;
	msg->names[next] = cpname;
	msg->nlens[next] = namelen;
	msg->stringlen = newlen;
	msg->nfields++;
	return(HA_OK);
}

/* Add a "name=value" line to the name, value pairs in a message */
int
ha_msg_add_nv(struct ha_msg* msg, const char * nvline)
{
	int		namelen;
	const char *	valp;
	int		vallen;

	if (!nvline) {
		ha_error("ha_msg_add_nv: NULL nvline");
		return(HA_FAIL);
	}
	/* How many characters before the '='? */
	if ((namelen = strcspn(nvline, EQUAL)) <= 0
	||	nvline[namelen] != '=') {
		ha_error("ha_msg_add_nv: line doesn't contain '='");
		ha_error(nvline);
		return(HA_FAIL);
	}
	valp = nvline + namelen +1; /* Point just *past* the '=' */
	vallen = strcspn(valp, CRNL);

	/* Call ha_msg_nadd to actually add the name/value pair */
	return(ha_msg_nadd(msg, nvline, namelen, valp, vallen));
	
}

/* Return the value associated with a particular name */
const char *
ha_msg_value(const struct ha_msg * msg, const char * name)
{
	int	j;
	if (!msg || !msg->names || !msg->values) {
		ha_error("ha_msg_value: NULL msg");
		return(NULL);
	}

	for (j=0; j < msg->nfields; ++j) {
		if (strcmp(name, msg->names[j]) == 0) {
			return(msg->values[j]);
		}
	}
	return(NULL);
}


/* Modify the value associated with a particular name */
int
ha_msg_mod(struct ha_msg * msg, const char * name, const char * value)
{
	int	j;
	for (j=0; j < msg->nfields; ++j) {
		if (strcmp(name, msg->names[j]) == 0) {
			char *	newv = ha_malloc(strlen(value)+1);
			if (newv == NULL) {
				ha_error("ha_msg_mod: out of memory");
				return(HA_FAIL);
			}
			ha_free(msg->values[j]);
			msg->values[j] = newv;
			strcpy(newv, value);
			return(HA_OK);
		}
	}
	return(ha_msg_add(msg, name, value));
}


/* Return the next message found in the stream */
struct ha_msg *
msgfromstream(FILE * f)
{
	char		buf[MAXLINE];
	char *		getsret;
	struct ha_msg*	ret;

	clearerr(f);
	/* Skip until we find a MSG_START (hopefully we skip nothing) */
	while ((getsret=fgets(buf, MAXLINE, f)) != NULL
	&&	strcmp(buf, MSG_START) != 0) {
		/* Nothing */
	}

	if (getsret == NULL || (ret = ha_msg_new(0)) == NULL) {
		/* Getting an error with EINTR is pretty normal */
		if (!ferror(f) || errno != EINTR) {
			ha_error("msgfromstream: cannot get message");
		}
		return(NULL);
	}

	/* Add Name=value pairs until we reach MSG_END or EOF */
	while ((getsret=fgets(buf, MAXLINE, f)) != NULL
	&&	strcmp(buf, MSG_END) != 0) {

		/* Add the "name=value" string on this line to the message */
		if (ha_msg_add_nv(ret, buf) != HA_OK) {
			ha_error("NV failure (msgfromsteam):");
			ha_error(buf);
			ha_msg_del(ret);
			return(NULL);
		}
	}
	return(ret);
}

/* Writes a message into a stream - used for serial lines */
int	
msg2stream(struct ha_msg* m, FILE * f)
{
	char *	s  = msg2string(m);
	if (s != NULL) {
		fputs(s, f);
		fflush(f);
		ha_free(s);
		return(HA_OK);
	}else{
		return(HA_FAIL);
	}
}

/* Converts a string (perhaps gotten via UDP) into a message */
struct ha_msg *
string2msg(const char * s)
{
	struct ha_msg*	ret;
	const char *	sp = s;
	int		startlen;
	int		endlen;

	if ((ret = ha_msg_new(0)) == NULL) {
		return(NULL);
	}

	startlen = sizeof(MSG_START)-1;
	if (strncmp(sp, MSG_START, startlen) != 0) {
		ha_log(LOG_ERR, "string2msg: no MSG_START");
		ha_log(LOG_ERR, "Bad message is: [%s]", sp);
		return(NULL);
	}else{
		sp += startlen;
	}

	endlen = sizeof(MSG_END)-1;

	/* Add Name=value pairs until we reach MSG_END or end of string */

	while (*sp != EOS && strncmp(sp, MSG_END, endlen) != 0) {

		/* Skip over initial CR/NL things */
		sp += strspn(sp, CRNL);

		/* End of message marker? */
		if (strncmp(sp, MSG_END, endlen) == 0) {
			break;
		}
		/* Add the "name=value" string on this line to the message */
		if (ha_msg_add_nv(ret, sp) != HA_OK) {
			ha_error("NV failure (string2msg):");
			ha_error(s);
			ha_msg_del(ret);
			return(NULL);
		}
		sp += strcspn(sp, CRNL);
	}
	return(ret);
}


/* Converts a message into a string (for sending out UDP interface) */
char *
msg2string(const struct ha_msg *m)
{
	int	j;
	char *	buf;
	char *	bp;

	if (m->nfields <= 0) {
		ha_error("msg2string: Message with zero fields");
		return(NULL);
	}

	buf = ha_malloc(m->stringlen);

	if (buf == NULL) {
		ha_error("msg2string: no memory for string");
	}else{
		bp = buf;
		strcpy(buf, MSG_START);
		for (j=0; j < m->nfields; ++j) {
			strcat(bp, m->names[j]);
			bp += m->nlens[j];
			strcat(bp, "=");
			bp++;
			strcat(bp, m->values[j]);
			bp += m->vlens[j];
			strcat(bp, "\n");
			bp++;
		}
		strcat(bp, MSG_END);
	}
	return(buf);
}

void
ha_log_message (const struct ha_msg *m)
{
	int	j;

	ha_log(LOG_INFO, "MSG: Dumping message with %d fields", m->nfields);

	for (j=0; j < m->nfields; ++j) {
		ha_log(LOG_INFO, "MSG[%d]: %s=%s",j, m->names[j], m->values[j]);
	}
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

/* Each of these functions returns static data requiring copying */
struct default_vals defaults [] = {
	{F_ORIG,	ha_msg_from,	0},
	{F_SEQ,		ha_msg_seq,	1},
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
		ha_error("msgfromstream: cannot get message");
		return(NULL);
	}

	/* Add Name=value pairs until we reach MSG_END or EOF */
	while ((getsret=fgets(buf, MAXLINE, f)) != NULL
	&&	strcmp(buf, MSG_END) != 0) {

		/* Add the "name=value" string on this line to the message */
		if (ha_msg_add_nv(ret, buf) != HA_OK) {
			ha_error("NV failure (controlfifo2msg):");
			ha_error(buf);
			ha_msg_del(ret);
			return(NULL);
		}
	}
	if ((type = ha_msg_value(ret, F_TYPE)) == NULL) {
		ha_log(LOG_ERR, "No type (controlfifo2msg)");
		ha_msg_del(ret);
		return(NULL);
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

		/* Don't put in duplicate values already gotten from other side */
		if (noseqno && ha_msg_value(ret, defaults[j].name) != NULL) {
			/* This keeps us from adding another "from" field */
			continue;
		}

		if (ha_msg_add(ret, defaults[j].name, defaults[j].value())
		!=	HA_OK)  {
			ha_msg_del(ret);
			return(NULL);
		}
	}
	if (!add_msg_auth(ret)) {
		ha_msg_del(ret);
		ret = NULL;
	}

	return(ret);
}

int
add_msg_auth(struct ha_msg * m)
{
	char	msgbody[MAXMSG];
	char	authstring[MAXLINE];
	const char *	authtoken;
	char *	bp = msgbody;
	int	j;

	check_auth_change(config);
	msgbody[0] = EOS;
	for (j=0; j < m->nfields; ++j) {
		strcat(bp, m->names[j]);
		bp += m->nlens[j];
		strcat(bp, "=");
		bp++;
		strcat(bp, m->values[j]);
		bp += m->vlens[j];
		strcat(bp, "\n");
		bp++;
	}


	if ((authtoken
	=	config->authmethod->auth->auth(config->authmethod, msgbody))
	==	NULL) {
		ha_log(LOG_ERR, authstring
		,	"Cannot compute message authentication [%s/%s/%s]"
		,	config->authmethod->auth->authname
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
	const char *	authtoken = 0;
	char *	bp = msgbody;
	int	j;
	int	authwhich = 0;
	struct auth_info*	which;
	
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
		ha_error("Bad/invalid auth token");
		return(0);
	}
	which = config->auth_config + authwhich;

	if (authwhich < 0 || authwhich >= MAXAUTH || which->auth == NULL) {
		ha_log(LOG_ERR
		,	"Invalid authentication type [%d] in message!"
		,	authwhich);
		return(0);
	}
		
	
	if ((authtoken = which->auth->auth(which, msgbody)) == NULL) {
		ha_error("Cannot check message authentication");
		return(0);
	}
	if (strcmp(authstring, authtoken) == 0) {
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
	FILE *		fp;
	if ((fp=fopen(LOADAVG, "r")) == NULL) {
		strcpy(loadavg, "n/a");
	}else{
		fgets(loadavg, sizeof(loadavg), fp);
		fclose(fp);
	}
	loadavg[strlen(loadavg)-1] = EOS;
	return(loadavg);
}
STATIC	const char *
ha_msg_ttl(void)
{
	static char	ttl[8];
	sprintf(ttl, "%d", config->hopfudge + config->nodecount);
	return(ttl);
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
 * $Log: ha_msg.c,v $
 * Revision 1.9  1999/11/22 20:28:23  alan
 * First pass of putting real packet retransmission.
 * Still need to request missing packets from time to time
 * in case retransmit requests get lost.
 *
 * Revision 1.8  1999/10/25 15:35:03  alan
 * Added code to move a little ways along the path to having error recovery
 * in the heartbeat protocol.
 * Changed the code for serial.c and ppp-udp.c so that they reauthenticate
 * packets they change the ttl on (before forwarding them).
 *
 * Revision 1.7  1999/10/10 20:11:56  alanr
 * New malloc/free (untested)
 *
 * Revision 1.6  1999/10/05 06:00:55  alanr
 * Added RPM Cflags to Makefiles
 *
 * Revision 1.5  1999/10/03 03:13:43  alanr
 * Moved resource acquisition to 'heartbeat', also no longer attempt to make the FIFO, it's now done in heartbeat.  It should now be possible to start it up more readily...
 *
 * Revision 1.4  1999/09/29 03:22:05  alanr
 * Added the ability to reread auth config file on SIGHUP
 *
 * Revision 1.3  1999/09/26 21:59:58  alanr
 * Allow multiple auth strings in auth file... (I hope?)
 *
 * Revision 1.2  1999/09/26 14:01:01  alanr
 * Added Mijta's code for authentication and Guenther Thomsen's code for serial locking and syslog reform
 *
 * Revision 1.9  1999/09/16 05:50:20  alanr
 * Getting ready for 0.4.3...
 *
 * Revision 1.8  1999/08/25 06:34:26  alanr
 * Added code to log outgoing messages in a FIFO...
 *
 * Revision 1.7  1999/08/18 04:28:48  alanr
 * added function to dump a message to the log...
 *
 * Revision 1.6  1999/08/17 03:46:48  alanr
 * added log entry...
 *
 */
