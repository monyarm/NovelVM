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

#ifndef AI5_INPUT_H
#define AI5_INPUT_H

#include "common/scummsys.h"

namespace AI5 {

enum InputEventType {
	kInputNone = -1,
	kInputActivate = 0,
	kInputCancel = 1,
	kInputUp = 2,
	kInputDown = 3,
	kInputLeft = 4,
	kInputRight = 5,
	kInputShift = 6,
	kInputCtrl = 7,
	kInputSpace = 9,
	kInputBackspace = 10,
	kInputPageUp = 11,
	kInputPageDown = 12,
	kInputL = 13,
	kInputS = 14,
	kInputTab = 15,
	kInputNrInputs
};

void handleEvents();
void inputKeyEvent(InputEventType type, bool down);
bool inputDown(InputEventType type);
/** Wait until the input is released, regardless of FLAG_WAIT_KEYUP. */
void inputWaitUntilUpAlways(InputEventType type);
void inputWaitUntilUp(InputEventType type);

} // End of namespace AI5

#endif // AI5_INPUT_H
