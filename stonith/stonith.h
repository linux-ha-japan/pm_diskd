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
 *	A machine which we have STONITHed won't do anything more to its
 *	peripherials etc. until it goes through the reboot cycle.
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
 *	Consequently they assume you've done an openlog() to initialize it
 *	for them.
 */
struct stonith_ops {
	void (*delete)			(Stonith*);		/* Destructor */
	int (*set_config_file)		(Stonith *, const char   * filename); 
	const char* (*conf_file_syntax)	(Stonith*);		/* Never fails */
	int (*set_config_info)		(Stonith *, const char   * confstring); 
	const char* (*conf_info_syntax)	(Stonith*);		/* Never fails */
	const char* (*devid)		(Stonith*);		/* Never fails */

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

extern Stonith *	stonith_new(const char * type);
extern const char **	stonith_types(void);	/* NULL-terminated list */

/*
 * It is intended that the conf_file_syntax member function return a string
 * describing the syntax of the configuration file that set_config_file() will
 * accept. This string can then be used as short help text in configuration tools,
 * etc.
 *
 * The conf_info_syntax() function serves a similar purpose with respect to the 
 * configuration string.
 *
 * The devid member function is intended to return the type of the Stonith
 * device.  Note that it may return a different result once it has attempted
 * to talk to the device (like after a status() call).
 *
 * A good way for a GUI to work which configures STONITH devices would be to
 * use the result of the stonith_types() call in a pulldown menu.
 *
 * Once the type is selected, create a Stonith object of the selected type.
 * Then, create a dialog box to create the configuration info for the device
 * using the text from the conf_info_syntax() member function to direct the
 * user in what information to supply in the conf_info string.
 *
 * Once the user has completed their selection, it can be tested for syntactic
 * validity with set_config_info().
 *
 * If it passes set_config_info(), it can be further validated using status()
 * which will then actually try and talk to the STONITH device.  If status()
 * returns S_OK, then communication with the device was successfully established.
 *
 * Normally that would mean that logins, passwords, device names, and IP addresses,
 * etc. have been validated as required by the particular device.
 *
 * At this point, you can ask the device which machines it knows how to reset
 * using the hostlist() member function.
 *
 * I am a concerned that the conf_file_syntax and conf_info_syntax functions
 * may not be the right interface in the multi-language Linux world.
 *
 */
