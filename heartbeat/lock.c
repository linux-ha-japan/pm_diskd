#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <lock.h>

#ifndef TTY_LOCK_D
#	define TTY_LOCK_D	"/var/lock"
#endif

/*
 * The following information is from the Filesystem Hierarchy Standard
 * version 2.1 dated 12 April, 2000.
 *
 * 5.6 /var/lock : Lock files
 * Lock files should be stored within the /var/lock directory structure.
 * Device lock files, such as the serial device lock files that were originally
 * found in either /usr/spool/locks or /usr/spool/uucp, must now be stored in
 * /var/lock. The naming convention which must be used is LCK.. followed by
 * the base name of the device file. For example, to lock /dev/cua0 the file
 * LCK..cua0 would be created.
 * 
 * The format used for device lock files must be the HDB UUCP lock file format.
 * The HDB format is to store the process identifier (PID) as a ten byte
 * ASCII decimal number, with a trailing newline. For example, if process 1230
 * holds a lock file, it would contain the eleven characters: space, space,
 * space, space, space, space, one, two, three, zero, and newline.
 * Then, anything wishing to use /dev/cua0 can read the lock file and act
 * accordingly (all locks in /var/lock should be world-readable).
 */

#define	DEVDIR	"/dev/"
#define	DEVLEN	(sizeof(DEVDIR)-1)

/* The code in this file originally written by Guenther Thomsen */
/* Somewhat mangled by Alan Robertson */

/*
 * Lock a tty (using lock files, see linux `man 2 open` close to O_EXCL) 
 * serial_device has to be _the complete path_, i.e. including '/dev/' to the
 * special file, which denotes the tty to lock -tho
 * return 0 on success, 
 * -1 if device is locked (lockfile exists and isn't stale),
 * -2 for temporarily failure, try again,
 * other negative value, if something unexpected happend (failure anyway)
 */


int
ttylock(const char *serial_device)
{
	return(DoLock("LCK..", serial_device+DEVLEN));
}

/*
 * Unlock a tty (remove its lockfile) 
 * do we need to check, if its (still) ours? No, IMHO, if someone else
 * locked our line, it's his fault  -tho
 * returns 0 on success
 * <0 if some failure occured
 */ 

int
ttyunlock(const char *serial_device)
{
	return(DoUnlock("LCK..", serial_device+DEVLEN));
}

/* This is what the FHS standard specifies for the size of our lock file */
#define	LOCKSTRLEN	11

int
DoLock(const char * prefix, const char *lockname)
{
	char lf_name[256], tf_name[256], buf[LOCKSTRLEN+1];
	int fd;
	pid_t pid, mypid;
	int rc;
	struct stat sbuf;

	mypid = getpid();

	snprintf(lf_name, sizeof(lf_name), "%s/%s%s"
	,	TTY_LOCK_D, prefix, lockname);

	snprintf(tf_name, sizeof(tf_name), "%s/tmp%d-%s"
	,	TTY_LOCK_D, mypid, lockname);

	if ((fd = open(lf_name, O_RDONLY)) >= 0) {
		if (fstat(fd, &sbuf) < 0 || sbuf.st_size < 1) {
			sleep(1); /* if someone was about to create one,
			   	   * give'm a sec to do so */
		}
		if (read(fd, buf, sizeof(buf)) < 1) {
			/* lockfile empty -> rm it and go on */;
		} else {
			if (sscanf(buf, "%d", &pid) < 1) {
				/* lockfile screwed up -> rm it and go on */
			} else {
				if (kill(pid, 0) < 0 && errno != ESRCH) {
					/* tty is locked by existing (not
					 * necessarily running) process
					 * -> give up */
					close(fd);
					return -1;
				} else {
					/* stale lockfile -> rm it and go on */
				}
			}
		}
		unlink(lf_name);
	}
	if ((fd = open(tf_name, O_CREAT | O_WRONLY, 0644)) < 0) {
		/* Hmmh, why did we fail? Anyway, nothing we can do about it */
		return -3;
	}
	snprintf(buf, sizeof(buf), "%*d\n", LOCKSTRLEN-1, mypid);

	if (write(fd, buf, LOCKSTRLEN) != LOCKSTRLEN) {
		/* Again, nothing we can do about this */
		return -3;
	}
	close(fd);

	switch (link(tf_name, lf_name)) {
	case 0:
		if (stat(tf_name, &sbuf) < 0) {
			/* something weird happened */
			rc = -3;
			break;
		}
		if (sbuf.st_nlink < 2) {
			/* somehow, it didn't get through - NFS trouble? */
			rc = -2;
			break;
		}
		rc = 0;
		break;
	case EEXIST:
		rc = -1;
		break;
	default:
		rc = -3;
	}
	unlink(tf_name);
	return rc;
}

int
DoUnlock(const char * prefix, const char *lockname)
{
	char lf_name[256];
	
	snprintf(lf_name, sizeof(lf_name), "%s/%s%s", TTY_LOCK_D
	,	prefix, lockname);
	return unlink(lf_name);
}
