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

#ifndef IKURA_FORMATS_GRAPHIC_GRAPHIC_H
#define IKURA_FORMATS_GRAPHIC_GRAPHIC_H

#include "common/str.h"
#include "common/stream.h"
#include "graphics/surface.h"

namespace Ikura::Format::Graphic {

// Picks the right decoder for a resource by its extension - real cabinets
// mix formats within the same archive (e.g. Snow Sakura's GGD cabinet has
// both .GG0-.GG9 GGA-format and .GGD GGD8-format members side by side; see
// engines/ikura/README.md's real-data validation table). Returns nullptr
// if the extension is unrecognized or the decoder rejects the stream.
Graphics::Surface *decodeImage(const Common::String &name, Common::SeekableReadStream &stream);

} // End of namespace Ikura::Format::Graphic

#endif
