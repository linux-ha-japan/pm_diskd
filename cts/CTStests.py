#!/usr/bin/env python

'''CTS: Cluster Testing System: Tests module

There are a few things we want to do here:

 '''

__copyright__='''
Copyright (C) 2000, 2001 Alan Robertson <alanr@unix.sh>
Licensed under the GNU GPL.
'''

#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

import CTS
import time


#	List of all class objects for tests which we ought to
#	consider running.

AllTestClasses = [ ]

class CTSTest:
    '''
    A Cluster test.
    We implement the basic set of properties and behaviors for a generic
    cluster test.

    Cluster tests track their own statistics.
    We keep each of the kinds of counts we track as separate {name,value}
    pairs.
    '''

    def __init__(self, cm):
        #self.name="the unnamed test"
        self.Stats = {"calls":0, "success":0, "failure":0, "skipped":0}
#        if not issubclass(cm.__class__, ClusterManager):
#            raise ValueError("Must be a ClusterManager object")
        self.CM = cm
        self.timeout=30

    def incr(self, name):
        '''Increment (or initialize) the value associated with the given name'''
        if not self.Stats.has_key(name):
            self.Stats[name]=0
        self.Stats[name] = self.Stats[name]+1

    def failure(self, reason="none"):
        '''Increment the failure count'''
        self.incr("failure")
        self.CM.log("Test " + self.name + " failed [reason:" + reason + "]")
        return None

    def success(self):
        '''Increment the success count'''
        self.incr("success")
        return 1

    def skipped(self):
        '''Increment the skipped count'''
        self.incr("skipped")
        return 1

    def __call__(self):
        '''Perform the given test'''
        raise ValueError("Abstract Class member (__call__)")
        self.incr("calls")
        return self.failure()

    def is_applicable(self):
        '''Return TRUE if we are applicable in the current test configuration'''
        raise ValueError("Abstract Class member (is_applicable)")
        return 1

    def canrunnow(self):
        '''Return TRUE if we can meaningfully run right now'''
        return 1

###################################################################
class StopTest(CTSTest):
###################################################################
    '''Stop (deactivate) the cluster manager on a node'''
    def __init__(self, cm):
        CTSTest.__init__(self, cm)
        self.name="stop"
        self.uspat   = self.CM["Pat:We_stopped"]
        self.thempat = self.CM["Pat:They_stopped"]
        self.allpat = self.CM["Pat:All_stopped"]

    def __call__(self, node):
        '''Perform the 'stop' test. '''
        self.incr("calls")
        if self.CM.ShouldBeStatus[node] != self.CM["up"]:
            return self.skipped()


        if node == self.CM.OurNode:
            self.incr("us")
            pat = self.uspat
        else:
            if self.CM.upcount() <= 1:
                self.incr("all")
                pat = (self.allpat % node)
            else:
                self.incr("them")
                pat = (self.thempat % node)

        watch = CTS.LogWatcher(self.CM["LogFileName"], [pat]
	,	timeout=self.timeout)
        watch.setwatch()
        self.CM.StopaCM(node)
        if watch.look():
            return self.success()
        else:
            return self.failure("no match against %s "% pat)
#
# We don't register StopTest because it's better when called by
# another test...
#

###################################################################
class StartTest(CTSTest):
###################################################################
    '''Start (activate) the cluster manager on a node'''
    def __init__(self, cm):
        CTSTest.__init__(self,cm)
        self.name="start"
        self.uspat   = self.CM["Pat:We_started"]
        self.thempat = self.CM["Pat:They_started"]

    def __call__(self, node):
        '''Perform the 'start' test. '''
        self.incr("calls")

        if self.CM.ShouldBeStatus[node] != self.CM["down"]:
            return self.skipped()

        if node == self.CM.OurNode or self.CM.upcount() < 1:
            self.incr("us")
            pat = self.uspat
        else:
            self.incr("them")
            pat = (self.thempat % node)

        watch = CTS.LogWatcher(self.CM["LogFileName"], [pat]
	,	timeout=self.timeout)
        watch.setwatch()

        self.CM.StartaCM(node)

        if watch.look():
            return self.success()
        else:
            return self.failure("did not find pattern " + pat)
#
# We don't register StartTest because it's better when called by
# another test...
#

###################################################################
class FlipTest(CTSTest):
###################################################################
    '''If it's running, stop it.  If it's stopped start it.
       Overthrow the status quo...
    '''
    def __init__(self, cm):
        CTSTest.__init__(self,cm)
        self.name="flip"
        self.start = StartTest(cm)
        self.stop = StopTest(cm)

    def __call__(self, node):
        '''Perform the 'flip' test. '''
        self.incr("calls")
        if self.CM.ShouldBeStatus[node] == self.CM["up"]:
            self.incr("stopped")
            ret = self.stop(node)
            type="up->down"
            # Give the cluster time to recognize it's gone...
            time.sleep(self.CM["DeadTime"]+1)
        elif self.CM.ShouldBeStatus[node] == self.CM["down"]:
            self.incr("started")
            ret = self.start(node)
            type="down->up"
        else:
            return self.skipped()

        self.incr(type)
        if ret:
            return self.success()
        else:
            return self.failure("%s failure" % type)

#	Register FlipTest as a good test to run
AllTestClasses.append(FlipTest)

###################################################################
class RestartTest(CTSTest):
###################################################################
    '''Stop and restart a node'''
    def __init__(self, cm):
        CTSTest.__init__(self,cm)
        self.name="Restart"
        self.start = StartTest(cm)
        self.stop = StopTest(cm)

    def __call__(self):
        '''Perform the 'restart' test. '''
        self.incr("calls")

        node = self.CM.Env.RandomNode()
        self.incr("node:" + node)

        if self.CM.ShouldBeStatus[node] == self.CM["down"]:
            self.incr("WasStopped")
            self.start(node)

        ret1 = self.stop(node)
        # Give the cluster time to recognize we're gone...
        time.sleep(self.CM["DeadTime"]+1)
        ret2 = self.start(node)

        if not ret1:
            return self.failure("stop failure")
        if not ret2:
            return self.failure("start failure")
        return self.success()

#	Register RestartTest as a good test to run
AllTestClasses.append(RestartTest)

###################################################################
class StonithTest(CTSTest):
###################################################################
    '''Reboot a node by whacking it with stonith.'''
    def __init__(self, cm, timeout=300):
        CTSTest.__init__(self,cm)
        self.name="Stonith"
        self.theystopped  = self.CM["Pat:They_stopped"]
        self.allstopped  = self.CM["Pat:All_stopped"]
        self.usstart   = self.CM["Pat:We_started"]
        self.themstart = self.CM["Pat:They_started"]
        self.timeout = timeout

    def __call__(self, node):
        '''Perform the 'stonith' test. (whack the node)'''
        self.incr("calls")
        stopwatch = None


	#	Figure out what log message to look for when/if it goes down

        if self.CM.ShouldBeStatus[node] != self.CM["down"]:
            if self.CM.upcount() != 1:
                stopwatch = (self.theystopped % node)

	#	Figure out what log message to look for when it comes up

        if (self.CM.upcount() <= 1):
            uppat = self.usstart
        else:
            uppat = (self.themstart % node)

        upwatch = CTS.LogWatcher(self.CM["LogFileName"], [uppat]
       ,	timeout=self.timeout)

        if stopwatch:
            watch = CTS.LogWatcher(self.CM["LogFileName"], [stopwatch]
            ,	timeout=self.CM["DeadTime"]+1)
            watch.setwatch()

	#	Reset (stonith) the node

        if not self.CM.Env.ResetNode(node):
            return self.failure("Stonith failure")

        upwatch.setwatch()

	#	Look() and see if the machine went down

        if stopwatch:
            if watch.look():
                ret1=1
            else:
                reason="Did not find " + stopwatch
                ret1=0
        else:
            ret1=1

	#	Look() and see if the machine came back up

        if upwatch.look():
            ret2=1
        else:
            reason="Did not find " + uppat
            ret2=0

        self.CM.ShouldBeStatus[node] = self.CM["up"]

	# I can't remember why I put this in here :-(

        time.sleep(10)

        if ret1 and ret2:
            return self.success()
        else:
            return self.failure(reason)

#	Register StonithTest as a good test to run
AllTestClasses.append(StonithTest)


###################################################################
class IPaddrtest(CTSTest):
###################################################################
    '''Find the machine supporting a particular IP address, and knock it down.

    [Hint:  This code isn't finished yet...]
    '''

    def __init__(self, cm, IPaddrs):
        CTSTest.__init__(self,cm)
        self.name="IPaddrtest"
        self.IPaddrs = IPaddrs

        self.start = StartTest(cm)
        self.stop = StopTest(cm)

    def __call__(self, IPaddr):
        '''
        Perform the IPaddr test...
        '''
        self.incr("calls")

        node = self.CM.Env.RandomNode()
        self.incr("node:" + node)

        if self.CM.ShouldBeStatus[node] == self.CM["down"]:
            self.incr("WasStopped")
            self.start(node)

        ret1 = self.stop(node)
        # Give the cluster time to recognize we're gone...
        time.sleep(self.CM["DeadTime"]+1)
        ret2 = self.start(node)


        if not ret1:
            return self.failure("Could not stop")
        if not ret2:
            return self.failure("Could not start")

        return self.success()


def TestList(cm):
    result = []
    for testclass in AllTestClasses:
        result.append(testclass(cm))
    return result
