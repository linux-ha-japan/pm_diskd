#
# heartbeat: Linux-HA telecom code: application heartbeat daemon
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
MAINTAINERCLEANFILES    = Makefile.in

INCLUDES                = -I$(top_builddir)/include -I$(top_srcdir)/include \
			-I$(top_builddir)/libltdl -I$(top_srcdir)/libltdl

hadir			= $(sysconfdir)/ha.d
halibdir		= $(libdir)/@HEARTBEATPKG@
commmoddir		= $(halibdir)/modules/comm
havarlibdir		= $(localstatedir)/lib/@HEARTBEATPKG@


CFLAGS			= @CFLAGS@	\
-DVAR_LIB_D='"$(havarlibdir)"'

## binary progs
halib_PROGRAMS		= apphbd

## SOURCES
apphbd_SOURCES	= apphbd.c
apphbd_LDADD		= $(top_builddir)/replace/libreplace.la	\
			$(top_builddir)/lib/pils/libpils.la	\
			$(top_builddir)/lib/clplumbing/libplumb.la	\
			$(top_builddir)/libltdl/libltdlc.la 	\
			$(GLIBLIB)

apphbd_LDFLAGS	= @LIBADD_DL@ @LIBLTDL@ -export-dynamic	\
			-dlpreopen force \
			-dlopen self

