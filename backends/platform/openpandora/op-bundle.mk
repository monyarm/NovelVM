# Special target to create bundles and PND's for the OpenPandora.

#bundle_name = release/novelvm-op-`date '+%Y-%m-%d'`
bundle_name = release/novelvm-op
f=$(shell which $(STRIP))
libloc = $(shell dirname $(f))

op-bundle: $(EXECUTABLE)
	$(MKDIR) "$(bundle_name)"
	$(MKDIR) "$(bundle_name)/novelvm"
	$(MKDIR) "$(bundle_name)/novelvm/bin"
	$(MKDIR) "$(bundle_name)/novelvm/data"
	$(MKDIR) "$(bundle_name)/novelvm/docs"
	$(MKDIR) "$(bundle_name)/novelvm/icon"
	$(MKDIR) "$(bundle_name)/novelvm/lib"

	$(CP) $(srcdir)/dists/openpandora/runnovelvm.sh $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/openpandora/PXML.xml $(bundle_name)/novelvm/data/

	$(CP) $(srcdir)/dists/openpandora/icon/novelvm.png $(bundle_name)/novelvm/icon/
	$(CP) $(srcdir)/dists/openpandora/icon/preview-pic.png  $(bundle_name)/novelvm/icon/


	$(CP) $(srcdir)/dists/openpandora/README-OPENPANDORA $(bundle_name)/novelvm/docs/
	$(CP) $(srcdir)/dists/openpandora/index.html $(bundle_name)/novelvm/docs/

	$(INSTALL) -c -m 644 $(DIST_FILES_DOCS) $(bundle_name)/novelvm/docs/

	$(INSTALL) -c -m 644 $(DIST_FILES_THEMES) $(bundle_name)/novelvm/data/
ifdef DIST_FILES_ENGINEDATA
	$(INSTALL) -c -m 644 $(DIST_FILES_ENGINEDATA) $(bundle_name)/novelvm/data/
endif
ifdef DIST_FILES_NETWORKING
	$(INSTALL) -c -m 644 $(DIST_FILES_NETWORKING) $(bundle_name)/novelvm/data/
endif
ifdef DIST_FILES_VKEYBD
	$(INSTALL) -c -m 644 $(DIST_FILES_VKEYBD) $(bundle_name)/novelvm/data/
endif

	$(STRIP) $(EXECUTABLE) -o $(bundle_name)/novelvm/bin/$(EXECUTABLE)

ifdef DYNAMIC_MODULES
	$(INSTALL) -d "$(bundle_name)/novelvm/plugins"
	$(INSTALL) -c -m 644 $(PLUGINS) "$(bundle_name)/novelvm/plugins"
	$(STRIP) $(bundle_name)/novelvm/plugins/*
endif

	$(CP) $(libloc)/../arm-angstrom-linux-gnueabi/usr/lib/libFLAC.so.8.2.0 $(bundle_name)/novelvm/lib/libFLAC.so.8
	tar -C $(bundle_name) -cvjf $(bundle_name).tar.bz2 .
	rm -R ./$(bundle_name)

op-pnd: $(EXECUTABLE)
	$(MKDIR) "$(bundle_name)"
	$(MKDIR) "$(bundle_name)/novelvm"
	$(MKDIR) "$(bundle_name)/novelvm/bin"
	$(MKDIR) "$(bundle_name)/novelvm/data"
	$(MKDIR) "$(bundle_name)/novelvm/docs"
	$(MKDIR) "$(bundle_name)/novelvm/icon"
	$(MKDIR) "$(bundle_name)/novelvm/lib"

	$(CP) $(srcdir)/dists/openpandora/runnovelvm.sh $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/openpandora/PXML.xml $(bundle_name)/novelvm/data/

	$(CP) $(srcdir)/dists/openpandora/icon/novelvm.png $(bundle_name)/novelvm/icon/
	$(CP) $(srcdir)/dists/openpandora/icon/preview-pic.png  $(bundle_name)/novelvm/icon/


	$(CP) $(srcdir)/dists/openpandora/README-OPENPANDORA $(bundle_name)/novelvm/docs/
	$(CP) $(srcdir)/dists/openpandora/index.html $(bundle_name)/novelvm/docs/

	$(INSTALL) -c -m 644 $(DIST_FILES_DOCS) $(bundle_name)/novelvm/docs/

	$(INSTALL) -c -m 644 $(DIST_FILES_THEMES) $(bundle_name)/novelvm/data/
ifdef DIST_FILES_ENGINEDATA
	$(INSTALL) -c -m 644 $(DIST_FILES_ENGINEDATA) $(bundle_name)/novelvm/data/
endif
ifdef DIST_FILES_NETWORKING
	$(INSTALL) -c -m 644 $(DIST_FILES_NETWORKING) $(bundle_name)/novelvm/data/
endif
ifdef DIST_FILES_VKEYBD
	$(INSTALL) -c -m 644 $(DIST_FILES_VKEYBD) $(bundle_name)/novelvm/data/
endif

	$(STRIP) $(EXECUTABLE) -o $(bundle_name)/novelvm/bin/$(EXECUTABLE)

ifdef DYNAMIC_MODULES
	$(INSTALL) -d "$(bundle_name)/novelvm/plugins"
	$(INSTALL) -c -m 644 $(PLUGINS) "$(bundle_name)/novelvm/plugins"
	$(STRIP) $(bundle_name)/novelvm/plugins/*
endif

	$(CP) $(libloc)/../arm-angstrom-linux-gnueabi/usr/lib/libFLAC.so.8.2.0 $(bundle_name)/novelvm/lib/libFLAC.so.8

	$(srcdir)/dists/openpandora/pnd_make.sh -p $(bundle_name).pnd -c -d $(bundle_name)/novelvm -x $(bundle_name)/novelvm/data/PXML.xml -i $(bundle_name)/novelvm/icon/novelvm.png

	$(CP) $(srcdir)/dists/openpandora/README-PND.txt $(bundle_name)

	tar -cvjf $(bundle_name)-pnd.tar.bz2 $(bundle_name).pnd $(bundle_name)/README-PND.txt
	rm -R ./$(bundle_name)

.PHONY: op-bundle op-pnd
