#ifndef IKURA_SCRIPT_CONTEXT_H
#define IKURA_SCRIPT_CONTEXT_H

#include "common/array.h"
#include "ikura/formats/script/script.h"

namespace Ikura::VM {

// VM state for one running script: its Format::Script::Script (bytecode,
// jump table, call/return position stack), plus the Ikura/GDL value model
// (VileVN reference: IParser, src/ikura/iparser.cpp) - growable
// variable/flag stores and the value encoding every opcode operand uses.
class Context {
public:
	// Takes ownership of script.
	explicit Context(Format::Script::Script *script) : _script(script) {}
	~Context() { delete _script; }

	Format::Script::Script &script() { return *_script; }

	// An operand is a 32-bit field: if bit 31 is set, the low 31 bits name
	// a variable to look up instead of being the value itself. Either way
	// the result is sign-extended from bit 30 (VileVN's own "Uint31").
	int32 decodeValue(uint32 raw) const;

	int32 getValue(uint32 position) const;
	void setValue(uint32 position, uint32 raw);

	bool getFlag(uint32 position) const;
	void setFlag(uint32 position, bool value);

	// System flags: a second, separate flag bank (VileVN reference:
	// IParser::System) for engine settings (message skip, automode, BGM
	// disable, etc.) rather than story state.
	bool getSystem(uint32 position) const;
	void setSystem(uint32 position, bool value);

private:
	Format::Script::Script *_script;
	Common::Array<uint32> _variables;
	Common::Array<bool> _flags;
	Common::Array<bool> _system;
};

} // End of namespace Ikura::VM

#endif
