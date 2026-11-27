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

#include "common/textconsole.h"

#include "ai5/mes.h"

namespace AI5 {

MesCodeTables g_mesCodes;

static const MesCodeTables defaultTables = {
	// stmtOpToInt: END, ZENKAKU, HANKAKU, SYS, CALL_PROC
	{ 0x00, 0x01, 0x02, 0x0b, 0x0f },
	// sysvar16ToInt
	{
		0,  // heap
		1,  // dst surface
		2,  // flags
		3,  // cursor x
		4,  // cursor y
		5,  // text start x
		6,  // text start y
		7,  // text end x
		8,  // text end y
		9,  // text cursor x
		10, // text cursor y
		11, // bg color
		12, // fg color
		13, // display number flags
		14, // font width
		15, // font height
		16, // font weight
		17, // char space
		18, // line space
		19, // cg x
		20, // cg y
		21, // cg w
		22, // cg h
		24, // nr menu entries
		25, // menu no
		23  // mask color
	},
	// sysvar32ToInt
	{
		0,               // memory
		1,               // cg offset
		2,               // data offset
		3,               // mpx offset
		4,               // ccd offset
		5,               // eve offset
		kMesCodeInvalid, // palette
		6,               // a6 offset
		7,               // file data
		8,               // menu entry addresses
		9,               // menu entry numbers
		10,              // map data
		11               // mask color 24
	}
};

static const MesCodeTables classicsTables = {
	{ 0x00, 0x01, 0x02, 0x0d, 0x11 },
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, kMesCodeInvalid, kMesCodeInvalid, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 },
	{ 0, 1, 2, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, 5, kMesCodeInvalid, 7, 8, 9, kMesCodeInvalid, kMesCodeInvalid }
};

static const MesCodeTables aiShimaiTables = {
	{ 0x00, 0x01, 0x02, 0x0b, 0x0f },
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, kMesCodeInvalid, kMesCodeInvalid, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, kMesCodeInvalid },
	{ 0, 1, 2, 3, 4, 5, kMesCodeInvalid, 6, 7, 8, 9, 10, 11 }
};

static const MesCodeTables allstarsTables = {
	{ 0x00, 0x01, 0x02, 0x0b, 0x0f },
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, kMesCodeInvalid, kMesCodeInvalid, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 },
	{ 0, 1, 2, 3, 4, 5, kMesCodeInvalid, 6, 7, 8, 9, 10, 11 }
};

static const MesCodeTables kakyuuseiTables = {
	{ 0x00, 0x01, 0x02, 0x0b, 0x0f },
	{ 0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, kMesCodeInvalid, 13, 14, 15, 16, 17, 18, 21, 22, 23, 24, 26, 27, 25 },
	{ kMesCodeInvalid, 0, 1, 2, 3, 4, kMesCodeInvalid, 5, 6, 7, 8, 9, kMesCodeInvalid }
};

static const MesCodeTables shuusakuTables = {
	{ 0x00, 0x01, 0x02, 0x0b, 0x0f },
	{ kMesCodeInvalid, 12, 14, kMesCodeInvalid, kMesCodeInvalid, 2, 3, 4, 5, 0, 1, 7, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, 18, 8, 9, 10, 11, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid },
	{ kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid, kMesCodeInvalid }
};

void mesSetGame(GameId game) {
	switch (game) {
	case GID_YUNO:
	case GID_SHANGRLIA:
		g_mesCodes = classicsTables;
		break;
	case GID_AISHIMAI:
		g_mesCodes = aiShimaiTables;
		break;
	case GID_BEYOND:
		g_mesCodes = allstarsTables;
		break;
	case GID_KAKYUUSEI:
	case GID_DOUKYUUSEI2:
		g_mesCodes = kakyuuseiTables;
		break;
	case GID_KAWARAZAKIKE:
	case GID_SHUUSAKU:
		g_mesCodes = shuusakuTables;
		break;
	default:
		g_mesCodes = defaultTables;
		break;
	}
}

} // End of namespace AI5
