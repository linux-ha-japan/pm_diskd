MAINTAINERCLEANFILES = Makefile.in

INCLUDES		= -I/usr/include/pacemaker \
			-I/usr/include/glib-2.0 \
			-I$(libdir)/glib-2.0/include \
			-I/usr/include/libxml2

halibdir		= $(CRM_DAEMON_DIR)
halib_PROGRAMS		= diskd

# BUILD

diskd_SOURCES		= diskd.c
diskd_LDADD		= -lcrmcommon \
			  -lgthread-2.0 \
			  -lcrmcluster


AM_CFLAGS 		= -Wall -Werror

