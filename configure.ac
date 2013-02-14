#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.53])
AC_INIT([pm_diskd], [1.2], BUG-REPORT-ADDRESS)
AM_INIT_AUTOMAKE

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB

#
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


# Checks for libraries.

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

# Checks for header files.
CFLAGS="$CFLAGS -I${prefix}/include/heartbeat"
CFLAGS="$CFLAGS -I${prefix}/include/pacemaker"

AC_CHECK_HEADERS([hb_config.h])
AC_CHECK_HEADERS([glue_config.h])
AC_CHECK_HEADERS([agent_config.h])

GLUE_HEADER=none
if test "$ac_cv_header_glue_config_h" = "yes"; then
   GLUE_HEADER=glue_config.h
elif test "$ac_cv_header_hb_config_h" = "yes"; then
   GLUE_HEADER=hb_config.h
else
   AC_MSG_FAILURE(Core development headers were not found)
fi

OCF_RA_DIR=`extract_header_define $GLUE_HEADER OCF_RA_DIR`
AC_DEFINE_UNQUOTED(OCF_RA_DIR,"$OCF_RA_DIR", ocf resource directory)
AC_SUBST(OCF_RA_DIR)

CRM_DAEMON_DIR=`extract_header_define crm_config.h CRM_DAEMON_DIR`
AC_DEFINE_UNQUOTED(CRM_DAEMON_DIR,"$CRM_DAEMON_DIR", crm daemon directory)
AC_SUBST(CRM_DAEMON_DIR)

CRM_DAEMON_GROUP=`extract_header_define $GLUE_HEADER HA_APIGROUP`
AC_DEFINE_UNQUOTED(CRM_DAEMON_GROUP,"$CRM_DAEMON_GROUP", Group to run Pacemaker daemons as)
AC_SUBST(CRM_DAEMON_GROUP)

CRM_DAEMON_USER=`extract_header_define $GLUE_HEADER HA_CCMUSER`
AC_DEFINE_UNQUOTED(CRM_DAEMON_USER,"$CRM_DAEMON_USER", User to run Pacemaker daemons as)
AC_SUBST(CRM_DAEMON_USER)

#
grep "release 6" /etc/redhat-release -q
if test $? = 0; then
	rhelver=6
else
	rhelver=5
fi
RHEL_VER="$rhelver"
AC_SUBST(RHEL_VER)

# Checks for library functions.

AC_CONFIG_FILES(Makefile	\
		resources/Makefile \
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
AC_MSG_RESULT([  Ocf RA dir               = ${OCF_RA_DIR}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  HA group name            = ${CRM_DAEMON_GROUP}])
AC_MSG_RESULT([  HA user name             = ${CRM_DAEMON_USER}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  CFLAGS                   = ${CFLAGS}])

