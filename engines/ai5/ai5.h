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

#ifndef AI5_AI5_H
#define AI5_AI5_H

#include "common/random.h"
#include "common/str.h"
#include "engines/engine.h"

#include "ai5/detection.h"

namespace Common {
class U32String;
}

namespace AI5 {

class AI5Engine : public Engine {
public:
	AI5Engine(OSystem *syst, const AI5GameDescription *gameDesc);

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;

	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave) override;
	Common::Error loadGameState(int slot) override;
	bool canSaveGameStateCurrently(Common::U32String *msg) override;
	bool canLoadGameStateCurrently(Common::U32String *msg) override;

	Common::RandomSource &getRandom() { return _rnd; }

private:
	const AI5GameDescription *_gameDescription;
	Common::RandomSource _rnd;
	bool _gameInitialized = false;
};

extern AI5Engine *g_ai5;

// Set when the engine runs a game that was detected by the fallback
// detector (an AI5WIN release that is not in the detection table).
// Enables additional diagnostics: engine warnings are then also
// appended to an "ai5-fallback.log" next to the game data.
extern bool g_ai5Fallback;
void ai5FallbackLog(const char *fmt, ...) GCC_PRINTF(1, 2);

} // End of namespace AI5

#endif // AI5_AI5_H
