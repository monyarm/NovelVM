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

#ifndef AI5_SYS_H
#define AI5_SYS_H

#include "common/scummsys.h"

namespace AI5 {

struct ParamList;

// Common System and Util functions. Functions taking a command parameter
// usually differ subtly between games and live in the game modules.
void utilWarnUnimplemented(ParamList &params);
void utilNoop(ParamList &params);
const char *sysSaveNameFmt(const char *fmt, uint saveNo);
const char *sysSaveNameNo(uint saveNo);
const char *sysSaveName(ParamList &params);
void sysSetFontSize(ParamList &params);
const char *sysNumberToStringDigits(uint32 n, uint displayDigits, bool halfwidth);
const char *sysNumberToString(uint32 n);
void sysDisplayNumber(ParamList &params);
void sysCursorSavePos(ParamList &params);
void sysFile(ParamList &params);
void sysLoadFile(ParamList &params);
void sysLoadImageAt(const char *name, uint i, uint xMult);
void sysLoadImage(ParamList &params);
void sysGraphicsCopy(ParamList &params);
void sysGraphicsCopyMasked(ParamList &params);
void sysGraphicsCopyMasked24(ParamList &params);
void sysGraphicsFillBg(ParamList &params);
void sysGraphicsCopySwap(ParamList &params);
void sysGraphicsSwapBgFg(ParamList &params);
void sysGraphicsCompose(ParamList &params);
void sysGraphicsBlend(ParamList &params);
void sysGraphicsBlendMasked(ParamList &params);
void sysGraphicsInvertColors(ParamList &params);
void sysGraphicsCopyProgressive(ParamList &params);
void sysWait(ParamList &params);
void sysSetTextColorsIndexed(ParamList &params);
void sysSetTextColorsIndexedWithSysvar(ParamList &params);
void sysSetTextColorsDirect(ParamList &params);
void sysFarcall(ParamList &params);
void sysGetCursorSegmentAt(uint x, uint y, uint32 off);
void sysGetCursorSegment(ParamList &params);
void sysMenuGetNo(ParamList &params);
void sysCheckInput(ParamList &params);
void sysStrlen(ParamList &params);
void sysGetTime(ParamList &params);

} // End of namespace AI5

#endif // AI5_SYS_H
