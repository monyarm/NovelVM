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

#ifndef IKURA_FORMATS_GRAPHIC_GGD24_H
#define IKURA_FORMATS_GRAPHIC_GGD24_H

#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// GGD 24-bit opaque CG (VileVN reference: CIkura::ggd_24b,
// res/converters/cikura.cpp). Dispatched on 3 distinct magic 4-byte
// headers (see docs/vilevn-audit/PROVENANCE.md); all three use this same
// body format. A back-reference LZ variant over 3-byte BGR pixel triples.
// Returns nullptr on any malformed input; caller owns the returned Surface.
Graphics::Surface *decodeGGD24(Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
