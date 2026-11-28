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

#ifndef AI5_MES_H
#define AI5_MES_H

#include "common/scummsys.h"

#include "ai5/detection.h"

namespace AI5 {

enum {
	kMesCodeInvalid = 0xff
};

enum MesParamType {
	kMesParamString = 1,
	kMesParamExpression = 2
};

// Virtual statement opcodes; the real values depend on the game.
enum MesStatementOp {
	kMesStmtEnd,
	kMesStmtZenkaku,
	kMesStmtHankaku,
	kMesStmtSys,
	kMesStmtCallProc,
	kMesStmtOpMax
};

enum MesSysVar16 {
	kSysVarHeap,               // offset to usable heap memory
	kSysVarDstSurface,         // destination surface for graphics operations
	kSysVarFlags,              // VM flags
	kSysVarCursorX,
	kSysVarCursorY,
	kSysVarTextStartX,
	kSysVarTextStartY,
	kSysVarTextEndX,
	kSysVarTextEndY,
	kSysVarTextCursorX,
	kSysVarTextCursorY,
	kSysVarBgColor,            // text colors (bgr555)
	kSysVarFgColor,
	kSysVarDisplayNumberFlags, // System.display_number flags
	kSysVarFontWidth,
	kSysVarFontHeight,
	kSysVarFontWeight,
	kSysVarCharSpace,
	kSysVarLineSpace,
	kSysVarCgX,
	kSysVarCgY,
	kSysVarCgW,
	kSysVarCgH,
	kSysVarNrMenuEntries,
	kSysVarMenuNo,             // return value of System.menu_get_no
	kSysVarMaskColor,          // mask color for graphics operations
	kSysVar16Max
};

enum MesSysVar32 {
	kSysVarMemory,             // pointer to memory
	kSysVarCgOffset,           // offset to CG file in file_data
	kSysVarDataOffset,         // offset to data file in file_data
	kSysVarMpxOffset,          // map files
	kSysVarCcdOffset,
	kSysVarEveOffset,
	kSysVarPalette,            // pointer to palette
	kSysVarA6Offset,           // offset to A6 file in file_data
	kSysVarFileData,           // pointer to file_data
	kSysVarMenuEntryAddresses, // pointer to menu_entry_addresses
	kSysVarMenuEntryNumbers,   // pointer to menu_entry_numbers
	kSysVarMapData,            // offset to map file (dungeon)
	kSysVarMaskColor24,        // 24-bit mask color
	kSysVar32Max
};

/**
 * Game-specific opcode and system variable numbering.
 */
struct MesCodeTables {
	byte stmtOpToInt[kMesStmtOpMax];
	byte sysvar16ToInt[kSysVar16Max];
	byte sysvar32ToInt[kSysVar32Max];
};

extern MesCodeTables g_mesCodes;

void mesSetGame(GameId game);

inline byte sysvar16(MesSysVar16 v) { return g_mesCodes.sysvar16ToInt[v]; }
inline byte sysvar32(MesSysVar32 v) { return g_mesCodes.sysvar32ToInt[v]; }

inline bool mesCharIsHankaku(byte b) {
	return !(b >= 0x81 && b <= 0x9f) && !(b >= 0xe0 && b <= 0xef);
}

inline bool mesCharIsZenkaku(byte b) {
	return (b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xef) || (b >= 0xfa && b <= 0xfc);
}

} // End of namespace AI5

#endif // AI5_MES_H
