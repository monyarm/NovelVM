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

#include "common/formats/gim.h"

#include "common/util.h"

namespace Common {

namespace {
const Graphics::PixelFormat kRGBA8888(4, 8, 8, 8, 8, 0, 8, 16, 24);
}

GIM::GIM(SeekableReadStream *stream) {
	if (!stream)
		return;

	uint32 size = (uint32)(stream->size() - stream->pos());
	if (size < 16)
		return;
	_data.resize(size);
	stream->read(_data.data(), size);

	// PSP files are little-endian (MIG.00.1PSP); PS3 exports use the
	// byte-reversed signature. Detect the container, not the host endian.
	if (memcmp(_data.data(), "MIG.00.1PSP", 11) == 0)
		_bigEndian = false;
	else if (memcmp(_data.data(), "GIM.10.00PSP", 11) == 0 ||
	         memcmp(_data.data(), "GIM.00.1PSP", 11) == 0)
		_bigEndian = true;
	else
		return;

	parseRegion(16, size);
	_ok = _hasImage && decode();
}

GIM::~GIM() {
	_surface.free();
}

uint16 GIM::read16(const byte *p) const {
	if (_bigEndian)
		return ((uint16)p[0] << 8) | p[1];
	return p[0] | ((uint16)p[1] << 8);
}

uint32 GIM::read32(const byte *p) const {
	if (_bigEndian)
		return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) | ((uint32)p[2] << 8) | p[3];
	return p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

bool GIM::range(uint32 offset, uint32 size) const {
	return offset <= _data.size() && size <= _data.size() - offset;
}

void GIM::parseRegion(uint32 start, uint32 end) {
	for (uint32 pos = start; pos + 16 <= end && pos + 16 <= _data.size();) {
		uint16 type = read16(_data.data() + pos);
		uint32 blockSize = read32(_data.data() + pos + 4);
		uint32 next = read32(_data.data() + pos + 8);
		uint32 blockEnd = pos + blockSize;
		if (blockSize < 16 || blockEnd > end || blockEnd > _data.size())
			return;

		if (type == 2 || type == 3) {
			parseRegion(pos + 16, blockEnd);
		} else if (type == 4 || type == 5) {
			parseImage(pos, blockEnd, type == 5);
		}

		uint32 advance = next ? pos + next : blockEnd;
		if (advance <= pos || advance > end)
			return;
		pos = advance;
	}
}

void GIM::parseImage(uint32 blockStart, uint32 blockEnd, bool palette) {
	if (blockStart + 16 + 0x30 > blockEnd)
		return;
	const byte *p = _data.data() + blockStart + 16;
	Image image;
	image.format = read16(p + 4);
	image.pixelOrder = read16(p + 6);
	image.width = read16(p + 8);
	image.height = read16(p + 10);
	image.bpp = read16(p + 12);
	image.pitchAlign = read16(p + 14);
	image.nextIndex = read32(p + 0x18);
	image.frameStart = read32(p + 0x1C);
	image.frameEnd = read32(p + 0x20);
	image.levelCount = read16(p + 0x2A);
	image.frameCount = read16(p + 0x2E);

	uint32 content = blockStart + 16;
	if (!image.width || !image.height || image.frameCount != 1 ||
	    image.nextIndex < 0x30 || image.frameStart < image.nextIndex ||
	    image.frameEnd < image.frameStart ||
	    !range(content + image.frameStart, image.frameEnd - image.frameStart))
		return;

	uint32 index = content + image.nextIndex;
	if (!range(index, 4))
		return;
	uint32 frameOffset = read32(_data.data() + index);
	if (frameOffset < image.frameStart || frameOffset > image.frameEnd)
		return;
	image.data = _data.data() + content + frameOffset;
	image.dataSize = image.frameEnd - frameOffset;

	if (palette) {
		if (!_hasPalette)
			_palette = image;
		_hasPalette = true;
	} else if (!_hasImage) {
		_image = image;
		_width = image.width;
		_height = image.height;
		_levelCount = image.levelCount;
		_hasImage = true;
	}
}

uint8 GIM::expand(uint32 value, uint32 bits) {
	if (bits == 8)
		return (uint8)value;
	return (uint8)((value * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1));
}

uint32 GIM::rgba(uint8 r, uint8 g, uint8 b, uint8 a) {
	return kRGBA8888.ARGBToColor(a, r, g, b);
}

uint32 GIM::paletteValue(uint32 index) const {
	if (!_hasPalette || index >= (uint32)_palette.width * _palette.height)
		return rgba(255, 255, 255, 255);
	uint32 byteOffset = index * 4;
	if (byteOffset + 4 > _palette.dataSize)
		return rgba(255, 255, 255, 255);
	const byte *p = _palette.data + byteOffset;
	return rgba(p[0], p[1], p[2], p[3]);
}

uint32 GIM::pixelValue(uint32 x, uint32 y, uint32 storageWidth) const {
	const Image &im = _image;
	uint32 bpp = im.bpp;
	uint32 bitOffset = (y * storageWidth + x) * bpp;
	uint32 byteOffset = bitOffset / 8;
	uint32 value = 0;
	uint32 bytes = (bpp + 7) / 8;
	if (byteOffset + bytes > im.dataSize)
		return 0;
	for (uint32 i = 0; i < bytes; ++i)
		value |= (uint32)im.data[byteOffset + i] << (i * 8);
	if (bpp < 8)
		value = (value >> (bitOffset & 7)) & ((1u << bpp) - 1);

	switch (im.format) {
	case 0:
		return rgba(expand(value >> 11, 5), expand(value >> 5, 6), expand(value, 5), 255);
	case 1:
		return rgba(expand(value >> 11, 5), expand(value >> 6, 5), expand(value >> 1, 5), (value & 1) ? 255 : 0);
	case 2:
		return rgba(expand(value >> 12, 4), expand(value >> 8, 4), expand(value >> 4, 4), expand(value, 4));
	case 3:
		return rgba(value & 0xff, (value >> 8) & 0xff, (value >> 16) & 0xff, (value >> 24) & 0xff);
	case 4:
	case 5:
		return paletteValue(value);
	case 6:
		return rgba(value & 0xff, value & 0xff, value & 0xff, value >> 8);
	default:
		return rgba(value & 0xff, (value >> 8) & 0xff, (value >> 16) & 0xff, (value >> 24) & 0xff);
	}
}

bool GIM::decode() {
	if (_image.bpp == 0)
		return false;
	_surface.create(_image.width, _image.height, kRGBA8888);

	if (_image.format >= 8 && _image.format <= 10) {
		uint32 blockBytes = _image.format == 8 ? 8 : 16;
		uint32 blocksWide = (_image.width + 3) / 4;
		uint32 blocksHigh = (_image.height + 3) / 4;
		uint32 offset = 0;
		for (uint32 by = 0; by < blocksHigh; ++by) {
			for (uint32 bx = 0; bx < blocksWide; ++bx) {
				if (offset + blockBytes > _image.dataSize)
					return false;
				const byte *block = _image.data + offset;
				uint8 colors[4][4] = {};
				uint32 colorOffset = _image.format == 8 ? 0 : 8;
				uint16 c0 = block[colorOffset] | (block[colorOffset + 1] << 8);
				uint16 c1 = block[colorOffset + 2] | (block[colorOffset + 3] << 8);
				colors[0][0] = expand(c0 >> 11, 5); colors[0][1] = expand(c0 >> 5, 6); colors[0][2] = expand(c0, 5); colors[0][3] = 255;
				colors[1][0] = expand(c1 >> 11, 5); colors[1][1] = expand(c1 >> 5, 6); colors[1][2] = expand(c1, 5); colors[1][3] = 255;
				if (c0 > c1 || _image.format != 8) {
					for (int c = 0; c < 3; ++c) {
						colors[2][c] = (2 * colors[0][c] + colors[1][c]) / 3;
						colors[3][c] = (colors[0][c] + 2 * colors[1][c]) / 3;
					}
					colors[2][3] = colors[3][3] = 255;
				} else {
					for (int c = 0; c < 3; ++c)
						colors[2][c] = (colors[0][c] + colors[1][c]) / 2;
					memset(colors[3], 0, 4);
				}
				uint32 alphaBits[16];
				for (int i = 0; i < 16; ++i) alphaBits[i] = 255;
				if (_image.format == 9) {
					for (int i = 0; i < 16; ++i)
						alphaBits[i] = (block[4 + i / 2] >> ((i & 1) * 4) & 0xF) * 17;
				} else if (_image.format == 10) {
					uint64 bits = 0;
					for (int i = 0; i < 6; ++i) bits |= (uint64)block[2 + i] << (i * 8);
					uint8 a[8] = {block[0], block[1]};
					if (a[0] > a[1]) {
						for (int i = 2; i < 8; ++i) a[i] = (uint8)(((8 - i) * a[0] + (i - 1) * a[1]) / 7);
					} else {
						for (int i = 2; i < 6; ++i) a[i] = (uint8)(((6 - i) * a[0] + (i - 1) * a[1]) / 5);
						a[6] = 0; a[7] = 255;
					}
					for (int i = 0; i < 16; ++i) alphaBits[i] = a[(bits >> (i * 3)) & 7];
				}
				uint32 colorBits = blockBytes == 8 ? read32(block + 4) : read32(block + 12);
				for (uint32 py = 0; py < 4; ++py) {
					for (uint32 px = 0; px < 4; ++px) {
						uint32 i = py * 4 + px;
						uint32 x = bx * 4 + px, y = by * 4 + py;
						if (x < _image.width && y < _image.height) {
							uint32 ci = (colorBits >> (i * 2)) & 3;
							*(uint32 *)_surface.getBasePtr(x, y) = rgba(colors[ci][0], colors[ci][1], colors[ci][2], alphaBits[i]);
						}
					}
				}
				offset += blockBytes;
			}
		}
		return true;
	}

	uint32 tileWidth = _image.pixelOrder ? 0x80 / _image.bpp : _image.width;
	if (!tileWidth)
		return false;
	uint32 rowBytes = (_image.width * _image.bpp + 7) / 8;
	if (_image.pitchAlign)
		rowBytes = (rowBytes + _image.pitchAlign - 1) / _image.pitchAlign * _image.pitchAlign;
	uint32 pitchWidth = (rowBytes * 8 + _image.bpp - 1) / _image.bpp;
	uint32 storageWidth = MAX<uint32>((_image.width + tileWidth - 1) / tileWidth * tileWidth, pitchWidth);
	uint32 storageHeight = _image.pixelOrder ? (_image.height + 7) / 8 * 8 : _image.height;
	for (uint32 sy = 0; sy < storageHeight; ++sy) {
		for (uint32 sx = 0; sx < storageWidth; ++sx) {
			uint32 dx = sx, dy = sy;
			if (_image.pixelOrder) {
				uint32 tileX = sx / tileWidth, tileY = sy / 8;
				uint32 inTile = (sy % 8) * tileWidth + sx % tileWidth;
				dx = tileX * tileWidth + inTile % tileWidth;
				dy = tileY * 8 + inTile / tileWidth;
			}
			if (dx < _image.width && dy < _image.height)
				*(uint32 *)_surface.getBasePtr(dx, dy) = pixelValue(sx, sy, storageWidth);
		}
	}
	return true;
}

} // namespace Common
