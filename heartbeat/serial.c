const static char * _serial_c_Id = "$Id: serial.c,v 1.1 1999/09/23 15:31:24 alanr Exp $";

/*
 *	Linux-HA serial heartbeat code
 *
 *	The basic facilities for round-robin (ring) heartbeats are
 *	contained within.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include <sys/stat.h>

#include <heartbeat.h>

struct serial_private {
        char *  ttyname;
        int     ttyfd;                  /* For direct TTY i/o */ 
        struct hb_media * next;   
};

/* Used to maintain a list of our serial ports in the ring */
STATIC struct hb_media*		lastserialport;


STATIC struct hb_media*	serial_new(const char * value);
STATIC struct ha_msg*	serial_read(struct hb_media*mp);
STATIC char *		ttygets(char * inbuf, int length
,				struct serial_private *tty);
STATIC int		serial_write(struct hb_media*mp, struct ha_msg *msg);
STATIC int		serial_open(struct hb_media* mp);
STATIC int		ttysetup(int fd);
STATIC int		opentty(char * serial_device);
STATIC int		serial_close(struct hb_media* mp);
STATIC int		serial_init(void);


/* Exported to the world */
const struct hb_media_fns	serial_media_fns =
{	"serial"		/* type */
,	"serial ring"		/* description */
,	serial_init		/* init */
,	serial_new		/* new */
,	NULL			/* parse */
,	serial_open		/* open */
,	serial_close		/* close */
,	serial_read		/* read */
,	serial_write		/* write */
};
int		serial_baud;	/* Also exported... */
int		baudrate;	/* Also exported... */

#define		IsTTYOBJECT(mp)	((mp) && ((mp)->vf == (void*)&serial_media_fns))
#define		TTYASSERT(mp)	ASSERT(IsTTYOBJECT(mp))

/* Initialize global serial data structures */
STATIC int
serial_init(void)
{
	(void)_serial_c_Id;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;	/* ditto */
	lastserialport = NULL;
	curnode = NULL;
	serial_baud = DEFAULTBAUD;
	baudrate = DEFAULTBAUDRATE;
	return(HA_OK);
}

/* Process a serial port declaration */
STATIC struct hb_media *
serial_new(const char * port)
{
	char	msg[MAXLINE];
	struct	stat	sbuf;
	struct hb_media * ret;


	/* Let's see if this looks like it might be a serial port... */
	if (*port != '/') {
		sprintf(msg, "Serial port not full pathname [%s] in config file"
		,	port);
		ha_error(msg);
		return(NULL);
	}

	if (stat(port, &sbuf) < 0) {
		sprintf(msg, "Nonexistent serial port [%s] in config file"
		,	port);
		ha_perror(msg);
		return(NULL);
	}
	if (!S_ISCHR(sbuf.st_mode)) {
		sprintf(msg, "Serial port [%s] not a char device in config file"
		,	port);
		ha_error(msg);
		return(NULL);
	}

	ret = MALLOCT(struct hb_media);
	if (ret != NULL) {
		struct serial_private * sp;
		sp = MALLOCT(struct serial_private);
		if (sp != NULL)  {
			sp->next = lastserialport;
			lastserialport=ret;
			sp->ttyname = (char *)malloc(strlen(port)+1);
			strcpy(sp->ttyname, port);
			ret->name = sp->ttyname;
			ret->vf = &serial_media_fns;
			ret->pd = sp;
		}else{
			free(ret);
			ret = NULL;
			ha_error("Out of memory (private serial data)");
		}
	}else{
		ha_error("Out of memory (serial data)");
	}
	return(ret);
}

STATIC int
serial_open(struct hb_media* mp)
{
	struct serial_private*	sp;
	char			msg[MAXLINE];

	TTYASSERT(mp);
	sp = (struct serial_private*)mp->pd;
	if ((sp->ttyfd = opentty(sp->ttyname)) < 0) {
		return(HA_FAIL);
	}
	sprintf(msg, "Starting serial heartbeat on tty %s", sp->ttyname);
	ha_log(msg);
	return(HA_OK);
}

STATIC int
serial_close(struct hb_media* mp)
{
	struct serial_private*	sp;

	TTYASSERT(mp);
	sp = (struct serial_private*)mp->pd;
	return(close(sp->ttyfd)< 0 ? HA_FAIL : HA_OK);
}


/* Set up a serial line the way we want it done */
STATIC int
ttysetup(int fd)
{
	struct TERMIOS	ti;

	if (GETATTR(fd, &ti) < 0) {
		ha_perror("cannot get tty attributes");
		return(HA_FAIL);
	}

#ifndef IUCLC
#	define IUCLC	0	/* Ignore it if not supported */
#endif
	ti.c_iflag &= ~(BRKINT|IGNBRK|IUCLC|IXANY|IXOFF|IXON|ICRNL|PARMRK);
	/* Unsure if I want PARMRK or not...  It may not matter much */
	ti.c_iflag |=  (INPCK|ISTRIP|IGNCR);

	ti.c_oflag &= ~(OPOST);

	ti.c_cflag &= ~(CBAUD|CSIZE|PARENB);
	ti.c_cflag |=  (serial_baud|CS8|CREAD|CLOCAL);

	ti.c_lflag &= ~(ICANON|ECHO|ISIG);
#if !defined(IRIX) && !defined(__FreeBSD__)
	ti.c_line = 0;
#endif
	ti.c_cc[VMIN] = 1;
	ti.c_cc[VTIME] = 1;
	if (SETATTR(fd, &ti) < 0) {
		ha_perror("cannot set tty attributes");
		return(HA_FAIL);
	}
	/* For good measure */
	FLUSH(fd);
	return(HA_OK);
}

/* Open a tty and set it's line parameters */
STATIC int
opentty(char * serial_device)
{
	int	fd;

	if ((fd=open(serial_device, O_RDWR)) < 0 ) {
		char msg[128];
		sprintf(msg, "cannot open %s", serial_device);
		ha_perror(msg);
		return(fd);
	}
	if (!ttysetup(fd)) {
		close(fd);
		return(-1);
	}
	return(fd);
}


/* This process does all the writing to our tty ports */
STATIC int
serial_write(struct hb_media*mp, struct ha_msg*m)
{
	char *	str;

	int	ourtty;
	int	wrc;
	int	size;

	TTYASSERT(mp);

	ourtty = ((struct serial_private*)(mp->pd))->ttyfd;
	if ((str=msg2string(m)) == NULL) {
		ha_error("Cannot convert message to tty string");
		return(HA_FAIL);
	}
	size = strlen(str);
	if (DEBUGPKT) {
		char msg[MAXLINE];
		sprintf(msg, "Sending pkt to %s [%d bytes]"
		,	mp->name, size);
		ha_log(msg);
	}
	if (DEBUGPKTCONT) {
		ha_log(str);
	}
	wrc = write(ourtty, str, size);

	if (wrc != size) {
		char msg[MAXLINE];
		sprintf(msg, "write failure on tty %s: %d vs %d"
		,	mp->name, wrc, size);
		ha_perror(msg);
	}
	free(str);
	return(HA_OK);
}

/* This process does all the reading from our tty ports */
STATIC struct ha_msg *
serial_read(struct hb_media*mp)
{
	char buf[MAXLINE];
	struct serial_private*	thissp;
	struct ha_msg*		ret;
	int			ttl;
	const char *		ttl_s;
	char			nttl[8];
	char *			newmsg;
	int			startlen;
	const char *		start = MSG_START;
	const char *		end = MSG_START;
	int			endlen;

	TTYASSERT(mp);
	thissp = (struct serial_private*)mp->pd;

	if ((ret = ha_msg_new(0)) == NULL) {
		ha_error("Cannot get new message");
		return(NULL);
	}
	startlen = strlen(start);
	if (start[startlen-1] == '\n') {
		--startlen;
	}
	endlen = strlen(end);
	if (end[endlen-1] == '\n') {
		--endlen;
	}
	/* Skip until we find a MSG_START (hopefully we skip nothing) */
	while (ttygets(buf, MAXLINE, thissp) != NULL
	&&	strncmp(buf, start, startlen) != 0) {
		/* Nothing */
	}
	/* Add Name=value pairs until we reach MSG_END or EOF */
	while (ttygets(buf, MAXLINE, thissp) != NULL
	&&	strncmp(buf, MSG_END, endlen) != 0) {

		/* Add the "name=value" string on this line to the message */
		if (ha_msg_add_nv(ret, buf) != HA_OK) {
			ha_msg_del(ret);
			return(NULL);
		}
	}

	/* Should this message should continue around the ring? */
	if (!should_ring_copy_msg(ret)) {
		/* Avoid infinite loops... Ignore this message */
		return(ret);
	}
	if ((ttl_s = ha_msg_value(ret, F_TTL)) == NULL) {
		return(NULL);
	}
	ttl = atoi(ttl_s);
	sprintf(nttl, "%d", ttl-1);

	ha_msg_mod(ret, F_TTL, nttl);

	if ((newmsg = msg2string(ret)) == NULL) {
		ha_error("Cannot convert new message to string");
	}else{
		struct hb_media*	sp;
		int			msglen;
		struct serial_private*	spp;
		/* Forward message to other port in ring (if any) */
		for (sp=lastserialport; sp; sp=spp->next) {
			TTYASSERT(sp);
			spp = (struct serial_private*)sp->pd;
			if (sp == mp) {
				/* That's us! */
				continue;
			}
			write(sp->wpipe[P_WRITEFD], newmsg, msglen);
		}
		free(newmsg);
	}
	return(ret);
}


/* Gets function for our tty */
STATIC char *
ttygets(char * inbuf, int length, struct serial_private *tty)
{
	char *	cp;
	char *	end = inbuf + length;
	int	rc;
	int	fd = tty->ttyfd;

	for(cp=inbuf; cp < end; ++cp) {
		errno = 0;
		/* One read per char -- yecch  (but it's easy) */
		rc = read(fd, cp, 1);
		if (rc != 1) {
			char	msg[MAXLINE];
			sprintf(msg, "EOF in ttygets [%s]", tty->ttyname);
			ha_perror(msg);
			return(NULL);
		}
		if (*cp == '\r' || *cp == '\n') {
			break;
		}
	}
	*cp = '\0';
	return(inbuf);
}
/*
 * $Log: serial.c,v $
 * Revision 1.1  1999/09/23 15:31:24  alanr
 * Initial revision
 *
 * Revision 1.11  1999/09/18 02:56:36  alanr
 * Put in Matt Soffen's portability changes...
 *
 * Revision 1.10  1999/09/16 05:50:20  alanr
 * Getting ready for 0.4.3...
 *
 * Revision 1.9  1999/08/17 03:49:26  alanr
 * added log entry to bottom of file.
 *
 */
