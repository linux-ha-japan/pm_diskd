#!/bin/sh
#
# ldirectord  Linux Director Daemon
#
# chkconfig: 2345 34 40
# description: Start and stop ldirectord on non-heartbeat systems
#              Using the config file /etc/ha.d/conf/ldirectord.cf
#              
# processname: ldirectrod
# config: /etc/ha.d/conf/ldirectord.cf
#
# Author: Horms <horms@valinux.com>
# Released: April 2000
# Licence: GNU General Public Licence
#

# Source function library.
. /etc/rc.d/init.d/functions

[ -f /etc/ha.d/conf/ldirectord.cf ] || exit 0


######################################################################
# Read arument and take action as appropriate
######################################################################

case "$1" in
  start)
        action "Starting ldirectord" /sbin/ldirectord  ldirectord.cf start
	;;
  stop)
        action "Stoping ldirectord" /sbin/ldirectord  ldirectord.cf stop
	;;
  restart)
	$0 stop
	$0 start
	;;
  status)
	/sbin/ldirectord  ldirectord.cf status
	;;
  *)
	echo "Usage: ipv4_conf {start|stop|restart|status}"
	exit 1
esac

exit 0
