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

} // End of namespace Ikura::VM

#endif
