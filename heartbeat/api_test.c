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
volatile struct process_info *	curproc = NULL;
static int hb_api_signon(const char * clientid);
static int hb_api_signoff(void);
static int hb_api_setfilter(unsigned);

void gotsig(int nsig);
int quitnow = 0;
void gotsig(int nsig)
{
	(void)nsig;
	quitnow = 1;
}

static char		OurPid[16];
static const char *	OurClientID = NULL;
static FILE*		MsgFIFO = NULL;
static FILE*		ReplyFIFO = NULL;
static int		SignedOnAlready = 0;
static char 		OurNode[SYS_NMLN];
static char		ReplyFIFOName[API_FIFO_LEN];

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

	if ((request = ha_msg_new(4)) == NULL) {
		fprintf(stderr, "api_process_request: out of memory/1\n");
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_TYPE, T_APIREQ) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/1\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_APIREQ, API_SIGNON) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_TO, OurNode) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_PID, OurPid) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	
	if (ha_msg_add(request, F_FROMID, OurClientID) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	
	fprintf(stderr, "Creating %s\n", ReplyFIFOName);
	mkfifo(ReplyFIFOName, 0600);
	fprintf(stderr, "Opening %s 'O_RDONLY'\n", ReplyFIFOName);

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
	fprintf(stderr, "Sending this message...\n");
	ha_log_message(request);
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;

	/* Read reply... */
	if ((reply=msgfromstream(ReplyFIFO)) == NULL) {
		ha_msg_del(request); request=NULL;
		perror("can't read reply");
		return HA_FAIL;
	}
	fprintf(stderr, "Got a reply\n");
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, "OK") == 0) {
		rc = HA_OK;
		SignedOnAlready = 1;
	}else{
		rc = HA_FAIL;
	}
	ha_log_message(reply);
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

	if ((request = ha_msg_new(4)) == NULL) {
		fprintf(stderr, "api_process_request: out of memory/1\n");
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_TYPE, T_APIREQ) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/1\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_APIREQ, API_SIGNOFF) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_TO, OurNode) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_PID, OurPid) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	
	if (ha_msg_add(request, F_FROMID, OurClientID) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	
	/* Send message */
	fprintf(stderr, "Sending this message...\n");
	ha_log_message(request);
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

	if ((request = ha_msg_new(4)) == NULL) {
		fprintf(stderr, "api_process_request: out of memory/1\n");
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_TYPE, T_APIREQ) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/1\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_APIREQ, API_SETFILTER) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_TO, OurNode) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	if (ha_msg_add(request, F_PID, OurPid) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	
	if (ha_msg_add(request, F_FROMID, OurClientID) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}

	snprintf(filtermask, sizeof(filtermask), "%x", fmask);
	if (ha_msg_add(request, F_FILTERMASK, filtermask) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return HA_FAIL;
	}
	
	/* Send message */
	fprintf(stderr, "Sending this message...\n");
	ha_log_message(request);
	msg2stream(request, MsgFIFO);
	ha_msg_del(request); request=NULL;

	/* Read reply... */
	if ((reply=msgfromstream(ReplyFIFO)) == NULL) {
		ha_msg_del(request); request=NULL;
		perror("can't read reply");
		return HA_FAIL;
	}
	fprintf(stderr, "Got a reply\n");
	if ((result = ha_msg_value(reply, F_APIRESULT)) != NULL
	&&	strcmp(result, "OK") == 0) {
		rc = HA_OK;
	}else{
		rc = HA_FAIL;
	}
	ha_log_message(reply);
	ha_msg_del(reply); reply=NULL;

	return rc;
}

int
main(int argc, char ** argv)
{
	struct ha_msg*	reply;
	unsigned	fmask;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	hb_api_signon(NULL);

#if 0
	fmask = ALLTREATMENTS;
#else
	fmask = DEFAULTREATMENT;
#endif
	hb_api_setfilter(fmask);

	siginterrupt(SIGINT, 1);
	signal(SIGINT, gotsig);
	/* Read all subsequent replies... */
	for(; !quitnow && (reply=msgfromstream(ReplyFIFO)) != NULL;) {
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
                                               /* HA-logging function */
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
