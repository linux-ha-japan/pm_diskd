#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <heartbeat.h>
#include <hb_api_core.h>
volatile struct process_info *	curproc = NULL;
int
main(int argc, char ** argv)
{
	struct ha_msg*	request;
	struct ha_msg*	reply;
	char		mypid[10];
	char		fifoname[128];
	FILE*		msgfifo;
	FILE*		replyfifo;
	int		fd;
	char		fdbuf[MAXLINE];
	char		filtermask[15];
	const char *		here = "kathyamy";
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	snprintf(mypid, sizeof(mypid), "%d", getpid());
	snprintf(fifoname, sizeof(fifoname), "/var/run/heartbeat-api/%d", getpid());

	if ((request = ha_msg_new(4)) == NULL) {
		fprintf(stderr, "api_process_request: out of memory/1\n");
		return 1;
	}
	if (ha_msg_add(request, F_TYPE, T_APIREQ) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/1\n");
		ha_msg_del(request); request=NULL;
		return 2;
	}
	if (ha_msg_add(request, F_APIREQ, API_NEWCLIENT) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return 3;
	}
	if (ha_msg_add(request, F_TO, here) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return 4;
	}
	if (ha_msg_add(request, F_PID, mypid) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return 4;
	}
	
	if (ha_msg_add(request, F_FROMID, "testclient") != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return 5;
	}
	
	fprintf(stderr, "Creating %s\n", fifoname);
	mkfifo(fifoname, 0600);
	fprintf(stderr, "Opening %s 'O_RDONLY'\n", fifoname);

	/* We open it this way to keep the open from hanging... */
	fd =open(fifoname, O_RDWR);

	if ((replyfifo = fdopen(fd, "r")) == NULL) {
		fprintf(stderr, "can't open reply fifo %s\n", fifoname);
		return 6;
	}
#if 0
	setvbuf(replyfifo, fdbuf, _IOLBF, sizeof(fdbuf));
#else
	setbuf(replyfifo, NULL);
	(void)fdbuf;
#endif

	if ((msgfifo = fopen(FIFONAME, "w")) == NULL) {
		perror("can't open " FIFONAME);
		return 6;
	}

	/* Send message */
	fprintf(stderr, "Sending this message...\n");
	ha_log_message(request);
	msg2stream(request, msgfifo);
	ha_msg_del(request); request=NULL;

	/* Read reply... */
	if ((reply=msgfromstream(replyfifo)) == NULL) {
		perror("can't read reply");
		return 7;
	}
	fprintf(stderr, "Got a reply\n");
	ha_log_message(reply);
	ha_msg_del(reply); reply=NULL;

#if 0
	if ((request = ha_msg_new(4)) == NULL) {
		fprintf(stderr, "api_process_request: out of memory/1\n");
		return 1;
	}
	if (ha_msg_add(request, F_TYPE, T_APIREQ) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/1\n");
		ha_msg_del(request); request=NULL;
		return 2;
	}
	if (ha_msg_add(request, F_APIREQ, API_SETFILTER) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return 3;
	}
	if (ha_msg_add(request, F_PID, mypid) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return 4;
	}
	
	if (ha_msg_add(request, F_FROMID, "testclient") != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return 5;
	}
	snprintf(filtermask, sizeof(filtermask), "%x", ALLTREATMENTS);
	if (ha_msg_add(request, F_FILTERMASK, filtermask) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(request); request=NULL;
		return 5;
	}

	/* Send message */
	fprintf(stderr, "Sending this message...\n");
	ha_log_message(request);
	msg2stream(request, msgfifo);
	ha_msg_del(request); request=NULL;

	/* Read reply... */
	if ((reply=msgfromstream(replyfifo)) == NULL) {
		perror("can't read reply");
		return 7;
	}
	fprintf(stderr, "Got a reply\n");
	ha_log_message(reply);
	ha_msg_del(reply); reply=NULL;
#else
	(void)filtermask;
#endif
	/* Read all subsequent replies... */
	for(; (reply=msgfromstream(replyfifo)) != NULL;) {
		fprintf(stderr, "Got another message...\n");
		ha_log_message(reply);
		ha_msg_del(reply); reply=NULL;
	}
	perror("msgfromstream returned NULL");
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
