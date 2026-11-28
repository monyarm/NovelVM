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

#ifndef AI5_GAMES_ISAKU_H
#define AI5_GAMES_ISAKU_H

#include "common/events.h"

namespace AI5 {

struct Game;

void initGameIsaku(Game &game);

void isakuBuiltinSePlay(const char *name);
void isakuItemWindowTick();
bool isakuItemWindowEvent(const Common::Event &e);
bool isakuItemWindowIsEnabled();
void isakuItemWindowCreate();
void isakuItemWindowToggle();
bool isakuItemWindowIsOpen();
void isakuItemWindowGetPos(int &x, int &y, int &w, int &h);
void isakuItemWindowGetCursorPos(int &x, int &y);
void isakuItemWindowEnable();
void isakuItemWindowDisable();
void isakuItemWindowGetMouseState(bool &lmbDown, bool &rmbDown);
void isakuItemWindowUpdate();

} // End of namespace AI5

#endif // AI5_GAMES_ISAKU_H
