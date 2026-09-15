#include "ikura/runtime/input.h"

namespace Ikura {

void Input::handleEvent(const Common::Event &event) {
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
