#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <heartbeat.h>
#include <hb_api_core.h>
volatile struct process_info *	curproc = NULL;
int
main(int argc, char ** argv)
{
	struct ha_msg*	msg;
	char		mypid[10];
	char		fifoname[128];
	char		cmd[128];
	FILE*		fifo;
	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	snprintf(mypid, sizeof(mypid), "%d", getpid());
	snprintf(fifoname, sizeof(fifoname), "/var/run/heartbeat-api/%d", getpid());
	snprintf(cmd, sizeof(cmd), "cat %s &", fifoname);

	if ((msg = ha_msg_new(4)) == NULL) {
		fprintf(stderr, "api_process_request: out of memory/1\n");
		return 1;
	}
	if (ha_msg_add(msg, F_TYPE, T_APIREQ) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/1\n");
		ha_msg_del(msg); msg=NULL;
		return 2;
	}
	if (ha_msg_add(msg, F_APIREQ, API_NEWCLIENT) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(msg); msg=NULL;
		return 3;
	}
	if (ha_msg_add(msg, F_PID, mypid) != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(msg); msg=NULL;
		return 4;
	}
	
	if (ha_msg_add(msg, F_FROMID, "testclient") != HA_OK) {
		fprintf(stderr, "api_process_request: cannot add field/2\n");
		ha_msg_del(msg); msg=NULL;
		return 5;
	}
	
	if ((fifo = fopen(FIFONAME, "w")) == NULL) {
		perror("can't open " FIFONAME);
		return 6;
	}
	mkfifo(fifoname, 0600);
	ha_log_message(msg);
	system(cmd);
	msg2stream(msg, fifo);
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
