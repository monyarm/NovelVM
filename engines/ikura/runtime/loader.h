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

#ifndef IKURA_RUNTIME_LOADER_H
#define IKURA_RUNTIME_LOADER_H

#include "common/archive.h"
#include "ikura/script/context.h"

namespace Ikura {

// Loads the entry-point script out of an already-open "isf" cabinet. The
// reference always boots a game by loading a script named "TITLE"
// (VileVN reference: IkuraDecoder::EventGameDialog(VD_TITLE),
// src/ikura/ikuradecoder.cpp - LoadScript("TITLE", "ISF")).
//
// cabinet is not owned and must outlive the returned Context - IOP_LS/
// IOP_LSBS look up other named scripts through it for as long as the
// script keeps running (see Context::setScriptCabinet, wired up here).
// Returns nullptr if cabinet is null or has no TITLE.ISF entry.
VM::Context *loadTitleScript(Common::Archive *cabinet);

} // End of namespace Ikura

#endif
