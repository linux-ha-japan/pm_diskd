#
# Makefile for ldirectord
#
# Produces all manner of documentation for ldirectord
#
# Author: Jacob Rief <jacob.rief@tis.at>
#
# This file: Horms <horms@vergenet.net>
#

POD2MAN = pod2man
SBIN    = $(BUILD_ROOT)/sbin
MAN     = $(BUILD_ROOT)/usr/man/man8
INSTALL = install
MKDIR   = mkdir

#####################################
# No servicable parts below this line


.PHONY=clean all install

all: ldirectord.8

clean:
	$(RM) -f ldirectord.8

ldirectord.8:	ldirectord
	$(POD2MAN) $< > $@

install: ldirectord.8
	if [ ! -d $(SBIN) ]; then $(MKDIR) -p $(SBIN); fi
	$(INSTALL) -m 0755 -o root -g root ldirectord $(SBIN)
	if [ ! -d $(MAN) ]; then $(MKDIR) -p $(MAN); fi
	$(INSTALL) -m 0644 -o root -g root ldirectord.8 $(MAN)

