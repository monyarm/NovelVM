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

#include "engines/advancedDetector.h"

#include "ai5/ai5.h"
#include "ai5/detection.h"

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
};

} // End of namespace AI5

#if PLUGIN_ENABLED_DYNAMIC(AI5)
REGISTER_PLUGIN_DYNAMIC(AI5, PLUGIN_TYPE_ENGINE, AI5::AI5MetaEngine);
#else
REGISTER_PLUGIN_STATIC(AI5, PLUGIN_TYPE_ENGINE, AI5::AI5MetaEngine);
#endif
