#	$Id: Makefile,v 1.5 2000/06/12 06:11:09 alan Exp $
#
#	Makefile for making High-Availability Linux documents
#
#
PKG=heartbeat
VERS=test!

#       Debian wants things to start with DESTDIR,
#       but Red Hat starts them with RPM_BUILD_ROOT     (sigh...)
#
#       When make is called is shuold be run as
#       BUILD_ROOT=$VAR make ...
#
#       e.g.
#       BUILD_ROOT=$RPM_BUILD_ROOT make install

DOCDIR=$(BUILD_ROOT)/usr/doc/$(PKG)

HTML2TXT = lynx -dump
INSTALL = install

MADEDOCS =			\
	ChangeLog		\
	HardwareGuide		\
	GettingStarted		\
	Requirements

DOCFILES =			\
	$(MADEDOCS)		\
	AUTHORS			\
	COPYING			\
	ChangeLog		\
	HardwareGuide.html	\
	GettingStarted.html	\
	Requirements.html	\
	README			\
	authkeys		\
	ha.cf			\
	haresources

SPECSRC = ../Specfile

.PHONY = all install clean pristene

all: $(DOCFILES)

ChangeLog:	$(SPECSRC)
	rm -fr ChangeLog
	sed -e '1,/^%changelog/d' -e '/^%/,$$d' <$(SPECSRC) >ChangeLog
	chmod 444 ChangeLog

HardwareGuide: HardwareGuide.html
	$(HTML2TXT) HardwareGuide.html >HardwareGuide

GettingStarted: GettingStarted.html
	$(HTML2TXT) GettingStarted.html >GettingStarted

Requirements: Requirements.html
	$(HTML2TXT) Requirements.html >Requirements


install: all $(DOCFILES)
	$(INSTALL) -cm 644 $(DOCFILES) $(DOCDIR)
	$(INSTALL) -cm 644 ../proc-ha/proc_ha.c $(DOCDIR)

rh-install: install

install_bin: all
rh-install_bin: install_bin

clean:
	rm -f core *.o *.swp .*.swp make.out 

pristene: clean
	rm -f $(MADEDOCS)
