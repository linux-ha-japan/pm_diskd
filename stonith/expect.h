/*
 *	Expect Tokens. 
 *
 *	If we find any of the given tokens in the input stream,
 *	we return it's "toktype", so we can tell which one was
 *	found.
 *
 */

struct Etoken {
	const char *	string;		/* The token to look for */
	int		toktype;	/* The type to return on match */
	int		matchto;	/* Modified during matches */
};

int ExpectToken(int fd
,	struct Etoken * toklist	/* List of tokens to match against */
				/* Final token has NULL string */
,	int to_secs		/* Timeout value in seconds */
,	char * buf		/* If non-NULL, then all the text
				 * matched/skipped over by this match */
,	int maxline);		/* Size of 'buf' area in bytes */


/*
 *	A handy little routine.  It runs the given process
 *	with it's standard output redirected into our *readfd, and
 *	its standard input redirected from our *writefd
 *
 *	Doing this with all the pipes, etc. required for doing this
 *	is harder than it sounds :-)
 */

int StartProcess(const char * cmd, int* readfd, int* writefd);

#ifndef EOS
#	define	EOS '\0'
#endif
