#
# doc: Linux-HA heartbeat code
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

docdir 			= $(datadir)/@HEARTBEATPKG@

htmlfiles		= HardwareGuide.html	\
			GettingStarted.html	\
			Requirements.html	\
			faqntips.html		\
			rsync.html

plainfiles              = $(htmlfiles:.html=)

CLEANFILES              = $(plainfiles)

SPECSRC 		= $(top_builddir)/heartbeat.spec

OTHER_DOCS              = AUTHORS COPYING ChangeLog \
                                README authkeys ha.cf haresources

doc_DATA		= $(OTHER_DOCS) $(plainfiles) $(htmlfiles) 

man_MANS		= heartbeat.8

EXTRA_DIST = $(htmlfiles) $(man_MANS) $(OTHER_DOCS)

ChangeLog: $(SPECSRC)
	rm -fr ChangeLog
	sed -e '1,/^%changelog/d' -e '/^%/,$$d' < $< > $@
	chmod 444 $@


HardwareGuide: HardwareGuide.html
	@HTML2TXT@ -dump $< >$@

GettingStarted: GettingStarted.html
	@HTML2TXT@ -dump $< >$@

Requirements: Requirements.html
	@HTML2TXT@ -dump $< >$@

faqntips: faqntips.html
	@HTML2TXT@ -dump $< >$@

rsync: rsync.html
	@HTML2TXT@ -dump $< >$@
