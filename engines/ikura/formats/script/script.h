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
