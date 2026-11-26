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

#include "bibleblack/bibleblack.h"

#include "base/plugins.h"

#include "engines/advancedDetector.h"

namespace BibleBlack
{
const char *BibleBlackEngine::getGameId() const { return _gameDescription->gameId; }
Common::Platform BibleBlackEngine::getPlatform() const { return _gameDescription->platform; }
} // namespace BibleBlack

static const PlainGameDescriptor BibleBlackGames[] = {
	{"BBLNdW", "Bible Black -La Noche de Walpurgis-", kFileFormatsGame, "https://vndb.org/v9"},
	{"BBTI", "Bible Black -The Infection-", kFileFormatsGame, "https://vndb.org/v1030"},
	{0, 0}};

namespace BibleBlack
{

static const ADGameDescription gameDescriptions[] = {
	{"BBLNdW",
	 0,
	 AD_ENTRY1s("A98SYS.PAK", nullptr, AD_NO_SIZE),
	 Common::EN_ANY,
	 Common::kPlatformWindows,
	 ADGF_NO_FLAGS,
	 GUIO1(GUIO_NONE)},
	{"BBTI",
	 0,
	 AD_ENTRY1s("A98SYS.PAK", nullptr, AD_NO_SIZE),
	 Common::EN_ANY,
	 Common::kPlatformWindows,
	 ADGF_NO_FLAGS,
	 GUIO1(GUIO_NONE)},

	AD_TABLE_END_MARKER};

} // End of namespace BibleBlack

class BibleBlackMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription>
{
public:
	BibleBlackMetaEngineDetection() : AdvancedMetaEngineDetection(BibleBlack::gameDescriptions, BibleBlackGames)
	{
	}


	const char *getOriginalCopyright() const override
	{
		return "";
	}
	const char *getName() const override
	{
		return "BibleBlack";
	}

	const char *getEngineName() const override
	{
		return "Bible Black";
	}
};

REGISTER_PLUGIN_STATIC(BIBLEBLACK_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, BibleBlackMetaEngineDetection);
