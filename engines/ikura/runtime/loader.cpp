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

#include "ikura/runtime/loader.h"

#include "common/path.h"

namespace Ikura {

VM::Context *loadTitleScript(Common::Archive *cabinet) {
	if (!cabinet)
		return nullptr;

	Common::SeekableReadStream *scriptStream = cabinet->createReadStreamForMember(Common::Path("TITLE.ISF"));
	if (!scriptStream)
		return nullptr;

	Format::Script::Script *script = Format::Script::Script::load(*scriptStream);
	delete scriptStream;
	if (!script)
		return nullptr;

	VM::Context *ctx = new VM::Context(script);
	ctx->setScriptCabinet(cabinet);
	return ctx;
}

} // End of namespace Ikura
