/*
 * apphbtest:	application heartbeat test program
 *
 * This program registers with the application heartbeat server
 * and issues heartbeats from time to time...
 *
 * Copyright(c) 2002 Alan Robertson <alanr@unix.sh>
 *
 *********************************************************************
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <portability.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <clplumbing/cl_log.h>
#include <apphb.h>
#include <time.h>

long pid;
int debug = 0;

void doafailtest(void);

void multi_hb_test(int child_proc_num, int hb_intvl_ms, int hb_num
,	int dofailuretests);

void hb_normal(int hb_intvl_ms, int delaysecs, int hb_num, int dofailtests);

#define APPNAME_LEN 256
#define OPTARGS "n:p:i:dF"
#define USAGE_STR "Usage: [-n heartbeat number]\
[-p process number]\
[-f heartbeat interval(ms)][-d] [-F]"

int
main(int argc,char ** argv)
{
	int flag;
	int hb_num = 10;
	int child_proc_num = 1;
	int hb_intvl_ms = 1000;
	int dofailuretests = 1000;
	
	while (( flag = getopt(argc, argv, OPTARGS)) != EOF) {
		switch(flag) {
			case 'n':	/* Number of heartbeat */
				hb_num = atoi(optarg);
				break;
			case 'p':	/* Number of heartbeat processes */
				child_proc_num = atoi(optarg);
				break;
			case 'i':	/* Heartbeat interval */
				hb_intvl_ms = atoi(optarg);
				break;
			case 'd':	/* Debug */
				debug += 1;
				break;
			case 'F':	/* include Failure cases */
				dofailuretests=TRUE;
				break;
			default:
				fprintf(stderr
				,	"%s "USAGE_STR"\n", argv[0]);
				return(1);	
		}
	}

	cl_log_set_entity(argv[0]);
	cl_log_enable_stderr(TRUE);

	pid = getpid();
	
	multi_hb_test(child_proc_num, hb_intvl_ms, hb_num, dofailuretests);
	
	return(0);
}

void
doafailtest(void)
{
	int	j;
	int	rc;
	
	cl_log(LOG_INFO, "Failure Client registering - pid: %ld", pid);
	
	rc = apphb_register("test program", "normal");
	if (rc < 0) {
		cl_perror("registration failure");
		exit(1);
	}
	if (debug) {
		cl_log(LOG_INFO, "Failure Client registered");
	}
	
	fprintf(stderr, "Client setting 2 second heartbeat period");
	rc = apphb_setinterval(2000);
	if (rc < 0) {
		cl_perror("setinterval failure");
		exit(2);
	}

	for (j=0; j < 10; ++j) {
		sleep(1);
		fprintf(stderr, "+");
		if (j == 8) {
			apphb_setwarn(500);
		}
		rc = apphb_hb();
		if (rc < 0) {
			cl_perror("apphb_hb failure");
			exit(3);
		}

	}
	fprintf(stderr, "\n");
	sleep(3);
	fprintf(stderr, "!");
	rc = apphb_hb();
	if (rc < 0) {
		cl_perror("late apphb_hb failure");
		exit(4);
	}

	cl_log(LOG_INFO, "Client %ld unregistering", pid);
	rc = apphb_unregister();
	if (rc < 0) {
		cl_perror("apphb_unregister failure");
		exit(5);
	}
	rc = apphb_register("test program", "HANGUP");
	if (rc < 0) {
		cl_perror("second registration failure");
		exit(1);
	}
	/* Now we leave without further adieu -- HANGUP */
	cl_log(LOG_INFO, "Client %ld HANGUP!", pid);
}

void 
hb_normal(int hb_intvl_ms, int delaysecs, int hb_num, int dofailuretests)
{
	int j;
	int rc;
	struct timespec time_spec;
	char app_name[APPNAME_LEN];
	
	if (delaysecs) {
		fprintf(stderr, "Sleep %d (%ld)\n", delaysecs, pid);
		time_spec.tv_sec = delaysecs;
		time_spec.tv_nsec = 0;
		nanosleep(&time_spec, NULL);
	}

	cl_log_set_facility(LOG_USER);
	cl_log(LOG_INFO, "Client %ld registering", pid);
	
	sprintf(app_name, "hb_normal_%ld", pid);
	rc = apphb_register(app_name, "normal");
	if (rc < 0) {
		cl_perror("registration failure");
		exit(1);
	}
	if (debug) {
		cl_log(LOG_INFO, "Client %s registered", app_name);
	}
	
	cl_log(LOG_INFO, "Client %ld setting %d ms heartbeat interval"
			, pid, hb_intvl_ms);
	rc = apphb_setinterval(hb_intvl_ms);
	if (rc < 0) {
		cl_perror("setinterval failure");
		exit(2);
	}

	for (j=0; j < hb_num; ++j) {
		/* Sleep for half of the heartbeat interval */
		time_spec.tv_sec = hb_intvl_ms / 2000;
		time_spec.tv_nsec = (hb_intvl_ms % 2000) * 500000;
		nanosleep(&time_spec, NULL);
		if(debug >= 1) 
			fprintf(stderr, "%ld:+\n", pid);
		rc = apphb_hb();
		if (rc < 0) {
			cl_perror("apphb_hb failure");
			exit(3);
		}
	}
	
	cl_log(LOG_INFO, "Client %ld unregistering", pid);
	rc = apphb_unregister();
	if (rc < 0) {
		cl_perror("apphb_unregister failure");
		exit(4);
	}
	if (dofailuretests) {
		doafailtest();
	}
}

void 
multi_hb_test(int child_proc_num, int hb_intvl_ms, int hb_num
,	int dofailuretests)
{
	int j;
	
	for (j=0; j < child_proc_num; ++j) {
		switch(fork()){
		case 0:
			pid=getpid();
			hb_normal(hb_intvl_ms, 1 ,hb_num, dofailuretests);
			exit(0);
			break;
		case -1:
			cl_perror("Can't fork!");
			exit(1);
			break;
		default:
			/* In the parent. */
			break;
		}
	}	
	/* Wait for all our child processes to exit*/
	while(wait(NULL) > 0);
	errno = 0;
}
