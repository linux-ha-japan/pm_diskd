/*
 * apphbtest:	application heartbeat test program
 *
 * This program registers with the application heartbeat server
 * and just issues heartbeats from time to time...
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
#include <apphb.h>

void doatest(void);
int
main(int argc,char ** argv)
{
	int	j;
	int	max = 1;

	if (argc > 1 && atoi(argv[1]) > 0) {
		max = atoi(argv[1]);
	}

	if (max == 1)  {
		doatest();
		return 0;
	}

	for (j=0; j < max; ++j) {
		switch(fork()){

		case 0:
			doatest();
			exit(0);
			break;
		case -1:
			fprintf(stderr, "Can't fork!\n");
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
doatest(void)
{
	int	j;
	int	rc;

	sleep(5);
	fprintf(stderr, "Client starting - pid: %ld\n", (long) getpid());
	rc = apphb_register("test program");
	if (rc < 0) {
		perror("registration failure");
		sleep(5);
		exit(1);
	}
	
	fprintf(stderr, "Client setting 2 second heartbeat period\n");
	rc = apphb_setinterval(2000);
	if (rc < 0) {
		perror("setinterval failure");
		exit(2);
	}

	for (j=0; j < 10; ++j) {
		sleep(1);
		fprintf(stderr, "+");
		rc = apphb_hb();
		if (rc < 0) {
			perror("apphb_hb failure");
			exit(3);
		}

	}
	fprintf(stderr, "\n");
	sleep(3);
	fprintf(stderr, "!");
	rc = apphb_hb();
	if (rc < 0) {
		perror("late apphb_hb failure");
		exit(4);
	}

	fprintf(stderr, "Client unregistering\n");
	rc = apphb_unregister();
	if (rc < 0) {
		perror("apphb_unregister failure");
		exit(5);
	}
}
