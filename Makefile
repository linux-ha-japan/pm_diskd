#	$Id: Makefile,v 1.17 1999/11/09 06:14:26 alan Exp $
#
#	Makefile for making High-Availability Linux heartbeat code
#
#	Package Name, Version and RPM release level
#
#	If you're installing this package without going through an RPM,
#	you'll need to read the README to see how to make PPP work for you.
#
#
PKG=heartbeat
VERS=0.4.5c
RPMREL=1

INITD=$(shell [ -d /etc/init.d ] && echo etc/init.d || echo etc/rc.d/init.d )

#	This defaults DESTDIR to "/"
#	Debian wants things to start with DESTDIR,
#	but Red Hat starts them with RPM_BUILD_ROOT	(sigh...)
#
#	I'm not sure whether to have them both (like I do now), or to
#	tell one of them to patch the DESTDIR/RPM_BUILD_ROOT to the other one...
#	If I understood DESTDIR's intent better, perhaps I'd do this right :-)
#
DESTDIR=
BRROOTDIR=$(RPM_BUILD_ROOT)/$(DESTDIR)
#
HA=$(BRROOTDIR)/etc/ha.d
HALIB=$(BRROOTDIR)/usr/lib/$(PKG)
HARCD=$(HA)/rc.d
VARRUN=$(BRROOTDIR)/var/run
FIFO=$(VARRUN)/heartbeat-fifo
HAPPP=$(VARRUN)/ppp.d
DOCDIR=$(BRROOTDIR)/usr/doc/heartbeat
INITSCRIPT=$(BRROOTDIR)/$(INITD)/$(PKG)
RESOURCEDIR=$(BRROOTDIR)/etc/ha.d/resource.d
SPECSRC=Specfile

# Can't include the Build Root as a part of the compilation process
B_HA=$(DESTDIR)/etc/ha.d
B_VARRUN=$(DESTDIR)/var/run
B_FIFO=$(B_VARRUN)/heartbeat-fifo
B_HAPPP=$(B_VARRUN)/ppp.d
#
VARS=DESTDIR=$(DESTDIR) RPM_BUILD_ROOT=$(RPM_BUILD_ROOT) PKG=$(PKG) VERS=$(VERS)	\
	OPTFLAGS="$(RPM_OPT_FLAGS)"
MAKE=make
MAKE_CMD = $(MAKE) $(VARS)

NONKERNELDIRS= doc heartbeat
KERNELDIRS= proc-ha
BUILDDIRS= $(NONKERNELDIRS) $(KERNELDIRS)

#

HTML2TXT = lynx -dump
INSTALL = install

WEBDIR=/home/alanr/ha-web/download
RPMSRC=$(DESTDIR)/usr/src/redhat/SRPMS/$(PKG)-$(VERS)-$(RPMREL).src.rpm
RPM386=$(DESTDIR)/usr/src/redhat/RPMS/i386/$(PKG)-$(VERS)-$(RPMREL).i386.rpm

all:
	@if [ -f /etc/redhat-release ];then T=rh-all; else T=all; fi;	\
	for j in $(BUILDDIRS);						\
	do ( cd $$j; $(MAKE_CMD) $$T; ); done;


all_dirs:	bin_dirs
	[ -d $(DOCDIR) ]  || mkdir -p $(DOCDIR)

bin_dirs:
	[ -d $(HA) ]	  || mkdir -p $(HA)
	[ -d $(HALIB) ]	  || mkdir -p $(HALIB)
	[ -d $(HARCD) ]	  || mkdir -p $(HARCD)
	[ -d $(HAPPP) ]   || mkdir -p $(HAPPP)
	[ -d $(RESOURCEDIR) ] || mkdir -p $(RESOURCEDIR)
#	For some reason Red Hat added this, but didn't define HAMOD (?)
#        [ -d $(HAMOD) ] || mkdir -p $(HAMOD)


install:	all_dirs
	@if [ -f /etc/redhat-release ];then T=rh-install; else T=install; fi;	\
	for j in $(BUILDDIRS);							\
	do ( cd $$j; $(MAKE_CMD) $$T; ); done;

install_bin: bin_dirs
	@if [ -f /etc/redhat-release ];then T=rh-install_bin;else T=install_bin;\
	fi;									\
	for j in $(BUILDDIRS);							\
	do ( cd $$j; $(MAKE_CMD) $$T; ); done;

#
#	For alanr's development environment...
#
handy: rpm
	cd doc; $(MAKE) ChangeLog
	su alanr -c "cp doc/ChangeLog doc/GettingStarted.html $(TARFILE) $(RPMSRC) $(RPM386) $(WEBDIR)"

clean:	local_clean rpmclean
	@for j in $(BUILDDIRS);				\
	do ( cd $$j; $(MAKE_CMD) clean; ); done

local_clean:
	rm -f *.o *.swp .*.swp core
	rm -f $(LIBCMDS)

pristene: local_clean rpmclean
	@for j in $(BUILDDIRS);				\
	do ( cd $$j; $(MAKE_CMD) pristene; ); done


###############################################################################
#
#	Below is all the boilerplate for making an RPM package out of
#	the things made above.
#
#	To make the rpm package, say "make rpm".
#
###############################################################################

RPM=/bin/rpm
TAR=/bin/tar
RPMFLAGS=-ba


RPMSRCDIR=$(DESTDIR)/usr/src/redhat/SOURCES
RPMSPECDIR=$(DESTDIR)/usr/src/redhat/SPECS

#
#       OURDIR:         The directory these sources are in
#       TARFILE:        The name of the .tar.gz file we produce
#
OURDIR=$(PKG)-$(VERS)
TARFILE=$(RPMSRCDIR)/$(PKG)-$(VERS).tar.gz

#
#       Things for making the tar.gz file

tar:            clean rpmclean
		D=/usr/tmp/$$$$/$(OURDIR);			\
		mkdir -p $$D;					\
		find . -print | cpio -pdm $$D;			\
		cd $$D/..;					\
		$(TAR)  -cf - $(OURDIR) | gzip - > $(TARFILE);	\
		rm -fr /usr/tmp/$$$$;

#
#       Definitions needed for making the RPM package...
#
#
#       The RPM package files we make are:
#               $(PKG)-$(VERS)-$(RPMREL).rpm
#        and    $(PKG)-$(VERS)-$(RPMREL).src.rpm
#
SPECFILE=$(PKG)-$(VERS).spec

rpmclean:
	rm -f $(PKG)-*.spec

#
#	Make the "real" spec file by substituting some handy variables
#	into the "source" spec file
#
$(SPECFILE):    $(SPECSRC)
		sed     -e 's#%PKG%#$(PKG)#g'		\
			-e 's#%VERS%#$(VERS)#g'		\
			-e 's#%RPMREL%#$(RPMREL)#g'	\
			-e 's#%HADIR%#$(HA)#g'		\
			-e 's#%HALIB%#$(HALIB)#g'	\
			-e 's#%MANDIR%#$(MANDIR)#g'	\
			-e 's#%DOCDIR%#$(DOCDIR)#g'	\
			< $(SPECSRC) > $(SPECFILE)

rpm:            tar $(SPECFILE)
		$(RPM) $(RPMFLAGS) $(SPECFILE)
