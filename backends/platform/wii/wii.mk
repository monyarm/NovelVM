WII_EXE_STRIPPED := novelvm_stripped$(EXEEXT)

all: $(WII_EXE_STRIPPED)

$(WII_EXE_STRIPPED): $(EXECUTABLE)
	$(STRIP) $< -o $@

clean: wiiclean

wiiclean:
	$(RM) $(WII_EXE_STRIPPED)

wiiload: $(WII_EXE_STRIPPED)
	$(DEVKITPPC)/bin/wiiload $<

geckoupload: $(WII_EXE_STRIPPED)
	$(DEVKITPPC)/bin/geckoupload $<

wiigdb:
	$(DEVKITPPC)/bin/powerpc-eabi-gdb -n $(EXECUTABLE)

wiidebug:
	$(DEVKITPPC)/bin/powerpc-eabi-gdb -n $(EXECUTABLE) -x $(srcdir)/backends/platform/wii/gdb.txt

# target to create a Wii snapshot
wiidist: all
	$(MKDIR) wiidist/novelvm
ifeq ($(GAMECUBE),1)
	$(DEVKITPPC)/bin/elf2dol $(EXECUTABLE) wiidist/novelvm/novelvm.dol
else
	$(STRIP) $(EXECUTABLE) -o wiidist/novelvm/boot.elf
	$(CP) $(srcdir)/dists/wii/icon.png wiidist/novelvm/
	sed "s/@REVISION@/$(VER_REV)/;s/@TIMESTAMP@/`date +%Y%m%d%H%M%S`/" < $(srcdir)/dists/wii/meta.xml > wiidist/novelvm/meta.xml
endif
ifeq ($(DYNAMIC_MODULES),1)
	$(MKDIR) wiidist/novelvm/plugins
	for i in $(PLUGINS); do $(STRIP) --strip-debug $$i -o wiidist/novelvm/plugins/`basename $$i`; done
endif
	sed 's/$$/\r/' < $(srcdir)/dists/wii/READMII > wiidist/novelvm/READMII.txt
	for i in $(DIST_FILES_DOCS); do sed 's/$$/\r/' < $$i > wiidist/novelvm/`basename $$i`.txt; done
	$(CP) $(DIST_FILES_THEMES) wiidist/novelvm/
ifneq ($(DIST_FILES_ENGINEDATA),)
	$(CP) $(DIST_FILES_ENGINEDATA) wiidist/novelvm/
endif
ifneq ($(DIST_FILES_NETWORKING),)
	$(CP) $(DIST_FILES_NETWORKING) wiidist/novelvm/
endif
ifneq ($(DIST_FILES_VKEYBD),)
	$(CP) $(DIST_FILES_VKEYBD) wiidist/novelvm/
endif

wiiloaddist: wiidist
	cd wiidist && zip -9r novelvm.zip novelvm/
	$(DEVKITPPC)/bin/wiiload wiidist/novelvm.zip

.PHONY: wiiclean wiiload geckoupload wiigdb wiidebug wiidist wiiloaddist
