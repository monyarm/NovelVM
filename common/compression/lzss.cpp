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

#include "common/compression/lzss.h"
#include "common/bitstream.h"
#include "common/stream.h"
#include "common/substream.h"

namespace Common {

enum {
	kLZSSWindowSize = 0x1000,
	kLZSSWindowMask = kLZSSWindowSize - 1
};

void decompressLZSS(SeekableReadStream &in, Array<byte> &out, uint32 limit, uint windowStart) {
	byte window[kLZSSWindowSize] = {};
	uint dst = windowStart;

	out.clear();
	if (!limit)
		limit = 0xffffffff;
	else
		out.reserve(limit);

	while (in.pos() < in.size() && out.size() < limit) {
		byte ctl = in.readByte();
		for (uint bit = 1; bit != 0x100; bit <<= 1) {
			if (ctl & bit) {
				if (in.pos() >= in.size())
					break;
				byte b = in.readByte();
				window[dst++ & kLZSSWindowMask] = b;
				out.push_back(b);
			} else {
				if (in.size() - in.pos() < 2)
					break;
				byte lo = in.readByte();
				byte hi = in.readByte();
				uint offset = ((hi & 0xf0) << 4) | lo;
				for (int count = 3 + (hi & 0xf); count != 0; --count) {
					if (out.size() >= limit)
						break;
					byte v = window[offset++ & kLZSSWindowMask];
					window[dst++ & kLZSSWindowMask] = v;
					out.push_back(v);
				}
			}
		}
	}
}

void decompressLZSSBitwise(SeekableReadStream &in, Array<byte> &out) {
	byte window[kLZSSWindowSize] = {};
	uint dst = 1;
	SeekableSubReadStream sub(&in, in.pos(), in.size());
	BitStream8MSB bits(sub);

	out.clear();
	while (bits.size() - bits.pos() >= 9) {
		if (bits.getBit()) {
			byte c = bits.getBits<8>();
			window[dst++ & kLZSSWindowMask] = c;
			out.push_back(c);
		} else {
			if (bits.size() - bits.pos() < 12)
				break;
			uint off = bits.getBits<12>();
			if (!off || bits.size() - bits.pos() < 4)
				break;
			uint len = bits.getBits<4>() + 2;
			for (uint i = 0; i < len; i++) {
				byte c = window[off++ & kLZSSWindowMask];
				window[dst++ & kLZSSWindowMask] = c;
				out.push_back(c);
			}
		}
	}
}

} // End of namespace Common
