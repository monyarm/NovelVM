POTFILE := $(srcdir)/po/novelvm.pot
POFILES := $(wildcard $(srcdir)/po/*.po)

ENGINE_INPUT_POTFILES := $(sort $(wildcard $(srcdir)/engines/*/POTFILES))
updatepot:
	cat $(srcdir)/po/POTFILES $(ENGINE_INPUT_POTFILES) | \
	xgettext -f - -D $(srcdir) -d novelvm --c++ -k_ -k_s -k_c:1,2c -k_sc:1,2c --add-comments=I18N\
		-kDECLARE_TRANSLATION_ADDITIONAL_CONTEXT:1,2c -o $(POTFILE) \
		--copyright-holder="NovelVM Team" --package-name=NovelVM \
		--package-version=$(VERSION) --msgid-bugs-address=novelvm-devel@lists.novelvm.org -o $(POTFILE)_

	sed -e 's/SOME DESCRIPTIVE TITLE/LANGUAGE translation for NovelVM/' \
		-e 's/UTF-8/CHARSET/' -e 's/PACKAGE/NovelVM/' $(POTFILE)_ > $(POTFILE).new

	rm $(POTFILE)_
	if test -f $(POTFILE); then \
		sed -f $(srcdir)/po/remove-potcdate.sed < $(POTFILE) > $(POTFILE).1 && \
		sed -f $(srcdir)/po/remove-potcdate.sed < $(POTFILE).new > $(POTFILE).2 && \
		if cmp $(POTFILE).1 $(POTFILE).2 >/dev/null 2>&1; then \
			rm -f $(POTFILE).new; \
		else \
			rm -f $(POTFILE) && \
			mv -f $(POTFILE).new $(POTFILE); \
		fi; \
		rm -f $(POTFILE).1 $(POTFILE).2; \
	else \
		mv -f $(POTFILE).new $(POTFILE); \
	fi;

%.po: $(POTFILE)
	msgmerge $@ $(POTFILE) -o $@.new
	if cmp $@ $@.new >/dev/null 2>&1; then \
		rm -f $@.new; \
	else \
		mv -f $@.new $@; \
	fi;

translations-dat: devtools/create_translations
	devtools/create_translations/create_translations translations.dat $(POFILES)
	mv translations.dat $(srcdir)/gui/themes/

update-translations: updatepot $(POFILES) translations-dat

update-translations: updatepot $(POFILES)
	@$(foreach file, $(POFILES), echo -n $(notdir $(basename $(file)))": ";msgfmt --statistic $(file);)
	@rm -f messages.mo

.PHONY: updatepot translations-dat update-translations
