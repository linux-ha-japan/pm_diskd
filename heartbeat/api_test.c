#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <heartbeat.h>
#include <hb_api_core.h>

struct stringlist {
	char *			value;
	struct stringlist *	next;
};

struct stringlist *		nodelist = NULL;
struct stringlist *		iflist = NULL;

struct MsgQueue {
	struct ha_msg *		value;
	struct MsgQueue *	next;
	struct MsgQueue *	prev;
};
struct MsgQueue *	firstQdmsg = NULL;
struct MsgQueue *	lastQdmsg = NULL;

static struct ha_msg*	hb_api_boilerplate(const char * apitype);
static int		hb_api_signon(const char * clientid);
static int		hb_api_signoff(void);
static int		hb_api_setfilter(unsigned);
static void		destroy_stringlist(struct stringlist *);
static struct stringlist*
			new_stringlist(const char *);
static int		get_nodelist(void);
static const char *	get_nodestatus(const char *host);
static const char *	get_ifstatus(const char *host, const char * intf);
static void		zap_nodelist(void);
static int		get_iflist(const char *host);
static void		zap_iflist(void);
static int		enqueue_msg(struct ha_msg*);
static struct ha_msg*	dequeue_msg(void);
static struct ha_msg*	read_api_msg(void);
static struct ha_msg*	read_hb_msg(void);

volatile struct process_info *	curproc = NULL;
static char		OurPid[16];
static const char *	OurClientID = NULL;
static FILE*		MsgFIFO = NULL;
static FILE*		ReplyFIFO = NULL;
static int		SignedOnAlready = 0;
static char 		OurNode[SYS_NMLN];
static char		ReplyFIFOName[API_FIFO_LEN];


static struct ha_msg*
hb_api_boilerplate(const char * apitype)
{
	struct ha_msg*	msg;
	if ((msg = ha_msg_new(4)) == NULL) {
		fprintf(stderr, "boilerplate: out of memory/1\n");
		return msg;
	}
	if (ha_msg_add(msg, F_TYPE, T_APIREQ) != HA_OK) {
		fprintf(stderr, "boilerplate: cannot add field\n");
		ha_msg_del(msg); msg=NULL;
		return msg;
	}
	if (ha_msg_add(msg, F_APIREQ, apitype) != HA_OK) {
		fprintf(stderr, "boilerplate: cannot add field\n");
		ha_msg_del(msg); msg=NULL;
		return msg;
	}
	if (ha_msg_add(msg, F_TO, OurNode) != HA_OK) {
		fprintf(stderr, "boilerplate: cannot add field\n");
		ha_msg_del(msg); msg=NULL;
		return msg;
	}
	if (ha_msg_add(msg, F_PID, OurPid) != HA_OK) {
		fprintf(stderr, "boilerplate: cannot add field\n");
		ha_msg_del(msg); msg=NULL;
		return msg;
	}
	
	if (ha_msg_add(msg, F_FROMID, OurClientID) != HA_OK) {
		fprintf(stderr, "boilerplate: cannot add field\n");
		ha_msg_del(msg); msg=NULL;
		return msg;
	}
	return(msg);
}
static int
hb_api_signon(const char * clientid)
{
	struct ha_msg*	request;
	struct ha_msg*	reply;
	int		fd;
	static char	ReplyFdBuf[MAXLINE];
	struct utsname	un;
	int		rc;
	const char *	result;

	if (SignedOnAlready) {
		return HA_OK;
	}
	snprintf(OurPid, sizeof(OurPid), "%d", getpid());
	snprintf(ReplyFIFOName, sizeof(ReplyFIFOName), "%s/%d", API_FIFO_DIR, getpid());
	if (clientid != NULL) {
		OurClientID = clientid;
	}else{
		OurClientID = OurPid;
	}

	if (uname(&un) < 0) {
		perror("uname failure");
		return HA_FAIL;
	}
	strncpy(OurNode, un.nodename, sizeof(OurNode));

	if ((request = hb_api_boilerplate(API_SIGNON)) == NULL) {
		fprintf(stderr, "api_process_request: cannot create msg\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	
	mkfifo(ReplyFIFOName, 0600);

	/* We open it this way to keep the open from hanging... */
	fd =open(ReplyFIFOName, O_RDWR);

	if ((ReplyFIFO = fdopen(fd, "r")) == NULL) {
		fprintf(stderr, "can't open reply fifo %s\n", ReplyFIFOName);
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	setvbuf(ReplyFIFO, ReplyFdBuf, _IOLBF, sizeof(ReplyFdBuf));

	if ((MsgFIFO = fopen(FIFONAME, "w")) == NULL) {
		ha_msg_del(request); request=NULL;
		perror("can't open " FIFONAME);
		return HA_FAIL;
	}

	/* Send message */
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;

	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		ha_msg_del(request); request=NULL;
		perror("can't read reply");
		return HA_FAIL;
	}
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, API_OK) == 0) {
		rc = HA_OK;
		SignedOnAlready = 1;
	}else{
		rc = HA_FAIL;
	}
	ha_msg_del(reply); reply=NULL;

	return rc;
}

static int
hb_api_signoff()
{
	struct ha_msg*	request;

	if (!SignedOnAlready) {
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_SIGNOFF)) == NULL) {
		fprintf(stderr, "api_process_request: can't create msg\n");
		return HA_FAIL;
	}
	
	/* Send message */
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;
	OurClientID = NULL;
	(void)fclose(MsgFIFO);
	(void)fclose(ReplyFIFO);
	(void)unlink(ReplyFIFOName);
	SignedOnAlready = 0;

	return HA_OK;
}

int
hb_api_setfilter(unsigned fmask)
{
	struct ha_msg*	request;
	struct ha_msg*	reply;
	int		rc;
	const char *	result;
	char		filtermask[32];

	if (!SignedOnAlready) {
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_SETFILTER)) == NULL) {
		fprintf(stderr, "api_process_request: can't create msg\n");
		return HA_FAIL;
	}

	snprintf(filtermask, sizeof(filtermask), "%x", fmask);
	if (ha_msg_add(request, F_FILTERMASK, filtermask) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	
	/* Send message */
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;

	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		ha_msg_del(request); request=NULL;
		perror("can't read reply");
		return HA_FAIL;
	}
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, API_OK) == 0) {
		rc = HA_OK;
	}else{
		rc = HA_FAIL;
	}
	ha_msg_del(reply); reply=NULL;

	return rc;
}

static int
get_nodelist(void)
{
	struct ha_msg*		request;
	struct ha_msg*		reply;
	const char *		result;
	struct stringlist*	sl;

	if (!SignedOnAlready) {
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_NODELIST)) == NULL) {
		fprintf(stderr, "api_process_request: can't create msg\n");
		return HA_FAIL;
	}

	/* Send message */
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;

	while ((reply=read_api_msg()) != NULL
	&& 	(result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	(strcmp(result, API_MORE) == 0 || strcmp(result, API_OK) == 0)
	&&	(sl = new_stringlist(ha_msg_value(reply, F_NODENAME))) != NULL){
		sl->next = nodelist;
		nodelist = sl->next;
		ha_msg_del(reply); reply=NULL;
		if (strcmp(result, API_OK) == 0) {
			return(HA_OK);
		}
	}
	if (reply != NULL) {
		zap_nodelist();
		ha_msg_del(reply); reply=NULL;
	}

	return HA_FAIL;
}
static int
get_iflist(const char *host)
{
	struct ha_msg*		request;
	struct ha_msg*		reply;
	const char *		result;
	struct stringlist*	sl;

	if (!SignedOnAlready) {
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_IFLIST)) == NULL) {
		fprintf(stderr, "api_process_request: can't create msg\n");
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_NODENAME, host) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}

	/* Send message */
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;

	while ((reply=read_api_msg()) != NULL
	&& 	(result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	(strcmp(result, API_MORE) == 0 || strcmp(result, API_OK) == 0)
	&&	(sl = new_stringlist(ha_msg_value(reply, F_IFNAME))) != NULL){
		sl->next = iflist;
		iflist = sl->next;
		ha_msg_del(reply); reply=NULL;
		if (strcmp(result, API_OK) == 0) {
			return(HA_OK);
		}
	}
	if (reply != NULL) {
		zap_iflist();
		ha_msg_del(reply); reply=NULL;
	}

	return HA_FAIL;
}
static const char *
get_nodestatus(const char *host)
{
	struct ha_msg*		request;
	struct ha_msg*		reply;
	const char *		result;
	const char *		status;
	static char		statbuf[128];
	const char *		ret;

	if (!SignedOnAlready) {
		return NULL;
	}

	if ((request = hb_api_boilerplate(API_NODESTATUS)) == NULL) {
		fprintf(stderr, "api_process_request: can't create msg\n");
		return NULL;
	}
	if (ha_msg_add(request, F_NODENAME, host) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field\n");
		ha_msg_del(request); request=NULL;
		return NULL;
	}

	/* Send message */
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;
	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		ha_msg_del(request); request=NULL;
		perror("can't read reply");
		return NULL;
	}
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, API_OK) == 0
	&&	(status = ha_msg_value(reply,F_STATUS)) != NULL) {
		strncpy(statbuf, status, sizeof(statbuf));
		ret = statbuf;
	}else{
		ret = NULL;
	}
	ha_msg_del(reply); reply=NULL;

	return ret;
}
static const char *
get_ifstatus(const char *host, const char * ifname)
{
	struct ha_msg*		request;
	struct ha_msg*		reply;
	const char *		result;
	const char *		status;
	static char		statbuf[128];
	const char *		ret;

	if (!SignedOnAlready) {
		return NULL;
	}

	if ((request = hb_api_boilerplate(API_IFSTATUS)) == NULL) {
		fprintf(stderr, "api_process_request: can't create msg\n");
		return NULL;
	}
	if (ha_msg_add(request, F_NODENAME, host) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field\n");
		ha_msg_del(request); request=NULL;
		return NULL;
	}
	if (ha_msg_add(request, F_IFNAME, ifname) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field\n");
		ha_msg_del(request); request=NULL;
		return NULL;
	}

	/* Send message */
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;
	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		ha_msg_del(request); request=NULL;
		perror("can't read reply");
		return NULL;
	}
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, API_OK) == 0
	&&	(status = ha_msg_value(reply,F_STATUS)) != NULL) {
		strncpy(statbuf, status, sizeof(statbuf));
		ret = statbuf;
	}else{
		ret = NULL;
	}
	ha_msg_del(reply); reply=NULL;

	return ret;
}
static void
zap_nodelist(void)
{
	destroy_stringlist(nodelist);
	nodelist=NULL;
}
static void
zap_iflist(void)
{
	destroy_stringlist(iflist);
	iflist=NULL;
}

static struct stringlist*
new_stringlist(const char *s)
{
	struct stringlist*	ret;
	char *			cp;

	if (s == NULL) {
		return(NULL);
	}

	if ((cp = (char *)ha_malloc(strlen(s)+1)) == NULL) {
		return(NULL);
	}
	if ((ret = MALLOCT(struct stringlist)) == NULL) {
		ha_free(cp);
		return(NULL);
	}
	ret->next = NULL;
	ret->value = cp;
	strcpy(cp, s);
	return(ret);
}

static void
destroy_stringlist(struct stringlist * s)
{
	struct stringlist *	this;
	struct stringlist *	next;

	for (this=s; this; this=next) {
		next = this->next;
		ha_free(this->value);
		memset(this, 0, sizeof(*this));
		ha_free(this);
	}
}

static int
enqueue_msg(struct ha_msg* msg)
{
	struct MsgQueue*	newQelem;
	if (msg == NULL) {
		return(HA_FAIL);
	}
	if ((newQelem = MALLOCT(struct MsgQueue)) == NULL) {
		return(HA_FAIL);
	}
	newQelem->value = msg;
	newQelem->prev = lastQdmsg;
	newQelem->next = NULL;
	if (lastQdmsg != NULL) {
		lastQdmsg->next = newQelem;
	}
	lastQdmsg = newQelem;
	if (firstQdmsg == NULL) {
		firstQdmsg = newQelem;
	}
	return HA_OK;
}

static struct ha_msg *
dequeue_msg()
{
	struct MsgQueue*	qret;
	struct ha_msg*		ret = NULL;
	

	qret = firstQdmsg;

	if (qret != NULL) {
		ret = qret->value;
		firstQdmsg=qret->next;
		if (firstQdmsg) {
			firstQdmsg->prev = NULL;
		}
		memset(qret, 0, sizeof(*qret));
		
		/*
		 * The only two pointers to this element are the first pointer,
		 * and the prev pointer of the next element in the queue.
		 * (or possibly lastQdmsg... See below)
		 */
		ha_free(qret);
	}
	if (firstQdmsg == NULL) {
		 /* Zap lastQdmsg if it pointed at this Q element */
		lastQdmsg=NULL;
	}
	return(ret);
}

static struct ha_msg *
read_api_msg(void)
{
	for (;;) {
		struct ha_msg*	msg;
		const char *	type;
		if ((msg=msgfromstream(ReplyFIFO)) == NULL) {
			return NULL;
		}
		if ((type=ha_msg_value(msg, F_TYPE)) != NULL
		&&	strcmp(type, T_APIRESP) == 0) {
			return(msg);
		}
		/* Got an unexpected non-api message */
		/* Queue it up for reading later */
		enqueue_msg(msg);
	}
	/*NOTREACHED*/
	return(NULL);
}

static struct ha_msg *
read_hb_msg(void)
{
	struct ha_msg*	msg;
	msg = dequeue_msg();

	if (msg != NULL) {
		return(msg);
	}
	return(msgfromstream(ReplyFIFO));
	
}

void gotsig(int nsig);
int quitnow = 0;
void gotsig(int nsig)
{
	(void)nsig;
	quitnow = 1;
}

int
main(int argc, char ** argv)
{
	struct ha_msg*	reply;
	unsigned	fmask;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	fprintf(stderr, "Signing in with heartbeat\n");
	hb_api_signon(NULL);

#if 0
	fmask = ALLTREATMENTS;
#else
	fmask = DEFAULTREATMENT;
#endif
	fprintf(stderr, "Setting message filter mask\n");
	hb_api_setfilter(fmask);
	get_nodelist();
	get_iflist("kathyamy");
	fprintf(stderr, "Node status: %s\n", get_nodestatus("kathyamy"));
	fprintf(stderr, "IF status: %s\n", get_ifstatus("kathyamy", "eth0"));

	siginterrupt(SIGINT, 1);
	signal(SIGINT, gotsig);
	/* Read all subsequent replies... */
	fprintf(stderr, "Now waiting for more messages...\n");
	for(; !quitnow && (reply=read_hb_msg()) != NULL;) {
		fprintf(stderr, "Got another message...\n");
		ha_log_message(reply);
		ha_msg_del(reply); reply=NULL;
	}
	if (!quitnow) {
		perror("msgfromstream returned NULL");
	}
	hb_api_signoff();
	return 0;
}

void *
ha_malloc(size_t size)
{
	return(malloc(size));
}

void
ha_free(void * ptr)
{
	free(ptr);
}
void
ha_log(int priority, const char * fmt, ...)
{
        va_list ap;
        char buf[MAXLINE];
 
        va_start(ap, fmt);
        vsnprintf(buf, MAXLINE, fmt, ap);
        va_end(ap);
 
	fprintf(stderr, "%s\n", buf);
}

void
ha_error(const char * msg)
{
 
	ha_log(0, msg);
 
}
