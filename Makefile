#	$Id: Makefile,v 1.48 2000/08/09 01:48:44 horms Exp $
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
VERS=0.4.9a
RPMREL=1

INITD=$(shell [ -d /etc/init.d ] && echo /etc/init.d || echo /etc/rc.d/init.d )
LOGROTATED=/etc/logrotate.d

#	Debian wants things to start with DESTDIR,
#	but Red Hat starts them with RPM_BUILD_ROOT	(sigh...)
#
#       When make is called it should be run as
#       BUILD_ROOT=$VAR make ...
#
#       e.g.
#       BUILD_ROOT=$RPM_BUILD_ROOT make install

HA=$(BUILD_ROOT)/etc/ha.d
HALIB=$(BUILD_ROOT)/usr/lib/$(PKG)
HARCD=$(HA)/rc.d
VARRUN=$(BUILD_ROOT)/var/run
FIFO=$(VARRUN)/heartbeat-fifo
HAPPP=$(VARRUN)/ppp.d
DOCDIR=$(BUILD_ROOT)/usr/doc/heartbeat
INITSCRIPT=$(BUILD_ROOT)/$(INITD)/$(PKG)
LOGROTATESCRIPT=$(BUILD_ROOT)/$(LOGROTATED)/$(PKG)
LOGROTATEDIR=$(BUILD_ROOT)/$(LOGROTATED)
RESOURCEDIR=$(BUILD_ROOT)/etc/ha.d/resource.d
CONFDIR=$(BUILD_ROOT)/etc/ha.d/conf
SPECSRC=Specfile

# Can't include the Build Root as a part of the compilation process
B_HA=$(DESTDIR)/etc/ha.d
B_VARRUN=$(DESTDIR)/var/run
B_FIFO=$(B_VARRUN)/heartbeat-fifo
B_HAPPP=$(B_VARRUN)/ppp.d
#
VARS=PKG=$(PKG) VERS=$(VERS)
MAKE=make
MAKE_CMD = $(MAKE) $(VARS)

NONKERNELDIRS= doc stonith heartbeat ldirectord
KERNELDIRS= 
BUILDDIRS= $(NONKERNELDIRS) $(KERNELDIRS) 

#

HTML2TXT = lynx -dump
INSTALL = install

WEBDIR=/usr/home/alanr/ha-web/download
RPMDIR=/usr/src/packages
RPMSRC=$(DESTDIR)$(RPMDIR)/SRPMS/$(PKG)-$(VERS)-$(RPMREL).src.rpm
RPM386=$(DESTDIR)$(RPMDIR)/RPMS/i386/$(PKG)-$(VERS)-$(RPMREL).i386.rpm

.PHONY =  		\
	all		\
	install		\
	handy		\
	clean		\
	pristene	\
	rpmclean	\
	rpm		\
	tar		\
	dist		\
	distclean	\
	tarclean	\
	clobber


all:
	for j in $(NONKERNELDIRS); 					\
	do 								\
		$(MAKE_CMD) -C $$j all; 				\
	done

all_dirs:	bin_dirs
	[ -d $(DOCDIR) ]  || mkdir -p $(DOCDIR)

bin_dirs:
	[ -d $(HA) ]	  || mkdir -p $(HA)
	[ -d $(HALIB) ]	  || mkdir -p $(HALIB)
	[ -d $(HARCD) ]	  || mkdir -p $(HARCD)
	[ -d $(HAPPP) ]   || mkdir -p $(HAPPP)
	[ -d $(RESOURCEDIR) ] || mkdir -p $(RESOURCEDIR)
	[ -d $(CONFDIR) ] || mkdir -p $(CONFDIR)
	[ -d $(LOGROTATED) ] && \
		[ -d $(LOGROTATEDIR) ] || mkdir -p $(LOGROTATEDIR)


install:	all_dirs
	@for j in $(NONKERNELDIRS); 					\
	do 								\
		$(MAKE_CMD) -C $$j install; 				\
	done
	@if [ -f /etc/redhat-release ];					\
	then 								\
		T=rh-install; else T=install; 				\
	fi;								\

install_bin: bin_dirs
	@for j in $(NONKERNELDIRS); 					\
	do 								\
		$(MAKE_CMD) -C $$j install_bin;				\
	done

#	For alanr's development environment...
#
handy: rpm
	cd doc; $(MAKE) ChangeLog
	su alanr -c "rm -f $(WEBDIR)/ChangeLog"
	su alanr -c "cp doc/ChangeLog doc/GettingStarted.html $(TARFILE) $(RPMSRC) $(RPM386) $(WEBDIR)"

clean:	local_clean
	@for j in $(BUILDDIRS);				\
	do 						\
		$(MAKE_CMD) -C $$j clean;		\
	done

local_clean:
	rm -f *.o *.swp .*.swp core *~ make.out
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
RPMFLAGS=-ta


RPMSRCDIR=$(DESTDIR)/usr/src/redhat/SOURCES
RPMSPECDIR=$(DESTDIR)/usr/src/redhat/SPECS

#
#       OURDIR:         The directory these sources are in
#       TARFILE:        The name of the .tar.gz file we produce
#
OURDIR=$(PKG)-$(VERS)
TARFILE=$(PKG)-$(VERS).tar.gz

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
		@if grep VERSION /etc/SuSE-release >/dev/null 2>&1;then	\
		  DISTRO=SuSE;						\
		  RPMSRCDIR=$(DESTDIR)/usr/src/packages/SOURCES;	\
		  RPMSPECDIR=$(DESTDIR)/usr/src/packages/SPECS;		\
		else							\
		  DISTRO=RedHat;					\
		fi;							\
		$(RPM) $(RPMFLAGS) $(TARFILE);				\
		rm -fr /var/tmp/$(PKG)-root;				\
		echo "##########################################";	\
		echo "# $(PKG) $(VERS) RPM for $$DISTRO is ready";	\
		echo "##########################################";
#
#       Things for making the tar.gz file

dist:		tar

tar:            $(TARFILE)

$(TARFILE):     tarclean clean $(SPECFILE) 
		mkdir -p $(OURDIR)
		find . -print | \
			egrep -v \
			"^./($(OURDIR)(/.*)?||.*tar\.gz|(.*/)?\.#.*)$$|CVS" | \
			cpio -pdm --quiet $(OURDIR)
		$(TAR)  -cf - $(OURDIR) | gzip - > $(TARFILE)
		rm -fr $(OURDIR)
		@echo "########################################################"
		@echo "# $(TARFILE) is ready for distribution"
		@echo "########################################################"


distclean:	tarclean

tarclean:	pristene
		rm -fr $(OURDIR) $(TARFILE)
		@for j in $(BUILDDIRS);			\
		do					\
			$(MAKE_CMD) -C $$j pristene;	\
		done


clobber:	tarclean rpmclean clean
