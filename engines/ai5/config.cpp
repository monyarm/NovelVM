/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

#include "common/archive.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/formats/ini-file.h"
#include "common/textconsole.h"

#include "ai5/config.h"

namespace AI5 {

Config g_config;

Config::Config() : voice(false), voicesub(false), sound(false), music(false), effect(false), screen(false),
		notify(false), bgType(false),
		// Different games have different defaults for bMESTYPE/bDATATYPE. Kakyuusei
		// is the only game so far that relies on the default.
		mesType(true), dataType(false), monitorScreen(0), kettei(false),
		transitionSpeed(1.0), msgSkipDelay(16), noWarpMouse(false), mapNoWallslide(false) {
	volume.music = volume.se = volume.effect = volume.voice = -1;
	soundinfo.music = soundinfo.effect = soundinfo.voice = false;
	itemwin.x = itemwin.y = 0;
}

static void setArc(ConfigArcFile &f, const Common::String &value) {
	f.arc = true;
	f.name = value;
}

static void handleKey(Config &c, const Common::String &section, const Common::String &key, const Common::String &value) {
	int v = atoi(value.c_str());
#define MATCH(s, n) (section.equalsIgnoreCase(s) && key.equalsIgnoreCase(n))
	if (MATCH("CONFIG", "TITLE"))
		c.title = value;
	else if (MATCH("CONFIG", "STARTMES"))
		c.startMes = value;
	else if (MATCH("CONFIG", "VOICE"))
		c.voice = v;
	else if (MATCH("CONFIG", "SOUND"))
		c.sound = v;
	else if (MATCH("CONFIG", "MUSIC"))
		c.music = v;
	else if (MATCH("CONFIG", "EFFECT"))
		c.effect = v;
	else if (MATCH("CONFIG", "SCREEN"))
		c.screen = v;
	else if (MATCH("CONFIG", "bNOTIFY"))
		c.notify = v;
	else if (MATCH("CONFIG", "DUNGEON") || MATCH("CONFIG", "bDEBUG"))
		; // ignored
	else if (MATCH("FILE", "bARCBG"))
		c.file.bg.arc = v;
	else if (MATCH("FILE", "bARCMES"))
		c.file.mes.arc = v;
	else if (MATCH("FILE", "bARCBGM"))
		c.file.bgm.arc = v;
	else if (MATCH("FILE", "bARCVOICE"))
		c.file.voice.arc = v;
	else if (MATCH("FILE", "bARCVOICESUB"))
		c.file.voicesub.arc = v;
	else if (MATCH("FILE", "bARCEFFECT"))
		c.file.effect.arc = v;
	else if (MATCH("FILE", "bARCDATA"))
		c.file.data.arc = v;
	else if (MATCH("FILE", "bARCPRIV"))
		c.file.priv.arc = v;
	else if (MATCH("FILE", "ARCBGNAME"))
		c.file.bg.name = value;
	else if (MATCH("FILE", "ARCMESNAME"))
		c.file.mes.name = value;
	else if (MATCH("FILE", "ARCBGMNAME"))
		c.file.bgm.name = value;
	else if (MATCH("FILE", "ARCVOICENAME"))
		c.file.voice.name = value;
	else if (MATCH("FILE", "ARCVOICESUBNAME"))
		c.file.voicesub.name = value;
	else if (MATCH("FILE", "ARCEFFECTNAME"))
		c.file.effect.name = value;
	else if (MATCH("FILE", "ARCDATANAME"))
		c.file.data.name = value;
	else if (MATCH("FILE", "ARCSPECIALNAME"))
		c.file.priv.name = value;
	else if (MATCH("FILE", "CDDRV"))
		c.file.cddrv = value;
	else if (MATCH("FILE", "MES"))
		setArc(c.file.mes, value);
	else if (MATCH("FILE", "PIC"))
		setArc(c.file.bg, value);
	else if (MATCH("FILE", "SEQ"))
		setArc(c.file.data, value);
	else if (MATCH("FILE", "BGM"))
		setArc(c.file.bgm, value);
	else if (MATCH("FILE", "SE"))
		setArc(c.file.effect, value);
	else if (MATCH("FILE", "SYSSE"))
		setArc(c.file.sysse, value);
	else if (MATCH("FILE", "MOVIE"))
		setArc(c.file.movie, value);
	else if (MATCH("FILE", "VOICE"))
		setArc(c.file.voice, value);
	else if (MATCH("GRAPHICS", "bBGTYPE"))
		c.bgType = v;
	else if (MATCH("MES", "bMESTYPE"))
		c.mesType = v;
	else if (MATCH("DATA", "bDATATYPE"))
		c.dataType = v;
	else if (MATCH("MONITOR", "SCREEN"))
		c.monitorScreen = v;
	else if (MATCH("ENV", "SOUNDBGM"))
		c.soundinfo.music = v;
	else if (MATCH("ENV", "SOUNDSE"))
		c.soundinfo.effect = v;
	else if (MATCH("ENV", "SOUNDVOICE"))
		c.soundinfo.voice = v;
	else if (MATCH("ENV", "VOLUMEBGM"))
		c.volume.music = v;
	else if (MATCH("ENV", "VOLUMESE"))
		c.volume.se = v;
	else if (MATCH("ENV", "VOLUMEVOICE"))
		c.volume.voice = v;
	else if (MATCH("ENV", "KETTEI"))
		c.kettei = v;
	else if (MATCH("VOLUME", "MUSIC") || MATCH("VOLUMEINFO", "MUSIC"))
		c.volume.music = v;
	else if (MATCH("VOLUME", "SE") || MATCH("VOLUMEINFO", "SE"))
		c.volume.se = v;
	else if (MATCH("VOLUME", "EFFECT") || MATCH("VOLUMEINFO", "EFFECT"))
		c.volume.effect = v;
	else if (MATCH("VOLUME", "VOICE") || MATCH("VOLUMEINFO", "VOICE"))
		c.volume.voice = v;
	else if (MATCH("SOUNDINFO", "MUSIC"))
		c.soundinfo.music = v;
	else if (MATCH("SOUNDINFO", "VOICE"))
		c.soundinfo.voice = v;
	else if (MATCH("SOUNDINFO", "EFFECT"))
		c.soundinfo.effect = v;
	else if (MATCH("ITEMWIN", "X"))
		c.itemwin.x = v;
	else if (MATCH("ITEMWIN", "Y"))
		c.itemwin.y = v;
	else
		debug(1, "AI5: unknown INI value %s.%s", section.c_str(), key.c_str());
#undef MATCH
}

// A re-release may ship its config under a name we do not know. Accept a file
// only if it still looks like an AI5 config, i.e. it parses and carries one of
// the sections the parser understands, so unrelated INI files are not used.
static bool isAi5Config(const Common::String &name) {
	Common::File f;
	if (!f.open(Common::Path(name)))
		return false;

	Common::INIFile ini;
	ini.allowNonEnglishCharacters();
	if (!ini.loadFromStream(f))
		return false;

	const Common::INIFile::SectionList sections = ini.getSections();
	for (Common::INIFile::SectionList::const_iterator s = sections.begin(); s != sections.end(); ++s) {
		if (s->name.equalsIgnoreCase("CONFIG") || s->name.equalsIgnoreCase("FILE"))
			return true;
	}
	return false;
}

bool loadConfig() {
	static const char *const names[] = { "AI5ENG.INI", "AI5WIN.INI", "AI.INI", "syuusaku.ini", "aiwin.ini" };
	Common::File f;
	Common::String name;
	for (uint i = 0; i < ARRAYSIZE(names) && name.empty(); i++) {
		if (f.open(names[i]))
			name = names[i];
	}
	if (name.empty()) {
		// Some re-releases rename the config file. Look for one by shape,
		// preferring names that still look like an AI5 config.
		static const char *const patterns[] = { "AI5*.INI", "AI*.INI", "*.INI" };
		for (uint p = 0; p < ARRAYSIZE(patterns) && name.empty(); p++) {
			Common::ArchiveMemberList files;
			SearchMan.listMatchingMembers(files, Common::Path(patterns[p]));
			for (Common::ArchiveMemberList::const_iterator it = files.begin(); it != files.end(); ++it) {
				const Common::String candidate = (*it)->getName();
				if (isAi5Config(candidate) && f.open(Common::Path(candidate))) {
					name = candidate;
					break;
				}
			}
		}
		if (name.empty())
			return false;
		warning("AI5: no known config file found, using \"%s\"", name.c_str());
	}

	Common::INIFile ini;
	ini.allowNonEnglishCharacters();
	if (!ini.loadFromStream(f))
		return false;

	g_config = Config();
	const Common::INIFile::SectionList sections = ini.getSections();
	for (Common::INIFile::SectionList::const_iterator s = sections.begin(); s != sections.end(); ++s) {
		for (Common::INIFile::SectionKeyList::const_iterator k = s->keys.begin(); k != s->keys.end(); ++k)
			handleKey(g_config, s->name, k->key, k->value);
	}

	if (g_config.startMes.empty())
		g_config.startMes = "START.MES";
	const struct {
		ConfigArcFile *file;
		const char *name;
	} defaults[] = {
		{ &g_config.file.bg, "BG.ARC" },
		{ &g_config.file.mes, "MES.ARC" },
		{ &g_config.file.bgm, "BGM.ARC" },
		{ &g_config.file.voice, "VOICE.ARC" },
		{ &g_config.file.effect, "BGM.ARC" },
		{ &g_config.file.data, "DATA.ARC" },
		{ &g_config.file.priv, "PRIV.ARC" }
	};
	for (uint i = 0; i < ARRAYSIZE(defaults); i++) {
		if (defaults[i].file->arc && defaults[i].file->name.empty())
			defaults[i].file->name = defaults[i].name;
	}

	Common::String iniName(name);
	g_config.exeName = iniName.substr(0, iniName.size() - 3) + "EXE";
	if (!Common::File::exists(Common::Path(g_config.exeName))) {
		// some re-releases rename the executable, e.g. AI5WIN_16bit_Color.exe
		Common::ArchiveMemberList exes;
		SearchMan.listMatchingMembers(exes, Common::Path(iniName.substr(0, iniName.size() - 4) + "*.EXE"));
		if (!exes.empty())
			g_config.exeName = exes.front()->getFileName();
	}
	return true;
}

} // End of namespace AI5
