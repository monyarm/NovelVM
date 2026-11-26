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
 * Based on VileVN, Copyright (C) ViLE Team (GPL-3.0).
 *
 */

#include "ikura/ikura.h"

#include "base/plugins.h"
#include "engines/advancedDetector.h"

namespace Ikura {
const char *IkuraEngine::getGameId() const { return _gameDescription->gameId; }
Common::Platform IkuraEngine::getPlatform() const { return _gameDescription->platform; }
} // End of namespace Ikura

static const PlainGameDescriptor IkuraGames[] = {
	{"ikura-fixture", "Ikura/GDL Engine Detector Fixture (not a game)", kFileFormatsGame, nullptr},
	{"CATGIRL", "Cat Girl Alliance", kFileFormatsGame, "https://vndb.org/v153"},
	{"CRESCENDO", "Crescendo", kFileFormatsGame, nullptr},
	{"SAGARA", "The Sagara Family", kFileFormatsGame, "https://vndb.org/v46"},
	{"SNOW", "Snow Sakura", kFileFormatsGame, "https://vndb.org/v71"},
	{"KANA", "Kana ~ Little Sister", kFileFormatsGame, "https://vndb.org/v2"},
	{"KANAOKAERI", "Kana ... Okaeri!", kFileFormatsGame, nullptr},
	{"HITOMI", "Hitomi -My Stepsister-", kFileFormatsGame, "https://vndb.org/v31"},
	{"IDOLS", "Idols Galore!", kFileFormatsGame, "https://vndb.org/v141"},
	{nullptr, nullptr, kUnknownGame, nullptr}
};

namespace Ikura {

// VileVN itself maintains a real per-game detection file list: see
// ViLE::Probe* in the reference `src/vile.cpp` (audited in
// docs/vilevn-audit/PROVENANCE.md). Every ikura game requires the shared
// archive pair ("isf"+"ggd", or "drsgrp"+"drssnr" for the DRS-era Kana)
// plus a small INI-style "<key>.suf" descriptor file whose internal `Key=`
// field names the title - the .suf filename alone is what we can detect on
// without real dumps to hash; the internal Key confirms it once the engine
// actually opens the file; DRM-free reissues have also shipped the same
// descriptor as "game.suf", "<key>us.suf", or "<key>ml.suf" instead, but
// those variants need a real dump to confirm which one before adding rows.
static const ADGameDescription gameDescriptions[] = {
	// Synthetic fixture for automated detector tests; no real game ships
	// a file named IKURA.FIXTURE.
	{
		"ikura-fixture",
		0,
		AD_ENTRY1s("IKURA.FIXTURE", nullptr, AD_NO_SIZE),
		Common::UNK_LANG,
		Common::kPlatformUnknown,
		ADGF_UNSTABLE | ADGF_TESTING,
		GUIO1(GUIO_NONE)
	},
	// Real-data signatures below, keyed on each game's ".suf" descriptor
	// (ViLE::Probe* key) alongside the shared archive pair. Filename-only,
	// no md5/size: nobody has hashed a real dump yet.
	{
		"CATGIRL",
		0,
		AD_ENTRY3s("isf", nullptr, AD_NO_SIZE, "ggd", nullptr, AD_NO_SIZE, "koneko.suf", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},
	{
		"CRESCENDO",
		0,
		AD_ENTRY3s("isf", nullptr, AD_NO_SIZE, "ggd", nullptr, AD_NO_SIZE, "cres.suf", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},
	{
		"SAGARA",
		0,
		AD_ENTRY3s("isf", nullptr, AD_NO_SIZE, "ggd", nullptr, AD_NO_SIZE, "sagara.suf", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},
	{
		"SNOW",
		0,
		AD_ENTRY3s("isf", nullptr, AD_NO_SIZE, "ggd", nullptr, AD_NO_SIZE, "yuki.suf", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},
	// Kana is DRS, not Ikura/GDL proper: drsgrp+drssnr replace isf+ggd.
	{
		"KANA",
		0,
		AD_ENTRY3s("drsgrp", nullptr, AD_NO_SIZE, "drssnr", nullptr, AD_NO_SIZE, "kana.suf", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},
	{
		"KANAOKAERI",
		0,
		AD_ENTRY3s("isf", nullptr, AD_NO_SIZE, "ggd", nullptr, AD_NO_SIZE, "kanaoka.suf", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},
	{
		"HITOMI",
		0,
		AD_ENTRY3s("isf", nullptr, AD_NO_SIZE, "ggd", nullptr, AD_NO_SIZE, "hitomi.suf", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},
	// Only the primary "mesia.suf" retail release is listed; the download
	// edition ships a typo'd "mesiaus.suf" whose internal Key reads
	// "mesiaius" instead - the reference engine special-cases that, but
	// it needs a real dump before it's worth a second row here.
	{
		"IDOLS",
		0,
		AD_ENTRY3s("isf", nullptr, AD_NO_SIZE, "ggd", nullptr, AD_NO_SIZE, "mesia.suf", nullptr, AD_NO_SIZE),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace Ikura

class IkuraMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	IkuraMetaEngineDetection() : AdvancedMetaEngineDetection(Ikura::gameDescriptions, IkuraGames) {
	}

	const char *getName() const override {
		return "ikura";
	}

	const char *getEngineName() const override {
		return "Ikura/GDL";
	}

	const char *getOriginalCopyright() const override {
		return "Ikura/GDL games (C) their respective publishers";
	}
};

REGISTER_PLUGIN_STATIC(IKURA_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, IkuraMetaEngineDetection);
