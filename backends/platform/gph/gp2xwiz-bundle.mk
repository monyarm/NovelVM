# Special target to create bundles for the GP2X Wiz.

bundle_name = release/novelvm-gp2xwiz
f=$(shell which $(STRIP))
libloc = $(shell dirname $(f))

gp2xwiz-bundle: $(EXECUTABLE)
	$(MKDIR) "$(bundle_name)"
	$(MKDIR) "$(bundle_name)/novelvm"
	$(MKDIR) "$(bundle_name)/novelvm/saves"
	$(MKDIR) "$(bundle_name)/novelvm/engine-data"
	$(MKDIR) "$(bundle_name)/novelvm/lib"

	echo "Please put your save games in this dir" >> "$(bundle_name)/novelvm/saves/PUT_SAVES_IN_THIS_DIR"

	$(CP) $(srcdir)/dists/gph/gp2xwiz/novelvm.gpe $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/gph/novelvm.png $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/gph/novelvmb.png $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/gph/README-GPH $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/gph/novelvm.ini $(bundle_name)/

	$(INSTALL) -c -m 644 $(DIST_FILES_DOCS) $(bundle_name)/novelvm/
	$(INSTALL) -c -m 644 $(DIST_FILES_THEMES) $(bundle_name)/novelvm/
ifdef DIST_FILES_ENGINEDATA
	$(INSTALL) -c -m 644 $(DIST_FILES_ENGINEDATA) $(bundle_name)/novelvm/engine-data
endif
ifdef DIST_FILES_NETWORKING
	$(INSTALL) -c -m 644 $(DIST_FILES_NETWORKING) $(bundle_name)/novelvm/
endif
ifdef DIST_FILES_VKEYBD
	$(INSTALL) -c -m 644 $(DIST_FILES_VKEYBD) $(bundle_name)/novelvm/
endif

	$(STRIP) $(EXECUTABLE) -o $(bundle_name)/novelvm/$(EXECUTABLE)

ifdef DYNAMIC_MODULES
	$(INSTALL) -d "$(bundle_name)/novelvm/plugins"
	$(INSTALL) -c -m 644 $(PLUGINS) "$(bundle_name)/novelvm/plugins"
	$(STRIP) $(bundle_name)/novelvm/plugins/*
endif

	$(CP) $(libloc)/../lib/libz.so.1.2.3 $(bundle_name)/novelvm/lib/libz.so.1
	$(CP) $(libloc)/../lib/libvorbisidec.so.1.0.2 $(bundle_name)/novelvm/lib/libvorbisidec.so.1

	tar -C $(bundle_name) -cvjf $(bundle_name).tar.bz2 .
	rm -R ./$(bundle_name)

gp2xwiz-bundle-debug: $(EXECUTABLE)
	$(MKDIR) "$(bundle_name)"
	$(MKDIR) "$(bundle_name)/novelvm"
	$(MKDIR) "$(bundle_name)/novelvm/saves"
	$(MKDIR) "$(bundle_name)/novelvm/engine-data"
	$(MKDIR) "$(bundle_name)/novelvm/lib"

	echo "Please put your save games in this dir" >> "$(bundle_name)/novelvm/saves/PUT_SAVES_IN_THIS_DIR"

	$(CP) $(srcdir)/dists/gph/gp2xwiz/novelvm-gdb.gpe $(bundle_name)/novelvm/novelvm.gpe
	$(CP) $(srcdir)/dists/gph/novelvm.png $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/gph/novelvmb.png $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/gph/README-GPH $(bundle_name)/novelvm/
	$(CP) $(srcdir)/dists/gph/novelvm.ini $(bundle_name)/

	$(INSTALL) -c -m 644 $(DIST_FILES_DOCS) $(bundle_name)/novelvm/
	$(INSTALL) -c -m 644 $(DIST_FILES_THEMES) $(bundle_name)/novelvm/
ifdef DIST_FILES_ENGINEDATA
	$(INSTALL) -c -m 644 $(DIST_FILES_ENGINEDATA) $(bundle_name)/novelvm/engine-data
endif
ifdef DIST_FILES_NETWORKING
	$(INSTALL) -c -m 644 $(DIST_FILES_NETWORKING) $(bundle_name)/novelvm/
endif
ifdef DIST_FILES_VKEYBD
	$(INSTALL) -c -m 644 $(DIST_FILES_VKEYBD) $(bundle_name)/novelvm/
endif

	$(INSTALL) -c -m 777 $(srcdir)/$(EXECUTABLE) $(bundle_name)/novelvm/$(EXECUTABLE)

ifdef DYNAMIC_MODULES
	$(INSTALL) -d "$(bundle_name)/novelvm/plugins"
	$(INSTALL) -c -m 644 $(PLUGINS) "$(bundle_name)/novelvm/plugins"
endif

	$(CP) $(libloc)/../lib/libz.so.1.2.3 $(bundle_name)/novelvm/lib/libz.so.1
	$(CP) $(libloc)/../lib/libvorbisidec.so.1.0.2 $(bundle_name)/novelvm/lib/libvorbisidec.so.1

	tar -C $(bundle_name) -cvjf $(bundle_name)-debug.tar.bz2 .
	rm -R ./$(bundle_name)

.PHONY: gp2xwiz-bundle gp2xwiz-bundle-debug
