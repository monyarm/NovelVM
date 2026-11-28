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

#include "common/config-manager.h"
#include "common/file.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "ai5/ai5.h"
#include "ai5/animplay.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/map.h"
#include "ai5/text.h"
#include "ai5/savedata.h"
#include "ai5/vm.h"
#include "ai5/games/aishimai.h"
#include "ai5/games/allstars.h"
#include "ai5/games/beyond.h"
#include "ai5/games/classics.h"
#include "ai5/games/doukyuusei.h"
#include "ai5/games/fallback.h"
#include "ai5/games/isaku.h"
#include "ai5/games/kakyuusei.h"
#include "ai5/games/shuusaku.h"

namespace AI5 {

AI5Engine *g_ai5 = nullptr;

AI5Engine::AI5Engine(OSystem *syst, const AI5GameDescription *gameDesc) : Engine(syst), _gameDescription(gameDesc), _rnd("ai5") {
	g_ai5 = this;
}

bool g_ai5Fallback = false;

static Common::WriteStream *g_fallbackLog = nullptr;

static void openFallbackLog() {
	// The saves directory is guaranteed writable, unlike the game data
	// directory, which may be mounted read-only.
	g_fallbackLog = g_system->getSavefileManager()->openForSaving("ai5-fallback.log", false);
	if (!g_fallbackLog)
		warning("Failed to open the fallback log for writing; logging to console only");
}

void ai5FallbackLog(const char *fmt, ...) {
	if (!g_ai5Fallback)
		return;

	char buf[512];
	va_list va;
	va_start(va, fmt);
	vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);

	if (g_fallbackLog) {
		Common::String line = Common::String::format("[%u] %s\n", (uint)g_system->getMillis(), buf);
		g_fallbackLog->write(line.c_str(), line.size());
		g_fallbackLog->flush();
	}
	warning("%s", buf);
}

bool AI5Engine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher
		|| f == kSupportsLoadingDuringRuntime
		|| f == kSupportsSavingDuringRuntime;
}

// Kawarazaki-ke's game-specific util calls live in the retail runtime and
// have no upstream definition: libai5 points this game at an empty util
// namespace (kawarazakike_tables.util = mes_util_none). Accept and skip the
// ones its scripts call, warning once for each.
#define KAWARAZAKI_UTIL_STUB(nr, what) \
	static void kawarazakiUtil##nr(ParamList &) { \
		static bool warned = false; \
		if (!warned) { \
			warned = true; \
			warning("Kawarazaki-ke: Util %u is not implemented (%s)", nr, what); \
		} \
	}

KAWARAZAKI_UTIL_STUB(34, "special display mode enter/leave")
KAWARAZAKI_UTIL_STUB(35, "background mode select")
KAWARAZAKI_UTIL_STUB(36, "title menu state")
KAWARAZAKI_UTIL_STUB(37, "title menu input")
KAWARAZAKI_UTIL_STUB(38, "title menu exit")
KAWARAZAKI_UTIL_STUB(40, "game over screen")
KAWARAZAKI_UTIL_STUB(41, "sound options menu")
KAWARAZAKI_UTIL_STUB(42, "ending sequence")
KAWARAZAKI_UTIL_STUB(43, "scene transition")
KAWARAZAKI_UTIL_STUB(44, "CG gallery init")
KAWARAZAKI_UTIL_STUB(45, "menu branch test")
KAWARAZAKI_UTIL_STUB(46, "CG gallery page")
KAWARAZAKI_UTIL_STUB(47, "title screen setup")
KAWARAZAKI_UTIL_STUB(48, "credits frame wait")
KAWARAZAKI_UTIL_STUB(49, "title screen input")

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

	g_yunoEng = false;
	switch (id) {
	case GID_ISAKU:
		initGameIsaku(*g_game);
		break;
	case GID_AISHIMAI:
		initGameAiShimai(*g_game);
		break;
	case GID_BEYOND:
		initGameBeyond(*g_game);
		break;
	case GID_ALLSTARS:
		initGameAllstars(*g_game);
		break;
	case GID_DOUKYUUSEI:
		initGameDoukyuusei(*g_game);
		break;
	case GID_DOUKYUUSEI2:
		initGameDoukyuusei2(*g_game);
		break;
	case GID_KAKYUUSEI: {
		// all files are in archives, and voices are in EVENT.ARC/EVERY.ARC
		const struct {
			ConfigArcFile *file;
			const char *name;
		} files[] = {
			{ &g_config.file.bg, "BG.ARC" },
			{ &g_config.file.mes, "MES.ARC" },
			{ &g_config.file.bgm, "BGM.ARC" },
			{ &g_config.file.voice, "EVENT.ARC" },
			{ &g_config.file.voice2, "EVERY.ARC" },
			{ &g_config.file.data, "DATA.ARC" }
		};
		for (uint i = 0; i < ARRAYSIZE(files); i++) {
			files[i].file->arc = true;
			if (files[i].file->name.empty())
				files[i].file->name = files[i].name;
		}
		initGameKakyuusei(*g_game);
		break;
	}
	case GID_YUNO:
		// the fan-made English patch renames the INI and changes the title
		g_yunoEng = g_config.title.hasPrefix("YU-NO");
		initGameYuno(*g_game);
		break;
	case GID_SHANGRLIA:
		initGameShangrlia(*g_game);
		break;
	case GID_KAWARAZAKIKE: {
		// The 2003 AIWIN re-release ships without the archive and
		// compression keys the config parser expects, so set the start
		// script, the archive names, and the raw mes members here.
		g_config.startMes = "STARTUP.MES";
		g_config.mesType = false;
		const struct {
			ConfigArcFile *file;
			const char *name;
		} files[] = {
			{ &g_config.file.mes, "mes.ARC" },
			{ &g_config.file.bg, "gcc.ARC" },
			{ &g_config.file.bgm, "bgm.AWF" },
			{ &g_config.file.voice, "voice.ARC" },
			{ &g_config.file.effect, "effect.AWF" }
		};
		for (uint i = 0; i < ARRAYSIZE(files); i++) {
			files[i].file->arc = true;
			if (files[i].file->name.empty())
				files[i].file->name = files[i].name;
		}
		// Kawarazaki-ke uses the same AIW tables as Shuusaku
		// (libai5: mes_sys_kawarazakike = mes_sys_shuusaku).
		initGameShuusaku(*g_game);
		// Its scripts call game-specific utils that upstream never
		// defined; accept and skip them. The utils shuusaku implements
		// itself are left alone.
		static const struct {
			int nr;
			SysFunc fn;
		} utilStubs[] = {
			{ 34, kawarazakiUtil34 },
			{ 35, kawarazakiUtil35 },
			{ 36, kawarazakiUtil36 },
			{ 37, kawarazakiUtil37 },
			{ 38, kawarazakiUtil38 },
			{ 40, kawarazakiUtil40 },
			{ 41, kawarazakiUtil41 },
			{ 42, kawarazakiUtil42 },
			{ 43, kawarazakiUtil43 },
			{ 44, kawarazakiUtil44 },
			{ 45, kawarazakiUtil45 },
			{ 46, kawarazakiUtil46 },
			{ 47, kawarazakiUtil47 },
			{ 48, kawarazakiUtil48 },
			{ 49, kawarazakiUtil49 }
		};
		for (uint i = 0; i < ARRAYSIZE(utilStubs); i++)
			g_game->util[utilStubs[i].nr] = utilStubs[i].fn;
		// Its scripts draw their own menus, so drop the shuusaku
		// overlay UI this disc does not ship art for.
		g_game->init = nullptr;
		// Its images are 24-bit (gcc.ARC), unlike the indexed art the
		// earlier AIW games use, so run the game in high color.
		g_game->bpp = 24;
		// Its scripts also carry unprefixed text like Doukyuusei 2's,
		// so accept those instead of erroring like Shuusaku does.
		g_game->unprefixedZen = vmStmtTxtNoLog;
		g_game->unprefixedHan = vmStmtStrNoLog;
		// Its mes files start with an offset table, its lists may
		// end on 0xfe, and its text operands and CommitMessage
		// parameters differ from the later AIW games.
		g_game->mesAddressTable = true;
		g_game->mesListTermFe = true;
		g_game->mesTextTermNul = true;
		g_game->stmtOp[0x22] = vmStmtCommitMessageAiw;
		// Its save slots use four-digit names (FLAG0000, FLAG0100).
		g_game->saveNameFourDigits = true;
		break;
	}
	case GID_SHUUSAKU:
		initGameShuusaku(*g_game);
		break;
	case GID_AI5_FALLBACK:
		g_ai5Fallback = true;
		openFallbackLog();
		ai5FallbackLog("generic AI5WIN fallback mode (unknown release)");
		initGameAi5Fallback(*g_game);
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

	_gameInitialized = true;
	vmLoadMes(g_config.startMes.c_str());
	g_game->vm.exec();

	mapFini();
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

bool AI5Engine::canLoadGameStateCurrently(Common::U32String *) {
	return _gameInitialized;
}

bool AI5Engine::canSaveGameStateCurrently(Common::U32String *msg) {
	if (!_gameInitialized && msg)
		*msg = Common::U32String("The game is not ready");
	return _gameInitialized;
}

Common::Error AI5Engine::saveGameState(int slot, const Common::String &desc, bool isAutosave) {
	savedataSaveFull(savedataSlotName(slot), desc);
	return Common::kNoError;
}

Common::Error AI5Engine::loadGameState(int slot) {
	savedataLoadFull(savedataSlotName(slot));
	return Common::kNoError;
}

} // End of namespace AI5
