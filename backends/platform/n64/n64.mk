N64_EXE_STRIPPED := novelvm_stripped$(EXEEXT)

bundle_name = n64-dist/novelvm
BASESIZE = 2097152

all: $(N64_EXE_STRIPPED)

$(N64_EXE_STRIPPED): $(EXECUTABLE)
	$(STRIP) $< -o $@

n64-distclean:
	rm -rf $(bundle_name)
	rm $(N64_EXE_STRIPPED)

n64-dist: all
	$(MKDIR) $(bundle_name)
	$(MKDIR) $(bundle_name)/romfs
ifdef DIST_FILES_ENGINEDATA
	$(CP) $(DIST_FILES_ENGINEDATA) $(bundle_name)/romfs
endif
ifdef DIST_FILES_NETWORKING
	$(CP) $(DIST_FILES_NETWORKING) $(bundle_name)/romfs
endif
ifdef DIST_FILES_VKEYBD
	$(CP) $(DIST_FILES_VKEYBD) $(bundle_name)/romfs
endif
	$(CP) $(DIST_FILES_DOCS) $(bundle_name)/
	genromfs -f $(bundle_name)/romfs.img -d $(bundle_name)/romfs -V novelvmn64
	mips64-objcopy $(EXECUTABLE) $(bundle_name)/novelvm.elf -O binary
	cat $(N64SDK)/hkz-libn64/bootcode $(bundle_name)/novelvm.elf $(bundle_name)/romfs.img > novelvm.v64
	$(srcdir)/backends/platform/n64/pad_rom.sh novelvm.v64
	rm novelvm.bak
	mv novelvm.v64 $(bundle_name)/novelvm.v64
