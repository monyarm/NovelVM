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
 * Based on VileVN, Copyright (C) ViLE Team (GPL-3.0).
 *
 */

#include "ikura/runtime/input.h"

namespace Ikura {

void Input::handleEvent(const Common::Event &event) {
	if (event.type == Common::EVENT_MOUSEMOVE || event.type == Common::EVENT_LBUTTONDOWN || event.type == Common::EVENT_RBUTTONDOWN)
		_mousePosition = event.mouse;

	switch (event.type) {
	case Common::EVENT_KEYDOWN:
		switch (event.kbd.keycode) {
		case Common::KEYCODE_RETURN:
		case Common::KEYCODE_KP_ENTER:
		case Common::KEYCODE_SPACE:
			_advance = true;
			break;
		case Common::KEYCODE_ESCAPE:
			_cancel = true;
			break;
		case Common::KEYCODE_LCTRL:
		case Common::KEYCODE_RCTRL:
			_skipHeld = true;
			break;
		default:
			break;
		}
		break;

	case Common::EVENT_KEYUP:
		switch (event.kbd.keycode) {
		case Common::KEYCODE_LCTRL:
		case Common::KEYCODE_RCTRL:
			_skipHeld = false;
			break;
		default:
			break;
		}
		break;

	case Common::EVENT_LBUTTONDOWN:
		_advance = true;
		break;

	case Common::EVENT_RBUTTONDOWN:
		_cancel = true;
		break;

	default:
		break;
	}
}

bool Input::consumeAdvance() {
	bool value = _advance;
	_advance = false;
	return value;
}

bool Input::consumeCancel() {
	bool value = _cancel;
	_cancel = false;
	return value;
}

} // End of namespace Ikura
