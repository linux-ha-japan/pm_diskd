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
			-I$(top_builddir)/include/stonith \
			-I$(top_srcdir)/include/stonith \
                        -I$(top_builddir) -I$(top_srcdir)

logrotatedir		= $(sysconfdir)/logrotate.d
stonithlibdir	       	= $(libdir)/@STONITHPKG@


CFLAGS			= @CFLAGS@ -DST_TEXTDOMAIN='"@STONITHPKG@"' \
-DSTONITH_MODULES='"$(stonithlibdir)"' \
-DLOCALEDIR='"@LOCALE@"'

## binaries
sbin_PROGRAMS		= stonith meatclient

stonith_SOURCES		= main.c
stonith_LDADD		= @LIBADD_DL@ libstonith.la  \
			$(top_builddir)/replace/libreplace.la

meatclient_SOURCES	= meatclient.c 

## libraries

lib_LTLIBRARIES		= libstonith.la

libstonith_la_SOURCES	= stonith.c main.c 
libstonith_la_LDFLAGS	= -version-info 0:0:0
libstonith_la_LIBADD	= @LIBADD_INTL@

## modules

#
#	The VACM and APC SNMP plugins require some more unusual libraries
#	so we make them optional...
#

#
#	We need "vacmclient_api.h" and -lvacmclient to make the VACM
#	plugin work.
#
if USE_VACM
vacm_LIB = vacm.la
else
vacm_LIB = 
endif

#
#	We need <ucd-snmp/asn1.h>, <ucd-snmp/snmp_api.h>, <ucd-snmp/snmp.h>
#	<ucd-snmp/snmp_client.h>, <ucd-snmp/mib.h>, -lsnmp and -lcrypto
#	for the apcmastersnmp plugin to work
#

if USE_UCD_SNMP
apcmastersnmp_LIB = apcmastersnmp.la
else
apcmastersnmp_LIB = 
endif

stonithlib_LTLIBRARIES	=	\
	apcmaster.la		\
	$(apcmastersnmp_LIB)	\
	apcsmart.la		\
	baytech.la		\
	meatware.la		\
	null.la			\
	nw_rpc100s.la		\
	rps10.la		\
	ssh.la			\
	$(vacm_LIB)		\
	wti_nps.la


apcmaster_la_SOURCES	= apcmaster.c expect.c 
apcmaster_la_LDFLAGS	= -module -avoid-version

apcmastersnmp_la_SOURCES	= apcmastersnmp.c
apcmastersnmp_la_LDFLAGS	= -module -avoid-version -lsnmp -lcrypto

apcsmart_la_SOURCES	= apcsmart.c expect.c
apcsmart_la_LDFLAGS	= -module -avoid-version

baytech_la_SOURCES	= baytech.c expect.c 
baytech_la_LDFLAGS	= -module -avoid-version

meatware_la_SOURCES	= meatware.c expect.c 
meatware_la_LDFLAGS	= -module -avoid-version

null_la_SOURCES		= null.c 
null_la_LDFLAGS		= -module -avoid-version

nw_rpc100s_la_SOURCES	= nw_rpc100s.c expect.c
nw_rpc100s_la_LDFLAGS	= -module -avoid-version

rps10_la_SOURCES	= rps10.c expect.c
rps10_la_LDFLAGS	= -module -avoid-version

ssh_la_SOURCES		= ssh.c
ssh_la_LDFLAGS		= -module -avoid-version

vacm_la_SOURCES		= vacm.c expect.c 
vacm_la_LDFLAGS		= -module -avoid-version -lvacmclient

wti_nps_la_SOURCES	= wti_nps.c expect.c 
wti_nps_la_LDFLAGS	= -module -avoid-version

## Man pages

man_MANS		= stonith.8

EXTRA_DIST              = $(man_MANS)
