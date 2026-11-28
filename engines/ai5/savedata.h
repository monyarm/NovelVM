/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage
 * (GPL-2.0-or-later).
 */

#ifndef AI5_SAVEDATA_H
#define AI5_SAVEDATA_H

#include "common/scummsys.h"
#include "common/system.h"

namespace Common {
class String;
}

namespace Graphics {
struct Surface;
}

namespace AI5 {

/**
 * Metadata of a wrapped savestate file.
 *
 * Saves are stored under the same "FLAG##" names the games use, but
 * files written by NovelVM carry a header with the description, a
 * thumbnail, the creation date and the play time, so they show up in
 * the launcher and the global main menu like native ScummVM savestates.
 * Files without the header (those shipped with the games) are loaded
 * as plain memory images.
 */
struct SaveHeader {
	Common::String desc;
	Graphics::Surface *thumbnail = nullptr;
	TimeDate td{};
	uint32 playTimeSec = 0;
};

bool savedataReadHeader(const char *saveName, SaveHeader &hdr);

void savedataRead(const char *saveName, byte *buf, uint32 off, uint32 size);
void savedataWrite(const char *saveName, const byte *buf, uint32 off, uint32 size);

// Save the whole memory image and stamp the file with the given
// description (used by the ScummVM save/load dialogs).
void savedataSaveFull(const char *saveName, const Common::String &desc);
void savedataLoadFull(const char *saveName);

// Name of the save file for a savestate slot: "FLAG%02u" (or "FLAG%u"
// for games that use unpadded names, e.g. Shuusaku).
const char *savedataSlotName(uint slot);

void savedataResumeLoad(const char *saveName);
void savedataResumeSave(const char *saveName);

void savedataLoad(const char *saveName, uint start);
void savedataSave(const char *saveName, uint start);

void savedataLoadVar4(const char *saveName);
void savedataLoadVar4Restore(const char *saveName);
void savedataSaveVar4(const char *saveName);
void savedataSaveUnionVar4(const char *saveName);

void savedataLoadVar4Slice(const char *saveName, uint from, uint to);
void savedataSaveVar4Slice(const char *saveName, uint from, uint to);

void savedataCopy(const char *srcSave, const char *dstSave);
void savedataSetMesName(const char *saveName, const char *mesName);
void savedataLoadVariables(const char *saveName, const char *vars);

} // namespace AI5

#endif // AI5_SAVEDATA_H
