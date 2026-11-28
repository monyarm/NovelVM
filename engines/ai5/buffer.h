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
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

#ifndef AI5_BUFFER_H
#define AI5_BUFFER_H

#include "common/endian.h"
#include "common/textconsole.h"

namespace AI5 {

/**
 * Little-endian reader over a byte array. Out of bounds reads are fatal.
 */
class Buffer {
public:
	Buffer(const byte *buf, uint32 size, uint32 index = 0) : _buf(buf), _size(size), _index(index) {}

	uint32 index() const { return _index; }
	uint32 size() const { return _size; }
	bool end() const { return _index >= _size; }
	uint32 remaining() const { return _index < _size ? _size - _index : 0; }
	void seek(uint32 off) { _index = off; }

	byte peekU8() const { check(1); return _buf[_index]; }
	uint16 peekU16() const { check(2); return READ_LE_UINT16(_buf + _index); }
	uint32 peekU32() const { check(4); return READ_LE_UINT32(_buf + _index); }
	byte readU8() { byte v = peekU8(); _index += 1; return v; }
	uint16 readU16() { uint16 v = peekU16(); _index += 2; return v; }
	uint32 readU32() { uint32 v = peekU32(); _index += 4; return v; }

private:
	void check(uint32 n) const {
		if (_index + n > _size)
			error("Buffer: out of bounds read at %u (size %u)", _index, _size);
	}

	const byte *_buf;
	uint32 _size;
	uint32 _index;
};

} // End of namespace AI5

#endif // AI5_BUFFER_H
