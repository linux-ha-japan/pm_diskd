
/*
 * Types of messages. 
 * DROPIT and/or DUPLICATE are only used when a debugging callback
 * is registered.
 */ 

#define	KEEPIT		1	/* A set of bits */
#define	DROPIT		2
#define DUPLICATE	4

#define	ALLTREATMENTS	(KEEPIT|DROPIT|DUPLICATE)
#define	DEBUGTREATMENTS	(DROPIT|DUPLICATE)
#define	DEFAULTREATMENT	(KEEPIT)

#define NR_TYPES 3

/* main hook called from heartbeat code. */

void    heartbeat_monitor(struct ha_msg * msg, int status, const char * iface);

/* Generic message callback structure and callback function definition */

struct message_callback {
	pid_t pid; /* which client registered the callback */
    void (*message_callback) (const struct ha_msg * msg, const char *iface
	, const char *node, pid_t pid);
};

typedef void (message_callback_t) (const struct ha_msg * msg, const char *iface
				  , const char *node);
