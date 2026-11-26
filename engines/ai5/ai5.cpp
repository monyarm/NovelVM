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

#include "common/system.h"
#include "common/textconsole.h"

#include "ai5/ai5.h"
#include "ai5/animplay.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/text.h"
#include "ai5/vm.h"
#include "ai5/games/isaku.h"

namespace AI5 {

AI5Engine *g_ai5 = nullptr;

AI5Engine::AI5Engine(OSystem *syst, const AI5GameDescription *gameDesc) : Engine(syst), _gameDescription(gameDesc), _rnd("ai5") {
	g_ai5 = this;
}

bool AI5Engine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher;
}

Common::Error AI5Engine::run() {
	const GameId id = _gameDescription->gameId;
	if (!loadConfig())
		return Common::Error(Common::kNoGameDataFoundError, "AI5WIN.INI");

	Common::ScopedPtr<Memory> mem(new Memory());
	Common::ScopedPtr<VM> vm(new VM());
	Common::ScopedPtr<Game> game(new Game());
	Common::ScopedPtr<AnimParser> animParser(new AnimParser(id));
	memset(mem.get(), 0, sizeof(Memory));
	memset(vm.get(), 0, sizeof(VM));
	memset(game.get(), 0, sizeof(Game));
	g_mem = mem.get();
	g_vm = vm.get();
	g_game = game.get();
	g_animParser = animParser.get();

	switch (id) {
	case GID_ISAKU:
		initGameIsaku(*g_game);
		break;
	default:
		return Common::Error(Common::kUnsupportedGameidError);
	}

	mesSetGame(id);
	assetInit(id);
	g_game->memInit();
	gfxInit();
	gfxTextInit();
	cursorInit(g_config.exeName.c_str());
	audioInit();
	vmInit();

	if (g_game->init)
		g_game->init();

	vmLoadMes(g_config.startMes.c_str());
	g_game->vm.exec();

	audioFini();
	cursorFini();
	gfxTextFini();
	gfxFini();
	assetFini();
	g_animParser = nullptr;
	g_game = nullptr;
	g_vm = nullptr;
	g_mem = nullptr;
	return Common::kNoError;
}

} // End of namespace AI5
