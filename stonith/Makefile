#	$Id: Makefile,v 1.8 2000/10/02 20:56:37 eric Exp $
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
	-Wtraditional			\
	-I.				\
	-DST_TEXTDOMAIN='"Stonith"'	\
	-DLOCALEDIR='"/usr/share/locale"'


STONITH = stonith


ARFILE = libstonith.a
SHLIB  = libstonith.so

S_OBJLIST =	stonith.o

MODULES = null.o

S_HDRS    =	expect.h stonith.h

LIBS=  $(MODULES:.o=.so)

BINDIR = $(BUILD_ROOT)/usr/sbin
LIBDIR = $(BUILD_ROOT)/usr/lib
MODDIR = $(LIBDIR)/$(STONITH)

CFLAGS := $(CFLAGS) -DSTONITH_MODULES='"$(MODDIR)"'

BINCMDS = $(STONITH)

LIBFILES = $(ARFILE) $(SHLIB)

PRODUCTS = $(BINCMDS) $(LIBS) baytech.so rps10.so nw_rpc100s.so

SPECSRC = Specfile

all: $(PRODUCTS)

baytech.so:	baytech.o expect.o
	$(CC) -shared -o $@ $^

rps10.so:	rps10.o expect.o
	$(CC) -shared -o $@ $^

nw_rpc100s.so:	nw_rpc100s.o expect.o
	$(CC) -shared -o $@ $^

$(MODULES:.o=.so): $(MODULES)
	$(CC) -shared -o $@ $(@:.so=.o)

$(ARFILE): $(S_OBJLIST)
	ar rc $(ARFILE) $(S_OBJLIST)

$(SHLIB): $(ARFILE)
	ld -shared -o $(SHLIB) --whole-archive $(ARFILE)

$(STONITH): $(ARFILE) main.o
	$(CC) -ldl main.o $(ARFILE) -o stonith

$(S_OBJLIST) : $(S_HDRS)

install: install_bin install_lib

install_bin: all
	if [ ! -d $(BINDIR) ]; then mkdir -p $(BINDIR) ; fi
	$(INSTALL) -cm 755 $(BINCMDS) $(BINDIR)
	ls -l $(BINDIR)/$(BINCMDS)

install_lib: $(LIBFILES) $(MODULES) baytech.so rps10.so nw_rpc100s.so
	$(INSTALL) -cm 755 $(LIBFILES) $(LIBDIR)
	mkdir -p $(MODDIR)
	$(INSTALL) -cm 755 $(MODULES:.o=.so) baytech.so rps10.so nw_rpc100s.so $(MODDIR)

clean:	
	rm -f *.o *.swp .*.swp *.a *.so core make.out
	rm -f $(BINCMDS) $(LIBFILES)

pristene: clean

