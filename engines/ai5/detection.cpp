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
#include "engines/advancedDetector.h"

#include "ai5/detection.h"
#include "ai5/detection_tables.h"

namespace AI5 {

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
};

} // End of namespace AI5

REGISTER_PLUGIN_STATIC(AI5_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, AI5::AI5MetaEngineDetection);
