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

#include "ai5/animplay.h"
#include "ai5/asset.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/savedata.h"
#include "ai5/sys.h"
#include "ai5/text.h"
#include "ai5/vm.h"
#include "ai5/games/classics.h"

namespace AI5 {

namespace {

enum {
	kMesNameSize = 128,
	kVar4Size = 2048,
	kMem16Size = 4096,

	kVar4Off = kMesNameSize,
	kSv16PtrOff = kVar4Off + kVar4Size,
	kVar16Off = kSv16PtrOff + 4,
	kSysvar16Off = kVar16Off + 26 * 2,
	kVar32Off = kSysvar16Off + 25 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	kHeapOff = kSysvar32Off + 62 * 4
};
static_assert(kHeapOff == 0xa4a, "unexpected Shangrlia heap offset");

enum {
	// size of the map in tiles
	kMapTW = 19,
	kMapTH = 11,
	// size of a tile in pixels
	kTileSize = 32,
	// top-left screen coordinate of the map
	kMapX = 16,
	kMapY = 40,
	kNoUnit = 0xff
};

struct Tile {
	byte tileNo;
	byte unitNo;
};

struct Unit {
	byte present;
	byte index;
	byte tx;
	byte ty;
	byte uk5;
};

struct {
	byte *mapPtr;
	byte *unitPtr;
	byte *unitparaPtr;
	byte *chikeiPtr;
} mapdata;

Unit units[64];
Tile tilemap[kMapTH][kMapTW];

int mouseTX, mouseTY, mouseBtn;

} // End of anonymous namespace

static void shangrliaMemRestore() {
	// XXX: In AI5WIN.EXE, these are 32-bit pointers into the VM's own
	//      address space. We treat them as offsets into Memory (similar
	//      to how AI5WIN.EXE treats 16-bit pointers).
	memSetSysvar16Ptr(kSysvar16Off);
	memSetSysvar32(kSysVarMemory, offsetof(Memory, mem16));
	memSetSysvar32(kSysVarFileData, offsetof(Memory, fileData));
	memSetSysvar32(kSysVarMenuEntryAddresses, offsetof(Memory, menuEntryAddresses));
	memSetSysvar32(kSysVarMenuEntryNumbers, offsetof(Memory, menuEntryNumbers));

	// this value is restored when loading a save via System.SaveData.resume_load...
	memSetSysvar16((uint)0, kHeapOff);
}

static void shangrliaMemInit() {
	// pointer table for memory access
	g_memPtr.mesName = memRaw();
	g_memPtr.var4 = memRaw() + kVar4Off;
	g_memPtr.systemVar16Ptr = memRaw() + kSv16PtrOff;
	g_memPtr.var16 = memRaw() + kVar16Off;
	g_memPtr.systemVar16 = memRaw() + kSysvar16Off;
	g_memPtr.var32 = memRaw() + kVar32Off;
	g_memPtr.systemVar32 = memRaw() + kSysvar32Off;

	memSetSysvar16(kSysVarFlags, 0x260f);
	memSetSysvar16(kSysVarTextStartX, 0);
	memSetSysvar16(kSysVarTextStartY, 0);
	memSetSysvar16(kSysVarTextEndX, g_game->surfaceSizes[0].w);
	memSetSysvar16(kSysVarTextEndY, g_game->surfaceSizes[0].h);
	memSetSysvar16(kSysVarFontWidth, 16);
	memSetSysvar16(kSysVarFontHeight, 16);
	memSetSysvar16(kSysVarCharSpace, 16);
	memSetSysvar16(kSysVarLineSpace, 16);
	memSetSysvar16(kSysVarMaskColor, 0);

	memSetSysvar32(kSysVarCgOffset, 0x20000);
	shangrliaMemRestore();
}

static void shangrliaSavedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: savedataResumeLoad(sysSaveName(params)); break;
	case 1: savedataResumeSave(sysSaveName(params)); break;
	case 2: savedataLoad(sysSaveName(params), kVar4Off); break;
	case 3: savedataSave(sysSaveName(params), kVar4Off); break;
	case 4: savedataLoadVar4(sysSaveName(params)); break;
	case 5: savedataSaveVar4(sysSaveName(params)); break;
	case 6: savedataSaveUnionVar4(sysSaveName(params)); break;
	case 7: savedataLoadVar4Slice(sysSaveName(params), vmExprParam(params, 2), vmExprParam(params, 3)); break;
	case 8: savedataSaveVar4Slice(sysSaveName(params), vmExprParam(params, 2), vmExprParam(params, 3)); break;
	default: error("System.SaveData.function[%u] not implemented", params.params[0].val);
	}
}

static void shangrliaSetSpeaker(ParamList &params) {
	uint no = vmExprParam(params, 0);
	switch (no) {
	case 0: gfxPaletteSetColor(15, 0x88, 0x88, 0x88); break;
	case 1: gfxPaletteSetColor(15, 0x03, 0xaa, 0xff); break;
	case 2: gfxPaletteSetColor(15, 0xff, 0x00, 0xaa); break;
	case 3: gfxPaletteSetColor(15, 0xdd, 0x00, 0xff); break;
	case 4: gfxPaletteSetColor(15, 0x03, 0xff, 0x00); break;
	case 5: gfxPaletteSetColor(15, 0x00, 0xff, 0xff); break;
	case 6: gfxPaletteSetColor(15, 0xff, 0xdd, 0x00); break;
	case 7: gfxPaletteSetColor(15, 0xff, 0xff, 0xff); break;
	case 0xfff: gfxPaletteSetColor(15, 0, 0, 0); break;
	default: warning("Unexpected color index: %u", params.params[0].val);
	}
	if (no < 8)
		animStart(no);
}

static void loadMap(const byte *map) {
	memset(tilemap, 0, sizeof(tilemap));
	if (READ_LE_UINT16(map + 1) != kMapTW)
		error("Unexpected map width: %u", READ_LE_UINT16(map + 1));
	if (READ_LE_UINT16(map + 3) != kMapTH)
		error("Unexpected map height: %u", READ_LE_UINT16(map + 3));

	const byte *p = map + 5;
	for (int row = 0; row < kMapTH; row++) {
		for (int col = 0; col < kMapTW; col++, p += 4) {
			tilemap[row][col].tileNo = *p;
			tilemap[row][col].unitNo = kNoUnit;
		}
	}
}

static void loadUnit(const byte *unit) {
	for (int i = 0; i < 64; i++, unit += 8) {
		units[i].present = unit[0] & 0xf;
		units[i].index = unit[1];
		units[i].tx = unit[3];
		units[i].ty = unit[4];
		units[i].uk5 = unit[5];
	}
}

static void placeUnits() {
	for (int i = 0; i < 64; i++) {
		if (!units[i].present)
			continue;
		tilemap[units[i].ty][units[i].tx].unitNo = i;
	}
}

static void updateMap() {
	loadMap(mapdata.mapPtr);
	loadUnit(mapdata.unitPtr);
	// TODO: unitpara and chikei are not parsed
	placeUnits();
}

static void utilMapInit(ParamList &params) {
	static const uint ptrParams[] = { 2, 3, 4, 5, 7, 8 };
	static const int ptrSizes[] = { 5 + kMapTW * kMapTH * 4, 64 * 8, 1, 1, 1, 1 };
	byte *ptrs[ARRAYSIZE(ptrParams)];
	for (uint i = 0; i < ARRAYSIZE(ptrParams); i++) {
		ptrs[i] = memRaw() + vmExprParam(params, ptrParams[i]);
		if (!memPtrValid(ptrs[i], ptrSizes[i]))
			error("Invalid map pointer in parameter %u: 0x%x", ptrParams[i], params.params[ptrParams[i]].val);
	}
	mapdata.mapPtr = ptrs[0];
	mapdata.unitPtr = ptrs[1];
	mapdata.unitparaPtr = ptrs[2];
	mapdata.chikeiPtr = ptrs[3];
	updateMap();
}

static void drawTile(int col, int row) {
	byte tileNo = tilemap[row][col].tileNo;
	uint tileOrd = (tileNo & 0xf) * 4 + (tileNo >> 4);
	int srcX = (tileOrd % 20) * kTileSize;
	int srcY = (tileOrd / 20) * kTileSize;
	int dstX = kMapX + col * kTileSize;
	int dstY = kMapY + row * kTileSize;
	gfxCopy(srcX, srcY, kTileSize, kTileSize, 1, dstX, dstY, 0);

	int unitNo = tilemap[row][col].unitNo;
	if (unitNo == kNoUnit)
		return;

	if (unitNo < 32) {
		tileOrd = units[unitNo].index + 64;
	} else {
		tileOrd = units[unitNo].index + 42;
		if (tileOrd >= 74)
			tileOrd += 8; // ???
	}
	srcX = (tileOrd % 20) * kTileSize;
	srcY = (tileOrd / 20) * kTileSize;
	gfxCopyMasked(srcX, srcY, kTileSize, kTileSize, 1, dstX, dstY, 0, 0xf);
}

static void drawTileCursor(int tx, int ty) {
	gfxCopyMasked(0, 160, kTileSize, kTileSize, 1, kMapX + tx * kTileSize, kMapY + ty * kTileSize, 0, 0xf);
}

static void drawMap() {
	for (int row = 0; row < kMapTH; row++) {
		for (int col = 0; col < kMapTW; col++)
			drawTile(col, row);
	}
}

static void getMouseState() {
	mouseBtn = 0;

	uint x, y;
	cursorGetPos(x, y);
	if (x >= kMapX && x < 640 - kMapX && y >= kMapY && y < 400 - 8) {
		mouseTX = (x - kMapX) / kTileSize;
		mouseTY = (y - kMapY) / kTileSize;
	}

	if (inputDown(kInputActivate))
		mouseBtn = 1;
	if (inputDown(kInputCancel))
		mouseBtn = 2;
}

static void handleMapInput() {
	// TODO: draw selected info
	do {
		int prevMouseTX = mouseTX;
		int prevMouseTY = mouseTY;
		getMouseState();
		if (prevMouseTX != mouseTX || prevMouseTY != mouseTY) {
			// move tile cursor
			drawTile(prevMouseTX, prevMouseTY);
			drawTileCursor(mouseTX, mouseTY);
		}
		vmPeek();
		gfxUpdate();
	} while (mouseBtn == 0 && !g_engine->shouldQuit());

	if (mouseBtn == 1) {
		memSetVar16(18, (mouseTX << 8) | mouseTY);
		memSetVar16(19, mouseTX);
	} else {
		memSetVar16(18, 0xffff);
	}
}

static void utilMap(ParamList &params) {
	switch (vmExprParam(params, 1)) {
	case 0:
		utilMapInit(params);
		break;
	case 4:
		updateMap();
		drawMap();
		// TODO: draw selected info
		break;
	case 6:
		drawMap();
		break;
	case 5:
		handleMapInput();
		break;
	case 7:
		loadMap(mapdata.mapPtr);
		loadUnit(mapdata.unitPtr);
		placeUnits();
		drawMap();
		break;
	case 1:
		error("Util.Map.function[%u] not implemented", params.params[1].val);
		break;
	default:
		break;
	}
}

static void shangrliaDrawText(const char *text) {
	const uint16 surface = memGetSysvar16(kSysVarDstSurface);
	const uint16 startX = memGetSysvar16(kSysVarTextStartX);
	const uint16 endX = memGetSysvar16(kSysVarTextEndX);
	const uint16 charSpace = memGetSysvar16(kSysVarCharSpace);
	const uint16 lineSpace = memGetSysvar16(kSysVarLineSpace);
	uint16 x = memGetSysvar16(kSysVarTextCursorX);
	uint16 y = memGetSysvar16(kSysVarTextCursorY);
	while (*text) {
		uint32 ch;
		bool zenkaku = sjisIs2Byte(*text);
		uint16 thisCharSpace = zenkaku ? charSpace : charSpace / 2;
		if (x + thisCharSpace > endX + 1) {
			x = startX;
			y += lineSpace;
		}

		text = sjisNextChar(text, ch);
		gfxTextDrawGlyph(x, y, surface, ch);
		x += thisCharSpace;
	}
	memSetSysvar16(kSysVarTextCursorX, x);
	memSetSysvar16(kSysVarTextCursorY, y);
}

static void shangrliaInit() {
	g_assetEffectIsBgm = false;
}

void initGameShangrlia(Game &g) {
	static const struct { uint16 w, h; } sizes[] = {
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 768 },
		{ 640, 768 },
		{ 1280, 800 }
	};
	g.id = GID_SHANGRLIA;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 8;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.memInit = shangrliaMemInit;
	g.memRestore = shangrliaMemRestore;
	g.init = shangrliaInit;
	g.drawTextZen = shangrliaDrawText;
	g.drawTextHan = shangrliaDrawText;

	memset(&mapdata, 0, sizeof(mapdata));
	mouseTX = mouseTY = mouseBtn = 0;

	classicsSetOps(g);

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = classicsCursor;
	g.sys[3] = classicsAnim;
	g.sys[4] = shangrliaSavedata;
	g.sys[5] = classicsAudio;
	g.sys[7] = sysFile;
	g.sys[8] = sysLoadImage;
	g.sys[9] = classicsPalette;
	g.sys[10] = classicsGraphics;
	g.sys[11] = sysWait;
	g.sys[12] = sysSetTextColorsIndexed;
	g.sys[13] = sysFarcall;
	g.sys[14] = classicsGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[17] = utilNoop;
	g.sys[18] = sysCheckInput;
	g.sys[20] = utilNoop;
	g.sys[21] = sysStrlen;
	g.sys[22] = utilNoop;
	g.sys[23] = shangrliaSetSpeaker;

	g.util[0] = utilMap;
	g.util[1] = classicsGetTextColors;

	g.flags[kFlagAnimEnable] = 0x0004;
	g.flags[kFlagMenuReturn] = 0x0008;
	g.flags[kFlagReturn] = 0x0010;
	g.flags[kFlagLog] = 0x0080;
	g.flags[kFlagVoiceEnable] = 0x0100;
	g.flags[kFlagAudioEnable] = kFlagAlwaysOn;
	g.flags[kFlagLoadPalette] = 0x2000;
	g.flags[kFlagWaitKeyup] = kFlagAlwaysOn;
	g.flags[kFlagSkipKeyup] = 0x4000;
	g.flags[kFlagPaletteOnly] = 0x8000;
}

} // End of namespace AI5
