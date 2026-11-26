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

#include "ikura/formats/graphic/ggp.h"

#include "common/array.h"
#include "common/memstream.h"
#include "image/png.h"

namespace Ikura::Format::Graphic {

Graphics::Surface *decodeGGP(Common::SeekableReadStream &stream) {
	byte hdr[36];
	stream.seek(0);
	if (stream.read(hdr, sizeof(hdr)) != sizeof(hdr))
		return nullptr;
	if (memcmp(hdr, "GGPFAIKE", 8) != 0)
		return nullptr;

	uint32 length = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
	uint32 regLength = hdr[32] | (hdr[33] << 8) | (hdr[34] << 16) | (hdr[35] << 24);
	uint32 fileSize = stream.size();
	uint32 payloadStart = sizeof(hdr) + regLength;
	if (length == 0 || regLength > fileSize - sizeof(hdr) || length > fileSize - payloadStart)
		return nullptr;

	Common::Array<byte> buffer(length);
	stream.seek(payloadStart);
	if (stream.read(buffer.data(), length) != length)
		return nullptr;

	for (uint32 i = 0; i < length; i++)
		buffer[i] ^= hdr[i % 8] ^ hdr[12 + (i % 8)];

	Common::MemoryReadStream png(buffer.data(), length);
	Image::PNGDecoder decoder;
	if (!decoder.loadStream(png))
		return nullptr;

	const Graphics::Surface *decoded = decoder.getSurface();
	if (!decoded)
		return nullptr;

	Graphics::Surface *result = new Graphics::Surface();
	result->copyFrom(*decoded);
	return result;
}

} // End of namespace Ikura::Format::Graphic
