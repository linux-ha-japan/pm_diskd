#	$Id: Makefile,v 1.18 2001/02/22 13:43:39 eric Exp $
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



HTML2TXT = lynx -dump
INSTALL = install
MAKE  	= make

# For FreeBSD
LIBDL =
LIBINTL = -lintl
# For Linux
LIBDL = -ldl
LIBINTL =

# ld -shared --whole-archive


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
	-Werror                         \
	$(PICFLAG)			\
	-I.				\
	-DST_TEXTDOMAIN='"Stonith"'	\
	-DLOCALEDIR='"/usr/share/locale"'


STONITH = stonith

MEATWARE = meatclient

ARFILE = libstonith.a
SHLIB  = libstonith.so

S_OBJLIST =	stonith.o


MODULES = null.o apcsmart.o meatware.o baytech.o rps10.o nw_rpc100s.o

S_HDRS    =	expect.h stonith.h

LIBS=  $(MODULES:.o=.so)

BINDIR = $(BUILD_ROOT)/usr/sbin
LIBDIR = $(BUILD_ROOT)/usr/lib
MODDIR = $(LIBDIR)/$(STONITH)
INCDIR = $(BUILD_ROOT)/usr/include

CFLAGS := $(CFLAGS) -DSTONITH_MODULES='"$(MODDIR)"'

BINCMDS = $(STONITH) $(MEATWARE)

LIBFILES = $(ARFILE) $(SHLIB)


# If you want the VA Linux Cluster stonith module (VACM) installed, 
# uncomment the following line.  You must have the vacmclient library 
#VACM_STONITH = vacm_stonith.so 
PRODUCTS = $(BINCMDS) $(LIBS) $(VACM_STONITH)


INCFILES = stonith.h

SPECSRC = Specfile

all: $(PRODUCTS)

vacm_stonith.so: vacm_stonith.o
	$(CC) -shared -o $@ $^ -lvacmclient

$(MODULES:.o=.so): $(MODULES)
	$(CC) -shared -o $@ $(@:.so=.o)

$(ARFILE): $(S_OBJLIST)
	ar rc $(ARFILE) $(S_OBJLIST)

$(SHLIB): $(ARFILE)
	ld -shared -o $(SHLIB) --whole-archive $(ARFILE)

$(STONITH): $(ARFILE) main.o
	$(CC) main.o $(ARFILE) $(LIBDL) $(LIBINTL) -o stonith

$(MEATWARE):
	$(CC) meatclient.c -o meatclient

$(S_OBJLIST) : $(S_HDRS)

install: install_bin install_lib install_inc

install_bin: all
	if [ ! -d $(BINDIR) ]; then mkdir -p $(BINDIR) ; fi
	$(INSTALL) -cm 755 $(BINCMDS) $(BINDIR)
	ls -l $(BINDIR)/$(BINCMDS)

install_lib: $(LIBFILES) $(MODULES) $(VACM_STONITH)
	$(INSTALL) -cm 755 $(LIBFILES) $(LIBDIR)
	mkdir -p $(MODDIR)
	$(INSTALL) -cm 755 $(MODULES:.o=.so) $(VACM_STONITH) $(MODDIR)

install_inc:
	if [ ! -d $(INCDIR) ]; then mkdir $(INCDIR); fi
	$(INSTALL) -cm 644 $(INCFILES) $(INCDIR)

clean:	
	rm -f *.o *.swp .*.swp *.a *.so core make.out
	rm -f $(BINCMDS) $(LIBFILES)

pristine: clean

