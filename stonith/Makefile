#	$Id: Makefile,v 1.3 2000/07/28 05:32:21 alan Exp $
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
E_OBJLIST =	expect.o	\
		baytech.o	\
		null.o	\
		stonith.o	

E_HDRS    =	expect.h stonith.h

LIBS=


LIBCMDS = $(STONITH)

PRODUCTS = $(LIBCMDS)

SPECSRC = Specfile

all: $(PRODUCTS)

libstonith.o: $(E_OBJLIST) $(E_HDRS)
	$(LD) -r $(LDFLAGS) $(E_OBJLIST) $(LIBS) -o libstonith.o

$(STONITH): libstonith.o main.o
	$(CC) main.o libstonith.o -o stonith

$(E_OBJLIST) : $(E_HDRS)

install: install_bin

install_bin: all
	[ -p $(FIFO) ] || mkfifo $(FIFO) || chmod 0600 $(FIFO)
	$(INSTALL) -cm 755 $(LIBCMDS)

clean:	
	rm -f *.o *.swp .*.swp core make.out
	rm -f $(LIBCMDS)

pristene: clean


