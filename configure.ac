#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.53])
AC_INIT([pm_diskd], [2.2], [iidayuus@intellilink.co.jp])
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB

#
extract_header_define_int() {
          AC_MSG_CHECKING(for $2 in $1)
          Cfile=/tmp/extract_define.$2.${$}
          printf "#include <stdio.h>\n" > ${Cfile}.c
          printf "#include <%s>\n" $1 >> ${Cfile}.c
          printf "int main(int argc, char **argv) { printf(\"%%d\", %s); return 0; }\n" $2 >> ${Cfile}.c
          $CC $CFLAGS ${Cfile}.c -o ${Cfile}
          value=`${Cfile}`
          AC_MSG_RESULT($value)
          printf $value
          rm -f ${Cfile}.c ${Cfile}
        }

extract_header_define() {
          AC_MSG_CHECKING(for $2 in $1)
          Cfile=/tmp/extract_define.$2.${$}
          printf "#include <stdio.h>\n" > ${Cfile}.c
          printf "#include <%s>\n" $1 >> ${Cfile}.c
          printf "int main(int argc, char **argv) { printf(\"%%s\", %s); return 0; }\n" $2 >> ${Cfile}.c
          $CC $CFLAGS ${Cfile}.c -o ${Cfile}
          value=`${Cfile}`
          AC_MSG_RESULT($value)
          printf $value
          rm -f ${Cfile}.c ${Cfile}
        }


# Checks for typedefs, structures, and compiler characteristics.
dnl Fix default variables - "prefix" variable if not specified
if test "$prefix" = "NONE"; then
	prefix="/usr"

	dnl Fix "localstatedir" variable if not specified
	if test "$localstatedir" = "\${prefix}/var"; then
		localstatedir="/var"
	fi
	dnl Fix "sysconfdir" variable if not specified
	if test "$sysconfdir" = "\${prefix}/etc"; then
		sysconfdir="/etc"
	fi
	dnl Fix "libdir" variable if not specified
	if test "$libdir" = "\${exec_prefix}/lib"; then
		if test -e /usr/lib64; then
			libdir="/usr/lib64"
		else
			libdir="/usr/lib"
		fi
	fi
fi

dnl ===============================================
dnl Libraries
dnl ===============================================

dnl check for pacemaker
PKG_CHECK_MODULES(CRM,    pacemaker-cluster)
LIBS="$LIBS $CRM_LIBS"

dnl check for gthread
PKG_CHECK_MODULES(GTHREAD, gthread-2.0 >= 2.0, have_gthread=yes, have_gthread=no)
AM_CONDITIONAL(HAVE_GTHREAD, test x$have_gthread = xyes)
if test x"$have_gthread" = xyes; then
	CFLAGS="$CFLAGS $GTHREAD_CFLAGS"
	LIBS="$LIBS $GTHREAD_LIBS"
fi

PKG_CHECK_MODULES([PACEMAKER], [pacemaker >= 1.1.13],
	AC_DEFINE_UNQUOTED([PACEMAKER_GE_1113], 1),
	AC_DEFINE_UNQUOTED([PACEMAKER_GE_1113], 0))

PKG_CHECK_MODULES([CHECK_ATTRD_UPDATE_BOTH], [pacemaker >= 1.1.15],
	AC_DEFINE_UNQUOTED([ATTRD_UPDATE_BOTH], 1),
	AC_DEFINE_UNQUOTED([ATTRD_UPDATE_BOTH], 0))

dnl ========================================================================
dnl Headers
dnl ========================================================================

AC_CHECK_HEADER([pacemaker/crm_config.h])

AC_PATH_PROGS(XML2CONFIG, xml2-config)
AC_MSG_CHECKING(for special libxml2 includes)
if test "x$XML2CONFIG" = "x"; then
	AC_MSG_ERROR(libxml2 config not found)
else
	XML2HEAD="`$XML2CONFIG --cflags`"
	AC_MSG_RESULT($XML2HEAD)
fi
CPPFLAGS="$CPPFLAGS $XML2HEAD"

if test "$ac_cv_header_pacemaker_crm_config_h" = "yes"; then
	CFLAGS="$CFLAGS -I${prefix}/include/pacemaker"
fi

SUPPORT_HEARTBEAT=`extract_header_define_int crm_config.h SUPPORT_HEARTBEAT`
if test "x$SUPPORT_HEARTBEAT" = x"1"; then
	CFLAGS="$CFLAGS -I${prefix}/include/heartbeat"
fi

OCF_ROOT_DIR=`extract_header_define crm/services.h OCF_ROOT_DIR`
AC_SUBST(OCF_ROOT_DIR)

CRM_DAEMON_DIR=`extract_header_define crm_config.h CRM_DAEMON_DIR`
AC_SUBST(CRM_DAEMON_DIR)


dnl The Makefiles and shell scripts we output
AC_CONFIG_FILES(Makefile	\
		resources/Makefile \
		resources/diskd \
		tools/Makefile \
		pm_diskd.spec
		)
AC_OUTPUT

dnl *****************
dnl Configure summary
dnl *****************

eval prefix="`eval echo ${prefix}`"
eval exec_prefix="`eval echo ${prefix}`"
eval bindir="`eval echo ${bindir}`"
eval sbindir="`eval echo ${sbindir}`"
eval libexecdir="`eval echo ${libexecdir}`"
eval datadir="`eval echo ${datadir}`"
eval sysconfdir="`eval echo ${sysconfdir}`"
eval sharedstatedir="`eval echo ${sharedstatedir}`"
eval localstatedir="`eval echo ${localstatedir}`"
eval libdir="`eval echo ${libdir}`"
eval includedir="`eval echo ${includedir}`"
eval oldincludedir="`eval echo ${oldincludedir}`"
eval infodir="`eval echo ${infodir}`"
eval mandir="`eval echo ${mandir}`"
eval docdir="`eval echo ${docdir}`"

AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE configuration:])
AC_MSG_RESULT([  Version                  = ${VERSION}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  Prefix                   = ${prefix}])
AC_MSG_RESULT([  Executables              = ${sbindir}])
AC_MSG_RESULT([  Libraries                = ${libdir}])
AC_MSG_RESULT([  Header files             = ${includedir}])
AC_MSG_RESULT([  State information        = ${localstatedir}])
AC_MSG_RESULT([  System configuration     = ${sysconfdir}])
AC_MSG_RESULT([  Document directory       = ${docdir}])
AC_MSG_RESULT([  Ocf RA dir               = ${OCF_ROOT_DIR}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  HA group name            = ${CRM_DAEMON_GROUP}])
AC_MSG_RESULT([  HA user name             = ${CRM_DAEMON_USER}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  CFLAGS                   = ${CFLAGS}])
AC_MSG_RESULT([  Libraries                = ${LIBS}])

