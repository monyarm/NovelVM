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
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

#ifndef AI5_GAME_H
#define AI5_GAME_H

#include "common/events.h"

#include "ai5/detection.h"

namespace AI5 {

enum {
	kGameMaxUtil = 601,
	kGameMaxSys = 256
};

struct ParamList;
struct AnimDrawCall;

typedef void (*VMOp)();
typedef void (*SysFunc)(ParamList &params);

struct VMImpl {
	void (*exec)();
	uint32 (*eval)();
	void (*readParams)(ParamList &params);
	byte endCode;
};

// virtual flags, mapped to real flag bits in Game::flags
enum GameFlag {
	kFlagReflector,    // enables reflector animation (YU-NO)
	kFlagAnimEnable,   // enables animation playback
	kFlagMenuReturn,   // return flag for menuexec
	kFlagReturn,       // return flag
	kFlagProcClear,    // cleared/restored on procedure call
	kFlagLogEnable,    // enables backlog
	kFlagLogText,      // controls whether text is written to backlog
	kFlagLog,          // write to backlog
	kFlagLogSys,       // controls whether system calls are written to backlog
	kFlagLoadPalette,  // enables loading of palette in System.load_image
	kFlagVoiceEnable,  // enables System.Voice subsystem
	kFlagAudioEnable,  // enables System.Audio subsystem
	kFlagStrlen,       // count the length of text rather than displaying it
	kFlagWaitKeyup,    // wait for keyup events
	kFlagSkipKeyup,    // skip keyup events in menus
	kFlagPaletteOnly,  // only the palette is loaded in System.load_image
	kFlagSavePalette,  // palette is saved to bank before palette operations
	kGameNrFlags
};

enum {
	kFlagAlwaysOn = 0xffff
};

struct Game {
	GameId id;
	struct { uint w, h; } view;
	struct { uint16 w, h; } surfaceSizes[16];
	// 8 = indexed, 16 = BGR555, 24 = BGR888
	uint bpp;
	uint32 var4Size;
	uint32 mem16Size;
	// Some games' mes files start with a table of code offsets.
	bool mesAddressTable;
	// Their lists may also end on 0xfe instead of just 0xff.
	bool mesListTermFe;
	// Their text operands are terminated by 0x00 instead of 0xff.
	bool mesTextTermNul;
	// The 2003 Kawarazaki re-release numbers its save slots with four
	// digits (FLAG0000, FLAG0100, ...), earlier games with two.
	bool saveNameFourDigits = false;

	void (*init)();                       // before running the initial mes file
	void (*update)();                     // called in vmPeek
	bool (*handleEvent)(const Common::Event &e); // before built-in input handling
	void (*memInit)();                    // early init
	void (*memRestore)();                 // when a full save file is loaded
	void (*unprefixedZen)();              // unprefixed text in the mes file
	void (*unprefixedHan)();
	void (*drawTextZen)(const char *text);
	void (*drawTextHan)(const char *text);
	void (*afterAnimDraw)(AnimDrawCall *call);

	VMImpl vm;
	VMOp stmtOp[256];
	VMOp exprOp[256];
	SysFunc util[kGameMaxUtil];
	SysFunc sys[kGameMaxSys];
	uint32 flags[kGameNrFlags];
};

extern Game *g_game;

} // End of namespace AI5

#endif // AI5_GAME_H
