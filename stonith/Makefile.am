#
# stonith: Linux-HA heartbeat code
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
			-I$(top_builddir)/linux-ha -I$(top_srcdir)/linux-ha  \
			-I$(top_builddir)/stonith -I$(top_srcdir)/stonith \
                        -I$(top_builddir) -I$(top_srcdir)

logrotatedir		= $(sysconfdir)/logrotate.d
stonithlibdir	       	= $(libdir)/@STONITHPKG@


CFLAGS			= @CFLAGS@ -DST_TEXTDOMAIN='"@STONITHPKG@"' \
-DSTONITH_MODULES='"$(stonithlibdir)"' \
-DLOCALEDIR='"@LOCALE@"'

## include files
#pkginclude_HEADERS	= ../include/stonith/expect.h ../include/stonith/stonith.h

## binaries
sbin_PROGRAMS		= stonith meatclient

stonith_SOURCES		= main.c stonith.h
stonith_LDADD		= @LIBADD_DL@ libstonith.la  \
			$(top_builddir)/replace/libreplace.la

meatclient_SOURCES	= meatclient.c stonith.h

## libraries

lib_LTLIBRARIES		= libstonith.la

include_HEADERS         = stonith.h

libstonith_la_SOURCES	= stonith.c main.c stonith.h
libstonith_la_LDFLAGS	= -version-info 0:0:0

## modules

stonithlib_LTLIBRARIES	= null.la baytech.la rps10.la nw_rpc100s.la ssh.la
stonithlib_LTLIBRARIES	+= apcsmart.la apcmaster.la meatware.la wti_nps.la

baytech_la_SOURCES	= baytech.c expect.c stonith.h
baytech_la_LDFLAGS	= -no-undefined -module -avoid-version

rps10_la_SOURCES	= rps10.c expect.c stonith.h expect.h
rps10_la_LDFLAGS	= -no-undefined -module -avoid-version

nw_rpc100s_la_SOURCES	= nw_rpc100s.c expect.c stonith.h expect.h
nw_rpc100s_la_LDFLAGS	= -no-undefined -module -avoid-version

apcsmart_la_SOURCES	= apcsmart.c expect.c stonith.h expect.h
apcsmart_la_LDFLAGS	= -no-undefined -module -avoid-version

meatware_la_SOURCES	= meatware.c expect.c expect.h
meatware_la_LDFLAGS	= -no-undefined -module -avoid-version

ssh_la_SOURCES		= ssh.c expect.c stonith.h expect.h
ssh_la_LDFLAGS		= -no-undefined -module -avoid-version

apcmaster_la_SOURCES	= apcmaster.c expect.c stonith.h expect.h
apcmaster_la_LDFLAGS	= -no-undefined -module -avoid-version

wti_nps_la_SOURCES	= meatware.c expect.c stonith.h expect.h
wti_nps_la_LDFLAGS	= -no-undefined -module -avoid-version

# a real module ?
null_la_SOURCES		= null.c stonith.h expect.h
null_la_LDFLAGS		= -no-undefined -module -avoid-version

## Man pages

man_MANS		= stonith.8

EXTRA_DIST              = $(man_MANS)
