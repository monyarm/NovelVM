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
};

// Executes a single instruction against ctx and advances its script
// position. lastOpcode is always set to the opcode looked at, even on
// failure, for diagnostics.
StepResult step(Context &ctx, byte &lastOpcode);

// Calls step() until it returns something other than kOk (every
// implemented opcode runs to completion in one step() call - there's no
// "waiting for input" state yet, so a run just executes straight through
// to the first stopping condition). lastOpcode is set the same way step()
// sets it.
StepResult run(Context &ctx, byte &lastOpcode);

} // End of namespace Ikura::VM

#endif
