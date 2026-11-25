/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
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
 */

#ifndef COMMON_FORMATS_UTF_TABLE_H
#define COMMON_FORMATS_UTF_TABLE_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Common {

/**
 * One cell read from a UTFTable. Integers and floats come back as raw,
 * zero-extended bits in `u`; strings in `str`; vldata as an offset/length
 * pair relative to UTFTable::dataSegment().
 */
struct UTFValue {
	bool valid = false;
	byte type = 0;
	uint64 u = 0;
	Common::String str;
	uint32 dataOffset = 0;
	uint32 dataLength = 0;
	uint64 asUint(uint64 def = 0) const { return valid ? u : def; }
	Common::String asString() const { return valid ? str : Common::String(); }
};

/**
 * Read-only parser for CRI "@UTF" binary tables, as found in CPK archives,
 * ACB cue sheets and USM movies. parse() copies the table, so the source
 * buffer may be freed afterwards.
 */
class UTFTable {
public:
	/** Parses a plain (unmasked) table. On failure the object is left empty. */
	bool parse(const byte *buf, uint32 size);
	int numRows() const { return (int)_rowCount; }
	/** Reads the first column named `name` in row `row`. */
	UTFValue get(int row, const char *name) const;
	const byte *dataSegment() const;
	uint32 dataSegmentSize() const;

	/** True if `buf` starts with a masked "@UTF" magic. */
	static bool isMasked(const byte *buf, uint32 size);
	/** XORs `buf` with the CPK table key stream; applying it twice is a no-op. */
	static void toggleMask(byte *buf, uint32 size);

private:
	struct Column {
		Common::String name;
		byte flags;    // storage bits (high nibble)
		byte type;     // type code (low nibble)
		uint32 offset; // constant: table offset; per-row: offset inside a row
	};

	void clear();
	UTFValue decode(byte type, uint32 pos) const;

	Common::Array<byte> _bytes;
	Common::Array<Column> _columns;
	uint32 _rowsStart = 0;
	uint32 _stringsStart = 0;
	uint32 _dataStart = 0;
	uint32 _rowWidth = 0;
	uint32 _rowCount = 0;
};

} // End of namespace Common

#endif
