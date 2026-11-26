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

#include "smt/smt.h"
#include "engines/advancedDetector.h"

class SMTMetaEngine : public AdvancedMetaEngine<ADGameDescription> {
public:
	const char *getName() const override
	{
		return "SMT";
		//return "Shin Megami Tensei";
	}
    
    Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
	bool hasFeature(MetaEngineFeature f) const override;
};

Common::Error SMTMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new SMT::SMTEngine(syst, desc);
	return Common::kNoError;
}


bool SMTMetaEngine::hasFeature(MetaEngineFeature f) const
{
	return (f == kSupportsListSaves) ||
			(f == kSupportsLoadingDuringStartup);
}

#if PLUGIN_ENABLED_DYNAMIC(SMT)
REGISTER_PLUGIN_DYNAMIC(SMT, PLUGIN_TYPE_ENGINE, SMTMetaEngine);
#else
REGISTER_PLUGIN_STATIC(SMT, PLUGIN_TYPE_ENGINE, SMTMetaEngine);
#endif
