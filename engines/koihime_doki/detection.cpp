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

#include "koihime_doki/koihime_doki.h"

#include "base/plugins.h"

#include "engines/advancedDetector.h"

namespace KoihimeDoki
{
const char *KoihimeDokiEngine::getGameId() const { return _gameDescription->gameId; }
Common::Platform KoihimeDokiEngine::getPlatform() const { return _gameDescription->platform; }
} // namespace KoihimeDoki

static const PlainGameDescriptor KoihimeDokiGames[] = {
	{"KM", "Koihime † Musou ~Doki ☆ Otome Darake no Sangokushi Engi~", kFileFormatsGame, "https://vndb.org/v369"},
	{0, 0}};

namespace KoihimeDoki
{

static const ADGameDescription gameDescriptions[] = {
	{"KM",
	 0,
	 AD_ENTRY1s("koihime.ico", nullptr, AD_NO_SIZE),
	 Common::EN_ANY,
	 Common::kPlatformWindows,
	 ADGF_NO_FLAGS,
	 GUIO1(GUIO_NONE)},

	AD_TABLE_END_MARKER};

} // End of namespace KoihimeDoki

class KoihimeDokiMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription>
{
public:
	KoihimeDokiMetaEngineDetection() : AdvancedMetaEngineDetection(KoihimeDoki::gameDescriptions, KoihimeDokiGames)
	{
	}

	const char *getName() const override
	{
		return "KoihimeDoki";
	}

	const char *getEngineName() const override
	{
		return "Koihime Musou";
	}

	const char *getOriginalCopyright() const override
	{
		return "";
	}

	};

REGISTER_PLUGIN_STATIC(KOIHIME_DOKI_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, KoihimeDokiMetaEngineDetection);

