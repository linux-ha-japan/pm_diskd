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
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <clplumbing/cl_log.h>
#include <apphb.h>

void doatest(int delaysecs);
long	pid;

int
main(int argc,char ** argv)
{
	int	j;
	int	max = 1;

	cl_log_set_entity(argv[0]);
	cl_log_enable_stderr(TRUE);
	pid = getpid();

	if (argc > 1 && atoi(argv[1]) > 0) {
		max = atoi(argv[1]);
	}

	if (max == 1)  {
		doatest(0);
		return 0;
	}

	for (j=0; j < max; ++j) {
		switch(fork()){

		case 0:
			doatest(5);
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
	return(0);
}
void
doatest(int delaysecs)
{
	int	j;
	int	rc;

	
	if (delaysecs) {
		fprintf(stderr, "Sleep %d (%ld)\n"
		,	delaysecs, pid);
		sleep(delaysecs);
	}
	cl_log_set_facility(LOG_USER);
	cl_log(LOG_INFO, "Client registering - pid: %ld", pid);
	
	rc = apphb_register("test program", "normal");
	if (rc < 0) {
		cl_perror("registration failure");
		exit(1);
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
