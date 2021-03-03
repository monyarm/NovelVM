novelvm.nro: $(EXECUTABLE)
	mkdir -p ./switch_release/novelvm/data
	mkdir -p ./switch_release/novelvm/doc
	nacptool --create "NovelVM" "Cpasjuste" "$(VERSION)" ./switch_release/novelvm.nacp
	elf2nro $(EXECUTABLE) ./switch_release/novelvm/novelvm.nro --icon=$(srcdir)/dists/switch/icon.jpg --nacp=./switch_release/novelvm.nacp

switch_release: novelvm.nro
	rm -f ./switch_release/novelvm.nacp
	cp $(DIST_FILES_THEMES) ./switch_release/novelvm/data
ifdef DIST_FILES_ENGINEDATA
	cp $(DIST_FILES_ENGINEDATA) ./switch_release/novelvm/data
endif
ifdef DIST_FILES_NETWORKING
	cp $(DIST_FILES_NETWORKING) ./switch_release/novelvm/data
endif
ifdef DIST_FILES_VKEYBD
	cp $(DIST_FILES_VKEYBD) ./switch_release/novelvm/data
endif
ifdef DIST_FILES_SHADERS
	mkdir -p ./switch_release/novelvm/data/shaders
	cp $(DIST_FILES_SHADERS) ./switch_release/novelvm/data/shaders
endif
	cp $(DIST_FILES_DOCS) ./switch_release/novelvm/doc/
	cp $(srcdir)/backends/platform/sdl/switch/README.SWITCH ./switch_release/novelvm/doc/

novelvm_switch.zip: switch_release
	cd ./switch_release && zip -r ../novelvm_switch.zip . && cd ..

.PHONY: novelvm.nro switch_release novelvm_switch.zip

