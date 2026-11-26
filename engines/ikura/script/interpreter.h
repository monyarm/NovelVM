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

#ifndef IKURA_SCRIPT_INTERPRETER_H
#define IKURA_SCRIPT_INTERPRETER_H

#include "ikura/script/context.h"

namespace Ikura::VM {

enum class StepResult {
	kOk,                  // opcode executed; call step() again to continue
	kEnd,                 // hit the end-of-script opcode (IOP_ED)
	kScriptExhausted,      // ran out of instructions without hitting IOP_ED
	kMalformedOpcode,      // payload length didn't match what the opcode expects
	kUnimplementedOpcode,  // a recognized opcode with no handler yet

	// Three different reasons execution paused, needing three different
	// resume policies from the caller:
	kWaitingForAdvance,    // e.g. IOP_PM after printing text, IOP_CLK: genuinely blocked until a player-input signal arrives. The caller must gate on that signal itself (e.g. consume Input's advance/cancel edge) before calling step()/run() again - calling again unconditionally would race straight past the pause.
	kYielded,              // e.g. IOP_IG finding nothing new: the *script's own bytecode* re-checks this via a jump-back loop, so the caller should just call step()/run() again next frame unconditionally - the opcode that yielded will itself re-read live input state (e.g. consume Input's advance/cancel edge) when the script loops back into it.
	kWaitingForTimer,      // IOP_GO/IOP_AWAIT: blocked until Context::waitDeadline() (a wall-clock tick) passes, OR a player-input advance signal arrives, whichever first (VileVN reference: EventGameTick's IS_WAITTIMER handling - keyctrl() skips the wait early same as it timing out). The caller must gate on (now >= ctx.waitDeadline() || advance signal) before calling step()/run() again.
};

// Executes a single instruction against ctx and advances its script
// position. lastOpcode is always set to the opcode looked at, even on
// failure, for diagnostics.
StepResult step(Context &ctx, byte &lastOpcode);

// Calls step() until it returns something other than kOk. lastOpcode is
// set the same way step() sets it. Safe to call again after
// kWaitingForAdvance/kYielded - the script's own position tracking
// (Format::Script's position stack) already remembers exactly where to
// resume; nothing interpreter-side needs to be saved or restored around a
// pause. See the two values' own comments above for the different resume
// policy each one needs from the caller.
StepResult run(Context &ctx, byte &lastOpcode);

} // End of namespace Ikura::VM

#endif
