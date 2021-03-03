DINGUX_EXE_STRIPPED := novelvm_stripped$(EXEEXT)

bundle_name = dingux-dist/novelvm
gcw0_bundle = gcw0-opk

all: $(DINGUX_EXE_STRIPPED)

$(DINGUX_EXE_STRIPPED): $(EXECUTABLE)
	$(STRIP) $< -o $@

dingux-distclean:
	rm -rf $(bundle_name)
	rm $(DINGUX_EXE_STRIPPED)

dingux-dist: all
	$(MKDIR) $(bundle_name)
	$(MKDIR) $(bundle_name)/saves
	$(STRIP) $(EXECUTABLE) -o $(bundle_name)/novelvm.elf
	$(CP) $(DIST_FILES_THEMES) $(bundle_name)/
ifdef DIST_FILES_ENGINEDATA
	$(CP) $(DIST_FILES_ENGINEDATA) $(bundle_name)/
endif
ifdef DIST_FILES_NETWORKING
	$(CP) $(DIST_FILES_NETWORKING) $(bundle_name)/
endif
ifdef DIST_FILES_VKEYBD
	$(CP) $(DIST_FILES_VKEYBD) $(bundle_name)/
endif
	$(CP) $(DIST_FILES_DOCS) $(bundle_name)/
ifdef DYNAMIC_MODULES
		$(MKDIR) $(bundle_name)/plugins
		$(CP) $(PLUGINS) $(bundle_name)/plugins
		$(STRIP) $(bundle_name)/plugins/*
endif
	$(CP) $(srcdir)/backends/platform/dingux/novelvm.gpe $(bundle_name)/
	$(CP) $(srcdir)/backends/platform/dingux/README.DINGUX $(bundle_name)/
	$(CP) $(srcdir)/backends/platform/dingux/novelvm.png $(bundle_name)/

# Special target for generationg GCW-Zero OPK bundle
$(gcw0_bundle): all
	$(MKDIR) $(gcw0_bundle)
	$(CP) $(DIST_FILES_DOCS) $(gcw0_bundle)/
	$(MKDIR) $(gcw0_bundle)/themes
	$(CP) $(DIST_FILES_THEMES) $(gcw0_bundle)/themes/
ifdef DIST_FILES_ENGINEDATA
	$(MKDIR) $(gcw0_bundle)/engine-data
	$(CP) $(DIST_FILES_ENGINEDATA) $(gcw0_bundle)/engine-data/
endif
ifdef DIST_FILES_NETWORKING
	$(CP) $(DIST_FILES_NETWORKING) $(gcw0_bundle)/
endif
ifdef DIST_FILES_VKEYBD
	$(CP) $(DIST_FILES_VKEYBD) $(gcw0_bundle)/
endif
ifdef DYNAMIC_MODULES
	$(MKDIR) $(gcw0_bundle)/plugins
	$(CP) $(PLUGINS) $(gcw0_bundle)/plugins/
endif
	$(CP) $(EXECUTABLE) $(gcw0_bundle)/novelvm

	$(CP) $(srcdir)/dists/gcw0/novelvm.png $(gcw0_bundle)/
	$(CP) $(srcdir)/dists/gcw0/default.gcw0.desktop $(gcw0_bundle)/
	$(CP) $(srcdir)/dists/gcw0/novelvmrc $(gcw0_bundle)/
	$(CP) $(srcdir)/dists/gcw0/novelvm.sh $(gcw0_bundle)/
	$(CP) $(srcdir)/backends/platform/dingux/README.GCW0 $(gcw0_bundle)/README.man.txt
	echo >> $(gcw0_bundle)/README.man.txt
	echo '[General README]' >> $(gcw0_bundle)/README.man.txt
	echo >> $(gcw0_bundle)/README.man.txt
	cat $(srcdir)/README.md | sed -e 's/\[/⟦/g' -e 's/\]/⟧/g' -e '/^1\.1)/,$$ s/^[0-9][0-9]*\.[0-9][0-9]*.*/\[&\]/' >> $(gcw0_bundle)/README.man.txt


#	$(CP) GeneralUser\ GS\ FluidSynth\ v1.44.sf2 $(gcw0_bundle)/

gcw0-opk-unstripped: $(gcw0_bundle)
	$(CP) $(PLUGINS) $(gcw0_bundle)/plugins/
	$(CP) $(EXECUTABLE) $(gcw0_bundle)/novelvm
	$(srcdir)/dists/gcw0/opk_make.sh -d $(gcw0_bundle) -o novelvm

gcw-opk: $(gcw0_bundle)
	$(STRIP) $(gcw0_bundle)/plugins/*
	$(STRIP) $(gcw0_bundle)/novelvm
	$(srcdir)/dists/gcw0/opk_make.sh -d $(gcw0_bundle) -o novelvm

GeneralUser_GS_1.44-FluidSynth.zip:
	curl -s https://www.novelvm.org/frs/extras/SoundFont/GeneralUser_GS_1.44-FluidSynth.zip -o GeneralUser_GS_1.44-FluidSynth.zip

GeneralUser\ GS\ FluidSynth\ v1.44.sf2: GeneralUser_GS_1.44-FluidSynth.zip
	unzip -n GeneralUser_GS_1.44-FluidSynth.zip
	mv "GeneralUser GS 1.44 FluidSynth/GeneralUser GS FluidSynth v1.44.sf2" .
	mv "GeneralUser GS 1.44 FluidSynth/README.txt" README.soundfont
	mv "GeneralUser GS 1.44 FluidSynth/LICENSE.txt" LICENSE.soundfont
