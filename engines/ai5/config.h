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

#ifndef AI5_CONFIG_H
#define AI5_CONFIG_H

#include "common/str.h"

namespace AI5 {

struct ConfigArcFile {
	bool arc;
	Common::String name;

	ConfigArcFile() : arc(false) {}
};

/**
 * Settings from the game's AI5WIN.INI.
 */
struct Config {
	// [CONFIG]
	Common::String title; // Shift-JIS
	Common::String startMes;
	bool voice;
	bool voicesub;
	bool sound;
	bool music;
	bool effect;
	bool screen;
	bool notify;
	// [FILE]
	struct {
		ConfigArcFile bg, mes, bgm, voice, voice2, voice3, voice4, voicesub, effect, data, priv, movie, sysse;
		Common::String cddrv;
	} file;
	bool bgType;   // [GRAPHICS] bBGTYPE
	bool mesType;  // [MES] bMESTYPE
	bool dataType; // [DATA] bDATATYPE
	int monitorScreen;
	struct {
		int music, se, effect, voice;
	} volume;
	struct {
		bool music, effect, voice;
	} soundinfo;
	struct {
		int x, y;
	} itemwin;
	bool kettei; // Shuusaku [ENV] KETTEI
	Common::String exeName; // INI name with .EXE

	// engine options
	double transitionSpeed;
	uint msgSkipDelay;
	bool noWarpMouse;
	bool mapNoWallslide;

	Config();
};

// Never destroyed: its Strings would release the string pool mutex during
// exit handlers, when g_system (which creates that mutex) is already gone.
extern Config &g_config;

/** Load the first INI file found from AI5ENG.INI, AI5WIN.INI, syuusaku.ini and aiwin.ini. */
bool loadConfig();

} // End of namespace AI5

#endif // AI5_CONFIG_H
