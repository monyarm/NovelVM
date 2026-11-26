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

#include "common/events.h"
#include "common/system.h"
#include "engines/util.h"

#include "ai5/ai5.h"

namespace AI5 {

AI5Engine::AI5Engine(OSystem *syst, const AI5GameDescription *gameDesc) : Engine(syst), _gameDescription(gameDesc) {
}

bool AI5Engine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher;
}

Common::Error AI5Engine::run() {
	initGraphics(640, 480);

	while (!shouldQuit()) {
		Common::Event event;
		while (_system->getEventManager()->pollEvent(event))
			;
		_system->updateScreen();
		_system->delayMillis(10);
	}

	return Common::kNoError;
}

} // End of namespace AI5
