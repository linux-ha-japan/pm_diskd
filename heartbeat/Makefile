#	$Id: Makefile,v 1.43 2000/12/05 15:28:46 alan Exp $
#
#	Makefile for making High-Availability Linux heartbeat code
#
#	If you're installing this package without going through an RPM,
#	you'll need to read the README to see how to make PPP work for you.
#
#
PKG=heartbeat

INITD=/etc/init.d
LOGROTATED=$(BUILD_ROOT)/etc/logrotate.d

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
GENLIB=$(BUILD_ROOT)/usr/lib/
HARCD=$(HA)/rc.d
VARRUN=$(BUILD_ROOT)/var/run
VARLIB=$(BUILD_ROOT)/var/lib/$(PKG)
APIDIR=$(VARLIB)/api
APIREG=$(VARLIB)/register
APICASUAL=$(VARLIB)/casual
FIFO=$(VARLIB)/fifo
HAPPP=$(VARRUN)/ppp.d
INITSCRIPT=$(BUILD_ROOT)/$(INITD)/$(PKG)
LOGROTATESCRIPT=$(LOGROTATED)/$(PKG)
RESOURCEDIR=$(BUILD_ROOT)/etc/ha.d/resource.d
COMM_MODULES_DIR=$(HALIB)/modules/comm/
AUTH_MODULES_DIR=$(HALIB)/modules/auth/
APIGID=60
APIGROUP=haclient


# For FreeBSD
LIBDL =
LIBINTL = -lintl
# For Linux
LIBDL = -ldl
LIBINTL =

# Can't include the Build Root as a part of the compilation process
B_HA=$(DESTDIR)/etc/ha.d
B_VARRUN=$(DESTDIR)/var/run
B_VARLIB=$(DESTDIR)/var/lib/$(PKG)
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
	-DVAR_LIB_D='"$(B_VARLIB)"'	\
	-DFIFONAME='"$(B_FIFO)"'	\
	-DPPP_D='"$(B_HAPPP)"'

#	-Wtraditional

LDFLAGS = -fident

SHAREDFLAGS = $(LIBDL) $(LIBINTL)  -rdynamic

HEARTBEAT = heartbeat
H_OBJLIST = 	heartbeat.o		\
		auth.o			\
		config.o		\
		hb_api.o		\
		ha_if.o			\
		ha_malloc.o		\
		ha_msg.o		\
		ha_msg_internal.o	\
		lock.o			\
		module.o

COMM_OBJS = ping.o serial.o ppp-udp.o udp.o 

AUTH_OBJS =	md5.o sha1.o crc.o

H_HDRS    = heartbeat.h ha_msg.h hb_api.h hb_api_core.h ha_if.h
HBLIBS=	../stonith/libstonith.a
LIBS=

SENDARP =	send_arp
S_OBJLIST =	send_arp.o
S_HDRS =

FINDIF	  =	findif
F_OBJLIST =	findif.o
F_HDRS	  =	

CLIENTOBJS = ha_msg.o lock.o client_lib.o
CLIENTLIB = libhbclient.a
CLIENTSO =  libhbclient.so

LIBS = $(CLIENTLIB) $(CLIENTSO) $(COMM_OBJS:.o=.so) $(AUTH_OBJS:.o=.so)

LIBCMDS = $(HEARTBEAT) $(SENDARP) $(FINDIF) $(LDIRECTORD) api_test

PRODUCTS = $(LIBCMDS) $(LIBS) 

LIBSCRIPTS = lib/mach_down lib/req_resource lib/ResourceManager

RESOURCECMDS= resource.d/IPaddr resource.d/AudibleAlarm resource.d/Raid1 resource.d/Filesystem

RCCMDS = rc.d/ip-request rc.d/ip-request-resp rc.d/status	\
	rc.d/ns_st rc.d/ifstat rc.d/shutdone
ROOTEXEC = harc
ROOTFILES = shellfuncs README.config

SPECSRC = Specfile

all: $(PRODUCTS)

$(HEARTBEAT): $(H_OBJLIST) $(H_HDRS) $(HBLIBS)
	$(CC) $(LDFLAGS) $(H_OBJLIST) $(SHAREDFLAGS) $(HBLIBS) -o $@

$(CLIENTLIB): $(CLIENTOBJS)
	ar rc $(CLIENTLIB) $(CLIENTOBJS)

$(CLIENTSO): $(CLIENTLIB)
	ld -shared -o $(CLIENTSO) --whole-archive $(CLIENTLIB)

$(COMM_OBJS:.o=.so): $(COMM_OBJS)
	$(CC) -shared -o $@ $(@:.so=.o)

$(AUTH_OBJS:.o=.so): $(AUTH_OBJS)
	$(CC) -shared -o $@ $(@:.so=.o)

api_test: api_test.o $(CLIENTLIB)
	$(CC) $(LDFLAGS) $^ -o api_test

$(H_OBJLIST) : $(H_HDRS)

$(SENDARP): $(S_OBJLIST) $(S_HDRS)
	$(CC) $(LDFLAGS) $(S_OBJLIST) -o $(SENDARP)

$(FINDIF): $(F_OBJLIST)
	$(CC) $(LDFLAGS) $(F_OBJLIST) -o $(FINDIF)

$(LDIRECTORD):
	$(MAKE) -C $(LDIRECTORD_DIR)

install: install_lib install_bin

install_bin: all
	RHGID=70
	if grep '^$(APIGROUP):' /etc/group >/dev/null; then : OK; 	\
	elif /usr/sbin/groupadd -g $(APIGID) $(APIGROUP);		\
        then : OK;							\
        else /usr/sbin/groupadd -g $(RHGID) $(APIGROUP);		\
        fi
	[ -d $(VARLIB) ]    || mkdir  -m 755 -p $(VARLIB)
	[ -p $(FIFO) ]      || mkfifo -m 600    $(FIFO)
	[ -p $(APIREG) ]    || mkfifo -m 620    $(APIREG)
	[ -d $(APIDIR) ]    || mkdir  -m 755 -p $(APIDIR)
	[ -d $(APICASUAL) ] || mkdir  -m 1770 -p $(APICASUAL)
	chgrp $(APIGROUP) $(APICASUAL) $(APIREG)
	$(INSTALL) -cm 755 $(LIBCMDS) $(LIBSCRIPTS) $(HALIB)
	$(INSTALL) -cm 755 $(RCCMDS) $(HARCD)
	$(INSTALL) -cm 755 $(RESOURCECMDS) $(RESOURCEDIR)
	$(INSTALL) -cm 644 $(ROOTFILES) $(HA)
	$(INSTALL) -cm 755 $(ROOTEXEC) $(HA)
	mkdir -p $(COMM_MODULES_DIR)
	mkdir -p $(AUTH_MODULES_DIR)
	$(INSTALL) -cm 755 $(COMM_OBJS:.o=.so) $(COMM_MODULES_DIR)
	$(INSTALL) -cm 755 $(AUTH_OBJS:.o=.so) $(AUTH_MODULES_DIR)
	if [ ! -d  $(BUILD_ROOT)/$(INITD) ]; then mkdir -p $(BUILD_ROOT)/$(INITD); fi
	$(INSTALL) -cm 755 heartbeat.sh $(INITSCRIPT)
	[ -d $(LOGROTATED) ] && \
		$(INSTALL) -cm 644 heartbeat.logrotate $(LOGROTATESCRIPT)

install_lib: all
	if [ ! -d  $(GENLIB) ]; then mkdir -p $(GENLIB); fi
	$(INSTALL) -cm 755 $(LIBS) $(GENLIB)

clean:	
	rm -f *.o *.swp .*.swp core make.out *.so
	rm -f $(PRODUCTS)

pristene: clean


