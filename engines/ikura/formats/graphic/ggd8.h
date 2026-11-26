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

#ifndef IKURA_FORMATS_GRAPHIC_GGD8_H
#define IKURA_FORMATS_GRAPHIC_GGD8_H

#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// GGD 8-bit paletted hitmap/opaque CG (VileVN reference: CIkura::ggd_8b,
// res/converters/cikura.cpp). A classic LZSS variant (12-bit ring-buffer
// style back-references, 8 literal/back-reference flags per control
// byte) over a 256-color palette. Returns nullptr on any malformed
// input; caller owns the returned Surface.
Graphics::Surface *decodeGGD8(Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
