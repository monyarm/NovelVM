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

#include "ikura/formats/graphic/graphic.h"

#include "ikura/formats/graphic/gga.h"
#include "ikura/formats/graphic/ggd8.h"
#include "ikura/formats/graphic/ggd24.h"
#include "ikura/formats/graphic/ggp.h"

namespace Ikura::Format::Graphic {

namespace {

Common::String extensionOf(const Common::String &name) {
	int dot = (int)name.size() - 1;
	while (dot >= 0 && name[dot] != '.')
		dot--;
	return dot >= 0 ? name.substr(dot) : Common::String();
}

} // namespace

Graphics::Surface *decodeImage(const Common::String &name, Common::SeekableReadStream &stream) {
	Common::String ext = extensionOf(name);
	if (ext.equalsIgnoreCase(".GGP"))
		return decodeGGP(stream);
	if (ext.equalsIgnoreCase(".GGD"))
		return decodeGGD8(stream);
	if (ext.equalsIgnoreCase(".DRG"))
		return decodeGGD24(stream);
	if (ext.size() == 4 && ext.substr(0, 3).equalsIgnoreCase(".GG"))
		return decodeGGA(stream);
	return nullptr;
}

} // End of namespace Ikura::Format::Graphic
