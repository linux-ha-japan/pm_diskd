#
# heartbeat: Linux-HA heartbeat code
#
# Copyright (C) 2001 Michael Moerz
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
MAINTAINERCLEANFILES    = Makefile.in

INCLUDES                = -I$(top_builddir)/include -I$(top_srcdir)/include \
			-I$(top_builddir)/stonith -I$(top_srcdir)/stonith \
			-I$(top_builddir)/linux-ha -I$(top_srcdir)/linux-ha \
			-I$(top_builddir)/libltdl -I$(top_srcdir)/libltdl \
                        -I$(top_builddir) -I$(top_srcdir)

hadir			= $(sysconfdir)/ha.d
halibdir		= $(libdir)/@HEARTBEATPKG@
commmoddir		= $(halibdir)/modules/comm
havarlibdir		= $(localstatedir)/lib/@HEARTBEATPKG@
havarrundir		= $(localstatedir)/run/
apidir		       	= $(havarlibdir)/api
apicasualdir		= $(havarlibdir)/casual
# fifos with path
hafifo			= $(havarlibdir)/fifo
apireg			= $(havarlibdir)/register


CFLAGS			= @CFLAGS@	\
-DVAR_RUN_D='"$(havarrundir)"'	\
-DVAR_LIB_D='"$(havarlibdir)"'		\
-DHA_D='"$(hadir)"'			\
-DHALIB='"$(halibdir)"'

## script subdirs
SUBDIRS			= init.d lib logrotate.d rc.d resource.d

noinst_HEADERS		= hb_module.h

## libraries

lib_LTLIBRARIES		= libhbclient.la 

libhbclient_la_SOURCES	= ha_msg.c lock.c client_lib.c
libhbclient_la_LDFLAGS	= -version-info 0:0:0

## binary progs
halib_PROGRAMS		= heartbeat send_arp findif api_test

## SOURCES
heartbeat_SOURCES	= heartbeat.c auth.c config.c hb_api.c \
			ha_malloc.c ha_msg.c ha_msg_internal.c lock.c \
			module.c ha_if.h ha_msg.h hb_api.h hb_api_core.h \
                        hb_md5.h hb_module.h hb_proc.h hb_sha1.h lock.h test.h
heartbeat_LDADD		= $(top_builddir)/stonith/libstonith.la \
			$(top_builddir)/replace/libreplace.la	\
			$(top_builddir)/lib/pils/libpils.la	\
			$(top_builddir)/libltdl/libltdlc.la

heartbeat_LDFLAGS	= @LIBADD_DL@ @LIBLTDL@ -export-dynamic	\
			-dlpreopen force \
			-dlopen self

send_arp_SOURCES	= send_arp.c

findif_SOURCES		= findif.c
findif_LDADD		=$(top_builddir)/replace/libreplace.la

api_test_SOURCES	= api_test.c
api_test_LDADD		= libhbclient.la

## SCRIPTS/DATA
ha_DATA			= shellfuncs README.config
ha_SCRIPTS		= harc


EXTRA_DIST              = $(ha_DATA) $(ha_SCRIPTS)


## additional Makefile targets
# additional installations not covered normally
install-exec-local:
	$(mkinstalldirs) $(DESTDIR)$(havarrundir)
	$(mkinstalldirs) $(DESTDIR)$(apidir)
	$(mkinstalldirs) $(DESTDIR)$(apicasualdir)
	test -p $(DESTDIR)$(hafifo) || mkfifo -m 600 $(DESTDIR)$(hafifo)
	test -p $(DESTDIR)$(apireg) || mkfifo -m 620 $(DESTDIR)$(apireg)

