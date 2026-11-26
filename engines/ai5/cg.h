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

#ifndef AI5_CG_H
#define AI5_CG_H

#include "common/array.h"
#include "common/str.h"

namespace AI5 {

enum CGType {
	kCGAkb,
	kCGGp4,
	kCGGp8,
	kCGG16,
	kCGG24,
	kCGG32,
	kCGGcc,
	kCGGpr,
	kCGGpx,
	kCGPng
};

/**
 * A decoded CG. If the palette is non-empty it holds 256 BGRx entries and
 * the pixels are 8-bit indices; otherwise the pixels are RGBA.
 */
struct CG {
	uint x, y, w, h;
	uint bpp;
	bool hasAlpha;
	Common::Array<byte> pixels;
	Common::Array<byte> palette;

	CG() : x(0), y(0), w(0), h(0), bpp(0), hasAlpha(false) {}
	bool isIndexed() const { return !palette.empty(); }
};

bool cgTypeFromName(const Common::String &name, CGType &type);
CG *loadCG(const byte *data, uint32 size, CGType type);

CG *decodeAkb(const byte *data, uint32 size);
CG *decodeGp4(const byte *data, uint32 size);
CG *decodeGp8(const byte *data, uint32 size);
CG *decodeGxx(const byte *data, uint32 size, uint bpp);
CG *decodeGcc(const byte *data, uint32 size);
CG *decodeGpr(const byte *data, uint32 size);
CG *decodeGpx(const byte *data, uint32 size);
CG *decodePng(const byte *data, uint32 size);


} // End of namespace AI5

#endif // AI5_CG_H
