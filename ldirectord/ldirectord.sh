#!/bin/sh
#
# ldirectord  Linux Director Daemon
#
# chkconfig: 2345 92 40
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
if
  [ -f /etc/rc.d/init.d/functions ]
then
  . /etc/rc.d/init.d/functions
fi


######################################################################
# Read arument and take action as appropriate
######################################################################

case "$1" in
  start)
        action "Starting ldirectord" /usr/sbin/ldirectord start
	;;
  stop)
        action "Stopping ldirectord" /usr/sbin/ldirectord stop
	;;
  restart)
	$0 stop
	$0 start
	;;
  status)
	/usr/sbin/ldirectord status
	;;
  *)
	echo "Usage: ipv4_conf {start|stop|restart|status}"
	exit 1
esac

exit 0
