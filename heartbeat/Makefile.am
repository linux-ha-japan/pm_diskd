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
			-I$(top_builddir)/libltdl -I$(top_srcdir)/libltdl

hadir			= $(sysconfdir)/ha.d
halibdir		= $(libdir)/@HEARTBEATPKG@
commmoddir		= $(halibdir)/modules/comm
authmoddir		= $(halibdir)/modules/auth
havarlibdir		= $(localstatedir)/lib/@HEARTBEATPKG@
apidir		       	= $(havarlibdir)/api
apicasualdir		= $(havarlibdir)/casual
# fifos with path
hafifo			= $(havarlibdir)/fifo
apireg			= $(havarlibdir)/register


CFLAGS			= @CFLAGS@	\
-DVAR_RUN_D='"$(localstatedir)/run"'	\
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
heartbeat_SOURCES	= heartbeat.c auth.c config.c hb_api.c ha_if.c \
			ha_malloc.c ha_msg.c ha_msg_internal.c lock.c \
			module.c
heartbeat_LDADD		= $(top_builddir)/stonith/libstonith.la \
			$(top_builddir)/replace/libreplace.la	\
			$(top_builddir)/libltdl/libltdlc.la

heartbeat_LDFLAGS	= @LIBADD_DL@ @LIBLTDL@ -export-dynamic	\
			-dlpreopen force \
			-dlopen self \
			-dlopen ping.la \
			-dlopen serial.la \
			-dlopen udp.la \
			-dlopen mcast.la \
			-dlopen md5.la \
			-dlopen sha1.la \
			-dlopen crc.la

send_arp_SOURCES	= send_arp.c

findif_SOURCES		= findif.c
findif_LDADD		= $(top_builddir)/replace/libreplace.la

api_test_SOURCES	= api_test.c
api_test_LDADD		= libhbclient.la

## binary modules

## communication modules
commmod_LTLIBRARIES	= ping.la serial.la udp.la mcast.la
# ppp_udp.la

ping_la_SOURCES		= ping.c
ping_la_LDFLAGS		= -export-dynamic -module -avoid-version

serial_la_SOURCES	= serial.c
serial_la_LDFLAGS	= -export-dynamic -module -avoid-version

#ppp_udp_la_SOURCES	= ppp-udp.c
#ppp_udp_la_LDFLAGS	= -export-dynamic -module -avoid-version

udp_la_SOURCES		= udp.c
udp_la_LDFLAGS		= -export-dynamic -module -avoid-version

mcast_la_SOURCES	= mcast.c
mcast_la_LDFLAGS	= -export-dynamic -module -avoid-version

## authentication modules
authmod_LTLIBRARIES	= md5.la sha1.la crc.la

md5_la_SOURCES		= md5.c
md5_la_LDFLAGS		= -export-dynamic -module -avoid-version

sha1_la_SOURCES		= sha1.c
sha1_la_LDFLAGS		= -export-dynamic -module -avoid-version

crc_la_SOURCES		= crc.c
crc_la_LDFLAGS		= -export-dynamic -module -avoid-version

## SCRIPTS/DATA
ha_DATA			= shellfuncs README.config
ha_SCRIPTS		= harc


## additional Makefile targets
# additional installations not covered normally
install-exec-local:
	$(mkinstalldirs) $(DESTDIR)$(apidir)
	$(mkinstalldirs) $(DESTDIR)$(apicasualdir)
	test -p $(DESTDIR)$(hafifo) || mkfifo -m 600 $(DESTDIR)$(hafifo)
	test -p $(DESTDIR)$(apireg) || mkfifo -m 620 $(DESTDIR)$(apireg)

