#
# InterfaceMgr: Interface manager plugins for Linux-HA
#
# Copyright (C) 2001 Alan Robertson
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
			-I$(top_builddir)/libltdl -I$(top_srcdir)/libltdl  \
			-I$(top_builddir)/lib/upmls -I$(top_srcdir)/lib/upmls \
			-I$(top_builddir) -I$(top_srcdir)

CFLAGS			= @CFLAGS@

## libraries

halibdir		= $(libdir)/@HEARTBEATPKG@
plugindir		= $(halibdir)/plugins/HBcomm
plugin_LTLIBRARIES	= bcast.la mcast.la serial.la ping.la rawiohb.la

bcast_la_SOURCES	= bcast.c
bcast_la_LDFLAGS	= -export-dynamic -module -avoid-version

mcast_la_SOURCES	= mcast.c
mcast_la_LDFLAGS	= -export-dynamic -module -avoid-version

serial_la_SOURCES	= serial.c
serial_la_LDFLAGS	= -export-dynamic -module -avoid-version

ping_la_SOURCES	        = ping.c
ping_la_LDFLAGS	        = -export-dynamic -module -avoid-version

rawiohb_la_SOURCES	= rawiohb.c block.c block.h checksum.c checksum.h \
                        device.c device.h int.h io.c io.h log.h \
                        options.c options.h packet.c packet.h \
                        hb_compat.c hb_compat.h 
rawiohb_la_LDFLAGS	= -export-dynamic -module -avoid-version
