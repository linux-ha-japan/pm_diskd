#
# linux-ha: Linux-HA heartbeat code
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

##auxdir                  = @ac_aux_dir@
##AUX_DIST                = $(auxdir)/install-sh $(auxdir)/missing \
##                        $(auxdir)/mkinstalldirs
##AUX_DIST_EXTRA          = $(auxdir)/readline.m4 $(auxdir)/sys_errlist.m4 \
##                        $(auxdir)/sys_siglist.m4
EXTRA_DIST              = bootstrap acconfig.h rc.config.heartbeat \
                        heartbeat.spec.in heartbeat.spec

AUTOMAKE_OPTIONS	= foreign
##ACLOCAL                 = aclocal -I $(auxdir)

MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure DRF/config-h.in \
                        DRF/stamp-h.in 

## proc-ha is left out from SUBDIRS (obsolescent / doesn't work currently)

SUBDIRS                 = libltdl doc debian replace include lib \
                        @XPERIMENTALDIRS@ ldirectord stonith heartbeat \
                        linux-ha
