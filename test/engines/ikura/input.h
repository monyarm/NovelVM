#include "ikura/runtime/input.h"

#include <cxxtest/TestSuite.h>

class IkuraInputTestSuite : public CxxTest::TestSuite {
public:
	static Common::Event keyDown(Common::KeyCode code) {
		Common::Event event;
		event.type = Common::EVENT_KEYDOWN;
		event.kbd.keycode = code;
		return event;
	}

	static Common::Event keyUp(Common::KeyCode code) {
		Common::Event event;
		event.type = Common::EVENT_KEYUP;
		event.kbd.keycode = code;
		return event;
	}

	void test_advance_latches_on_enter_space_and_left_click() {
		Ikura::Input input;
		TS_ASSERT(!input.consumeAdvance());

		input.handleEvent(keyDown(Common::KEYCODE_RETURN));
		TS_ASSERT(input.consumeAdvance());
		TS_ASSERT(!input.consumeAdvance()); // consuming clears it

		input.handleEvent(keyDown(Common::KEYCODE_SPACE));
		TS_ASSERT(input.consumeAdvance());

		Common::Event click;
		click.type = Common::EVENT_LBUTTONDOWN;
		input.handleEvent(click);
		TS_ASSERT(input.consumeAdvance());
	}

	void test_cancel_latches_on_escape_and_right_click() {
		Ikura::Input input;
		input.handleEvent(keyDown(Common::KEYCODE_ESCAPE));
		TS_ASSERT(input.consumeCancel());
		TS_ASSERT(!input.consumeCancel());

		Common::Event click;
		click.type = Common::EVENT_RBUTTONDOWN;
		input.handleEvent(click);
		TS_ASSERT(input.consumeCancel());
	}

	void test_skip_is_a_held_level_not_a_latch() {
		Ikura::Input input;
		TS_ASSERT(!input.isSkipHeld());

		input.handleEvent(keyDown(Common::KEYCODE_LCTRL));
		TS_ASSERT(input.isSkipHeld());
		TS_ASSERT(input.isSkipHeld()); // querying doesn't consume it

		input.handleEvent(keyUp(Common::KEYCODE_LCTRL));
		TS_ASSERT(!input.isSkipHeld());
	}

	void test_unrelated_keys_are_ignored() {
		Ikura::Input input;
		input.handleEvent(keyDown(Common::KEYCODE_a));
		TS_ASSERT(!input.consumeAdvance());
		TS_ASSERT(!input.consumeCancel());
		TS_ASSERT(!input.isSkipHeld());
	}
};
