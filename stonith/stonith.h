/*
 *	S hoot
 *	T he
 *	O ther
 *	N ode
 *	I n
 *	T he
 *	H ead
 *
 *	Cause the other machine to reboot or die - now.
 *
 *	We guarantee that when we report that the machine has been
 *	rebooted, then it has been (barring misconfiguration or hardware errors)
 *
 *	A machine which we have STONITHed won't do anything more to its peripherials
 *	etc. until it goes through the reboot cycle.
 */

/*
 *	Return codes from "Stonith" member functions.
 */

#define	S_OK		0	/* Machine correctly reset	*/
#define	S_BADCONFIG	1	/* Bad config info given	*/
#define	S_ACCESS	2	/* Can't access STONITH device	*/
				/* (login/passwd problem?)	*/
#define	S_BADHOST	3	/* Bad/illegal host/node name	*/
#define	S_RESETFAIL	4	/* Reset failed			*/
#define	S_TIMEOUT	5	/* Timed out in the dialogues	*/
#define	S_ISOFF		5	/* Can't reboot: Outlet is off	*/
#define	S_OOPS		6	/* Something strange happened	*/

typedef struct stonith {
	struct stonith_ops *	s_ops;
	void *			pinfo;
}Stonith;

/*
 *	These functions all use syslog(3) for error messages.
 *	Consequently they assume you've done an openlog() to initialize it for them.
 */
struct stonith_ops {
	void (*delete)			(Stonith*); /* Destructor */

	int (*set_config_file)		(Stonith *, const char * filename); 

	int (*set_config_info)		(Stonith *, const char * confstring); 
	const char* (*devid)		(Stonith*);

	/*
	 * Must call set_config_info or set_config_file before calling any of
	 * these.
	 */

	int (*status)			(Stonith *s);
	int (*reset_host)		(Stonith * s, const char * hostname);

	char** (*hostlist)		(Stonith* s);
					/* Returns list of hosts it supports */
	void (*free_hostlist)		(char** hostlist);
};

extern Stonith * stonith_new(const char * type);
