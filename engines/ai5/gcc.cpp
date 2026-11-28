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
 * Based on ai5-sdl2/libai5, Copyright (C) 2024 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

// Adapted from GARbro
//
// Copyright (C) 2015 by morkt
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//

#include "common/compression/lzss.h"
#include "common/bitstream.h"
#include "common/endian.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/textconsole.h"

#include "ai5/cg.h"

namespace AI5 {

namespace {

class ByteReader {
public:
	ByteReader(const byte *buf, uint32 size, uint32 pos) : _buf(buf), _size(size), _pos(pos) {}
	byte readByte() { return _pos < _size ? _buf[_pos++] : 0; }

private:
	const byte *_buf;
	uint32 _size;
	uint32 _pos;
};

void decodeChunk(Common::Array<byte> &out, const byte *chunk, int chunkSize) {
	static uint16 chunkNextIndex[0x10000];

	// count of each byte value in chunk
	uint16 byteCount[256] = {};
	for (int i = 0; i < chunkSize; i++)
		++byteCount[chunk[2 + i]];

	// number of bytes less than i, for each i
	uint16 bytesLt[256];
	for (uint16 i = 0, count = 0; i < 256; i++) {
		bytesLt[i] = count;
		count += byteCount[i];
		byteCount[i] = 0;
	}

	for (int i = 0; i < chunkSize; i++) {
		int b = chunk[2 + i];
		int r = byteCount[b] + bytesLt[b];
		chunkNextIndex[r] = (uint16)i;
		byteCount[b]++;
	}

	int chunkI = chunkNextIndex[READ_LE_UINT16(chunk)];
	for (int i = 0; i < chunkSize; i++) {
		out.push_back(chunk[2 + chunkI]);
		chunkI = chunkNextIndex[chunkI];
	}
}

int readCount(Common::BitStream8LSB &b) {
	int result = 1;
	int bitCount = 0;
	while (!b.getBit())
		bitCount++;
	for (; bitCount > 0; bitCount--) {
		result <<= 1;
		if (b.getBit())
			result |= 1;
	}
	return result;
}

int readDelta(Common::BitStream8LSB &control, ByteReader &in, int8 prevB) {
	if (control.getBit()) {
		int n = readCount(control);
		if (control.getBit())
			return (prevB - n) & 0xff;
		return (prevB + n) & 0xff;
	}
	return in.readByte();
}

void readCompressedChunk(Common::BitStream8LSB &control, ByteReader &in, Common::Array<byte> &out, byte *chunk, int chunkSize) {
	byte bufA[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	byte bufB[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

	int chunkI = 0;
	int8 prevB = -1;
	while (chunkI < chunkSize + 2) {
		int b;
		byte bufBI;
		if (!control.getBit()) {
			if (control.getBit()) {
				bufBI = readCount(control) & 0xf;
				b = bufB[bufBI];
				chunk[chunkI++] = b;
			} else {
				b = readDelta(control, in, prevB);
				chunk[chunkI++] = b;
				for (bufBI = 0; bufBI < 0xf && bufB[bufBI] != b; bufBI++)
					;
			}
		} else {
			int bufAI;
			int count = readCount(control);
			if (control.getBit()) {
				bufAI = 0;
				b = bufA[0];
			} else if (control.getBit()) {
				bufAI = readCount(control) & 0xf;
				b = bufA[bufAI];
			} else {
				b = readDelta(control, in, prevB);
				for (bufAI = 0; bufAI < 0xf && bufA[bufAI] != b; bufAI++)
					;
			}
			if (bufAI != 0) {
				for (int i = bufAI & 0xf; i > 0; --i)
					bufA[i] = bufA[i - 1];
				bufA[0] = b;
			}
			for (int n = 0; n < count && chunkI < 0x10001; n++)
				chunk[chunkI++] = b;

			for (bufBI = 0; bufBI < 0xf && bufB[bufBI] != b; bufBI++)
				;
		}
		if (bufBI) {
			for (int i = bufBI & 0xf; i != 0; --i)
				bufB[i] = bufB[i - 1];
			bufB[0] = b;
		}
		prevB = b;
	}

	decodeChunk(out, chunk, chunkSize);
}

void readRawChunk(Common::BitStream8LSB &control, ByteReader &in, Common::Array<byte> &out, int chunkSize) {
	int n = 0;
	while (n < chunkSize) {
		if (!control.getBit()) {
			// single pixel
			out.push_back(in.readByte());
			out.push_back(in.readByte());
			out.push_back(in.readByte());
			n += 3;
		} else {
			// RLE
			int count = readCount(control);
			byte b = in.readByte();
			byte g = in.readByte();
			byte r = in.readByte();
			for (int i = 0; i < count; ++i) {
				out.push_back(b);
				out.push_back(g);
				out.push_back(r);
			}
			n += 3 * count;
		}
	}
}

void altUnpack(const byte *data, uint32 size, uint32 offset, uint32 total, Common::Array<byte> &out) {
	static byte chunk[0x10001];
	memset(chunk, 0, sizeof(chunk));

	Common::MemoryReadStream controlStream(data + offset, size - offset);
	Common::BitStream8LSB control(controlStream);
	ByteReader in(data, size, offset + READ_LE_UINT32(data + 0x10));

	out.clear();
	out.reserve(total);
	uint32 dst = 0;
	while (dst < total) {
		int chunkSize = MIN<uint32>(total - dst, 0xffff);
		if (control.getBit())
			readCompressedChunk(control, in, out, chunk, chunkSize);
		else
			readRawChunk(control, in, out, chunkSize);
		dst += chunkSize;
	}
	out.resize(total);
}

void lzssUnpack(const byte *data, uint32 size, uint32 offset, uint32 total, Common::Array<byte> &out) {
	Common::MemoryReadStream in(data + offset, size - offset);
	Common::decompressLZSS(in, out, total);
	if (out.size() != total) {
		warning("decodeGcc: unexpected unpacked size: %u (expected %u)", out.size(), total);
		out.resize(total);
	}
}

void unpackAlpha(const byte *data, uint32 size, Common::Array<byte> &alpha, uint &w, uint &h) {
	uint32 controlOffset = 0x20 + READ_LE_UINT32(data + 0x0c);
	Common::MemoryReadStream controlStream(data + controlOffset, size - controlOffset);
	Common::BitStream8LSB control(controlStream);

	w = READ_LE_UINT16(data + 0x18);
	h = READ_LE_UINT16(data + 0x1a);
	uint total = w * h;
	alpha.clear();
	alpha.resize(total);

	ByteReader in(data, size, controlOffset + READ_LE_UINT32(data + 0x1c));
	uint dst = 0;
	while (dst < total) {
		if (control.getBit()) {
			// RLE
			int count = readCount(control);
			byte v = in.readByte();
			for (int i = 0; i < count && dst < total; ++i)
				alpha[dst++] = v;
		} else {
			alpha[dst++] = in.readByte();
		}
	}
}

} // End of anonymous namespace

CG *decodeGcc(const byte *data, uint32 size) {
	if (size < 0x20)
		return nullptr;

	Common::ScopedPtr<CG> cg(new CG());
	cg->x = READ_LE_UINT16(data + 4);
	cg->y = READ_LE_UINT16(data + 6);
	cg->w = READ_LE_UINT16(data + 8);
	cg->h = READ_LE_UINT16(data + 10);
	cg->hasAlpha = data[3] == 'm';
	cg->bpp = cg->hasAlpha ? 32 : 24;

	const uint w = cg->w, h = cg->h;
	Common::Array<byte> color, alpha;
	uint alphaW = 0, alphaH = 0;
	switch (READ_LE_UINT32(data)) {
	case MKTAG('n', '4', '2', 'G'): // G24n
		lzssUnpack(data, size, 0x14, w * h * 3, color);
		break;
	case MKTAG('m', '4', '2', 'G'): // G24m
		lzssUnpack(data, size, 0x20, w * h * 3, color);
		unpackAlpha(data, size, alpha, alphaW, alphaH);
		break;
	case MKTAG('n', '4', '2', 'R'): // R24n
		altUnpack(data, size, 0x14, w * h * 3, color);
		break;
	case MKTAG('m', '4', '2', 'R'): // R24m
		altUnpack(data, size, 0x20, w * h * 3, color);
		unpackAlpha(data, size, alpha, alphaW, alphaH);
		break;
	default:
		warning("decodeGcc: unsupported image type");
		return nullptr;
	}

	cg->pixels.resize(w * h * 4);
	for (uint row = 0; row < h; row++) {
		const byte *src = color.data() + row * w * 3;
		byte *dst = cg->pixels.data() + (h - (row + 1)) * w * 4;
		for (uint col = 0; col < w; col++, src += 3, dst += 4) {
			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];
			dst[3] = 255;
		}
	}

	if (!alpha.empty()) {
		// the mask is full size (including x/y offsets)
		if (cg->x + w > alphaW) {
			warning("decodeGcc: alpha width is too small");
		} else if (cg->y + h > alphaH) {
			warning("decodeGcc: alpha height is too small");
		} else {
			for (uint row = 0; row < h; row++) {
				const byte *src = alpha.data() + cg->x + row * alphaW;
				byte *dst = cg->pixels.data() + (h - (row + 1)) * w * 4;
				for (uint col = 0; col < w; col++, src++, dst += 4)
					dst[3] = *src;
			}
		}
	}
	return cg.release();
}

} // End of namespace AI5
