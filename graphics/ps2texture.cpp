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

#include "graphics/ps2texture.h"

#include "common/endian.h"
#include "common/textconsole.h"

namespace Graphics {

bool ps2IsIndexed(PS2PixelFormat fmt) {
	switch (fmt) {
	case kPSMT8:
	case kPSMT8H:
	case kPSMT4:
	case kPSMT4HL:
	case kPSMT4HH:
		return true;
	default:
		return false;
	}
}

int ps2ClutColorCount(PS2PixelFormat fmt) {
	switch (fmt) {
	case kPSMT8:
	case kPSMT8H:
		return 256;
	case kPSMT4:
	case kPSMT4HL:
	case kPSMT4HH:
		return 16;
	default:
		return 0;
	}
}

int ps2TexelDataSize(PS2PixelFormat fmt, int w, int h) {
	switch (fmt) {
	case kPSMCT32:
	case kPSMCT24:
	case kPSMZ32:
	case kPSMZ24:
		return w * h * 4;
	case kPSMCT16:
	case kPSMCT16S:
	case kPSMZ16:
	case kPSMZ16S:
		return w * h * 2;
	case kPSMT8:
	case kPSMT8H:
		return w * h;
	case kPSMT4:
	case kPSMT4HL:
	case kPSMT4HH:
		return (w * h) / 2;
	default:
		return 0;
	}
}

// Bytes of bitmap data TM2::getSurface() reads for a given TIM2 image type
// (1=16-bit, 2=24-bit, 3=32-bit, 4=PSMT4, 5=PSMT8); -1 for an unsupported
// type. Computed in 64-bit because w/h come straight from the file.
static int64 ps2Tm2DecodeByteSize(byte imageType, int w, int h) {
	int64 texels = (int64)w * h;
	switch (imageType) {
	case 1: return texels * 2;
	case 2: return texels * 4; // PSMT24 is stored in 32-bit words
	case 3: return texels * 4;
	case 4: return texels / 2;
	case 5: return texels;
	default: return -1;
	}
}

/**
 * GS CSM1 swizzle, applied to a 256-entry CLUT: each 32-entry group is split
 * into three 8-colour blocks that are swapped into linear order.
 */
void ps2UnswizzleClut(byte *rgba, int colorCount) {
	for (int i = 0; i + 32 <= colorCount; i += 32) {
		for (int j = 0; j < 8; j++) {
			byte *a = rgba + (i + 8 + j) * 4;
			byte *b = rgba + (i + 16 + j) * 4;
			for (int k = 0; k < 4; k++)
				SWAP(a[k], b[k]);
		}
	}
}

bool ps2UnswizzleIndices(const byte *source, uint32 sourceSize, byte *destination,
		uint32 width, uint32 height, uint32 storageWidth) {
	const uint64 count = (uint64)width * height;
	for (uint32 y = 0; y < height; ++y) {
		for (uint32 x = 0; x < width; ++x) {
			const uint64 block = (uint64)(y & ~0xFU) * storageWidth + (x & ~0xFU) * 2;
			const uint64 swap = (((y + 2) >> 2) & 1) * 4;
			const uint64 positionY = ((((y & ~3U) >> 1) + (y & 1)) & 7);
			const uint64 column = positionY * storageWidth * 2 + (((x + swap) & 7) * 4);
			const uint64 byteNumber = ((y >> 1) & 1) + ((x >> 2) & 2);
			const uint64 sourceOffset = block + column + byteNumber;
			if (sourceOffset >= sourceSize || y * width + x >= count)
				return false;
			destination[y * width + x] = source[sourceOffset];
		}
	}
	return true;
}

static void readClut32(byte *dst, const byte *src, int count) {
	// PSMCT32: one 32-bit word per colour, byte order R,G,B,A.
	for (int i = 0; i < count; i++) {
		dst[i * 4 + 0] = src[i * 4 + 0];
		dst[i * 4 + 1] = src[i * 4 + 1];
		dst[i * 4 + 2] = src[i * 4 + 2];
		dst[i * 4 + 3] = src[i * 4 + 3];
	}
}

static void readClut24(byte *dst, const byte *src, int count) {
	// PSMCT24: 32-bit word, alpha ignored.
	for (int i = 0; i < count; i++) {
		dst[i * 4 + 0] = src[i * 4 + 0];
		dst[i * 4 + 1] = src[i * 4 + 1];
		dst[i * 4 + 2] = src[i * 4 + 2];
		dst[i * 4 + 3] = 0xFF;
	}
}

static void readClut16(const byte *src, byte *rgbOut) {
	uint16 v = READ_LE_UINT16(src);
	// PSMCT16/16S: A1B5G5R5, alpha bit dropped (opaque). Expand 5 bits to
	// 8 with bit replication (v<<3 | v>>2); a plain <<3 leaves the low 3
	// bits zero, which visibly bands gradients.
	byte r = (byte)(v & 0x1F);
	byte g = (byte)((v >> 5) & 0x1F);
	byte b = (byte)((v >> 10) & 0x1F);
	rgbOut[0] = (byte)((r << 3) | (r >> 2));
	rgbOut[1] = (byte)((g << 3) | (g >> 2));
	rgbOut[2] = (byte)((b << 3) | (b >> 2));
}

Graphics::Surface *ps2CreateIndexedSurface(PS2PixelFormat clutFmt, byte *clut,
		int clutCount, byte *indices, PS2PixelFormat texFmt, int w, int h,
		bool clutIsSwizzled, bool clutAlphaHalfRange) {
	Common::Array<byte> rgbaClut;
	rgbaClut.resize(clutCount * 4);

	switch (clutFmt) {
	case kPSMCT32:
		readClut32(rgbaClut.data(), clut, clutCount);
		if (clutAlphaHalfRange) {
			for (int i = 0; i < clutCount; i++) {
				int a = (int)((rgbaClut[i * 4 + 3] / 128.0f) * 255);
				rgbaClut[i * 4 + 3] = (byte)MIN(a, 255);
			}
		}
		break;
	case kPSMCT24:
		readClut24(rgbaClut.data(), clut, clutCount);
		break;
	case kPSMCT16:
	case kPSMCT16S:
		for (int i = 0; i < clutCount; i++)
			readClut16(clut + i * 2, rgbaClut.data() + i * 4);
		break;
	default:
		return nullptr;
	}

	if (clutIsSwizzled && clutCount == 256)
		ps2UnswizzleClut(rgbaClut.data(), clutCount);

	Graphics::Surface *surface = new Graphics::Surface();
	surface->create(w, h, Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24));

	int texelCount = w * h;
	for (int i = 0; i < texelCount; i++) {
		byte index = 0;
		switch (texFmt) {
		case kPSMT4:
		case kPSMT4HL:
		case kPSMT4HH:
			// two texels per byte, low nibble first
			index = (i & 1) ? (indices[i / 2] >> 4) : (indices[i / 2] & 0x0F);
			break;
		default:
			index = indices[i];
			break;
		}
		if (index >= clutCount)
			index = 0;

		byte *dst = (byte *)surface->getBasePtr(i % w, i / w);
		dst[0] = rgbaClut[index * 4 + 2]; // ScummVM stores BGRA
		dst[1] = rgbaClut[index * 4 + 1];
		dst[2] = rgbaClut[index * 4 + 0];
		dst[3] = rgbaClut[index * 4 + 3];
	}

	return surface;
}

Graphics::Surface *ps2CreateColorSurface(PS2PixelFormat fmt, byte *data,
		int w, int h) {
	Graphics::Surface *surface = new Graphics::Surface();
	surface->create(w, h, Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24));

	int texelCount = w * h;
	for (int i = 0; i < texelCount; i++) {
		byte *dst = (byte *)surface->getBasePtr(i % w, i / w);
		switch (fmt) {
		case kPSMCT32:
			// GS memory stores PSMCT32 as R,G,B,A bytes; the surface
			// stores BGRA (B at byte 0), so R/B swap on write -- same
			// convention already applied correctly below for PSMCT16/16S
			// and by ps2CreateIndexedSurface's CLUT writeback. Alpha is
			// also GS half-range (0..0x80) here, same as CLUT alpha.
			dst[0] = data[i * 4 + 2];
			dst[1] = data[i * 4 + 1];
			dst[2] = data[i * 4 + 0];
			dst[3] = (byte)MIN(255, (int)((data[i * 4 + 3] / 128.0f) * 255));
			break;
		case kPSMCT24:
			dst[0] = data[i * 4 + 2];
			dst[1] = data[i * 4 + 1];
			dst[2] = data[i * 4 + 0];
			dst[3] = 0xFF;
			break;
		case kPSMCT16:
		case kPSMCT16S: {
			byte rgb[3];
			readClut16(data + i * 2, rgb);
			dst[0] = rgb[2];
			dst[1] = rgb[1];
			dst[2] = rgb[0];
			dst[3] = 0xFF;
			break;
		}
		default:
			dst[0] = dst[1] = dst[2] = dst[3] = 0;
			break;
		}
	}

	return surface;
}

namespace PS2GsTex {
} // namespace PS2GsTex

TM2::TM2(Common::SeekableReadStream *stream) {
	if (!stream)
		return;

	uint32 magic = stream->readUint32BE();
	if (magic != MKTAG('T', 'I', 'M', '2'))
		return;

	stream->readByte(); // version
	byte format = stream->readByte();
	uint16 count = stream->readUint16LE();
	stream->skip(8); // reserved

	if (format != 0) {
		// 128-byte header variant (format only selects the header size).
		stream->skip(128 - 16);
	}

	for (int i = 0; i < count; i++) {
		Picture *pic = new Picture();

		int32 totalSize = stream->readSint32LE();
		stream->skip(4); // clut size (unreliable for some titles)
		int32 imageSize = stream->readSint32LE();
		stream->skip(2); // header size
		pic->clutCount = stream->readSint16LE();
		stream->readByte(); // picture format
		stream->readByte(); // mipmap count
		pic->clutType = stream->readByte();
		pic->imageType = stream->readByte();
		pic->w = stream->readUint16LE();
		pic->h = stream->readUint16LE();
		uint64 tex0raw = stream->readUint64LE();
		stream->skip(8); // tex1
		stream->skip(4); // gs regs
		stream->skip(4); // gs clut

		PS2GsTex::Register tex0(tex0raw);
		pic->texFormat = tex0.psm();

		// CLUT colour count per image type; the file stores one colour per
		// 32-bit PSMCT32 word (4 bytes) for both indexed types. An indexed
		// picture without at least one CLUT entry has nothing to index into.
		bool indexed = (pic->imageType == 4 || pic->imageType == 5);
		int64 clutBytes = indexed ? (int64)pic->clutCount * 4 : 0;

		// Validate every file-supplied size before allocating or decoding:
		// a malformed picture must neither drive an over-sized allocation nor
		// let getSurface() read past the bitmap/clut buffers. Reject and stop
		// (the remaining layout cannot be trusted once one record is bad).
		int64 decodeBytes = ps2Tm2DecodeByteSize(pic->imageType, pic->w, pic->h);
		int64 remaining = (int64)stream->size() - stream->pos();
		if (stream->err() || stream->eos() ||
				imageSize < 0 || imageSize > remaining ||
				pic->w <= 0 || pic->h <= 0 ||
				pic->clutCount < 0 || pic->clutCount > 256 ||
				(indexed && pic->clutCount == 0) ||
				decodeBytes < 0 || decodeBytes > imageSize ||
				clutBytes < 0 || clutBytes > remaining - imageSize) {
			warning("TM2: rejecting malformed picture %d (imageSize %d, %dx%d, type %d)",
					i, imageSize, pic->w, pic->h, pic->imageType);
			delete pic;
			break;
		}

		pic->dataSize = imageSize;
		pic->data = new byte[imageSize];
		stream->read(pic->data, imageSize);

		if (clutBytes > 0) {
			pic->clutSize = (int)clutBytes;
			pic->clut = new byte[clutBytes];
			stream->read(pic->clut, clutBytes);

			// CSM1 CLUT swizzle: indicated by TEX0 bit 55 (CSM2 clears it)
			// or, in the file, the high bit of the CLUT type byte. Applied
			// exactly once (in ps2CreateIndexedSurface), never in place here.
			pic->clutIsSwizzled = pic->clutCount == 256 &&
				(pic->clutType & 0x80) == 0 && !tex0.csm2();
		}

		_pictures.push_back(pic);

		// Skip any mipmap levels (raw texture data follows after the layout
		// record; currently decoded pictures discard mip levels entirely).
		int64 consumed = (int64)totalSize - 48 - imageSize - clutBytes;
		if (consumed > 0)
			stream->skip(consumed);
	}
}

TM2::~TM2() {
	for (uint i = 0; i < _pictures.size(); i++)
		delete _pictures[i];
}

Graphics::Surface *TM2::getSurface(int i) {
	if (i < 0 || i >= (int)_pictures.size())
		return nullptr;

	Picture *pic = _pictures[i];

	switch (pic->imageType) {
	case 4: // 4-bit indexed (PSMT4)
	case 5: { // 8-bit indexed (PSMT8)
		PS2PixelFormat texFmt = (pic->imageType == 4) ? kPSMT4 : kPSMT8;
		return ps2CreateIndexedSurface(kPSMCT32, pic->clut, pic->clutCount,
			pic->data, texFmt, pic->w, pic->h, pic->clutIsSwizzled, false);
	}
	case 1: // 16-bit RGBA
		return ps2CreateColorSurface(kPSMCT16, pic->data, pic->w, pic->h);
	case 2: // 24-bit RGB (stored in 32-bit words)
		return ps2CreateColorSurface(kPSMCT24, pic->data, pic->w, pic->h);
	case 3: // 32-bit RGBA
		return ps2CreateColorSurface(kPSMCT32, pic->data, pic->w, pic->h);
	default:
		return nullptr;
	}
}

} // namespace Graphics
