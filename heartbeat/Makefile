#	$Id: Makefile,v 1.23 2000/08/04 03:45:56 alan Exp $
#
#	Makefile for making High-Availability Linux heartbeat code
#
#	If you're installing this package without going through an RPM,
#	you'll need to read the README to see how to make PPP work for you.
#
#
PKG=heartbeat

INITD=$(shell [ -d /etc/init.d ] && echo /etc/init.d || echo /etc/rc.d/init.d )
LOGROTATED=/etc/logrotate.d

#       Debian wants things to start with DESTDIR,
#       but Red Hat starts them with RPM_BUILD_ROOT     (sigh...)
#
#       When make is called is shuold be run as
#       BUILD_ROOT=$VAR make ...
#
#       e.g.
#       BUILD_ROOT=$RPM_BUILD_ROOT make install

HA=$(BUILD_ROOT)/etc/ha.d
HALIB=$(BUILD_ROOT)/usr/lib/$(PKG)
HARCD=$(HA)/rc.d
VARRUN=$(BUILD_ROOT)/var/run
FIFO=$(VARRUN)/heartbeat-fifo
HAPPP=$(VARRUN)/ppp.d
INITSCRIPT=$(BUILD_ROOT)/$(INITD)/$(PKG)
LOGROTATESCRIPT=$(BUILD_ROOT)/$(LOGROTATED)/$(PKG)
RESOURCEDIR=$(BUILD_ROOT)/etc/ha.d/resource.d

# Can't include the Build Root as a part of the compilation process
B_HA=$(DESTDIR)/etc/ha.d
B_VARRUN=$(DESTDIR)/var/run
B_FIFO=$(B_VARRUN)/heartbeat-fifo
B_HAPPP=$(B_VARRUN)/ppp.d
B_HALIB=/usr/lib/$(PKG)

HTML2TXT = lynx -dump
INSTALL = install
MAKE  	= make


#
#	(We like warnings!  Unfortunately, several Linux header files have errors that prohibit
#	 turning on -pedantic-errors)
#
CFLAGS=-g 				\
	$(OPTFLAGS)			\
	-Wall				\
	-Wmissing-prototypes		\
	-Wmissing-declarations		\
	-Wstrict-prototypes		\
	-Wshadow			\
	-Wpointer-arith			\
	-Wwrite-strings			\
	-Werror				\
	-I.				\
	-I../stonith			\
	-DVERSION='"$(VERS)"'		\
	-DHA_D='"$(B_HA)"'		\
	-DHALIB='"$(B_HALIB)"'		\
	-DVAR_RUN_D='"$(B_VARRUN)"'	\
	-DFIFONAME='"$(B_FIFO)"'	\
	-DPPP_D='"$(B_HAPPP)"'

#	-Wtraditional

HEARTBEAT = heartbeat
H_OBJLIST = 	heartbeat.o		\
		auth.o			\
		config.o		\
		hb_api.o		\
		ha_malloc.o		\
		ha_msg.o		\
		ha_msg_internal.o	\
		lock.o			\
		md5.o			\
		ppp-udp.o		\
		serial.o		\
		sha1.o			\
		udp.o
H_HDRS    = heartbeat.h ha_msg.h hb_api.h hb_api_core.h
HBLIBS=	../stonith/libstonith.o
LIBS=

SENDARP =	send_arp
S_OBJLIST =	send_arp.o
S_HDRS =

FINDIF	  =	findif
F_OBJLIST =	findif.o
F_HDRS	  =	



LIBCMDS = $(HEARTBEAT) $(SENDARP) $(FINDIF) $(LDIRECTORD) api_test

PRODUCTS = $(LIBCMDS)

LIBSCRIPTS = lib/mach_down lib/req_resource lib/ResourceManager

RESOURCECMDS= resource.d/IPaddr resource.d/AudibleAlarm

RCCMDS = rc.d/ip-request rc.d/ip-request-resp rc.d/status
ROOTFILES = harc shellfuncs README.config

SPECSRC = Specfile

all: $(PRODUCTS)

$(HEARTBEAT): $(H_OBJLIST) $(H_HDRS) $(LIBS) $(HBLIBS)
	$(CC) $(LDFLAGS) $(H_OBJLIST) $(LIBS) $(HBLIBS) -o $(HEARTBEAT)

api_test: api_test.o ha_msg.o lock.o
	$(CC) $(LDFLAGS) api_test.o ha_msg.o lock.o -o api_test

$(H_OBJLIST) : $(H_HDRS)

$(SENDARP): $(S_OBJLIST) $(S_HDRS)
	$(CC) $(LDFLAGS) $(S_OBJLIST) $(LIBS) -o $(SENDARP)

$(FINDIF): $(F_OBJLIST)
	$(CC) $(LDFLAGS) $(F_OBJLIST) $(LIBS) -o $(FINDIF)

$(LDIRECTORD):
	$(MAKE) -C $(LDIRECTORD_DIR)

install: install_bin

install_bin: all
	[ -p $(FIFO) ] || mkfifo $(FIFO) || chmod 0600 $(FIFO)
	$(INSTALL) -cm 755 $(LIBCMDS) $(LIBSCRIPTS) $(HALIB)
	$(INSTALL) -cm 755 $(RCCMDS) $(HARCD)
	$(INSTALL) -cm 755 $(RESOURCECMDS) $(RESOURCEDIR)
	$(INSTALL) -cm 755 $(ROOTFILES) $(HA)
	if [ ! -d  $(BUILD_ROOT)/$(INITD) ]; then mkdir -p $(BUILD_ROOT)/$(INITD); fi
	$(INSTALL) -cm 755 heartbeat.sh $(INITSCRIPT)
	[ -d $(LOGROTATED) ] && \
		$(INSTALL) -cm 644 heartbeat.logrotate $(LOGROTATESCRIPT)

clean:	
	rm -f *.o *.swp .*.swp core make.out
	rm -f $(LIBCMDS)

pristene: clean


