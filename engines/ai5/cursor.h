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

#ifndef AI5_CURSOR_H
#define AI5_CURSOR_H

#include "common/scummsys.h"

namespace AI5 {

enum CursorDirection {
	kCursorDirNone = 0,
	kCursorDirUp = 1,
	kCursorDirDown = 2
};

/** Load the cursors from the game executable's resources. */
void cursorInit(const char *exeName);
void cursorFini();
void cursorLoad(uint no, uint nrFrames, const uint *frameTime);
void cursorUnload();
void cursorReload();
void cursorShow();
void cursorHide();
void cursorSetPos(uint x, uint y);
void cursorGetPos(uint &x, uint &y);
/** Advance the cursor animation; called while handling events. */
void cursorUpdate();
void cursorSetDirection(CursorDirection dir);
CursorDirection cursorGetDirection();

} // End of namespace AI5

#endif // AI5_CURSOR_H
