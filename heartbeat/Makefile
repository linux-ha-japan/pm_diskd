#	$Id: Makefile,v 1.2 1999/09/26 14:00:59 alanr Exp $
#
#	Makefile for making High-Availability Linux heartbeat code
#
#	If you're installing this package without going through an RPM,
#	you'll need to read the README to see how to make PPP work for you.
#
#
PKG=heartbeat

INITD=$(shell [ -d /etc/init.d ] && echo etc/init.d || echo etc/rc.d/init.d )

#	This defaults DESTDIR to "/"
#	Debian wants things to start with DESTDIR,
#	but Red Hat starts them with RPM_BUILD_ROOT	(sigh...)

RPM_BUILD_ROOT=
DESTDIR=
BRROOTDIR=$(RPM_BUILD_ROOT)/$(DESTDIR)
#
HA=$(BRROOTDIR)/etc/ha.d
HALIB=$(BRROOTDIR)/usr/lib/$(PKG)
HARCD=$(HA)/rc.d
VARRUN=$(BRROOTDIR)/var/run
FIFO=$(VARRUN)/heartbeat-fifo
HAPPP=$(VARRUN)/ppp.d
INITSCRIPT=$(BRROOTDIR)/$(INITD)/$(PKG)
RESOURCEDIR=$(BRROOTDIR)/etc/ha.d/resource.d

# Can't include the Build Root as a part of the compilation process
B_HA=$(DESTDIR)/etc/ha.d
B_VARRUN=$(DESTDIR)/var/run
B_FIFO=$(B_VARRUN)/heartbeat-fifo
B_HAPPP=$(B_VARRUN)/ppp.d

HTML2TXT = lynx -dump
INSTALL = install


#
#	(We like warnings!  Unfortunately, Linux's /usr/include/socketbits.h
#	has an error that prohibits turning on -pedantic-errors)
#
CFLAGS=-g 				\
	-Wall				\
	-Wmissing-prototypes		\
	-Wmissing-declarations		\
	-Wstrict-prototypes		\
	-Wshadow			\
	-Wtraditional			\
	-Wpointer-arith			\
	-Wwrite-strings			\
	-Werror				\
	-I.				\
	-DHA_D='"$(B_HA)"'		\
	-DVAR_RUN_D='"$(B_VARRUN)"'	\
	-DFIFONAME='"$(B_FIFO)"'	\
	-DPPP_D='"$(B_HAPPP)"'

HEARTBEAT = heartbeat
H_OBJLIST = heartbeat.o serial.o udp.o ppp-udp.o ha_msg.o auth.o md5.o sha1.o
H_HDRS    = heartbeat.h ha_msg.h
LIBS=

SENDARP =	send_arp
S_OBJLIST =	send_arp.o
S_HDRS =

FINDIF	  =	findif
F_OBJLIST =	findif.o
F_HDRS	  =	


LIBCMDS = $(HEARTBEAT) $(SENDARP) $(FINDIF)

PRODUCTS = $(LIBCMDS)

LIBSCRIPTS = lib/mach_down lib/req_resource lib/ResourceManager

RESOURCECMDS= resource.d/IPaddr

RCCMDS = rc.d/ip-request rc.d/ip-request-resp rc.d/status
ROOTFILES = harc shellfuncs

SPECSRC = Specfile

all: $(PRODUCTS)
rh-all: all

$(HEARTBEAT): $(H_OBJLIST) $(H_HDRS)
	$(CC) $(LDFLAGS) $(H_OBJLIST) $(LIBS) -o $(HEARTBEAT)

$(H_OBJLIST) : $(H_HDRS)

$(SENDARP): $(S_OBJLIST) $(S_HDRS)
	$(CC) $(LDFLAGS) $(S_OBJLIST) $(LIBS) -o $(SENDARP)

$(FINDIF): $(F_OBJLIST)
	$(CC) $(LDFLAGS) $(F_OBJLIST) $(LIBS) -o $(FINDIF)

install: install_bin
rh-install: install

install_bin: all
	[ -p $(FIFO) ] || mkfifo -m 0600 $(FIFO)
	$(INSTALL) -m 755 $(LIBCMDS) $(LIBSCRIPTS) $(HALIB)
	$(INSTALL) -m 755 $(RCCMDS) $(HARCD)
	$(INSTALL) -m 755 $(RESOURCECMDS) $(RESOURCEDIR)
	$(INSTALL) -m 755 $(ROOTFILES) $(HA)
	$(INSTALL) -m 755 heartbeat.sh $(INITSCRIPT)

rh-install_bin: install_bin

clean:	
	rm -f *.o *.swp .*.swp core
	rm -f $(LIBCMDS)

pristene: clean
