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

#include "common/events.h"
#include "common/system.h"

#include "ai5/ai5.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/vm.h"

namespace AI5 {

static uint32 g_keyDownTimestamp[kInputNrInputs];
static bool g_keyDown[kInputNrInputs];

void inputKeyEvent(InputEventType type, bool down) {
	if (type == kInputNone)
		return;
	assert(type >= 0 && type < kInputNrInputs);
	g_keyDown[type] = down;
	if (down)
		g_keyDownTimestamp[type] = g_system->getMillis();
}

static InputEventType inputEventFromKeycode(Common::KeyCode k) {
	switch (k) {
	case Common::KEYCODE_KP_ENTER:
	case Common::KEYCODE_RETURN:    return kInputActivate;
	case Common::KEYCODE_ESCAPE:    return kInputCancel;
	case Common::KEYCODE_UP:        return kInputUp;
	case Common::KEYCODE_DOWN:      return kInputDown;
	case Common::KEYCODE_LEFT:      return kInputLeft;
	case Common::KEYCODE_RIGHT:     return kInputRight;
	case Common::KEYCODE_LSHIFT:
	case Common::KEYCODE_RSHIFT:    return kInputShift;
	case Common::KEYCODE_LCTRL:
	case Common::KEYCODE_RCTRL:     return kInputCtrl;
	case Common::KEYCODE_SPACE:     return kInputSpace;
	case Common::KEYCODE_BACKSPACE: return kInputBackspace;
	case Common::KEYCODE_PAGEUP:    return kInputPageUp;
	case Common::KEYCODE_PAGEDOWN:  return kInputPageDown;
	case Common::KEYCODE_l:         return kInputL;
	case Common::KEYCODE_s:         return kInputS;
	case Common::KEYCODE_TAB:       return kInputTab;
	default:                        return kInputNone;
	}
}

void handleEvents() {
	Common::Event e;
	while (g_system->getEventManager()->pollEvent(e)) {
		if (g_game->handleEvent && g_game->handleEvent(e))
			continue;
		switch (e.type) {
		case Common::EVENT_KEYDOWN:
			inputKeyEvent(inputEventFromKeycode(e.kbd.keycode), true);
			break;
		case Common::EVENT_KEYUP:
			inputKeyEvent(inputEventFromKeycode(e.kbd.keycode), false);
			break;
		case Common::EVENT_LBUTTONDOWN:
			inputKeyEvent(kInputActivate, true);
			break;
		case Common::EVENT_LBUTTONUP:
			inputKeyEvent(kInputActivate, false);
			break;
		case Common::EVENT_RBUTTONDOWN:
			inputKeyEvent(kInputCancel, true);
			break;
		case Common::EVENT_RBUTTONUP:
			inputKeyEvent(kInputCancel, false);
			break;
		case Common::EVENT_WHEELUP:
			cursorSetDirection(kCursorDirUp);
			break;
		case Common::EVENT_WHEELDOWN:
			cursorSetDirection(kCursorDirDown);
			break;
		case Common::EVENT_SCREEN_CHANGED:
			gfxScreenDirty();
			break;
		default:
			break;
		}
	}
	cursorUpdate();
}

void vmDelay(int ms) {
	g_system->delayMillis(ms);
}

uint32 vmGetTicks() {
	return g_system->getMillis();
}

bool inputDown(InputEventType type) {
	assert(type >= 0 && type < kInputNrInputs);
	handleEvents();
	return g_keyDown[type] || g_system->getMillis() - g_keyDownTimestamp[type] < 30;
}

void inputWaitUntilUpAlways(InputEventType type) {
	while (inputDown(type) && !g_engine->shouldQuit()) {
		vmPeek();
		vmDelay(16);
	}
}

void inputWaitUntilUp(InputEventType type) {
	if (!vmFlagIsOn(kFlagWaitKeyup))
		return;
	inputWaitUntilUpAlways(type);
}

} // End of namespace AI5
