#	$Id: Makefile,v 1.5 2000/08/30 20:31:13 alan Exp $
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
	-Werror				\
	-Wtraditional			\
	-I.				\
	-DST_TEXTDOMAIN='"Stonith"'	\
	-DLOCALEDIR='"/usr/share/locale"'


STONITH = stonith

ARFILE = libstonith.a
SHLIB  = libstonith.so

S_OBJLIST =	expect.o	\
		baytech.o	\
		null.o		\
		stonith.o

S_HDRS    =	expect.h stonith.h

LIBS=

BINDIR = $(BUILD_ROOT)/usr/sbin
LIBDIR = $(BUILD_ROOT)/usr/lib

BINCMDS = $(STONITH)

LIBFILES = $(ARFILE) $(SHLIB)

PRODUCTS = $(BINCMDS)

SPECSRC = Specfile

all: $(PRODUCTS)

$(ARFILE): $(S_OBJLIST)
	ar rc $(ARFILE) $(S_OBJLIST)

$(SHLIB): $(ARFILE)
	ld -shared -o $(SHLIB) --whole-archive $(ARFILE)

$(STONITH): $(ARFILE) main.o
	$(CC) main.o $(ARFILE) -o stonith

$(S_OBJLIST) : $(S_HDRS)

install: install_bin install_lib

install_bin: all
	$(INSTALL) -cm 755 $(BINCMDS) $(BINDIR)

install_lib: $(LIBFILES)
	$(INSTALL) -cm 755 $(LIBFILES) $(LIBDIR)

clean:	
	rm -f *.o *.swp .*.swp *.a *.so core make.out
	rm -f $(BINCMDS) $(LIBFILES)

pristene: clean

