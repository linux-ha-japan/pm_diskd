#
# Makefile for ldirectord
# $Id: Makefile,v 1.16 2001/01/16 21:17:46 alan Exp $
#
# Produces all manner of documentation for ldirectord
#
# Author: Jacob Rief <jacob.rief@tiscover.com>
#
# This file: Horms <horms@vergenet.net>
#

POD2MAN		= pod2man
SBIN		= $(BUILD_ROOT)/usr/sbin
MANDIR		= $(BUILD_ROOT)/usr/man/man8
LOGROTATED	= etc/logrotate.d
LOGROTATE	= $(BUILD_ROOT)/$(LOGROTATED)
INITD		= etc/init.d
INIT		= $(BUILD_ROOT)/$(INITD)
CONF            = $(BUILD_ROOT)/etc/ha.d/conf
INSTALL		= install
MKDIR		= mkdir

#####################################
# No servicable parts below this line


.PHONY=clean all install pristine

all: ldirectord.8

pristine: clean
clean:
	$(RM) -f ldirectord.8

ldirectord.8:	ldirectord
	$(POD2MAN) $< > $@

install: ldirectord.8
	[ -d $(CONF) ] ||  $(MKDIR) -p $(CONF)
	[ -d $(SBIN) ] ||  $(MKDIR) -p $(SBIN)
	$(INSTALL) -m 0755 ldirectord $(SBIN)
	[ -d $(MANDIR) ] ||  $(MKDIR) -p $(MANDIR)
	$(INSTALL) -m 0644 ldirectord.8 $(MANDIR)/man8
	[ -d $(LOGROTATE) ] ||  $(MKDIR) -p $(LOGROTATE) 
	$(INSTALL) -m 0644  ldirectord.logrotate $(LOGROTATE)/ldirectord
	[  -d $(INIT) ]     ||  $(MKDIR) -p $(INIT)	
	$(INSTALL) -m 0755  ldirectord.sh $(INIT)/ldirectord
	
# Override implicit rule so ldirectord won't get clobered by ldirectord.sh
ldirectord:
	

