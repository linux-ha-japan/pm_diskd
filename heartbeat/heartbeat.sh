#!/bin/sh
#
#	$Id: heartbeat.sh,v 1.5 1999/10/03 03:14:04 alanr Exp $
#
# heartbeat     Start high-availability services
#
# Author:       Alan Robertson	<alanr@henge.com>
#
#		Parts are patterned after some Red Hat examples, but now
#		it should run (except for status) under SuSE also...
#
# chkconfig: - 25 40
# description: Startup script high-availability services.
# processname: heartbeat
# pidfile: /var/run/heartbeat.pid
# config: /etc/ha.d/heartbeat.cf

HA_DIR=/etc/ha.d; export HA_DIR
CONFIG=$HA_DIR/ha.cf
. $HA_DIR/shellfuncs
exec 2>>/var/log/ha-debug

RHFUNCS=/etc/rc.d/init.d/functions
PROC_HA=$HA_BIN/ha.o
PROC_HA_SRC=$HA_DOCDIR/proc_ha.c
SUBSYS=heartbeat
#
#	Places to find killproc or killall in...
#
KILLPROCS="/sbin/killproc /usr/bin/killall"
INSMOD=/sbin/insmod
US=`uname -n`

# Set this to a 1 if you want to automatically load kernel modules
USE_MODULES=0

#
#	Source in Red Hat's function library.
#	Unfortunately, I require this for "status"
#	As a result, "status" only works on the Red Hat distribution
#
if
  [ ! -x $RHFUNCS ]
then
  daemon() {
	$*
  }
else
  . $RHFUNCS
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
    [ ! -c $WATCHDEV -a $insmod = yes ]
  then
    minor=`cat /proc/misc | grep watchdog | cut -c1-4`
    mknod -m 600 $WATCHDEV c $MISCDEV $minor
  fi
} # init_watchdog()

#
#	Install /proc/ha module, if it's not already installed
#
#	We try compiling it from source if our .o laying
#	around won't install correctly.  It's kinda lame, but it's
#	almost certainly better than nothing...
#
#	Of course, this won't work if you don't have a C compiler,
#	or you don't have headers that match your kernel, or other
#	things.  But, we only try it if for some reason we can't
#	install the object we have laying around, so it should be
#	harmless at worst.
#
#	This is a nasty problem.  This is the best we can do for now.
#	Sorry.
#
#

init_proc_ha() {
  if
    [ ! -d /proc/ha ]
  then
    if
      $INSMOD $PROC_HA
    then
     : $PROC_HA Module loaded OK
    else
     :  OOPS!  Maybe a kernel version problem?
      cc -Wall -fomit-frame-pointer -c $PROC_HA_SRC -o $PROC_HA
      $INSMOD $PROC_HA
    fi
  fi
}


#
#	Start the heartbeat daemon...
#

start_heartbeat() {
  if
    daemon $HA_BIN/heartbeat # -d
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
  ha_log "INFO: $SUBSYS starting."
  if
    [ $USE_MODULES = 1 ]
  then
    #	Create /dev/watchdog and load module if we should
    init_watchdog
    #	Load /proc/ha module
    #	NOTE: Current version of proc_ha module kills heartbeat processes!
    # init_proc_ha
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
    #	Wait for things to settle a bit
    ha_log "INFO: $SUBSYS initialization complete."
  else
    RC=$?
    echo "Heartbeat did not start [rc=$RC]"
    return $RC
  fi
}

#
#	Give up all our precious HA-protected IP addresses to the rest
#	of the cluster.  Hope they know what to do with them!
#	Then we kill heartbeat.
#
StopHA() {

  MGR=$HA_BIN/ResourceManager
  ha_log "$SUBSYS: shutdown in progress."

#
#	Extract all HA-protected key resources from the resources file
#

  $MGR listkeys '.*' |	#	This will give us all key resources
  while
    read resourcekey
  do
    ha_debug "Examining resource group $resourcekey"
    case `$MGR status $resourcekey` in
      *[Rr]unning*)	;;
      *)			ha_debug "Resource group $resourcekey stopped."
    			continue;;
    esac

    ha_debug "$MGR givegroup $resourcekey"
    $MGR givegroup $resourcekey
  done
  KILLPROC=""

  #	Use the Red Hat killproc function if it's there...

  if
    [ -x $RHFUNCS ]
  then
    killproc $SUBSYS
  else
    for j in $KILLPROCS
    do
      if
        [ -x $j ]
      then
        KILLPROC=$j;
        break;
      fi
    done
    if
      [ "X$KILLPROC" != X ]
    then
      $KILLPROC $HA_BIN/heartbeat
    else
      echo "No killproc/killall"
      exit 1
    fi
  fi
  lrc=$?
  ha_log "$SUBSYS: shutdown complete."
  return $lrc
}


RC=0
# See how we were called.

case "$1" in
  start)
	echo -n "Starting High-Availability services: "
	StartHA
	RC=$?
	echo
	[ $RC -eq 0 ] && touch /var/lock/subsys/$SUBSYS
	;;

  stop)
	echo -n "Stopping High-Availability services: "
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
	StopHA
	StartHA
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
