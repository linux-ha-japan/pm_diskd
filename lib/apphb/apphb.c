#include <portability.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <apphb.h>
#define	time	footime
#define	index	fooindex
#include	<glib.h>
#undef time
#undef index

#include <clplumbing/ipc.h>
#include <clplumbing/apphb_cs.h>

static struct OCF_IPC_CHANNEL*	hbcomm = NULL;
static GHashTable *		hbattrs;
static int			hbstatus = -1;


static int apphb_getrc(void);

/* Get return code from previous operation */
static int
apphb_getrc(void)
{
	struct apphb_rc * rcs;
	int		rc;

	struct OCF_IPC_MESSAGE * msg;

	if (hbcomm->ops->recv(hbcomm, &msg) != CH_SUCCESS) {
		return EIO;
	}
	rcs = msg->msg_body;
	rc = rcs->rc;
	msg->msg_done(msg);
	return rc;
}

/* Register for application heartbeat services */
int
apphb_register(const char * appname)
{
	int	err;
	struct OCF_IPC_MESSAGE Msg;
	struct apphb_signupmsg msg;
	static char path [] = PATH_ATTR;
	static char sockpath [] = APPHBSOCKPATH;

	if (hbcomm != NULL) {
		errno = EEXIST;
		return -1;
	}

	if (appname == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (strlen(appname) >= APPHB_OLEN) {
		errno = ENAMETOOLONG;
		return -1;
	}

	/* Create communication channel with server... */

	hbattrs = g_hash_table_new(g_str_hash,g_str_equal);
	g_hash_table_insert(hbattrs, path, sockpath);
  
	hbcomm = ipc_channel_constructor(IPC_ANYTYPE, hbattrs);
  
	if (hbcomm == NULL
	||	(hbstatus = hbcomm->ops->initiate_connection(hbcomm)
	!=	CH_SUCCESS)) {
		apphb_unregister();
  		errno = EBADF;
		return -1;
	}

	/* Send registration message ... */
	strncpy(msg.msgtype, REGISTER, sizeof(msg.msgtype));
	strncpy(msg.appname, appname, sizeof(msg.appname));
	msg.pid = getpid();

	Msg.msg_body = &msg;
	Msg.msg_len = sizeof(msg);
	Msg.msg_done = NULL;
	Msg.msg_private = NULL;
	Msg.ch = hbcomm;

	if (hbcomm->ops->send(hbcomm, &Msg) != CH_SUCCESS) {
		apphb_unregister();
  		errno = EBADF;
		return -1;
	}
	if ((err = apphb_getrc()) != 0) {
		hbstatus = -1;
		errno = err;
		return -1;
	}
	return 0;
}

/* Unregister from application heartbeat services */
int
apphb_unregister(void)
{
	int	rc = 0;
	int	err;
	struct apphb_msg msg;
	struct OCF_IPC_MESSAGE Msg;


	if (hbcomm == NULL || hbstatus != CH_SUCCESS) {
		errno = ESRCH;
		rc = -1;
	}

	/* Send an unregister message to the server... */
	if (hbcomm != NULL && hbstatus == CH_SUCCESS) {
		strncpy(msg.msgtype, UNREGISTER, sizeof(msg.msgtype));
		Msg.msg_body = &msg;
		Msg.msg_len = sizeof(msg);
		Msg.msg_done = NULL;
		Msg.msg_private = NULL;
		Msg.ch = hbcomm;

		if (hbcomm->ops->send(hbcomm, &Msg) != CH_SUCCESS) {
			rc = -1;
			rc = EBADF;
		}else if ((err = apphb_getrc()) != 0) {
			errno = err;
			rc = -1;
		}
	}

	/* Destroy and NULL out hbcomm */
	if (hbcomm) {
  		hbcomm->ops->destroy(hbcomm);
		hbcomm = NULL;
	}else{
		errno = ESRCH;
		rc = -1;
	}
	/* Destroy and NULL out hbattrs */
	if (hbattrs) {
		g_hash_table_destroy(hbattrs);
		hbattrs = NULL;
	}
	
	return rc;
}

/* Set application heartbeat interval (in milliseconds) */
int
apphb_setinterval(int hbms)
{
	struct apphb_msmsg	msg;
	struct OCF_IPC_MESSAGE	Msg;
	int			err;

	if (hbcomm == NULL || hbstatus != CH_SUCCESS) {
		errno = ESRCH;
		return -1;
	}
	if (hbms < 0) {
		errno = EINVAL;
		return -1;
	}
	strncpy(msg.msgtype, SETINTERVAL, sizeof(msg.msgtype));
	msg.ms = hbms;
	Msg.msg_body = &msg;
	Msg.msg_len = sizeof(msg);
	Msg.msg_done = NULL;
	Msg.msg_private = NULL;
	Msg.ch = hbcomm;

	if (hbcomm->ops->send(hbcomm, &Msg) != CH_SUCCESS) {
		errno = EBADF;
		return -1;
	}
	if ((err = apphb_getrc()) != 0) {
		errno = err;
		return -1;
	}
	return  0;
}

/* Perform application heartbeat */
int
apphb_hb(void)
{
	struct apphb_msg msg;
	struct OCF_IPC_MESSAGE	Msg;

	if (hbcomm == NULL || hbstatus != CH_SUCCESS) {
		errno = ESRCH;
		return -1;
	}
	strncpy(msg.msgtype, HEARTBEAT, sizeof(msg.msgtype));
	Msg.msg_body = &msg;
	Msg.msg_len = sizeof(msg);
	Msg.msg_done = NULL;
	Msg.msg_private = NULL;
	Msg.ch = hbcomm;

	if (hbcomm->ops->send(hbcomm, &Msg) != CH_SUCCESS) {
		errno = EBADF;
		return -1;
	}
	/* NOTE: we do not expect a return code from server */
	return 0;
}
