#!/bin/sh

#
#	Copyright 2001 horms <horms@vergenet.net>
#		(mangled by alanr)
#
#	This code is known to work with bash, might have trouble with /bin/sh
#	on some systems.  Our goal is to not require dragging along anything
#	more than we need.  If this doesn't work on your system,
#	(i.e., your /bin/sh is broken) send us a patch.
#

# Run this to generate all the initial makefiles, etc.

die() {
        echo >&2
        echo "$@" >&2
        exit 1
}

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

THEDIR=`pwd` || die "Error running pwd"

#
#	All errors are fatal from here on out...
#	The shell will complain and exit on any "uncaught" error code.
#
#	Uncaught means not in the if-clause of a conditional statement
#
set -e

#
#	And this will ensure sure some kind of error message comes out.
#
trap 'echo ""; echo "$0 exiting due to error (sorry!)." >&2' 0
cd $srcdir

DIE=0

gnu="ftp://ftp.gnu.org/pub/gnu/"

for command in autoconf automake libtoolize
do
  pkg=$command
  case $command in 
    libtoolize)	pkg=libtool;;
  esac
  URL=$gnu/$pkg/
  if
    $command --version </dev/null >/dev/null 2>&1
  then
    : OK $pkg is installed
  else
    cat <<-!EOF >&2

	You must have $pkg installed to compile the linux-ha package.
	Download the appropriate package for your system,
	or get the source tarball at: $URL
	!EOF
    DIE=1
  fi
done

if test "$DIE" -eq 1; then
	exit 1
fi

if
  test -z "$*"
then
  cat <<-!
	Running ./configure with no arguments.
	If you wish to pass any arguments to it, please specify them
	       on the $0 command line.
	!
fi

#	Is CC set in the environment before starting?
case $CC in
  xlc)
    am_opt=--include-deps;;
esac

aclocal $ACLOCAL_FLAGS

#
#	Do we really want to ignore missing autoheader?
#
(autoheader --version)  < /dev/null > /dev/null 2>&1	\
&&		autoheader

libtoolize --ltdl --force --copy
automake --add-missing $am_opt
autoconf

cd $THEDIR

$srcdir/configure "$@"

echo 
echo "Now type 'make' to compile the system."
trap '' 0
