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

#ifndef AI5_DUNGEON_H
#define AI5_DUNGEON_H

#include "common/scummsys.h"

namespace AI5 {

enum DungeonDirection {
	DUNGEON_EAST  = 0,
	DUNGEON_NORTH = 1,
	DUNGEON_WEST  = 2,
	DUNGEON_SOUTH = 3
};

enum DungeonMoveCommand {
	DUNGEON_MOVE_FORWARD  = 0,
	DUNGEON_MOVE_BACKWARD = 1,
	DUNGEON_ROTATE_LEFT   = 2,
	DUNGEON_ROTATE_RIGHT  = 3
};

enum DungeonSpeed {
	DUNGEON_FAST = 0,
	DUNGEON_NORMAL = 1,
	DUNGEON_SLOW = 2
};

void dungeonLoad(byte *mp3, byte *kabe1, byte *kabe2, byte *kabe3, byte *kabePal, byte *dunA6);
void dungeonSetPos(unsigned x, unsigned y, DungeonDirection dir);
void dungeonGetPos(uint16 *x, uint16 *y, uint16 *dir);
uint16 dungeonMove(DungeonMoveCommand cmd);
void dungeonDraw();
void dungeonSetMoveSpeed(DungeonSpeed speed);
DungeonSpeed dungeonGetMoveSpeed();

} // End of namespace AI5

#endif // AI5_DUNGEON_H
