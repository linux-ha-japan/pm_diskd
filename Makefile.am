## Makefile.am -- Process this file with automake to produce Makefile.in

# Initial creation: Michael Moerz <e9625136@stud3.tuwien.ac.at>

##auxdir                  = @ac_aux_dir@
##AUX_DIST                = $(auxdir)/install-sh $(auxdir)/missing \
##                        $(auxdir)/mkinstalldirs
##AUX_DIST_EXTRA          = $(auxdir)/readline.m4 $(auxdir)/sys_errlist.m4 \
##                        $(auxdir)/sys_siglist.m4
EXTRA_DIST              = bootstrap

AUTOMAKE_OPTIONS	= foreign
##ACLOCAL                 = aclocal -I $(auxdir)

MAINTAINERCLEANFILES    = Makefile.in aclocal.m4 configure DRF/config-h.in \
                        DRF/stamp-h.in 

## proc-ha is left out from SUBDIRS (obsolescent / doesn't work currently)

SUBDIRS                 = libltdl doc replace lib/pils lib/plugins/InterfaceMgr lib/plugins/HBauth @XPERIMENTALDIRS@ ldirectord stonith heartbeat
