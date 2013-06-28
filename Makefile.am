MAINTAINERCLEANFILES = Makefile.in

SUBDIRS		= resources tools
doc_DATA	= README HowToBuild_pm_extras.txt

SPEC                    = $(PACKAGE_NAME).spec
TARFILE                 = $(PACKAGE_NAME)-$(VERSION).tar.gz
EXTRA_DIST              = HowToBuild_pm_extras.txt $(SPEC) tools

$(TARFILE):
	$(MAKE) dist

RPM_ROOT		= $(shell pwd)
RPMBUILDOPTS		= --define "_sourcedir $(RPM_ROOT)" \
			  --define "_specdir $(RPM_ROOT)"

srpm: clean
	rm -f $(TARFILE)
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(WITH_LIST) $(RPMBUILDOPTS) --nodeps -bs --rmsource $(SPEC)

rpm: clean
	rm -f $(TARFILE)
	$(MAKE) $(SPEC) $(TARFILE)
	rpmbuild $(WITH_LIST) $(RPMBUILDOPTS) -ba --rmsource $(SPEC)
