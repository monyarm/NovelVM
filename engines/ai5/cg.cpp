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

#include "common/bitstream.h"
#include "common/compression/lzss.h"
#include "common/endian.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/textconsole.h"
#include "graphics/surface.h"
#include "image/png.h"

#include "ai5/cg.h"

namespace AI5 {

static void lzssDecompress(const byte *data, uint32 size, Common::Array<byte> &out) {
	Common::MemoryReadStream in(data, size);
	Common::decompressLZSS(in, out);
}

/** Count zero bits (up to limit), consuming the terminating one bit. */
static uint readZeros(Common::BitStream8MSB &b, uint limit) {
	uint n = 0;
	while (n < limit && !b.getBit())
		n++;
	return n;
}

/** Count one bits (up to limit), consuming the terminating zero bit. */
static uint readOnes(Common::BitStream8MSB &b, uint limit) {
	uint n = 0;
	while (n < limit && b.getBit())
		n++;
	return n;
}

bool cgTypeFromName(const Common::String &name, CGType &type) {
	static const struct {
		const char *ext;
		CGType type;
	} types[] = {
		{ ".akb", kCGAkb }, { ".gp4", kCGGp4 }, { ".gp8", kCGGp8 },
		{ ".g16", kCGG16 }, { ".g24", kCGG24 }, { ".g32", kCGG32 },
		{ ".gcc", kCGGcc }, { ".gpx", kCGGpx }, { ".gpr", kCGGpr },
		{ ".png", kCGPng }
	};
	for (uint i = 0; i < ARRAYSIZE(types); i++) {
		if (name.hasSuffixIgnoreCase(types[i].ext)) {
			type = types[i].type;
			return true;
		}
	}
	return false;
}

CG *loadCG(const byte *data, uint32 size, CGType type) {
	switch (type) {
	case kCGAkb: return decodeAkb(data, size);
	case kCGGp4: return decodeGp4(data, size);
	case kCGGp8: return decodeGp8(data, size);
	case kCGG16: return decodeGxx(data, size, 16);
	case kCGG24: return decodeGxx(data, size, 24);
	case kCGG32: return decodeGxx(data, size, 32);
	case kCGGcc: return decodeGcc(data, size);
	case kCGGpx: return decodeGpx(data, size);
	case kCGGpr: return decodeGpr(data, size);
	case kCGPng: return decodePng(data, size);
	default:
		break;
	}
	error("loadCG: invalid CG type %d", type);
}

// AKB

enum {
	kAkbNoAlpha = 0x40000000
};

CG *decodeAkb(const byte *data, uint32 size) {
	if (size < 32 || memcmp(data, "AKB ", 4))
		return nullptr;

	Common::ScopedPtr<CG> cg(new CG());
	cg->x = READ_LE_UINT32(data + 16);
	cg->y = READ_LE_UINT32(data + 20);
	cg->w = READ_LE_UINT16(data + 24) - cg->x;
	cg->h = READ_LE_UINT16(data + 28) - cg->y;
	uint32 flags = READ_LE_UINT32(data + 8);

	if (cg->w == 0 || cg->h == 0) {
		warning("decodeAkb: CG dimension is 0: %ux%u", cg->w, cg->h);
		return nullptr;
	}

	Common::Array<byte> decomp;
	lzssDecompress(data + 32, size - 32, decomp);
	const uint w = cg->w, h = cg->h;
	cg->pixels.resize(w * h * 4);
	if (flags & kAkbNoAlpha) {
		// RGB
		if (decomp.size() < w * h * 3) {
			warning("decodeAkb: unexpected decompressed size: %u (expected %u)", decomp.size(), w * h * 3);
			return nullptr;
		}
		for (int srcRow = 0, row = h - 1; row >= 0; srcRow++, row--) {
			const byte *src = decomp.data() + srcRow * w * 3;
			byte *dst = cg->pixels.data() + row * w * 4;
			for (uint col = 0; col < w; col++, src += 3, dst += 4) {
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
				dst[3] = 0; // alpha is corrected in the color decode step
			}
		}
	} else {
		// RGBA
		if (decomp.size() < w * h * 4) {
			warning("decodeAkb: unexpected decompressed size: %u (expected %u)", decomp.size(), w * h * 4);
			return nullptr;
		}
		for (int srcRow = 0, row = h - 1; row >= 0; srcRow++, row--) {
			const byte *src = decomp.data() + srcRow * w * 4;
			byte *dst = cg->pixels.data() + row * w * 4;
			for (uint col = 0; col < w; col++, src += 4, dst += 4) {
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
				dst[3] = src[3];
			}
		}
	}

	// decode colors
	byte *px = cg->pixels.data();
	if (flags & kAkbNoAlpha)
		px[3] = 255;
	for (uint col = 1; col < w; col++) {
		uint prev = col - 1;
		px[col * 4 + 0] += px[prev * 4 + 0];
		px[col * 4 + 1] += px[prev * 4 + 1];
		px[col * 4 + 2] += px[prev * 4 + 2];
		if (flags & kAkbNoAlpha)
			px[col * 4 + 3] = 255;
		else
			px[col * 4 + 3] += px[prev * 4 + 3];
	}
	for (uint row = 1; row < h; row++) {
		const byte *prev = px + (row - 1) * w * 4;
		byte *dst = px + row * w * 4;
		for (uint col = 0; col < w * 4; col++)
			dst[col] += prev[col];
	}
	return cg.release();
}

// GP4 (16-color)

namespace {

class Gp4Bitstream {
public:
	Gp4Bitstream(const byte *data, uint32 size, uint32 pos) : _data(data), _size(size), _byteIndex(pos), _bitIndex(7), _warned(false) {}

	byte getBit() {
		if (_byteIndex >= _size) {
			if (!_warned) {
				warning("decodeGp4: attempted to read beyond end of file");
				_warned = true;
			}
			return 0;
		}
		byte bit = (_data[_byteIndex] >> _bitIndex) & 1;
		if (_bitIndex == 0) {
			_bitIndex = 7;
			_byteIndex++;
		} else {
			_bitIndex--;
		}
		return bit;
	}

	uint16 getBits(byte n) {
		uint16 bits = 0;
		for (int i = 0; i < n; i++)
			bits = (bits << 1) | getBit();
		return bits;
	}

private:
	const byte *_data;
	uint32 _size;
	uint32 _byteIndex;
	byte _bitIndex;
	bool _warned;
};

enum {
	kGp4Colors = 16
};

void gp4WritePx(CG &cg, uint x, uint y, byte color) {
	if (x >= cg.w || y >= cg.h) {
		warning("decodeGp4: write to invalid pixel %u,%u", x, y);
		return;
	}
	cg.pixels[y * cg.w + x] = color;
}

byte gp4GetPx(CG &cg, uint x, uint y) {
	if (x >= cg.w || y >= cg.h) {
		warning("decodeGp4: read from invalid pixel %u,%u", x, y);
		return 0;
	}
	return cg.pixels[y * cg.w + x];
}

void gp4DecodeRlePos(Gp4Bitstream &b, int &x, int &y) {
	int hori, vert;
	if (b.getBit() == 0) {
		hori = 1;
		vert = (int)b.getBits(4) - 8;
	} else if (b.getBit() == 0) {
		hori = 0;
		vert = (int)b.getBits(3) - 8;
		if (vert <= -7) {
			if (vert == -7)
				vert = 0;
			vert = vert - 8;
		}
	} else {
		hori = 1;
		do {
			hori++;
		} while (b.getBit() == 1);
		vert = (int)b.getBits(4) - 8;
	}
	x = x - hori * 4;
	y = y + vert;
}

uint16 gp4DecodeRleLength(Gp4Bitstream &b) {
	uint16 length;
	if (b.getBit() == 0) {
		length = b.getBit() + 2;
	} else if (b.getBit() == 0) {
		length = b.getBits(2) + 4;
	} else if (b.getBit() == 0) {
		length = b.getBits(3) + 8;
	} else {
		length = b.getBits(6) + 16;
		if (length >= 79)
			length = b.getBits(10) + 79;
	}
	return length;
}

void gp4Decode(Gp4Bitstream &b, uint dstX, byte table[kGp4Colors + 1][kGp4Colors], CG &cg) {
	uint dstY = 0;
	int tableIndex = kGp4Colors;

	while (dstY < cg.h) {
		if (b.getBit() == 0) {
			// pixels
			for (uint x = 0; x < 4; x++) {
				byte color = table[tableIndex][0];
				int colorIndex = 0;
				while (b.getBit() == 1) {
					colorIndex++;
					table[tableIndex][0] = table[tableIndex][colorIndex];
					table[tableIndex][colorIndex] = color;
					color = table[tableIndex][0];
				}
				tableIndex = color;
				gp4WritePx(cg, dstX + x, dstY, color);
			}
			dstY++;
		} else {
			// copy of earlier pixels
			int replicaX = dstX, replicaY = dstY;
			gp4DecodeRlePos(b, replicaX, replicaY);
			uint16 length = gp4DecodeRleLength(b);
			for (uint16 y = 0; y < length; y++, dstY++) {
				for (uint16 x = 0; x < 4; x++)
					gp4WritePx(cg, dstX + x, dstY, gp4GetPx(cg, replicaX + x, replicaY + y));
			}
		}
	}
}

} // End of anonymous namespace

CG *decodeGp4(const byte *data, uint32 size) {
	if (size < 40)
		return nullptr;

	Common::ScopedPtr<CG> cg(new CG());
	cg->x = READ_BE_UINT16(data + 0);
	cg->y = READ_BE_UINT16(data + 2);
	cg->w = READ_BE_UINT16(data + 4) + 1;
	cg->h = READ_BE_UINT16(data + 6) + 1;
	cg->bpp = 8;
	cg->pixels.resize(cg->w * cg->h);
	cg->palette.resize(256 * 4);

	byte table[kGp4Colors + 1][kGp4Colors];
	for (int i = 0; i < kGp4Colors + 1; i++) {
		for (int j = 0; j < kGp4Colors; j++)
			table[i][j] = (i + j) & 0xf;
	}

	// palette
	for (int i = 0; i < kGp4Colors; i++) {
		uint16 c = READ_BE_UINT16(data + 8 + i * 2);
		byte g = c >> 12 & 0xf;
		byte r = c >> 7 & 0xf;
		byte b = c >> 2 & 0xf;
		cg->palette[i * 4 + 0] = b | b << 4;
		cg->palette[i * 4 + 1] = g | g << 4;
		cg->palette[i * 4 + 2] = r | r << 4;
		cg->palette[i * 4 + 3] = 0;
	}

	Gp4Bitstream b(data, size, 40);
	uint dstX = 0;
	for (uint x = 0; x < cg->w / 4; x++, dstX += 4)
		gp4Decode(b, dstX, table, *cg);

	return cg.release();
}

// GP8 (256-color)

CG *decodeGp8(const byte *data, uint32 size) {
	const uint32 pos = 8 + 256 * 4;
	if (size < pos)
		return nullptr;

	Common::ScopedPtr<CG> cg(new CG());
	cg->x = READ_LE_UINT16(data + 0);
	cg->y = READ_LE_UINT16(data + 2);
	cg->w = READ_LE_UINT16(data + 4);
	cg->h = READ_LE_UINT16(data + 6);
	cg->bpp = 8;

	uint16 stride = cg->w;
	if (stride & 3)
		stride = (stride & 0xfffc) + 4;

	cg->palette.resize(256 * 4);
	memcpy(cg->palette.data(), data + 8, 256 * 4);

	Common::Array<byte> px;
	lzssDecompress(data + pos, size - pos, px);
	if (px.size() != stride * cg->h) {
		warning("decodeGp8: unexpected pixel data size (expected %u; got %u)", stride * cg->h, px.size());
		if (px.size() < stride * cg->h)
			return nullptr;
	}

	cg->pixels.resize(cg->w * cg->h);
	for (uint i = 0; i < cg->h; i++)
		memcpy(cg->pixels.data() + cg->w * i, px.data() + stride * (cg->h - (i + 1)), cg->w);

	return cg.release();
}

// G16/G24/G32 (direct color)

CG *decodeGxx(const byte *data, uint32 size, uint bpp) {
	if (size < 8)
		return nullptr;

	Common::ScopedPtr<CG> cg(new CG());
	cg->x = READ_LE_UINT16(data + 0);
	cg->y = READ_LE_UINT16(data + 2);
	cg->w = READ_LE_UINT16(data + 4);
	cg->h = READ_LE_UINT16(data + 6);
	cg->bpp = bpp;

	Common::Array<byte> px;
	lzssDecompress(data + 8, size - 8, px);

	const uint stride = ((bpp / 8) * cg->w + 3) & ~3;
	if (px.size() != stride * cg->h) {
		warning("decodeGxx: unexpected size for CG: expected %u; got %u", stride * cg->h, px.size());
		return nullptr;
	}

	cg->pixels.resize(cg->w * cg->h * 4);
	byte *out = cg->pixels.data();
	for (int row = cg->h - 1; row >= 0; row--) {
		const byte *src = px.data() + stride * row;
		for (uint col = 0; col < cg->w; col++) {
			if (bpp == 16) {
				uint16 c = READ_LE_UINT16(src + col * 2);
				*out++ = (c & 0x7c00) >> 7;
				*out++ = (c & 0x03e0) >> 2;
				*out++ = (c & 0x001f) << 3;
				*out++ = 0xff;
			} else if (bpp == 24) {
				*out++ = src[col * 3 + 2];
				*out++ = src[col * 3 + 1];
				*out++ = src[col * 3 + 0];
				*out++ = 0xff;
			} else {
				*out++ = src[col * 4 + 2];
				*out++ = src[col * 4 + 1];
				*out++ = src[col * 4 + 0];
				*out++ = src[col * 4 + 3];
			}
		}
	}
	return cg.release();
}

// GPX: 256-color, compressed with a 2D variant of LZSS. Data is encoded
// either horizontally or vertically.

int gpxDecodeRunLength(Common::BitStream8MSB &b) {
	switch (readZeros(b, 5)) {
	case 0: return 0x2 + b.getBit();
	case 1: return 0x4 + b.getBits(2);
	case 2: return 0x8 + b.getBits(3);
	case 3: return 0x10 + b.getBits(6);
	case 4: return 0x50 + b.getBits(8);
	case 5: return 0x150 + b.getBits(10);
	default:
		return 0;
	}
}

void gpxDecodeOffset(Common::BitStream8MSB &b, int &xOff, int &yOff) {
	// offset when reading from the current line (always negative)
	static const int sameLineOffsets[8] = {
		-1, -2, -4, -6, -8, -12, -16, -20
	};
	// offset when reading from an earlier line
	static const int prevLineOffsets[16] = {
		-20, -16, -12, -8, -6, -4, -2, -1, 0, 1, 2, 4, 6, 8, 12, 16
	};

	if (!b.getBit()) {
		// source is more than 1 line distant
		if (!b.getBit())
			yOff = -(int)(b.getBits(2) + 4);
		else
			yOff = -(int)(b.getBit() + 2);
		xOff = prevLineOffsets[b.getBits(4)];
	} else if (b.getBit()) {
		// previous line
		yOff = -1;
		xOff = prevLineOffsets[b.getBits(4)];
	} else {
		// current line
		yOff = 0;
		xOff = sameLineOffsets[b.getBits(3)];
	}
}

CG *decodeGpx(const byte *data, uint32 size) {
	if (size < 0x2ce)
		return nullptr;

	Common::ScopedPtr<CG> cg(new CG());
	cg->x = READ_LE_UINT16(data + 0);
	cg->y = READ_LE_UINT16(data + 2);
	cg->w = READ_LE_UINT16(data + 4);
	cg->h = READ_LE_UINT16(data + 6);
	cg->bpp = 8;
	bool rotated = READ_LE_UINT16(data + 8);

	cg->palette.resize(256 * 4);
	const int c = 10; // palette starts at index 10
	for (int i = 0; i < 236; i++) {
		cg->palette[(i + c) * 4 + 2] = data[10 + i * 3 + 0];
		cg->palette[(i + c) * 4 + 1] = data[10 + i * 3 + 1];
		cg->palette[(i + c) * 4 + 0] = data[10 + i * 3 + 2];
		cg->palette[(i + c) * 4 + 3] = 255;
	}

	const int w = cg->w, h = cg->h;
	cg->pixels.resize(w * h);
	byte *px = cg->pixels.data();
	Common::MemoryReadStream stream(data + 0x2ce, size - 0x2ce);
	Common::BitStream8MSB b(stream);

	// Copies may overlap (a literal followed by a long run of it), so they
	// are done byte by byte.
	if (!rotated) {
		for (int row = 0; row < h; row++) {
			for (int col = 0; col < w;) {
				byte *dst = px + row * w + col;
				if (!b.getBit()) {
					int xOff, yOff;
					gpxDecodeOffset(b, xOff, yOff);
					const byte *src = px + (row + yOff) * w + col + xOff;
					int len = gpxDecodeRunLength(b);
					for (int i = 0; i < len; i++)
						*dst++ = *src++;
					col += len;
				} else {
					*dst = b.getBits(8);
					col++;
				}
			}
		}
	} else {
		for (int col = 0; col < w; col++) {
			for (int row = 0; row < h;) {
				byte *dst = px + row * w + col;
				if (!b.getBit()) {
					int xOff, yOff;
					gpxDecodeOffset(b, yOff, xOff);
					const byte *src = px + (row + yOff) * w + col + xOff;
					int len = gpxDecodeRunLength(b);
					for (int i = 0; i < len; i++, dst += w, src += w)
						*dst = *src;
					row += len;
				} else {
					*dst = b.getBits(8);
					row++;
				}
			}
		}
	}
	return cg.release();
}

// GPR: like GPX, but 15-bit color with an optional alpha mask.

namespace {

struct Point {
	int x, y;
};

const Point gprPointOffset[] = {
	{ -1,  0 }, { -2,  0 }, { -3,  0 }, { -4,  0 },
	{  4, -1 }, {  3, -1 }, {  2, -1 }, {  1, -1 },
	{  0, -1 }, { -1, -1 }, { -2, -1 }, { -3, -1 }, { -4, -1 },
	{  4, -2 }, {  3, -2 }, {  2, -2 }, {  1, -2 },
	{  0, -2 }, { -1, -2 }, { -2, -2 }, { -3, -2 }, { -4, -2 },
	{  4, -3 }, {  3, -3 }, {  2, -3 }, {  1, -3 },
	{  0, -3 }, { -1, -3 }, { -2, -3 }, { -3, -3 }, { -4, -3 },
	{  4, -4 }, {  3, -4 }, {  2, -4 }, {  1, -4 },
	{  0, -4 }, { -1, -4 }, { -2, -4 }, { -3, -4 }, { -4, -4 },
	{  4, -5 }, {  3, -5 }, {  2, -5 }, {  1, -5 },
	{  0, -5 }, { -1, -5 }, { -2, -5 }, { -3, -5 }, { -4, -5 },
	{  4, -6 }, {  3, -6 }, {  2, -6 }, {  1, -6 },
	{  0, -6 }, { -1, -6 }, { -2, -6 }, { -3, -6 }, { -4, -6 },
	{ -1, -7 }, {  0, -7 }, {  1, -7 }, {  2, -7 }, {  3, -7 }, {  4, -7 },
};

const Point gprPointOffsetV[] = {
	{ -1,  0 }, { -2,  0 }, { -3,  0 }, { -4,  0 },
	{  4, -1 }, {  3, -1 }, {  2, -1 }, {  1, -1 },
	{  0, -1 }, { -1, -1 }, { -2, -1 }, { -3, -1 }, { -4, -1 },
	{  4, -2 }, {  3, -2 }, {  2, -2 }, {  1, -2 },
	{  0, -2 }, { -1, -2 }, { -2, -2 }, { -3, -2 }, { -4, -2 },
	{  4, -3 }, {  3, -3 }, {  2, -3 }, {  1, -3 },
	{  0, -3 }, { -1, -3 }, { -2, -3 }, { -3, -3 }, { -4, -3 },
	{  4, -4 }, {  3, -4 }, {  2, -4 }, {  1, -4 },
	{  0, -4 }, { -1, -4 }, { -2, -4 }, { -3, -4 }, { -4, -4 },
	{  4, -5 }, {  3, -5 }, {  2, -5 }, {  1, -5 },
	{  0, -5 }, { -1, -5 }, { -2, -5 }, { -3, -5 }, { -4, -5 },
	{  4, -6 }, {  3, -6 }, {  2, -6 }, {  1, -6 },
	{  0, -6 }, { -1, -6 }, { -2, -6 }, { -3, -6 }, { -4, -6 },
	{  1, -7 }, {  0, -7 }, {  1, -7 }, { -2, -7 }, { -3, -7 }, { -4, -7 },
};

void gprWriteBgr555(byte *dst, uint16 px) {
	dst[0] = (((px >> 10) & 0x1f) / 31.f) * 255;
	dst[1] = (((px >> 5) & 0x1f) / 31.f) * 255;
	dst[2] = ((px & 0x1f) / 31.f) * 255;
	dst[3] = 255;
}

void gprDecodePixels(CG &cg, const byte *data, uint32 size, bool vertical) {
	const int w = cg.w, h = cg.h;
	byte *px = cg.pixels.data();
	Common::MemoryReadStream stream(data, size);
	Common::BitStream8MSB b(stream);

	const int outer = vertical ? w : h;
	const int inner = vertical ? h : w;
	for (int o = 0; o < outer; o++) {
		for (int i = 0; i < inner;) {
			const int col = vertical ? o : i;
			const int row = vertical ? i : o;
			byte *dst = px + (row * w + col) * 4;
			switch (readOnes(b, 2)) {
			case 0:
				// literal pixel
				gprWriteBgr555(dst, b.getBits(15));
				i++;
				break;
			case 1: {
				// single pixel at an (x,y) offset from dst
				uint n = b.getBits(6);
				const byte *src;
				if (vertical)
					src = px + ((row + gprPointOffsetV[n].x) * w + col + gprPointOffsetV[n].y) * 4;
				else
					src = px + ((row + gprPointOffset[n].y) * w + col + gprPointOffset[n].x) * 4;
				memcpy(dst, src, 4);
				i++;
				break;
			}
			default: {
				// copy previously decoded pixels
				int x, y;
				if (vertical)
					gpxDecodeOffset(b, y, x);
				else
					gpxDecodeOffset(b, x, y);
				const byte *src = px + ((row + y) * w + col + x) * 4;
				int len = gpxDecodeRunLength(b);
				const int step = vertical ? w * 4 : 4;
				for (int n = 0; n < len; n++, dst += step, src += step)
					memcpy(dst, src, 4);
				i += len;
				break;
			}
			}
		}
	}
}

void gprDecodeMask(CG &cg, const byte *data, uint32 size, bool vertical) {
	const int w = cg.w, h = cg.h;
	byte *px = cg.pixels.data();
	Common::MemoryReadStream stream(data, size);
	Common::BitStream8MSB b(stream);

	const int outer = vertical ? w : h;
	const int inner = vertical ? h : w;
	for (int o = 0; o < outer; o++) {
		for (int i = 0; i < inner;) {
			const int col = vertical ? o : i;
			const int row = vertical ? i : o;
			byte *dst = px + (row * w + col) * 4;
			if (!b.getBit()) {
				// copy previously decoded alpha
				int x, y;
				if (vertical)
					gpxDecodeOffset(b, y, x);
				else
					gpxDecodeOffset(b, x, y);
				const byte *src = px + ((row + y) * w + col + x) * 4;
				int len = gpxDecodeRunLength(b);
				const int step = vertical ? w * 4 : 4;
				for (int n = 0; n < len; n++, dst += step, src += step)
					dst[3] = src[3];
				i += len;
			} else {
				// literal alpha
				byte a = b.getBits(8);
				a = a == 0x20 ? 255 : a * 8;
				dst[3] = 255 - a;
				i++;
			}
		}
	}
}

} // End of anonymous namespace

CG *decodeGpr(const byte *data, uint32 size) {
	if (size < 14)
		return nullptr;

	bool mask;
	if (!memcmp(data, "R15n", 4))
		mask = false;
	else if (!memcmp(data, "R15m", 4))
		mask = true;
	else
		return nullptr;

	Common::ScopedPtr<CG> cg(new CG());
	cg->x = READ_LE_UINT16(data + 4);
	cg->y = READ_LE_UINT16(data + 6);
	cg->w = READ_LE_UINT16(data + 8);
	cg->h = READ_LE_UINT16(data + 10);
	uint16 vertical = READ_LE_UINT16(data + 12);
	cg->pixels.resize(cg->w * cg->h * 4);

	if (mask) {
		if (size < 18)
			return nullptr;
		uint32 maskPtr = READ_LE_UINT32(data + 14);
		if (maskPtr >= size)
			return nullptr;
		gprDecodePixels(*cg, data + 18, size - 18, vertical & 1);
		gprDecodeMask(*cg, data + maskPtr, size - maskPtr, vertical & 2);
	} else {
		gprDecodePixels(*cg, data + 14, size - 14, vertical & 1);
	}
	return cg.release();
}

// PNG

CG *decodePng(const byte *data, uint32 size) {
#ifdef USE_PNG
	Common::MemoryReadStream stream(data, size);
	Image::PNGDecoder png;
	if (!png.loadStream(stream))
		return nullptr;

	const Graphics::Surface *src = png.getSurface();
	Graphics::Surface *rgba = src->convertTo(Graphics::PixelFormat::createFormatRGBA32(), png.getPalette().data(), png.getPalette().size());

	Common::ScopedPtr<CG> cg(new CG());
	cg->w = rgba->w;
	cg->h = rgba->h;
	cg->bpp = 24;
	cg->hasAlpha = src->format.aBits() > 0;
	cg->pixels.resize(cg->w * cg->h * 4);
	for (uint row = 0; row < cg->h; row++)
		memcpy(cg->pixels.data() + row * cg->w * 4, rgba->getBasePtr(0, row), cg->w * 4);

	rgba->free();
	delete rgba;
	return cg.release();
#else
	warning("decodePng: PNG support not compiled in");
	return nullptr;
#endif
}

} // End of namespace AI5
