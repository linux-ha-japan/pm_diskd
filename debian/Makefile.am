#
# linux-ha: Linux-HA heartbeat code
#
# Copyright (C) 2001 Michael Moerz
#                    This instance created by Horms
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

EXTRA_DIST	        = changelog control copyright dpkg-checkbuild \
                          heartbeat-ldirectord.conffiles \
                          heartbeat-ldirectord.files \
                          heartbeat-ldirectord.postinst \
                          heartbeat-ldirectord.postrm \
                          heartbeat-pils.dirs heartbeat-pils.files \
			  heartbeat-pils-dev.dirs heartbeat-pils-dev.files \
                          heartbeat-stonith.dirs heartbeat-stonith.files \
			  heartbeat-stonith-dev.files \
                          heartbeat-stonith.postinst heartbeat.conffiles \
                          heartbeat.dirs heartbeat.files \
			  heartbeat-dev.dirs heartbeat-dev.files \
                          heartbeat.postinst heartbeat.postrm \
			  rules \
			  undocumented.7

clean-local:
	rm -f *.substvars *.debhelper files
	rm -r -f heartbeat heartbeat-dev heartbeat-ldirectord \
		heartbeat-pils heartbeat-pils-dev \
		heartbeat-stonith heartbeat-stonith-dev tmp
