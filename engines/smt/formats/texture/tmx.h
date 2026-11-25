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

#ifndef SMT_FORMATS_TEXTURE_TMX_H
#define SMT_FORMATS_TEXTURE_TMX_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/stream.h"

#include "graphics/ps2texture.h"
#include "graphics/surface.h"

namespace SMT {

/**
 * TMX: Atlus' PS2 texture container (Persona 3/4 et al.).
 *
 * A thin container: a 0x10-byte chunk header followed by format settings,
 * an optionally CSM1-swizzled CLUT (PSMCT32/24/16/16S) and a linear index
 * bitmap (PSMT8/8H/4/4HL/4HH). Pixel/CLUT decoding is delegated to
 * Graphics::ps2* helpers shared with TM2 and other PS2 texture containers.
 *
 * Header layout is little-endian (verified against real P3/P4 corpus).
 * mipKL and wrapModes values are informational; mip levels are not decoded.
 */
class TMX {
public:
	TMX(const byte *data, uint32 size);
	TMX(Common::SeekableReadStream *stream);
	TMX(const char *path);
	~TMX() {}

	/** Decoded RGBA8888 surface, or nullptr on parse failure. */
	Graphics::Surface *getSurface();

	int width() const { return _w; }
	int height() const { return _h; }
	int mipMapCount() const { return _formatSettings.mipMapCount; }

private:
	struct FormatSettings {
		uint32 length;
		byte numPalettes;
		Graphics::PS2PixelFormat paletteFmt;
		uint16 width;
		uint16 height;
		Graphics::PS2PixelFormat pixelFmt;
		byte mipMapCount;
		int16 mipKL;
		byte wrapModes;
	} _formatSettings;

	int _w = 0;
	int _h = 0;
	Common::Array<byte> _clut;
	Common::Array<byte> _indices;
	bool _ok = false;

	void readFile(Common::SeekableReadStream *stream);
	void readHeader(Common::SeekableReadStream *s);
	void readClut(Common::SeekableReadStream *s);
	void readIndices(Common::SeekableReadStream *s);
};

} // namespace SMT

#endif
