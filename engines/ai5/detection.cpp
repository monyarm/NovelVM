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
 */

#include "base/plugins.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/md5.h"
#include "engines/advancedDetector.h"

#include "ai5/detection.h"
#include "ai5/detection_tables.h"

namespace AI5 {

// Fallback descriptor for AI5WIN releases that are not in the detection
// table. The string fields point into the static Common::String objects
// below, which stay valid for the lifetime of the descriptor.
static AI5GameDescription s_fallbackDesc = {
	{
		"ai5",
		"",
		AD_ENTRY1(nullptr, nullptr),
		Common::UNK_LANG,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO0()
	},
	GID_AI5_FALLBACK
};

static Common::String s_fallbackGameId = "ai5";
static Common::String s_fallbackExtra;
static Common::String s_fallbackFileName;
static Common::String s_fallbackMd5;

class AI5MetaEngineDetection : public AdvancedMetaEngineDetection<AI5GameDescription> {
public:
	AI5MetaEngineDetection() : AdvancedMetaEngineDetection(gameDescriptions, ai5Games) {
	}

	const char *getName() const override {
		return "ai5";
	}

	const char *getEngineName() const override {
		return "AI5WIN";
	}

	const char *getOriginalCopyright() const override {
		return "AI5WIN (C) elf";
	}

	ADDetectedGame fallbackDetect(const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const override {
		// The engine configuration file identifies an AI5WIN release.
		static const char *iniNames[] = {
			"AI5WIN.INI", "AI5ENG.INI", "syuusaku.ini", "aiwin.ini"
		};

		Common::FSNode iniNode;
		Common::String iniName;
		for (Common::FSList::const_iterator file = fslist.begin(); file != fslist.end(); ++file) {
			if (file->isDirectory())
				continue;
			for (uint i = 0; i < ARRAYSIZE(iniNames); i++) {
				if (file->getName().equalsIgnoreCase(iniNames[i])) {
					iniNode = *file;
					iniName = iniNames[i];
					break;
				}
			}
			if (!iniName.empty())
				break;
		}

		Common::File f;
		if (iniName.empty() || !f.open(iniNode))
			return ADDetectedGame();

		Common::String md5 = Common::computeStreamMD5AsString(f, 5000);
		int32 fileSize = (int32)f.size();
		f.close();

		s_fallbackGameId = "ai5";
		s_fallbackExtra = Common::String::format("%s (unknown release)", iniName.c_str());
		s_fallbackFileName = iniName;
		s_fallbackMd5 = md5;

		s_fallbackDesc.desc.gameId = s_fallbackGameId.c_str();
		s_fallbackDesc.desc.extra = s_fallbackExtra.c_str();
		s_fallbackDesc.desc.filesDescriptions[0].fileName = s_fallbackFileName.c_str();
		s_fallbackDesc.desc.filesDescriptions[0].md5 = s_fallbackMd5.c_str();
		s_fallbackDesc.desc.filesDescriptions[0].fileSize = fileSize;
		s_fallbackDesc.desc.language = Common::UNK_LANG;
		s_fallbackDesc.desc.platform = Common::kPlatformWindows;
		s_fallbackDesc.desc.flags = ADGF_UNSTABLE;

		ADDetectedGame game(&s_fallbackDesc.desc);
		game.matchedFiles[iniNode.getPath()].md5 = md5;
		game.matchedFiles[iniNode.getPath()].size = fileSize;
		game.hasUnknownFiles = true;

		if (extra)
			*extra = new ADDetectedGameExtraInfo{Common::String(), Common::String("AI5WIN game (unknown release)")};

		return game;
	}
};

} // End of namespace AI5

REGISTER_PLUGIN_STATIC(AI5_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, AI5::AI5MetaEngineDetection);
