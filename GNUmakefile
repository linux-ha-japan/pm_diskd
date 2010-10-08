-include Makefile

PACKAGE		= pm_diskd

distdir		= $(PACKAGE)
TARFILE		= $(PACKAGE).tar.bz2
SPEC		= $(PACKAGE).spec

RPM_ROOT	= $(shell pwd)
RPM_OPTS	= --define "_sourcedir $(RPM_ROOT)" 	\
		  --define "_specdir   $(RPM_ROOT)"

TAG		?= tip

export:
	rm -f $(TARFILE)
	hg archive -t tbz2 -r $(TAG) $(TARFILE)
	echo `date`: Rebuilt $(TARFILE) from $(TAG)

srpm:	export
	rm -f *.src.rpm
	rpmbuild -bs --nodeps $(RPM_OPTS) $(SPEC)

rpm:	srpm
	rpmbuild -ba $(RPM_OPTS) $(SPEC)

