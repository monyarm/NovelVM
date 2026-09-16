#ifndef IKURA_RUNTIME_INPUT_H
#define IKURA_RUNTIME_INPUT_H

#include "common/events.h"
#include "common/rect.h"
#include "common/scummsys.h"

namespace Ikura {

// Translates Common::Event into the three signals Ikura/GDL opcodes read
// (VileVN reference: EngineVN::keyok/keycancel/keyctrl, src/engine/evn.cpp).
// advance/cancel are edge-triggered latches - set by an input event, cleared
// by whichever opcode consumes them (e.g. IOP_STX cmd 0x08, IOP_CLK) - while
// skip is a held-level query (Ctrl key down) that's never consumed.
class Input {
public:
	void handleEvent(const Common::Event &event);

	bool consumeAdvance();
	bool consumeCancel();
	bool isSkipHeld() const { return _skipHeld; }

	// IOP_IG/IOP_IH's hotspot hit-testing (VileVN reference: MouseMove/
	// MouseLeftDown's X/Y args). Updated on every event that carries a
	// position - never consumed/edge-triggered, it's a live cursor
	// position, not a signal.
	Common::Point mousePosition() const { return _mousePosition; }

private:
	bool _advance = false;
	bool _cancel = false;
	bool _skipHeld = false;
	Common::Point _mousePosition;
};

} // End of namespace Ikura

#endif
