/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common/savefile.h"
#include "common/system.h"
#include "common/translation.h"

#include "graphics/surface.h"

#include "engines/advancedDetector.h"
#include "engines/savestate.h"

#include "ai5/ai5.h"
#include "ai5/detection.h"
#include "ai5/game.h"
#include "ai5/savedata.h"

namespace AI5 {

class AI5MetaEngine : public AdvancedMetaEngine<AI5GameDescription> {
public:
	const char *getName() const override {
		return "ai5";
	}

	Common::Error createInstance(OSystem *syst, Engine **engine, const AI5GameDescription *desc) const override {
		*engine = new AI5Engine(syst, desc);
		return Common::kNoError;
	}

	bool hasFeature(MetaEngineFeature f) const override {
		return f == kSupportsListSaves
		    || f == kSupportsDeleteSave
		    || f == kSavesSupportMetaInfo
		    || f == kSavesSupportThumbnail
		    || f == kSavesSupportCreationDate
		    || f == kSavesSupportPlayTime;
	}

	int getMaximumSaveSlot() const override {
		return 99;
	}

	SaveStateList listSaves(const char *target) const override {
		Common::String pattern = Common::String(target) + ".FLAG*";
		Common::StringArray files = g_system->getSavefileManager()->listSavefiles(pattern);

		SaveStateList saveList;
		for (uint i = 0; i < files.size(); i++) {
			// The save file names are "<target>.FLAG<slot>".
			const Common::String &file = files[i];
			uint prefixLen = strlen(target) + 1;
			if (file.size() <= prefixLen)
				continue;
			Common::String name = file.substr(prefixLen);
			if (!name.hasPrefix("FLAG"))
				continue;
			int slot = atoi(name.c_str() + 4);

			SaveHeader hdr;
			if (!savedataReadHeader(name.c_str(), hdr))
				continue;
			saveList.push_back(SaveStateDescriptor(this, slot, hdr.desc));
			if (hdr.thumbnail) {
				hdr.thumbnail->free();
				delete hdr.thumbnail;
			}
		}

		Common::sort(saveList.begin(), saveList.end(), SaveStateDescriptorSlotComparator());
		return saveList;
	}

	SaveStateDescriptor querySaveMetaInfos(const char *target, int slot) const override {
		// Try both save file name variants: some games use "FLAG%02u"
		// (zero-padded), Shuusaku uses "FLAG%u".
		Common::String names[2] = {
			Common::String::format("FLAG%02d", slot),
			Common::String::format("FLAG%d", slot)
		};
		for (int i = 0; i < 2; i++) {
			SaveHeader hdr;
			if (!savedataReadHeader(names[i].c_str(), hdr))
				continue;

			SaveStateDescriptor desc(this, slot, hdr.desc);
			if (hdr.thumbnail)
				desc.setThumbnail(hdr.thumbnail);
			desc.setPlayTime(hdr.playTimeSec * 1000);
			desc.setSaveDate(hdr.td.tm_year + 1900, hdr.td.tm_mon + 1, hdr.td.tm_mday);
			desc.setSaveTime(hdr.td.tm_hour, hdr.td.tm_min);
			desc.setWriteProtectedFlag(slot == getAutosaveSlot());
			return desc;
		}

		return SaveStateDescriptor();
	}

	bool removeSaveState(const char *target, int slot) const override {
		Common::String names[2] = {
			Common::String::format("FLAG%02d", slot),
			Common::String::format("FLAG%d", slot)
		};
		for (int i = 0; i < 2; i++) {
			Common::String name = Common::String(target) + "." + names[i];
			if (g_system->getSavefileManager()->removeSavefile(name))
				return true;
		}
		return false;
	}

	Common::String getSavegameFile(int saveGameIdx, const char *target) const override {
		// Engines other than Shuusaku use "FLAG%02u" save names,
		// Shuusaku uses "FLAG%u".
		if (g_game && g_game->id == GID_SHUUSAKU)
			return Common::String(target) + "." + Common::String::format("FLAG%u", saveGameIdx);
		return Common::String(target) + "." + Common::String::format("FLAG%02u", saveGameIdx);
	}
};

} // End of namespace AI5

#if PLUGIN_ENABLED_DYNAMIC(AI5)
REGISTER_PLUGIN_DYNAMIC(AI5, PLUGIN_TYPE_ENGINE, AI5::AI5MetaEngine);
#else
REGISTER_PLUGIN_STATIC(AI5, PLUGIN_TYPE_ENGINE, AI5::AI5MetaEngine);
#endif
