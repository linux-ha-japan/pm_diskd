#!/bin/sh
#
#	$Id: heartbeat.sh,v 1.27 2000/08/01 12:25:05 alan Exp $
#
# heartbeat     Start high-availability services
#
# Author:       Alan Robertson	<alanr@suse.com>
#
#		This script works correctly under SuSE, Debian,
#		Conectiva and a few others.  Please let me know if it
#		doesn't work under your distribution, and we'll fix it.
#		We don't hate anyone, and like for everyone to use
#		our software, no matter what distribution you're using.
#
# chkconfig: 2345 34 40
# description: Startup script high-availability services.
# processname: heartbeat
# pidfile: /var/run/heartbeat.pid
# config: /etc/ha.d/ha.cf
  

HA_DIR=/etc/ha.d; export HA_DIR
CONFIG=$HA_DIR/ha.cf
. $HA_DIR/shellfuncs

# exec 2>>/var/log/ha-debug

DISTFUNCS=/etc/rc.d/init.d/functions
PROC_HA=$HA_BIN/ha.o
SUBSYS=heartbeat
INSMOD=/sbin/insmod
US=`uname -n`

# Set this to a 1 if you want to automatically load kernel modules
USE_MODULES=1

#
#	Some non-SUSE distributions like it if we use their functions...
#
if
  [ ! -x $DISTFUNCS ]
then
  # Provide our own versions of these functions
  status() {
	$HA_BIN/heartbeat -s
  }
  echo_failure() {
      echo " Heartbeat failure [rc=$1]"
      return $1
  }
  echo_success() {
	: Cool!  It started!
  }
else
  . $DISTFUNCS
fi

#
#	See if they've configured things yet...
#
if
  [ ! -f /etc/ha.d/ha.cf ]
then
  echo -n "Heartbeat not configured."
  echo_failure 1
  exit 0
fi


#
#	Install the softdog module if we need to
#
init_watchdog() {
#
# 	We need to install it if watchdog is specified in $CONFIG, and
#	/dev/watchdog refers to a softdog device, or it /dev/watchdog
#	doesn't exist at all.
#
#	If we need /dev/watchdog, then we'll make it if necessary.
#
#	Whatever the user says we should use for watchdog device, that's
#	what we'll check for, use and create if necessary.  If they misspell
#	it, or don't put it under /dev, so will we.
#	Hope they do it right :-)
#
#
  insmod=no
  # What do they think /dev/watchdog is named?
  MISCDEV=`grep ' misc$' /proc/devices | cut -c1-4`
  MISCDEV=`echo $MISCDEV`
  WATCHDEV=`grep -v '^#' $CONFIG | grep watchdog |
	sed s'%^[ 	]*watchdog[ 	]*%%'`
  WATCHDEV=`echo $WATCHDEV`
  if
    [ "X$WATCHDEV" != X ]
  then
    : Watchdog requested by $CONFIG file
  #
  #	We try and insmod the module if there's no dev or the dev exists
  #	and points to the softdog major device.
  #
    if
      [ ! -c "$WATCHDEV" ]
    then
      insmod=yes
    else
      case `ls -l "$WATCHDEV" 2>/dev/null` in
      *$MISCDEV,*)
	    insmod=yes;;
      *)	: "$WATCHDEV isn't a softdog device (wrong major)" ;;
      esac
    fi
  else
    : No watchdog device specified in $CONFIG file.
  fi
  case $insmod in
    yes)
      if
        grep softdog /proc/modules >/dev/null 2>&1 
      then
        : softdog already loaded
      else
        $INSMOD softdog >/dev/null 2>&1
      fi;;
  esac
  if
    [ "X$WATCHDEV" != X -a ! -c "$WATCHDEV" -a $insmod = yes ]
  then
    minor=`cat /proc/misc | grep watchdog | cut -c1-4`
    mknod -m 600 $WATCHDEV c $MISCDEV $minor
  fi
} # init_watchdog()


#
#	Start the heartbeat daemon...
#

start_heartbeat() {
  if
    ERROR="$($HA_BIN/heartbeat 2>&1)"
  then
    : OK
  else
    return $?
  fi
}


#
#	Start Linux-HA
#

StartHA() {
  echo -n "Starting High-Availability services: "
  if
    [ $USE_MODULES = 1 ]
  then
    #	Create /dev/watchdog and load module if we should
    init_watchdog
  fi
  rm -f /var/run/ppp.d/*
  if
    [  -f $HA_DIR/ipresources -a ! -f $HA_DIR/haresources ]
  then
    mv $HA_DIR/ipresources $HA_DIR/haresources
  fi
  #	Start heartbeat daemon
  if
    start_heartbeat
  then
    echo_success
    return 0 
  else
    RC=$?
    echo_failure $RC
    if [ ! -z "$ERROR" ]; then
      echo
      echo "$ERROR"
    fi 
    return $RC
  fi
}

#
#	Ask heartbeat to stop.  It will give up its resources...
#
StopHA() {
  echo -n "Stopping High-Availability services: "

  if
    $HA_BIN/heartbeat -k &> /dev/null	# Kill it
  then
    echo_success
    return 0
  else
    RC=$?
    echo_failure $RC
    return $RC
  fi
}

#
#	Ask heartbeat to restart.  It will *keep* its resources
#
RestartHA() {
  echo -n "Restarting High-Availability services: "

  if
    $HA_BIN/heartbeat -r # Restart, and keep your resources
  then
    echo_success
    return 0
  else
    RC=$?
    echo_failure $RC
    return $RC
  fi
}

RC=0
# See how we were called.

case "$1" in
  start)
	StartHA
	RC=$?
	echo
	[ $RC -eq 0 ] && touch /var/lock/subsys/$SUBSYS
	;;

  stop)
	StopHA
	RC=$?
	echo
	[ $RC -eq 0 ] && rm -f /var/lock/subsys/$SUBSYS
	;;

  status)
	status heartbeat
	RC=$?
	;;

  restart|reload)
	RestartHA
	RC=$?
	;;

  *)
	echo "Usage: ha {start|stop|status|restart|reload}"
	exit 1
esac

exit $RC
#
#
#  $Log: heartbeat.sh,v $
#  Revision 1.27  2000/08/01 12:25:05  alan
#  More political changes to the comments ;-)
#
#  Revision 1.26  2000/08/01 12:21:55  alan
#  I modified some comments to make it less obvious tht we are specifically
#  Red-Hat compatible.
#
#  Revision 1.25  2000/06/21 04:34:48  alan
#  Changed henge.com => linux-ha.org and alanr@henge.com => alanr@suse.com
#
#  Revision 1.24  2000/06/12 22:07:59  alan
#  Spelling correction in a comment.
#
#  Revision 1.23  2000/06/12 22:06:30  alan
#  Finished updating the code for restart.
#
#  Revision 1.22  2000/06/12 22:03:11  alan
#  Put in a fix to the link status code, to undo something I'd broken, and also to simplify it.
#  I changed heartbeat.sh so that it uses the -r flag to restart heartbeat instead
#  of stopping and starting it.
#
#  Revision 1.21  2000/06/12 06:11:09  alan
#  Changed resource takeover order to left-to-right
#  Added new version of nice_failback.  Hopefully it works wonderfully!
#  Regularized some error messages
#  Print the version of heartbeat when starting
#  Hosts now have three statuses {down, up, active}
#  SuSE compatability due to Friedrich Lobenstock and alanr
#  Other minor tweaks, too numerous to mention.
#
#  Revision 1.20  2000/04/27 12:50:20  alan
#  Changed the port number to 694.  Added the pristene target to the ldirectord
#  Makefile.  Minor tweaks to heartbeat.sh, so that it gives some kind of
#  message if there is no configuration file yet.
#
#  Revision 1.19  2000/04/24 07:08:13  horms
#  Added init script to ldirectord, fixed hearbeat.sh to work with RH6.2 again, heartbeat.sh now aborts if /etc/ha.d/ha.cf is not present. Added sample ldirectord.cf. Moved logging directives to the top of the sample ha.cf. Incremented version in master Makefile to 0.4.7apre2. KERNELDIRS now don't get any treatment in the master makefile, this is to fix a bug (introduced by me) with using an emty  in a for i in  under some shells
#
#  Revision 1.18  2000/04/24 06:34:45  horms
#  Made init work cleanly with RH 6.2 again
#
#  Revision 1.17  2000/04/23 13:16:17  alan
#  Changed the code in heartbeat.sh to no longer user RH's daemon or
#  killproc functions.
#
#  Revision 1.16  2000/04/03 08:26:29  horms
#
#
#  Tidied up the output from heartbeat.sh (/etc/rc.d/init.d/heartbeat)
#  on Redhat 6.2
#
#  Loging to syslog if a facility is specified in ha.cf is instead of
#  rather than as well as file logging as per instructions in ha.cf
#
#  Fixed a small bug in shellfunctions that caused logs to syslog
#  to be garbled.
#
#  Revision 1.15  1999/11/11 06:02:43  alan
#  Minor change to make heartbeat default enabled on startup.
#
#  Revision 1.14  1999/11/11 05:48:52  alan
#  Added code to start up heartbeat automatically.
#
#  Revision 1.13  1999/10/19 13:55:36  alan
#  Changed comments about being red hat compatible
#  Also, changed heartbeat.c to be both SuSE and Red Hat compatible in it's -s
#  output
#
#  Revision 1.12  1999/10/19 01:56:51  alan
#  Removed the sleep between shutdown and startup, since that's now in
#  heartbeat itself.
#
#  Revision 1.11  1999/10/19 01:49:10  alan
#  Put in a sleep between stop and start in restart to make it more reliable.
#
#  Revision 1.10  1999/10/10 19:45:21  alanr
#  Changed comment
#
#  Revision 1.9  1999/10/05 05:17:49  alanr
#  Added -s (status) option to heartbeat, and used it in heartbeat.sh...
#
#  Revision 1.8  1999/10/05 04:35:26  alanr
#  Changed it to use the new heartbeat -k option to shut donw heartbeat.
#
#  Revision 1.7  1999/10/04 03:12:39  alanr
#  Shutdown code now runs from heartbeat.
#  Logging should be in pretty good shape now, too.
#
#  Revision 1.6  1999/10/04 01:47:22  alanr
#  Fix the problem reported by Thomas Hepper with the code for loading the watchdog
#  device correctly.
#
#  Revision 1.5  1999/10/03 03:14:04  alanr
#  Moved resource acquisition to 'heartbeat', also no longer attempt to make the FIFO, it's now done in heartbeat.  It should now be possible to start it up more readily...
#
#  Revision 1.4  1999/10/02 17:48:08  alanr
#  Put back call to init_fifo.  Thanks to Thomas Hepper
#
#  Revision 1.3  1999/10/02 04:59:22  alanr
#  FreeBSD mkfifo cleanup
#
#  Revision 1.2  1999/09/23 15:53:13  alanr
#
#  First version to work :-)
#  Got this first version to work...
#
#  Revision 1.1.1.1  1999/09/23 15:31:24  alanr
#  High-Availability Linux
#
#  Revision 1.12  1999/09/14 23:07:09  alanr
#  another comment change...
#
#  Revision 1.11  1999/09/14 23:05:13  alanr
#  comment change...
#
#  Revision 1.10  1999/09/14 22:32:50  alanr
#  Put in Thomas Hepper's fix for killproc.
#  Lots of other changes I think...
#
#  Revision 1.9  1999/09/07 04:46:34  alanr
#  made it exit with proper return codes.
#  Also, moved things around according to the FHS...
#
#  Revision 1.8  1999/08/22 04:10:37  alanr
#  changed the name of this file to heartbeat.sh.
#  Also moved the change log to the end of the file...
#
#  Revision 1.7  1999/08/22 04:03:13  alanr
#  Merged this file with the heartbeat script as suggested by Guenther Thomsen
#
#  Revision 1.6  1999/08/21 21:54:12  alanr
#  Restructured the code in preparation for combining this script with the
#  init script under /etc/rc.d/init.d.
#
#  Revision 1.5  1999/08/17 04:34:53  alanr
#  added code to create /dev/watchdog and load softdog if necessary...
#
#
