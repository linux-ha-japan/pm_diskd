#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/times.h>

#include "expect.h"

#ifndef EOS
#	define	EOS '\0'
#endif


/*
 *	Look for ('expect') any of a series of tokens in the input
 *	Return the token type for the given token or -1 on error.
 */

int
ExpectToken(int	fd, struct Etoken * toklist, int to_secs, char * buf
,	int maxline)
{
	clock_t		starttime;
	clock_t		endtime;
	int		wraparound=0;
	int		tickstousec = (1000000/CLK_TCK);
	clock_t		now;
	clock_t		ticks;
	int		nchars = 1; /* reserve space for an EOS */
	struct timeval	tv;

	struct Etoken *	this;

	/* Figure out when to give up.  Handle lbolt wraparound */

	starttime = times(NULL);
	ticks = (to_secs*CLK_TCK);
	endtime = starttime + ticks;

	if (endtime < starttime) {
		wraparound = 1;
	}

	for (this=toklist; this->string; ++this) {
		this->matchto = 0;
	}


	while (now = times(NULL),
		(wraparound && (now > starttime || now <= endtime))
		||	(!wraparound && now <= endtime)) {

		fd_set infds;
		char	ch;
		clock_t		timeleft;
		int		retval;

		timeleft = endtime - now;

		tv.tv_sec = timeleft / CLK_TCK;
		tv.tv_usec = (timeleft % CLK_TCK) * tickstousec;

		if (tv.tv_sec == 0 && tv.tv_usec < tickstousec) {
			/* Give 'em a little chance */
			tv.tv_usec = tickstousec;
		}

		/* Watch our FD to see when it has input. */
           	FD_ZERO(&infds);
           	FD_SET(fd, &infds);

		retval = select(fd+1, &infds, NULL, NULL, &tv); 
		if (retval <= 0) {
			errno = ETIME;
			return(-1);
		}
		/* Whew!  All that work just to read one character! */
		
		if (read(fd, &ch, sizeof(ch)) <= 0) {
			return(-1);
		}
		/* Save the text, if we can */
		if (buf && nchars < maxline) {
			*buf = ch;
			++buf;
			*buf = EOS;
			++nchars;
		}
#if 0
		fprintf(stderr, "%c", ch);
#endif

		/* See how this character matches our expect strings */

		for (this=toklist; this->string; ++this) {

			if (ch == this->string[this->matchto]) {

				/* It matches the current token */

			 	++this->matchto;
				if (this->string[this->matchto] == EOS){
					/* Hallelujah! We matched */
					return(this-toklist);
				}
			}else{

				/* It doesn't appear to match this token */

				int	curlen;
				int	nomatch=1;
				/*
				 * If we already had a match (matchto is
				 * greater than zero), we look for a match
				 * of the tail of the pattern matched so far
				 * (with the current character) against the head
				 * of the pattern.
				 */

				/*
				 * This is to make the string "aab" match
				 * the pattern "ab" correctly 
				 * Painful, but nice to do it right.
				 */

				for (curlen = (this->matchto)
				;	nomatch && curlen >= 0;
					--curlen) 				{
					const char *	tail;
					tail=(this->string)
					+	this->matchto
					-	curlen;

					if (strncmp(this->string, tail
					,	curlen) == 0
					&&	this->string[curlen] == ch)  {
						
						if (this->string[curlen+1]==EOS){
							/* We matched! */
							return(this-toklist);
						}
						this->matchto = curlen+1;
						nomatch=0;
					}
				}
				if (nomatch) {
					this->matchto = 0;
				}
			}
		}
	}
	errno = ETIME;
	return(-1);
}

int
StartProcess(const char * cmd, int * readfd, int * writefd)
{
	pid_t	pid;
	int	wrpipe[2];	/* The pipe the parent process writes to */
				/* (which the child process reads from) */
	int	rdpipe[2];	/* The pipe the parent process reads from */
				/* (which the child process writes to) */

	if (pipe(wrpipe) < 0) {
		perror("cannot create pipe\n");
		return(-1);
	}
	if (pipe(rdpipe) < 0) {
		perror("cannot create pipe\n");
		close(wrpipe[0]);
		close(wrpipe[1]);
		return(-1);
	}
	switch(pid=fork()) {

		case -1:	perror("cannot StartProcess cmd");
				close(rdpipe[0]);
				close(wrpipe[1]);
				close(wrpipe[0]);
				close(rdpipe[1]);
				return(-1);

		case 0:		/* We are the child */

				/* Redirect stdin */
				close(0);
				dup2(wrpipe[0], 0);
				close(wrpipe[0]);
				close(wrpipe[1]);

				/* Redirect stdout */
				close(1);
				dup2(rdpipe[1], 1);
				close(rdpipe[0]);
				close(rdpipe[1]);

				execlp("/bin/sh", "sh", "-c", cmd, NULL);
				perror("cannot exec shell!");
				exit(1);

		default:	/* We are the parent */
				*readfd = rdpipe[0];
				close(rdpipe[1]);

				*writefd = wrpipe[1];
				close(wrpipe[0]);
				return(pid);
	}
	/*NOTREACHED*/
	return(-1);
}
