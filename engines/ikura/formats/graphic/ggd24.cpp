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

#include "ikura/formats/graphic/ggd24.h"

#include "common/array.h"

namespace Ikura::Format::Graphic {

namespace {
// Sanity cap, not part of the reference format: real ikura CGs are at
// most 800x600. Blocks a malicious header from requesting a multi-GB
// decompression buffer.
const int kMaxDimension = 8192;
}

Graphics::Surface *decodeGGD24(Common::SeekableReadStream &stream) {
	byte hdr[8];
	stream.seek(0);
	if (stream.read(hdr, sizeof(hdr)) != sizeof(hdr))
		return nullptr;

	int width = hdr[4] | (hdr[5] << 8);
	int height = hdr[6] | (hdr[7] << 8);
	if (width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension)
		return nullptr;

	uint32 srcLen = stream.size();
	Common::Array<byte> srcBuf(srcLen);
	stream.seek(0);
	if (stream.read(srcBuf.data(), srcLen) != srcLen)
		return nullptr;

	uint32 dstLen = (uint32)width * (uint32)height * 3;
	Common::Array<byte> dstBuf(dstLen);

	// Back-reference LZ over 3-byte pixel triples. The reference
	// (CIkura::ggd_24b) does none of the bounds checks below; malformed
	// input there reads/writes past its buffers.
	uint32 i = 8, o = 0;
	while (i < srcLen && o < dstLen) {
		byte ctrl = srcBuf[i++];
		if (ctrl == 0) {
			if (i >= srcLen || o < 3)
				return nullptr;
			byte count = srcBuf[i++];
			o -= 3;
			byte r = dstBuf[o], g = dstBuf[o + 1], b = dstBuf[o + 2];
			o += 3;
			for (int j = 0; j < count; j++) {
				if (o + 3 > dstLen)
					return nullptr;
				dstBuf[o++] = r;
				dstBuf[o++] = g;
				dstBuf[o++] = b;
			}
		} else if (ctrl == 1 || ctrl == 3) {
			// ctrl 3 is ctrl 1 with an implicit count of 1.
			if (i >= srcLen)
				return nullptr;
			byte count = (ctrl == 1) ? srcBuf[i++] : 1;
			if (ctrl == 1 && i >= srcLen)
				return nullptr;
			byte dist = srcBuf[i++];
			uint32 back = (uint32)dist * 3;
			if (back == 0 || back > o)
				return nullptr;
			uint32 tpos = o - back;
			for (int j = 0; j < count; j++) {
				if (tpos + 3 > dstLen || o + 3 > dstLen)
					return nullptr;
				dstBuf[o++] = dstBuf[tpos++];
				dstBuf[o++] = dstBuf[tpos++];
				dstBuf[o++] = dstBuf[tpos++];
			}
		} else if (ctrl == 2 || ctrl == 4) {
			// ctrl 4 is ctrl 2 with an implicit count of 1.
			byte count = 1;
			if (ctrl == 2) {
				if (i >= srcLen)
					return nullptr;
				count = srcBuf[i++];
			}
			if (i + 1 >= srcLen)
				return nullptr;
			byte distLo = srcBuf[i++];
			byte distHi = srcBuf[i++];
			uint32 back = ((distHi << 8) | distLo) * 3u;
			if (back == 0 || back > o)
				return nullptr;
			uint32 tpos = o - back;
			for (int j = 0; j < count; j++) {
				if (tpos + 3 > dstLen || o + 3 > dstLen)
					return nullptr;
				dstBuf[o++] = dstBuf[tpos++];
				dstBuf[o++] = dstBuf[tpos++];
				dstBuf[o++] = dstBuf[tpos++];
			}
		} else {
			int count = ctrl - 4;
			for (int j = 0; j < count; j++) {
				if (i + 3 > srcLen || o + 3 > dstLen)
					return nullptr;
				dstBuf[o++] = srcBuf[i++];
				dstBuf[o++] = srcBuf[i++];
				dstBuf[o++] = srcBuf[i++];
			}
		}
	}

	Graphics::Surface *result = new Graphics::Surface();
	result->create(width, height, Graphics::PixelFormat::createFormatRGBA32());
	uint32 p = 0;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			byte r = dstBuf[p], g = dstBuf[p + 1], b = dstBuf[p + 2];
			p += 3;
			*(uint32 *)result->getBasePtr(x, y) = result->format.ARGBToColor(0xFF, r, g, b);
		}
	}
	return result;
}

} // End of namespace Ikura::Format::Graphic
