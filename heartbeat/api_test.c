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
	llc_msg_callback_t 	cf;
	void *			pd;
	struct gen_callback*	next;
}gen_callback_t;

typedef struct llc_private {
	const char *			PrivateId;
	llc_nstatus_callback_t		node_callback;
	void*				node_private;
	llc_ifstatus_callback_t		if_callback;
	void*				if_private;
	struct gen_callback*		genlist;
	struct stringlist*		nextnode;
	struct stringlist*		nextif;
}llc_private_t;
static const char * OurID = "Heartbeat private data";
#define ISOURS(l) (l && l->ll_cluster_private &&				\
		(((llc_private_t*)(l->ll_cluster_private))->PrivateId) == OurID)

static void		ClearLog(void);
static struct ha_msg*	hb_api_boilerplate(const char * apitype);
static int		hb_api_signon(struct ll_cluster*, const char * clientid);
static int		hb_api_signoff(struct ll_cluster*);
static int		hb_api_setfilter(unsigned);
static void		destroy_stringlist(struct stringlist *);
static struct stringlist*
			new_stringlist(const char *);
static int		get_nodelist(llc_private_t*);
static void		zap_nodelist(llc_private_t*);
static int		get_iflist(llc_private_t*, const char *host);
static void		zap_iflist(llc_private_t*);
static int		enqueue_msg(struct ha_msg*);
static struct ha_msg*	dequeue_msg(void);
static gen_callback_t*	search_gen_callback(const char * type, llc_private_t*);
static int		add_gen_callback(const char * msgtype
,	llc_private_t*, llc_msg_callback_t, void*);
static int		del_gen_callback(llc_private_t*, const char * msgtype);

static struct ha_msg*	read_api_msg(void);
static struct ha_msg*	read_hb_msg(ll_cluster_t*, int blocking);

static int		hb_api_setsignal(ll_cluster_t*, int nsig);
static int set_msg_callback
			(ll_cluster_t*, const char * msgtype
,			llc_msg_callback_t callback, void * p);
static int
set_nstatus_callback (ll_cluster_t*
,		llc_nstatus_callback_t cbf, 	void * p);
static int
		set_ifstatus_callback (ll_cluster_t* ci
,		llc_ifstatus_callback_t cbf, void * p);
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
static int		CallbackCall(llc_private_t* p, struct ha_msg * msg);
static struct ha_msg *	read_msg_w_callbacks(ll_cluster_t* llc, int blocking);
static int		rcvmsg(ll_cluster_t* llc, int blocking);

volatile struct process_info *	curproc = NULL;
static char		OurPid[16];
static const char *	OurClientID = NULL;
static FILE*		MsgFIFO = NULL;
static FILE*		ReplyFIFO = NULL;
static int		SignedOnAlready = 0;
static char 		OurNode[SYS_NMLN];
static char		ReplyFIFOName[API_FIFO_LEN];
static ll_cluster_t*	hb_cluster_new(void);

#define	ZAPMSG(m)	{ha_msg_del(m); (m) = NULL;}

/*
 * All the boilerplate common to creating heartbeat API request
 * messages.
 */
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

/*
 * Sign on as a heartbeat client process.
 */

static int
hb_api_signon(struct ll_cluster* cinfo, const char * clientid)
{
	struct ha_msg*	request;
	struct ha_msg*	reply;
	int		fd;
	static char	ReplyFdBuf[MAXLINE];
	struct utsname	un;
	int		rc;
	const char *	result;

	if (!ISOURS(cinfo)) {
		ha_log(LOG_ERR, "hb_api_signon: bad cinfo");
		return HA_FAIL;
	}
	if (SignedOnAlready) {
		hb_api_signoff(cinfo);
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

/*
 * Sign off (disconnect) as a heartbeat client process.
 */
static int
hb_api_signoff(struct ll_cluster* cinfo)
{
	struct ha_msg*	request;

	if (!ISOURS(cinfo)) {
		ha_log(LOG_ERR, "hb_api_signoff: bad cinfo");
		return HA_FAIL;
	}
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
/*
 * delete:  destroy the API object
 */
static int
hb_api_delete(struct ll_cluster* ci)
{
	llc_private_t* pi;
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "hb_api_signoff: bad cinfo");
		return HA_FAIL;
	}
	pi = (llc_private_t*)ci->ll_cluster_private;
	hb_api_signoff(ci);
	zap_iflist(pi);
	zap_nodelist(pi);
	memset(pi, 0, sizeof(*pi));
	ha_free(pi);
	memset(ci, 0, sizeof(*ci));
	ha_free(ci);
	return HA_OK;
}

/*
 * Set message filter mode.
 */
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

/*
 * Set signal for message notification.
 * Is this a security hole?
 */
int
hb_api_setsignal(ll_cluster_t* lcl, int nsig)
{
	struct ha_msg*	request;
	struct ha_msg*	reply;
	int		rc;
	const char *	result;
	char		csignal[32];

	ClearLog();
	if (!ISOURS(lcl)) {
		ha_log(LOG_ERR, "hb_api_setsignal: bad cinfo");
		return HA_FAIL;
	}
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

/*
 * Retrieve the list of nodes in the cluster.
 */
static int
get_nodelist(llc_private_t* pi)
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
		nodelist = sl;
		ZAPMSG(reply);
		if (strcmp(result, API_OK) == 0) {
			pi->nextnode = nodelist;
			return(HA_OK);
		}
	}
	if (reply != NULL) {
		zap_nodelist(pi);
		ZAPMSG(reply);
	}

	return HA_FAIL;
}
/*
 * Retrieve the list of interfaces for the given host.
 */
static int
get_iflist(llc_private_t* pi, const char *host)
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
		iflist = sl;
		ZAPMSG(reply);
		if (strcmp(result, API_OK) == 0) {
			pi->nextif = iflist;
			return(HA_OK);
		}
	}
	if (reply != NULL) {
		zap_iflist(pi);
		ZAPMSG(reply);
	}

	return HA_FAIL;
}
/*
 * Return the status of the given node.
 */
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
	if (!ISOURS(lcl)) {
		ha_log(LOG_ERR, "get_nodestatus: bad cinfo");
		return NULL;
	}
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
/*
 * Return the status of the given interface for the given machine.
 */
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
	if (!ISOURS(lcl)) {
		ha_log(LOG_ERR, "get_ifstatus: bad cinfo");
		return NULL;
	}
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
/*
 * Zap our list of nodes
 */
static void
zap_nodelist(llc_private_t* pi)
{
	destroy_stringlist(nodelist);
	nodelist=NULL;
	pi->nextnode = NULL;
}
/*
 * Zap our list of interfaces.
 */
static void
zap_iflist(llc_private_t* pi)
{
	destroy_stringlist(iflist);
	iflist=NULL;
	pi->nextif = NULL;
}

/*
 * Create a new stringlist.
 */
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

/*
 * Destroy (free) a stringlist.
 */
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

/*
 * Enqueue a message to be read later.
 */
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

/*
 * Dequeue a message.
 */
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

/*
 * Search the general callback list for the given message type
 */
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
 
/*
 * Add a general callback to the list of general callbacks.
 */
static int
add_gen_callback(const char * msgtype, llc_private_t* lcp
,	llc_msg_callback_t funp, void* pd)
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

/*
 * Delete a general callback from the list of general callbacks.
 */
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
 
/*
 * Read an API message.  All other messages are enqueued to be read later.
 */
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

/*
 * Read a heartbeat message.  Read from the queue first.
 */
static struct ha_msg *
read_hb_msg(ll_cluster_t* llc, int blocking)
{
	struct ha_msg*	msg;

	if (!ISOURS(llc)) {
		ha_log(LOG_ERR, "read_hb_msg: bad cinfo");
		return HA_FAIL;
	}
	msg = dequeue_msg();

	if (msg != NULL) {
		return(msg);
	}
	if (!blocking && !msgready(llc)) {
		return(NULL);
	}
	msg = msgfromstream(ReplyFIFO);

	return msg;
}

/*
 * Add a callback for the given message type.
 */
static int
set_msg_callback(ll_cluster_t* ci, const char * msgtype
,			llc_msg_callback_t callback, void * p)
{

	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "set_msg_callback: bad cinfo");
		return HA_FAIL;
	}
	return(add_gen_callback(msgtype,
	(llc_private_t*)ci->ll_cluster_private, callback, p));
}

/*
 * Set the node status change callback.
 */
static int
set_nstatus_callback (ll_cluster_t* ci
,		llc_nstatus_callback_t cbf, 	void * p)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	pi->node_callback = cbf;
	pi->node_private = p;
	return(HA_OK);
}
/*
 * Set the interface status change callback.
 */
static int
set_ifstatus_callback (ll_cluster_t* ci
,		llc_ifstatus_callback_t cbf, void * p)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	pi->if_callback = cbf;
	pi->if_private = p;
	return(HA_OK);
}

/*
 * Call the callback associated with this message (if any)
 * Return TRUE if a callback was called.
 */
static int
CallbackCall(llc_private_t* p, struct ha_msg * msg)
{
	const char *	mtype=  ha_msg_value(msg, F_TYPE);
	struct gen_callback*	gcb;

	if (mtype == NULL) {
		return(0);
	}
	
	/* Special case: node status (change) */

	if (p->node_callback && strcasecmp(mtype, T_STATUS) == 0) {
		p->node_callback(ha_msg_value(msg, F_ORIG)
		,	ha_msg_value(msg, F_STATUS), p->node_private);
		return(1);
	}

	/* Special case: interface status (change) */

	if (p->if_callback && strcasecmp(mtype, T_IFSTATUS) == 0) {
		p->if_callback(ha_msg_value(msg, F_NODE)
		,	ha_msg_value(msg, F_IFNAME)
		,	ha_msg_value(msg, F_STATUS)
		,	p->if_private);
		return(1);
	}

	/* The general case: Any other message type */

	for (gcb = p->genlist; gcb; gcb=gcb->next) {
		if (gcb->cf && strcasecmp(gcb->msgtype, mtype) == 0) {
			gcb->cf(msg, gcb->pd);
			return(1);
		}
	}
	return(0);
}
/*
 * Return the next message not handled by a callback.
 * Invoke callbacks for messages encountered along the way.
 */
static struct ha_msg *
read_msg_w_callbacks(ll_cluster_t* llc, int blocking)
{
	struct ha_msg*	msg = NULL;
	llc_private_t* p = (llc_private_t*) llc->ll_cluster_private;

	do {
		if (msg) {
			ZAPMSG(msg);
		}
		msg = read_hb_msg(llc, blocking);

	}while (msg && CallbackCall(p, msg));
	return(msg);
}
/*
 * Receive messages.  Activate callbacks.  Messages without callbacks
 * are ignored.  Potentially several messages could be acted on.
 * Perhaps this is a bug?
 */
static int
rcvmsg(ll_cluster_t* llc, int blocking)
{
	struct ha_msg*	msg = NULL;
	
	msg=read_msg_w_callbacks(llc, blocking);

	if (msg) {
		ZAPMSG(msg);
		return(1);
	}
	return(0);
}

/*
 * Initialize nodewalk. (mainly retrieve list of nodes)
 */
static int
init_nodewalk (ll_cluster_t* ci)
{
	llc_private_t*	pi;
	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "init_nodewalk: bad cinfo");
		return HA_FAIL;
	}
	pi = (llc_private_t*)ci->ll_cluster_private;

	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	zap_nodelist(pi);

	return(get_nodelist(pi));
}

/*
 * Return the next node in the list, or NULL if none.
 */
static const char *
nextnode (ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	const char *	ret;

	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "nextnode: bad cinfo");
		return NULL;
	}
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
/*
 * Clean up after a nodewalk (throw away node list)
 */
static int
end_nodewalk(ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "end_nodewalk: bad cinfo");
		return HA_FAIL;
	}
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	zap_nodelist(pi);
	return(HA_OK);
}

/*
 * Initialize interface walk. (mainly retrieve list of interfaces)
 */
static int
init_ifwalk (ll_cluster_t* ci, const char * host)
{
	llc_private_t*	pi;
	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "init_ifwalk: bad cinfo");
		return HA_FAIL;
	}
	pi = (llc_private_t*)ci->ll_cluster_private;
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	zap_iflist(pi);
	return(get_iflist(pi, host));
}

/*
 * Return the next interface in the iflist, or NULL if none.
 */
static const char *
nextif (ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	const char *	ret;

	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "nextif: bad cinfo");
		return HA_FAIL;
	}
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

/*
 * Clean up after a ifwalk (throw away interface list)
 */
static int
end_ifwalk(ll_cluster_t* ci)
{
	llc_private_t*	pi = ci->ll_cluster_private;
	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "end_ifwalk: bad cinfo");
		return HA_FAIL;
	}
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	zap_iflist(pi);
	return HA_OK;
}

/*
 * Return the file descriptor associated with this object.
 */
static int
get_inputfd(ll_cluster_t* ci)
{
	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "get_inputfd: bad cinfo");
		return(-1);
	}
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return -1;
	}
	return(fileno(ReplyFIFO));
}

/*
 * Return TRUE (1) if there is a message ready to read.
 */
static int
msgready(ll_cluster_t*ci )
{
	fd_set		fds;
	struct timeval	tv;
	int		rc;

	ClearLog();
	if (!ISOURS(ci)) {
		ha_log(LOG_ERR, "msgready: bad cinfo");
		return 0;
	}
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return 0;
	}
	if (firstQdmsg) {
		return 1;
	}
	FD_ZERO(&fds);
	FD_SET(get_inputfd(ci), &fds);
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	rc = select(1, &fds, NULL, NULL, &tv);
	
	return (rc > 0);
}

/*
 * Set message filter mode
 */
static int
setfmode(ll_cluster_t* lcl, int mode)
{
	unsigned	filtermask;

	ClearLog();
	if (!ISOURS(lcl)) {
		ha_log(LOG_ERR, "setfmode: bad cinfo");
		return HA_FAIL;
	}
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
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
/*
 * Send a message to the cluster.
 */
static int
sendclustermsg(ll_cluster_t* lcl, struct ha_msg* msg)
{
	ClearLog();
	if (!ISOURS(lcl)) {
		ha_log(LOG_ERR, "sendclustermsg: bad cinfo");
		return HA_FAIL;
	}
	if (!SignedOnAlready) {
		ha_log(LOG_ERR, "not signed on");
		return HA_FAIL;
	}
	return(msg2stream(msg, MsgFIFO));
}

/*
 * Send a message to a specific node in the cluster.
 */
static int
sendnodemsg(ll_cluster_t* lcl, struct ha_msg* msg
,			const char * nodename)
{
	ClearLog();
	if (!ISOURS(lcl)) {
		ha_log(LOG_ERR, "sendnodemsg: bad cinfo");
		return HA_FAIL;
	}
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

static char	APILogBuf[MAXLINE];
int		BufLen = 0;

void
ClearLog(void)
{
	APILogBuf[0] = EOS;
	BufLen = 1;
}

static const char *
APIError(ll_cluster_t* lcl)
{
	return(APILogBuf);
}
void
ha_log(int priority, const char * fmt, ...)
{
	int	len;
        va_list ap;
        char buf[MAXLINE];
 
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
	len = strlen(buf);

	if ((BufLen + len) >= sizeof(APILogBuf)) {
		ClearLog();
	}
		
	if (APILogBuf[0] != EOS && APILogBuf[strlen(APILogBuf)-1] != '\n') {
		strncat(APILogBuf, "\n", sizeof(APILogBuf));
		BufLen++;
	}

	strncat(APILogBuf, buf, sizeof(APILogBuf));
	BufLen += len;
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

static struct llc_ops heartbeat_ops = {
	hb_api_signon,		/* signon */
	hb_api_signoff,		/* signon */
	hb_api_delete,		/* delete */
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
	rcvmsg,			/* rcvmsg */
	read_msg_w_callbacks,	/* readmsg */
	setfmode,		/* setfmode */
	APIError,		/* errormsg */
};


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

/*
 * Create a new heartbeat API object
 */
static ll_cluster_t*
hb_cluster_new()
{
	ll_cluster_t*	ret;
	struct llc_private* hb;

	if ((hb = MALLOCT(struct llc_private)) == NULL) {
		return(NULL);
	}
	memset(hb, 0, sizeof(*hb));
	if ((ret = MALLOCT(ll_cluster_t)) == NULL) {
		ha_free(hb);
		hb = NULL;
		return(NULL);
	}
	memset(ret, 0, sizeof(*ret));

	hb->PrivateId = OurID;
	ret->ll_cluster_private = hb;
	ret->llc_ops = &heartbeat_ops;

	return ret;
}

/*
 * Create a new low-level cluster object of the specified type.
 */
ll_cluster_t*
ll_cluster_new(const char * llctype)
{
	if (strcmp(llctype, "heartbeat") == 0) {
		return hb_cluster_new();
	}
	return NULL;
}
void NodeStatus(const char * node, const char * status, void * private);

void
NodeStatus(const char * node, const char * status, void * private)
{
	fprintf(stderr, "Status update: Node %s now has status %s\n"
	,	node, status);
}
void LinkStatus(const char * node, const char *, const char *, void*);
void
LinkStatus(const char * node, const char * lnk, const char * status
,	void * private)
{
	fprintf(stderr, "Link Status update: Link %s/%s now has status %s\n"
	,	node, lnk, status);
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
	ll_cluster_t*	hb;
	const char *	node;
	const char *	intf;

	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	hb = ll_cluster_new("heartbeat");
	fprintf(stderr, "Signing in with heartbeat\n");
	hb->llc_ops->signon(hb, NULL);

	hb->llc_ops->set_nstatus_callback(hb, NodeStatus, NULL);
	hb->llc_ops->set_ifstatus_callback(hb, LinkStatus, NULL);

#if 0
	fmask = LLC_FILTER_RAW;
#else
	fmask = LLC_FILTER_DEFAULT;
#endif
	fprintf(stderr, "Setting message filter mode\n");
	hb->llc_ops->setfmode(hb, fmask);
	fprintf(stderr, "Setting message signal\n");
	hb->llc_ops->setmsgsignal(hb, 0);

	hb->llc_ops->init_nodewalk(hb);
	while((node = hb->llc_ops->nextnode(hb))!= NULL) {
		fprintf(stderr, "Cluster node: %s: status: %s\n", node
		,	hb->llc_ops->node_status(hb, node));
		hb->llc_ops->init_ifwalk(hb, node);
		while ((intf = hb->llc_ops->nextif(hb))) {
			fprintf(stderr, "\tnode %s: intf: %s ifstatus: %s\n"
			,	node, intf
			,	hb->llc_ops->if_status(hb, node, intf));
		}
		hb->llc_ops->end_ifwalk(hb);
	}
	hb->llc_ops->end_nodewalk(hb);

	siginterrupt(SIGINT, 1);
	signal(SIGINT, gotsig);
	/* Read all subsequent replies... */
	fprintf(stderr, "Now waiting for more messages...\n");
	for(; !quitnow && (reply=hb->llc_ops->readmsg(hb, 1)) != NULL;) {
		fprintf(stderr, "Got another message...\n");
		ha_log_message(reply);
		fputs(hb->llc_ops->errmsg(hb), stderr);
		fputs("\n", stderr);
		ClearLog();
		ZAPMSG(reply);
	}
	if (!quitnow) {
		perror("read_hb_msg returned NULL");
	}
	hb->llc_ops->signoff(hb);
	hb->llc_ops->delete(hb);
	return 0;
}
