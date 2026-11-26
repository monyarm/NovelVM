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

#ifndef AI5_SAVEDATA_H
#define AI5_SAVEDATA_H

#include "common/scummsys.h"

namespace AI5 {

/**
 * Read bytes from a save file into a buffer. off is both the read offset in
 * the file and the write offset in the buffer.
 */
void savedataRead(const char *saveName, byte *buf, uint32 off, uint32 size);
/**
 * Write bytes from a buffer into a save file. off is both the read offset in
 * the buffer and the write offset in the file.
 */
void savedataWrite(const char *saveName, const byte *buf, uint32 off, uint32 size);
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

} // End of namespace AI5

#endif // AI5_SAVEDATA_H
