#
# Makefile for ldirectord
#
# Produces all manner of documentation for ldirectord
#
# Author: Jacob Rief <jacob.rief@tis.at>
#
# This file: Horms <horms@vergenet.net>
#

POD2MAN		= pod2man
SBIN		= $(BUILD_ROOT)/sbin
MAN		= $(BUILD_ROOT)/usr/man/man8
LOGROTATED	= /etc/logrotate.d
LOGROTATE	= $(BUILD_ROOT)/$(LOGROTATED)
INITD		=\
$(shell [ -d /etc/init.d ] && echo /etc/init.d || echo /etc/rc.d/init.d )
INIT		= $(BUILD_ROOT)/$(INITD)
CONF            = $(BUILD_ROOT)/etc/ha.d/conf
INSTALL		= install
MKDIR		= mkdir

#####################################
# No servicable parts below this line


.PHONY=clean all install

all: ldirectord.8

clean:
	$(RM) -f ldirectord.8

ldirectord.8:	ldirectord
	$(POD2MAN) $< > $@

install: ldirectord.8
	[ -d $(CONF) ] ||  $(MKDIR) -p $(CONF)
	[ -d $(SBIN) ] ||  $(MKDIR) -p $(SBIN)
	$(INSTALL) -m 0755 ldirectord $(SBIN)
	[ -d $(MAN) ] ||  $(MKDIR) -p $(MAN)
	$(INSTALL) -m 0644 ldirectord.8 $(MAN)
	if [ -d $(LOGROTATED) ]; then 					 \
		[ -d $(LOGROTATE) ] ||  $(MKDIR) -p $(LOGROTATE) 	;\
		$(INSTALL) -m 0644 					\
			ldirectord.logrotate $(LOGROTATE)/ldirectord	;\
	fi
	if [ -d $(INITD) ]; then 					 \
		[ -d $(INIT) ] ||  $(MKDIR) -p $(INIT) 			;\
		$(INSTALL) -m 0755 					\
			ldirectord.sh $(INIT)/ldirectord		;\
	fi
	
# Override implicit rule so ldirectord won't get clobered by ldirectord.sh
ldirectord:
	

