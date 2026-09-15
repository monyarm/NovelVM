#ifndef IKURA_FORMATS_SCRIPT_SCRIPT_H
#define IKURA_FORMATS_SCRIPT_SCRIPT_H

#include "common/array.h"
#include "common/stream.h"

namespace Ikura::Format::Script {

// Ikura/GDL (ISF) and DRS script container (VileVN reference: IkuraScript,
// src/ikura/iscript.cpp). Holds a decrypted instruction stream, a jump
// table, and a call/return position stack; does not execute anything -
// the opcode interpreter consumes getOpcode() separately.
//
// Both on-disk variants store the instruction stream rotate-"encrypted"
// (byte = (byte<<6 | byte>>2) & 0xFF, applied per-byte from a
// format-specific start offset); load() undoes that once, up front.
class Script {
public:
	// Detects ISF vs DRS from the stream's content and parses
	// accordingly. Returns nullptr on any malformed input.
	static Script *load(Common::SeekableReadStream &stream);

	// Extracts the next instruction at the current position and advances
	// past it. False at end of the position stack, or on any malformed
	// instruction header/length.
	bool getOpcode(byte &opcode, const byte *&data, uint32 &length);

	// Jumps to jumpTable[index]. False if index is out of range or there
	// is no current position (i.e. every call() has been matched by ret()
	// and then ret() was called once more).
	bool jump(uint16 index);

	// Same as jump(), but first pushes the current position so ret() can
	// resume after the call.
	bool call(uint16 index);

	// Pops the current position. False if the stack was already empty.
	// Popping the last position (net effect: nothing left to resume) is
	// not an error - the reference itself allows an unmatched ret() to
	// end script execution this way (see script.cpp).
	bool ret();

	// True once the position stack has been emptied via ret() (or was
	// never non-empty, i.e. load() itself failed).
	bool atEnd() const { return _positions.empty(); }

private:
	Script() = default;

	bool loadISF(const Common::Array<byte> &raw);
	bool loadDRS(const Common::Array<byte> &raw);

	Common::Array<byte> _buffer;
	Common::Array<uint32> _jumpTable;
	Common::Array<uint32> _positions;
	uint32 _scriptStart = 0;
	uint16 _revision = 0;
	byte _xorKey = 0; // parsed for completeness; the reference stores it but never reads it back
};

} // End of namespace Ikura::Format::Script

#endif
