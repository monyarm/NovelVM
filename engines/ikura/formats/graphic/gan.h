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

#ifndef IKURA_FORMATS_GRAPHIC_GAN_H
#define IKURA_FORMATS_GRAPHIC_GAN_H

#include "common/array.h"
#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// GAN animation (VileVN reference: CIkura::gan, res/converters/cikura.cpp).
// A sequence of 24-bit frames, always 640x480 regardless of the game's
// actual resolution (a property of the reference format, not a
// limitation of this port - see gan.cpp). Each frame is either a
// standalone RLE-compressed frame or an RLE delta against the previous
// one. Returns an empty array if the file is too malformed to identify
// any frames at all; a truncated individual frame stops decoding that
// frame early (rest left black) rather than discarding the whole
// animation, since VileVN itself has no error handling here and a
// partial frame is still useful for a cosmetic animation. Caller owns
// every returned Surface.
Common::Array<Graphics::Surface *> decodeGAN(Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
