#
# linux-ha: Linux-HA telecom code
#
# Copyright (C) 2002 Alan Robertson
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
#

##auxdir                  = @ac_aux_dir@
##AUX_DIST                = $(auxdir)/install-sh $(auxdir)/missing \
##                        $(auxdir)/mkinstalldirs
##AUX_DIST_EXTRA          = $(auxdir)/readline.m4 $(auxdir)/sys_errlist.m4 \
##                        $(auxdir)/sys_siglist.m4
EXTRA_DIST              = bootstrap acconfig.h rc.config.heartbeat \
                        	heartbeat.spec.in heartbeat.spec

RPMREL			= @RPMREL@
RPM			= @RPM@
RPMFLAGS		= -ta
TARFILE			= $(distdir).tar.gz
#
#       Debian wants things to start with DESTDIR,
#       but Red Hat starts them with RPM_BUILD_ROOT     (sigh...)
#
#       When make is called it should be run as
#       BUILD_ROOT=$VAR make ...
#
#       e.g.
#       BUILD_ROOT=$RPM_BUILD_ROOT make install
#
#	(The above comments carried over from the previous version of
#	the make system -- not sure this is all carried out like it
#	should be to make everyone happy -- at least at this time...)
#
DESTDIR=
RPMDIR=$(HOME)/rpms
WEBDIR=/home/alanr/ha-web/download
RPMSRC=$(DESTDIR)$(RPMDIR)/SRPMS/$(PACKAGE)-$(VERSION)-$(RPMREL).src.rpm
RPM386=$(DESTDIR)$(RPMDIR)/RPMS/i386/$(PACKAGE)-$(VERSION)-$(RPMREL).i386.rpm
RPMstonith=$(DESTDIR)$(RPMDIR)/RPMS/i386/$(PACKAGE)-stonith-$(VERSION)-$(RPMREL).i386.rpm
RPMldir=$(DESTDIR)$(RPMDIR)/RPMS/i386/$(PACKAGE)-ldirectord-$(VERSION)-$(RPMREL).i386.rpm

AUTOMAKE_OPTIONS	= foreign
##ACLOCAL                 = aclocal -I $(auxdir)

MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure DRF/config-h.in \
                        DRF/stamp-h.in 

## proc-ha is left out from SUBDIRS (obsolescent / doesn't work currently)

SUBDIRS                 = libltdl doc debian replace include lib \
                        @XPERIMENTALDIRS@ ldirectord stonith heartbeat \
                        cts linux-ha

rpm:		dist
		$(RPM) $(RPMFLAGS) $(TARFILE) </dev/null;

clean:	am_config_clean
am_config_clean:
		rm -fr $(PACKAGE)-*.tar.gz

handy: rpm
	cd doc; $(MAKE) ChangeLog
	rm -f $(WEBDIR)/ChangeLog
	cp doc/ChangeLog doc/GettingStarted.html $(TARFILE) $(RPMSRC) $(RPM386) $(RPMstonith) $(RPMldir) $(WEBDIR)



.PHONY: rpm handy
