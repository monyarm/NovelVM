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
#include "common/str.h"

namespace Common {

// A value read from a CRI @UTF table row/column. Only one member is
// meaningful, per `type` (see UTFTable's private UTFType); `valid` is false
// for absent (kStorageNone/kStorageZero) columns.
struct UTFValue {
	bool valid = false;
	byte type = 0;
	uint64 u = 0;
	Common::String str;

	// For kTypeData (vldata) columns: offset and length into the data segment.
	uint32 dataOffset = 0;
	uint32 dataLength = 0;

	uint64 asUint(uint64 def = 0) const { return valid ? u : def; }
	Common::String asString() const { return valid ? str : Common::String(); }
};

/**
 * Parser for CRI's "@UTF" table format: a big-endian, columnar, schema-based
 * binary table. Used for CPK's header/TOC/ITOC and USM's per-stream metadata
 * chunks alike -- same binary layout in both containers. Real byte layout
 * verified against Persona 3 Portable's umd0.cpk TOC/header tables and
 * Persona 5 Royal's mov050.usm per-stream @SFV/@SFA metadata chunks.
 *
 * `buf` must point at the "@UTF" magic; the packet framing CPK and USM each
 * wrap it in (CPK: sentinel+size+optional XOR "decryption"; USM: none) is
 * the caller's responsibility.
 */
class UTFTable {
public:
	bool parse(const byte *buf, uint32 size);

	int numRows() const { return _numRows; }
	UTFValue get(int row, const char *name) const;

	// Access the raw data segment for vldata columns.
	const byte *dataSegment() const { return _packet.data() + _dataOffset; }
	uint32 dataSegmentSize() const { return (uint32)_packet.size() - _dataOffset; }

private:
	struct UTFColumn {
		byte flags = 0;
		Common::String name;
		UTFValue constantValue;
	};

	Common::Array<byte> _packet;
	Common::Array<UTFColumn> _columns;
	uint32 _rowsOffset = 0;
	uint32 _stringsOffset = 0;
	uint32 _dataOffset = 0;
	uint16 _rowLength = 0;
	int _numRows = 0;

	Common::String readCString(uint32 pos) const;
	UTFValue readTyped(uint32 &pos, byte type) const;
};

} // namespace Common

#endif
