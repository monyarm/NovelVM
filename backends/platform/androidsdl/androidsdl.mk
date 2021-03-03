# Special target to create an AndroidSDL snapshot
androidsdl: 
	$(MKDIR) release
	$(INSTALL) -c -m 644 $(DIST_FILES_THEMES) $(DIST_FILES_NETWORKING) $(DIST_FILES_VKEYBD) $(DIST_FILES_ENGINEDATA) release
	$(INSTALL) -c -m 644 $(DIST_FILES_DOCS)  release
	zip -j novelvm_2_1_0-git-appdata.zip release/*
	split -d -b 1000000 novelvm_2_1_0-git-appdata.zip novelvm_2_1_0-git-appdata.zip0
	$(RM) -r novelvm_2_1_0-git-appdata.zip

.PHONY: androidsdl
