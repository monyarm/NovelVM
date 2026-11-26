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
