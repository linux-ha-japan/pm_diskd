#
# $Id: Makefile,v 1.5 2000/12/12 22:09:34 alan Exp $
#
# Makefile for the Linux-HA /proc interface
#
# Author(s): Volker Wiegand <wiegand@suse.de>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.
#
#
# Please visit the end of this file for a complete history log.
#
#
#	This Makefile includes Red Hat "hacks" to allow multiple
#	module versions (UP, SMP, BOOT) to be built at once.
#
#	Thanks to Cristian Gafton for enlightening me (alanr) on what to do.
#

#       Debian wants things to start with DESTDIR,
#       but Red Hat starts them with RPM_BUILD_ROOT     (sigh...)
#
#       When make is called is shuold be run as
#       BUILD_ROOT=$VAR make ...
#
#       e.g.
#       BUILD_ROOT=$RPM_BUILD_ROOT make install

PKG=heartbeat

OBJDIR = $(BUILD_ROOT)/usr/lib/$(PKG)
INSTALL = install

CC = gcc

#
#	Volker had this thing set on, but I don't seem to have it. YMMV
#
# VFLAGS = -V2.7.2.3
#
#
CFLAGS += -Wall -fomit-frame-pointer -DKERNEL -DMODULE $(OPTFLAGS)

SRC = proc_ha.c

#
#	This is for other than Red Hat >= 6.1 installs...
#
OBJLIST = ha.o


.PHONY = all install clean pristine

all: $(OBJLIST)

ha.o: $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $@

install: all
	$(INSTALL) -cm 644 $(OBJLIST) $(OBJDIR)

install_bin: install
#
#	This is for Red Hat >= 6.1 install...
#

UPFLAGS = -D__BOOT_KERNEL_UP=1
SMPFLAGS = -D__BOOT_KERNEL_SMP=1
BOOTFLAGS = -D__BOOT_KERNEL_BOOT=1
RH_OBJLIST = ha-up.o ha-smp.o ha-boot.o

rh-all: $(RH_OBJLIST)

ha-up.o: $(SRC)
	$(CC) $(CFLAGS) $(UPFLAGS) -c $(SRC) -o $@

ha-smp.o: $(SRC)
	$(CC) $(CFLAGS) $(SMPFLAGS) -c $(SRC) -o $@

ha-boot.o: $(SRC)
	$(CC) $(CFLAGS) $(BOOTFLAGS) -c $(SRC) -o $@

rh-install: $(RH_OBJLIST)
	$(INSTALL) -cm 644 $(RH_OBJLIST) $(OBJDIR)

rh-install_bin: rh-install
clean:
	rm -f *.o core *~

pristine: clean

#####################################################
# $Log: Makefile,v $
# Revision 1.5  2000/12/12 22:09:34  alan
# Makefile changes to correct a spelling error and make it build correctly on IA64.
#
# Revision 1.4  2000/04/15 22:05:27  horms
# Tidied up build process
#
# Revision 1.3  1999/10/05 06:00:58  alanr
# Added RPM Cflags to Makefiles
#
# Revision 1.2  1999/09/30 18:34:27  alanr
# Matt Soffen's FreeBSD changes
#
# Revision 1.1.1.1  1999/09/23 15:31:24  alanr
# High-Availability Linux
#
# Revision 1.4  1999/09/23 14:32:59  alanr
# This version is for new directory structure, plus the ability to do both red hat and non-red-hat installs out of the same makefile...
#
# Revision 1.2  1999/09/18 23:35:14  alanr
# Fixed up the comments.
#
# Revision 1.1  1999/09/18 23:29:41  alanr
# Initial revision
#
# Revision 1.2  1999/09/07 07:32:05  alanr
# Moved BIN directory...
#
# Revision 1.1  1999/07/06 12:17:07  alanr
# Initial revision
#
#####################################################
