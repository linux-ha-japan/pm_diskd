#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <heartbeat.h>
#include <hb_api_core.h>
#include <hb_api.h>

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

typedef struct gen_callback {
	char *			msgtype;
	llc_msg_callback_t *	cf;
	void *			pd;
	struct gen_callback*	next;
}gen_callback_t;

typedef struct llc_private {
	llc_nstatus_callback_t*		node_callback;
	void*				node_private;
	llc_ifstatus_callback_t*	if_callback;
	void*				if_private;
	struct gen_callback*		genlist;
	struct stringlist*		nextnode;
	struct stringlist*		nextif;
}llc_private_t;

static void		ClearLog(void);
static struct ha_msg*	hb_api_boilerplate(const char * apitype);
static int		hb_api_signon(const char * clientid);
static int		hb_api_signoff(void);
static int		hb_api_setfilter(unsigned);
static void		destroy_stringlist(struct stringlist *);
static struct stringlist*
			new_stringlist(const char *);
static int		get_nodelist(void);
static void		zap_nodelist(void);
static int		get_iflist(const char *host);
static void		zap_iflist(void);
static int		enqueue_msg(struct ha_msg*);
static struct ha_msg*	dequeue_msg(void);
static gen_callback_t*	search_gen_callback(const char * type, llc_private_t*);
static int		add_gen_callback(const char * msgtype
,	llc_private_t*, llc_msg_callback_t *, void*);
static int		del_gen_callback(llc_private_t*, const char * msgtype);

static struct ha_msg*	read_api_msg(void);
static struct ha_msg*	read_hb_msg(void);

static int		hb_api_setsignal(ll_cluster_t*, int nsig);
static int set_msg_callback
			(ll_cluster_t*, const char * msgtype
,			llc_msg_callback_t* callback, void * p);
static int
set_nstatus_callback (ll_cluster_t*
,		llc_nstatus_callback_t* cbf, 	void * p);
static int
		set_ifstatus_callback (ll_cluster_t* ci
,		const char *node
,		const char *iface
,		llc_ifstatus_callback_t* cbf, void * p);
static int init_nodewalk (ll_cluster_t*);
static const char * nextnode (ll_cluster_t* ci);
static int init_ifwalk (ll_cluster_t* ci, const char * host);
static const char *	get_nodestatus(ll_cluster_t*, const char *host);
static const char *	get_ifstatus(ll_cluster_t*, const char *host
,	const char * intf);
static int		get_inputfd(ll_cluster_t*);
static int		msgready(ll_cluster_t*);
static int		setfmode(ll_cluster_t*, int mode);
static int		sendclustermsg(ll_cluster_t*, struct ha_msg* msg);
static int		sendnodemsg(ll_cluster_t*, struct ha_msg* msg
,			const char * nodename);
static const char *	APIError(ll_cluster_t*);

volatile struct process_info *	curproc = NULL;
static char		OurPid[16];
static const char *	OurClientID = NULL;
static FILE*		MsgFIFO = NULL;
static FILE*		ReplyFIFO = NULL;
static int		SignedOnAlready = 0;
static char 		OurNode[SYS_NMLN];
static char		ReplyFIFOName[API_FIFO_LEN];

#define	ZAPMSG(m)	{ha_msg_del(m); (m) = NULL;}

static struct ha_msg*
hb_api_boilerplate(const char * apitype)
{
	struct ha_msg*	msg;
	if ((msg = ha_msg_new(4)) == NULL) {
		ha_log(LOG_ERR, "boilerplate: out of memory");
		return msg;
	}
	if (ha_msg_add(msg, F_TYPE, T_APIREQ) != HA_OK) {
		ha_log(LOG_ERR, "boilerplate: cannot add F_TYPE field");
		ZAPMSG(msg);
		return msg;
	}
	if (ha_msg_add(msg, F_APIREQ, apitype) != HA_OK) {
		ha_log(LOG_ERR, "boilerplate: cannot add F_APIREQ field");
		ZAPMSG(msg);
		return msg;
	}
	if (ha_msg_add(msg, F_TO, OurNode) != HA_OK) {
		ha_log(LOG_ERR, "boilerplate: cannot add F_TO field");
		ZAPMSG(msg);
		return msg;
	}
	if (ha_msg_add(msg, F_PID, OurPid) != HA_OK) {
		ha_log(LOG_ERR, "boilerplate: cannot add F_PID field");
		ZAPMSG(msg);
		return msg;
	}
	
	if (ha_msg_add(msg, F_FROMID, OurClientID) != HA_OK) {
		ha_log(LOG_ERR, "boilerplate: cannot add F_FROMID field");
		ZAPMSG(msg);
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
	snprintf(ReplyFIFOName, sizeof(ReplyFIFOName), "%s/%d", API_FIFO_DIR
	,	getpid());
	if (clientid != NULL) {
		OurClientID = clientid;
	}else{
		OurClientID = OurPid;
	}

	if (uname(&un) < 0) {
		ha_perror("uname failure");
		return HA_FAIL;
	}
	strncpy(OurNode, un.nodename, sizeof(OurNode));

	if ((request = hb_api_boilerplate(API_SIGNON)) == NULL) {
		return HA_FAIL;
	}
	
	mkfifo(ReplyFIFOName, 0600);

	/* We open it this way to keep the open from hanging... */
	if ((fd = open(ReplyFIFOName, O_RDWR)) < 0) {
		ha_log(LOG_ERR, "hb_api_signon: Can't open reply fifo %s"
		,	ReplyFIFOName);
		return HA_FAIL;
	}

	if ((ReplyFIFO = fdopen(fd, "r")) == NULL) {
		ha_log(LOG_ERR, "hb_api_signon: Can't fdopen reply fifo %s"
		,	ReplyFIFOName);
		ZAPMSG(request);
		return HA_FAIL;
	}
	setvbuf(ReplyFIFO, ReplyFdBuf, _IOLBF, sizeof(ReplyFdBuf));

	if ((MsgFIFO = fopen(FIFONAME, "w")) == NULL) {
		ZAPMSG(request);
		ha_perror("Can't fopen " FIFONAME);
		return HA_FAIL;
	}

	/* Send message */
	if (msg2stream(request, MsgFIFO) != HA_OK) {
		ZAPMSG(request);
		ha_perror("can't send message to MsgFIFO");
		return HA_FAIL;
	}
		
	ZAPMSG(request);

	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		return HA_FAIL;
	}
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, API_OK) == 0) {
		rc = HA_OK;
		SignedOnAlready = 1;
	}else{
		rc = HA_FAIL;
	}
	ZAPMSG(reply);

	return rc;
}

static int
hb_api_signoff()
{
	struct ha_msg*	request;

	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_SIGNOFF)) == NULL) {
		ha_log(LOG_ERR, "hb_api_signoff: can't create msg");
		return HA_FAIL;
	}
	
	/* Send message */
	if (msg2stream(request, MsgFIFO) != HA_OK) {
		ZAPMSG(request);
		ha_perror("can't send message to MsgFIFO");
		return HA_FAIL;
	}
	ZAPMSG(request);
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
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_SETFILTER)) == NULL) {
		ha_log(LOG_ERR, "hb_api_setfilter: can't create msg");
		return HA_FAIL;
	}

	snprintf(filtermask, sizeof(filtermask), "%x", fmask);
	if (ha_msg_add(request, F_FILTERMASK, filtermask) != HA_OK) {
		ha_log(LOG_ERR, "hb_api_setfilter: cannot add field/2");
		ZAPMSG(request);
		return HA_FAIL;
	}
	
	/* Send message */
	if (msg2stream(request, MsgFIFO) != HA_OK) {
		ZAPMSG(request);
		ha_perror("can't send message to MsgFIFO");
		return HA_FAIL;
	}
	ZAPMSG(request);

	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		ZAPMSG(request);
		return HA_FAIL;
	}
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, API_OK) == 0) {
		rc = HA_OK;
	}else{
		rc = HA_FAIL;
	}
	ZAPMSG(reply);

	return rc;
}
int
hb_api_setsignal(ll_cluster_t* lcl, int nsig)
{
	struct ha_msg*	request;
	struct ha_msg*	reply;
	int		rc;
	const char *	result;
	char		csignal[32];

	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_SETSIGNAL)) == NULL) {
		ha_log(LOG_ERR, "hb_api_setsignal: can't create msg");
		return HA_FAIL;
	}

	snprintf(csignal, sizeof(csignal), "%d", nsig);
	if (ha_msg_add(request, F_SIGNAL, csignal) != HA_OK) {
		ha_log(LOG_ERR, "hb_api_setsignal: cannot add field/2");
		ZAPMSG(request);
		return HA_FAIL;
	}
	
	/* Send message */
	if (msg2stream(request, MsgFIFO) != HA_OK) {
		ha_perror("can't send message to MsgFIFO");
		ZAPMSG(request);
		return HA_FAIL;
	}
	ZAPMSG(request);

	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		ZAPMSG(request);
		return HA_FAIL;
	}
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, API_OK) == 0) {
		rc = HA_OK;
	}else{
		rc = HA_FAIL;
	}
	ZAPMSG(reply);

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
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_NODELIST)) == NULL) {
		ha_log(LOG_ERR, "get_nodelist: can't create msg");
		return HA_FAIL;
	}

	/* Send message */
	if (msg2stream(request, MsgFIFO) != HA_OK) {
		ZAPMSG(request);
		ha_perror("can't send message to MsgFIFO");
		return HA_FAIL;
	}
	ZAPMSG(request);

	while ((reply=read_api_msg()) != NULL
	&& 	(result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	(strcmp(result, API_MORE) == 0 || strcmp(result, API_OK) == 0)
	&&	(sl = new_stringlist(ha_msg_value(reply, F_NODENAME))) != NULL){
		sl->next = nodelist;
		nodelist = sl->next;
		ZAPMSG(reply);
		if (strcmp(result, API_OK) == 0) {
			return(HA_OK);
		}
	}
	if (reply != NULL) {
		zap_nodelist();
		ZAPMSG(reply);
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
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}

	if ((request = hb_api_boilerplate(API_IFLIST)) == NULL) {
		ha_log(LOG_ERR, "get_iflist: can't create msg");
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_NODENAME, host) != HA_OK) {
		ha_log(LOG_ERR, "get_iflist: cannot add field");
		ZAPMSG(request);
		return HA_FAIL;
	}

	/* Send message */
	if (msg2stream(request, MsgFIFO) != HA_OK) {
		ZAPMSG(request);
		ha_perror("Can't send message to MsgFIFO");
		return HA_FAIL;
	}
	ZAPMSG(request);

	while ((reply=read_api_msg()) != NULL
	&& 	(result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	(strcmp(result, API_MORE) == 0 || strcmp(result, API_OK) == 0)
	&&	(sl = new_stringlist(ha_msg_value(reply, F_IFNAME))) != NULL){
		sl->next = iflist;
		iflist = sl->next;
		ZAPMSG(reply);
		if (strcmp(result, API_OK) == 0) {
			return(HA_OK);
		}
	}
	if (reply != NULL) {
		zap_iflist();
		ZAPMSG(reply);
	}

	return HA_FAIL;
}
static const char *
get_nodestatus(ll_cluster_t* lcl, const char *host)
{
	struct ha_msg*		request;
	struct ha_msg*		reply;
	const char *		result;
	const char *		status;
	static char		statbuf[128];
	const char *		ret;

	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return NULL;
	}

	if ((request = hb_api_boilerplate(API_NODESTATUS)) == NULL) {
		return NULL;
	}
	if (ha_msg_add(request, F_NODENAME, host) != HA_OK) {
		ha_log(LOG_ERR, "get_nodestatus: cannot add field");
		ZAPMSG(request);
		return NULL;
	}

	/* Send message */
	if (msg2stream(request, MsgFIFO) != HA_OK) {
		ZAPMSG(request);
		ha_perror("Can't send message to MsgFIFO");
		return NULL;
	}
	ZAPMSG(request);

	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		ZAPMSG(request);
		return NULL;
	}
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, API_OK) == 0
	&&	(status = ha_msg_value(reply, F_STATUS)) != NULL) {
		strncpy(statbuf, status, sizeof(statbuf));
		ret = statbuf;
	}else{
		ret = NULL;
	}
	ZAPMSG(reply);

	return ret;
}
static const char *
get_ifstatus(ll_cluster_t* lcl, const char *host, const char * ifname)
{
	struct ha_msg*		request;
	struct ha_msg*		reply;
	const char *		result;
	const char *		status;
	static char		statbuf[128];
	const char *		ret;

	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return NULL;
	}

	if ((request = hb_api_boilerplate(API_IFSTATUS)) == NULL) {
		return NULL;
	}
	if (ha_msg_add(request, F_NODENAME, host) != HA_OK) {
		ha_log(LOG_ERR, "get_ifstatus: cannot add field");
		ZAPMSG(request);
		return NULL;
	}
	if (ha_msg_add(request, F_IFNAME, ifname) != HA_OK) {
		ha_log(LOG_ERR, "get_ifstatus: cannot add field");
		ZAPMSG(request);
		return NULL;
	}

	/* Send message */
	if (msg2stream(request, MsgFIFO) != HA_OK) {
		ZAPMSG(request);
		ha_perror("Can't send message to MsgFIFO");
		return NULL;
	}
	ZAPMSG(request);

	/* Read reply... */
	if ((reply=read_api_msg()) == NULL) {
		ZAPMSG(request);
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
	ZAPMSG(reply);

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

static gen_callback_t*
search_gen_callback(const char * type, llc_private_t* lcp)
{
	struct gen_callback*	gcb;

	for (gcb=lcp->genlist; gcb != NULL; gcb=gcb->next) {
		if (strcmp(type, gcb->msgtype) == 0) {
			return(gcb);
		}
	}
	return(NULL);
}
 
static int
add_gen_callback(const char * msgtype, llc_private_t* lcp
,	llc_msg_callback_t * funp, void* pd)
{
	struct gen_callback*	gcb;
	char *			type;

	if ((gcb = search_gen_callback(msgtype, lcp)) == NULL) {
		gcb = MALLOCT(struct gen_callback);
		if (gcb == NULL) {
			return(HA_FAIL);
		}
		type = ha_malloc(strlen(msgtype)+1);
		if (type == NULL) {
			ha_free(gcb);
			return(HA_FAIL);
		}
		strcpy(type, msgtype);
		gcb->msgtype = type;
		gcb->next = lcp->genlist;
		lcp->genlist = gcb;
	}else if (funp == NULL) {
		return(del_gen_callback(lcp, msgtype));
	}
	gcb->cf = funp;
	gcb->pd = pd;
	return(HA_OK);
}

static int	
del_gen_callback(llc_private_t* lcp, const char * msgtype)
{
	struct gen_callback*	gcb;
	struct gen_callback*	prev = NULL;

	for (gcb=lcp->genlist; gcb != NULL; gcb=gcb->next) {
		if (strcmp(msgtype, gcb->msgtype) == 0) {
			if (prev) {
				prev->next = gcb->next;
			}else{
				lcp->genlist = gcb->next;
			}
			ha_free(gcb->msgtype);
			gcb->msgtype = NULL;
			free(gcb);
			return(HA_OK);
		}
		prev = gcb;
	}
	return(HA_FAIL);
}
 
static struct ha_msg *
read_api_msg(void)
{
	for (;;) {
		struct ha_msg*	msg;
		const char *	type;
		if ((msg=msgfromstream(ReplyFIFO)) == NULL) {
			ha_perror("read_api_msg: "
			"Cannot read reply from ReplyFIFO");
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
static int
set_msg_callback (ll_cluster_t* ci, const char * msgtype
,			llc_msg_callback_t* callback, void * p)
{

	ClearLog();
	return(add_gen_callback(msgtype,
	(llc_private_t*)ci->ll_cluster_private, callback, p));
}
static int
set_nstatus_callback (ll_cluster_t* ci
,		llc_nstatus_callback_t* cbf, 	void * p)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	pi->node_callback = cbf;
	pi->node_private = p;
	return(HA_OK);
}
static int
set_ifstatus_callback (ll_cluster_t* ci
,		const char *node
,		const char *iface
,		llc_ifstatus_callback_t* cbf, void * p)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	pi->if_callback = cbf;
	pi->if_private = p;
	return(HA_OK);
}
static int
init_nodewalk (ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	zap_nodelist();
	pi->nextnode = nodelist;

	return(get_nodelist());
}

static const char *
nextnode (ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	const char *	ret;

	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return NULL;
	}
	if (pi->nextnode == NULL) {
		return(NULL);
	}
	ret = pi->nextnode->value;

	pi->nextnode = pi->nextnode->next;
	return(ret);
}
static int
end_nodewalk(ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	pi->nextnode = NULL;
	zap_nodelist();
	return(HA_OK);
}

static int
init_ifwalk (ll_cluster_t* ci, const char * host)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	zap_iflist();
	pi->nextif = iflist;
	return(get_iflist(host));
}
static const char *
nextif (ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	const char *	ret;
	ClearLog();

	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	if (pi->nextif == NULL) {
		return(NULL);
	}
	ret = pi->nextif->value;

	pi->nextif = pi->nextif->next;
	return(ret);
}
static int
end_ifwalk(ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	pi->nextif = NULL;
	zap_iflist();
	return(HA_OK);
}

static int
get_inputfd(ll_cluster_t*ci )
{
	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return -1;
	}
	return(fileno(ReplyFIFO));
}
static int
msgready(ll_cluster_t*ci )
{
	fd_set		fds;
	struct timeval	tv;
	int		rc;

	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return 0;
	}
	FD_ZERO(&fds);
	FD_SET(get_inputfd(ci), &fds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	rc = select(1, &fds, NULL, NULL, &tv);
	
	return (rc > 0);
}
static int
setfmode(ll_cluster_t* lcl, int mode)
{
	unsigned	filtermask;

	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return 0;
	}
	switch(mode) {

		case LLC_FILTER_DEFAULT:
			filtermask = DEFAULTREATMENT;
			break;
		case LLC_FILTER_PMODE:
			filtermask = (KEEPIT|DUPLICATE|DROPIT);
			break;
		case LLC_FILTER_ALLHB:
			filtermask = (KEEPIT|DUPLICATE|DROPIT|NOCHANGE);
			break;
		case LLC_FILTER_RAW:
			filtermask = ALLTREATMENTS;
			break;
		default:
			return(HA_FAIL);
	}
	return(hb_api_setfilter(filtermask));
	
}
static int
sendclustermsg(ll_cluster_t* lcl, struct ha_msg* msg)
{
	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	return(msg2stream(msg, MsgFIFO));
}
static int
sendnodemsg(ll_cluster_t* lcl, struct ha_msg* msg
,			const char * nodename)
{
	ClearLog();
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	if (ha_msg_mod(msg, F_TO, nodename) != HA_OK) {
		ha_log(LOG_ERR, "sendnodemsg: cannot set F_TO field");
		return(HA_FAIL);
	}
	return(msg2stream(msg, MsgFIFO));
}


struct llc_ops heartbeat_ops = {
	set_msg_callback,	/* set_msg_callback */
	set_nstatus_callback,	/* set_nstatus_callback */
	set_ifstatus_callback,	/* set_ifstatus_callback */
	init_nodewalk,		/* init_nodewalk */
	nextnode,		/* nextnode */
	end_nodewalk,		/* end_nodewalk */
	get_nodestatus,		/* node_status */
	init_ifwalk,		/* init_ifwalk */
	nextif,			/* nextif */
	end_ifwalk,		/* end_ifwalk */
	get_ifstatus,		/* if_status */
	sendclustermsg,		/* sendclustermsg */
	sendnodemsg,		/* sendnodemsg */
	get_inputfd,		/* inputfd */
	msgready,		/* msgready */
	hb_api_setsignal,	/* setmsgsignal */
	NULL,			/* rcvmsg */
	NULL,			/* readmsg */
	setfmode,		/* setfmode */
	APIError,		/* errormsg */
};

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
	fprintf(stderr, "Setting message signal\n");
	hb_api_setsignal(NULL, 0);
	fprintf(stderr, "Getting list of nodes\n");
	get_nodelist();
	fprintf(stderr, "Getting list of interfaces\n");
	get_iflist("kathyamy");
	fprintf(stderr, "Node status: %s\n"
	,	get_nodestatus(NULL, "kathyamy"));
	fprintf(stderr, "IF status: %s\n"
	,	get_ifstatus(NULL, "kathyamy", "eth0"));

	siginterrupt(SIGINT, 1);
	signal(SIGINT, gotsig);
	/* Read all subsequent replies... */
	fprintf(stderr, "Now waiting for more messages...\n");
	for(; !quitnow && (reply=read_hb_msg()) != NULL;) {
		fprintf(stderr, "Got another message...\n");
		ha_log_message(reply);
		ZAPMSG(reply);
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

static char	APILogBuf[MAXLINE];
void
ClearLog(void)
{
	APILogBuf[0] = EOS;
}


static const char *
APIError(ll_cluster_t* lcl)
{
	return(APILogBuf);
}

void
ha_log(int priority, const char * fmt, ...)
{
        va_list ap;
        char buf[MAXLINE];
 
        va_start(ap, fmt);
        vsnprintf(buf, MAXLINE, fmt, ap);
        va_end(ap);
 
	if (APILogBuf[0] != EOS && APILogBuf[strlen(APILogBuf)-1] != '\n') {

		/* Strncat checks for overflow */
		strncat(APILogBuf, "\n", sizeof(APILogBuf));
	}
			
	strncat(APILogBuf, buf, sizeof(APILogBuf));
}



void
ha_error(const char * msg)
{
 
	ha_log(0, msg);
 
}

void
ha_perror(const char * fmt, ...)
{
	const char *	err;
	char	errornumber[16];
	extern int	sys_nerr;

	va_list ap;
	char buf[MAXLINE];

	if (errno < 0 || errno >= sys_nerr) {
		sprintf(errornumber, "error %d\n", errno);
		err = errornumber;
	}else{
		err = sys_errlist[errno];
	}
	va_start(ap, fmt);
	vsnprintf(buf, MAXLINE, fmt, ap);
	va_end(ap);

	ha_log(LOG_ERR, "%s: %s", buf, err);

}
