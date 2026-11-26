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
 */

#include "common/formats/utf_table.h"

#include "common/endian.h"

namespace Common {

namespace {

const uint32 kHeaderSize = 0x20;
// Offsets in the header are counted from this byte, not from the magic.
const uint32 kOffsetBase = 8;

// Storage bits, high nibble of a column descriptor.
const byte kStorageNamed = 0x10;
const byte kStorageInSchema = 0x20;
const byte kStorageInRow = 0x40;
const byte kStorageUndefined = 0x80;

enum {
	kTypeString = 0xA,
	kTypeVLData = 0xB,
	kTypeWide = 0xC,
	kTypeCount = 0xD
};

// Byte width of each type code, 0x0 to 0xC.
const byte kTypeWidth[kTypeCount] = { 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 4, 8, 16 };

const byte kPlainMagic[4] = { 0x40, 0x55, 0x54, 0x46 };
const byte kMaskedMagic[4] = { 0x1F, 0x9E, 0xF3, 0xF5 };

const byte kMaskSeed = 0x5F;
const byte kMaskStep = 0x15;

/**
 * Reads the NUL-terminated string at `rel` bytes into [strings, dataStart).
 * The string stops at dataStart if no terminator comes first.
 */
bool readString(const byte *table, uint32 strings, uint32 dataStart, uint32 rel, Common::String &out) {
	if (rel > dataStart - strings)
		return false;
	const byte *p = table + strings + rel;
	const byte *end = table + dataStart;
	const byte *q = p;
	while (q < end && *q != 0)
		++q;
	out = Common::String((const char *)p, (uint32)(q - p));
	return true;
}

} // End of anonymous namespace

void UTFTable::clear() {
	_bytes.clear();
	_columns.clear();
	_rowsStart = _stringsStart = _dataStart = 0;
	_rowWidth = _rowCount = 0;
}

bool UTFTable::parse(const byte *buf, uint32 size) {
	clear();
	if (!buf || size < kHeaderSize || memcmp(buf, kPlainMagic, 4) != 0)
		return false;

	// Real tables declare their exact length; some writers overstate it, so
	// never trust it beyond the buffer.
	const uint64 declared = (uint64)READ_BE_UINT32(buf + 4) + kOffsetBase;
	const uint64 extent = declared <= size ? declared : size;

	// TODO: 0x08 is read as a version and ignored; only version 0 has been
	// seen on real data, version 1 tables are untested.
	const uint64 rows = (uint64)READ_BE_UINT16(buf + 0x0A) + kOffsetBase;
	const uint64 strings = (uint64)READ_BE_UINT32(buf + 0x0C) + kOffsetBase;
	const uint64 data = (uint64)READ_BE_UINT32(buf + 0x10) + kOffsetBase;
	if (rows < kHeaderSize || rows > strings || strings > data || data > extent)
		return false;

	const uint16 columnCount = READ_BE_UINT16(buf + 0x18);
	const uint16 rowWidth = READ_BE_UINT16(buf + 0x1A);
	const uint32 rowCount = READ_BE_UINT32(buf + 0x1C);
	if (rowCount > 0x7FFFFFFF)
		return false;
	if ((uint64)rowCount * rowWidth > strings - rows)
		return false;
	// Zero-width rows cost nothing to claim; cap them so callers can't be
	// made to loop billions of times over a tiny table.
	if (rowWidth == 0 && rowCount > extent)
		return false;

	const uint32 R = (uint32)rows;
	const uint32 S = (uint32)strings;
	const uint32 D = (uint32)data;

	Common::Array<Column> columns;
	columns.reserve(columnCount);
	uint32 cursor = kHeaderSize;
	uint32 rowOffset = 0;
	for (uint16 i = 0; i < columnCount; ++i) {
		if ((uint64)cursor + 5 > R)
			return false;
		const byte desc = buf[cursor];
		const uint32 nameOffset = READ_BE_UINT32(buf + cursor + 1);
		cursor += 5;

		Column col;
		col.flags = desc & 0xF0;
		col.type = desc & 0x0F;
		col.offset = 0;
		if (!(col.flags & kStorageNamed) || (col.flags & kStorageUndefined) || col.type >= kTypeCount) {
			return false;
		}
		if (!readString(buf, S, D, nameOffset, col.name)) {
			return false;
		}

		const uint32 width = kTypeWidth[col.type];
		if (col.flags & kStorageInSchema) {
			// TODO: Descriptors with both the constant and per-row bits are
			// treated as constant; no real table with them has been seen.
			if ((uint64)cursor + width > R) {
				return false;
			}
			col.offset = cursor;
			cursor += width;
		} else if (col.flags & kStorageInRow) {
			col.offset = rowOffset;
			rowOffset += width;
		}
		columns.push_back(col);
	}

	_bytes.resize((uint32)extent);
	memcpy(_bytes.data(), buf, (uint32)extent);
	_columns = columns;
	_rowsStart = R;
	_stringsStart = S;
	_dataStart = D;
	_rowWidth = rowWidth;
	_rowCount = rowCount;
	return true;
}

UTFValue UTFTable::get(int row, const char *name) const {
	UTFValue v;
	if (row < 0 || (uint32)row >= _rowCount || !name)
		return v;

	const Column *col = nullptr;
	for (uint i = 0; i < _columns.size(); ++i) {
		if (_columns[i].name == name) {
			col = &_columns[i];
			break;
		}
	}
	if (!col)
		return v;

	v.type = col->type;
	const uint32 width = kTypeWidth[col->type];
	uint32 pos;
	if (col->flags & kStorageInSchema) {
		pos = col->offset;
	} else if (col->flags & kStorageInRow) {
		const uint64 cell = (uint64)_rowsStart + (uint64)row * _rowWidth + col->offset;
		if (cell + width > _stringsStart)
			return v;
		pos = (uint32)cell;
	} else {
		return v;
	}
	return decode(col->type, pos);
}

UTFValue UTFTable::decode(byte type, uint32 pos) const {
	UTFValue v;
	v.type = type;
	const byte *p = _bytes.data() + pos;

	switch (type) {
	case kTypeString:
		v.valid = readString(_bytes.data(), _stringsStart, _dataStart, READ_BE_UINT32(p), v.str);
		break;
	case kTypeVLData:
		v.dataOffset = READ_BE_UINT32(p);
		v.dataLength = READ_BE_UINT32(p + 4);
		v.valid = true;
		break;
	case kTypeWide:
		// No caller needs 128-bit values; report them as unreadable.
		break;
	default:
		// Integers and floats: raw bits, zero-extended, no sign extension.
		switch (kTypeWidth[type]) {
		case 1:
			v.u = p[0];
			break;
		case 2:
			v.u = READ_BE_UINT16(p);
			break;
		case 4:
			v.u = READ_BE_UINT32(p);
			break;
		default:
			v.u = READ_BE_UINT64(p);
			break;
		}
		v.valid = true;
		break;
	}
	return v;
}

const byte *UTFTable::dataSegment() const {
	return _bytes.empty() ? nullptr : _bytes.data() + _dataStart;
}

uint32 UTFTable::dataSegmentSize() const {
	return _bytes.empty() ? 0 : _bytes.size() - _dataStart;
}

bool UTFTable::isMasked(const byte *buf, uint32 size) {
	return buf && size >= 4 && memcmp(buf, kMaskedMagic, 4) == 0;
}

// TODO: The mask scheme is taken from another tool's description and has
// not been checked against a real masked archive.
void UTFTable::toggleMask(byte *buf, uint32 size) {
	byte key = kMaskSeed;
	for (uint32 i = 0; i < size; ++i) {
		buf[i] ^= key;
		key = (byte)(key * kMaskStep);
	}
}

} // End of namespace Common
