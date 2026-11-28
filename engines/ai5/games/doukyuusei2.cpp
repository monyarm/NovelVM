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

#include "ai5/ai5.h"
#include "ai5/animplay.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/backlog.h"
#include "ai5/cursor.h"
#include "ai5/effect.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/map.h"
#include "ai5/menu.h"
#include "ai5/savedata.h"
#include "ai5/sys.h"
#include "ai5/text.h"
#include "ai5/vm.h"
#include "ai5/games/doukyuusei.h"

namespace AI5 {

#include "ai5/games/doukyuusei2_pal.h"

namespace {

enum {
	kMesNameSize = 128,
	kVar4Size = 2048,
	kMem16Size = 4096,
	kVar4Off = kMesNameSize,
	kSv16PtrOff = kVar4Off + kVar4Size,
	kVar16Off = kSv16PtrOff + 4,
	kSysvar16Off = kVar16Off + 26 * 2,
	kVar32Off = kSysvar16Off + 28 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	kHeapOff = kSysvar32Off + 110 * 4
};
static_assert(kHeapOff == 0xb10, "unexpected Doukyuusei 2 heap offset");

} // End of anonymous namespace
#define PALETTE_LOG(...) debug(5, __VA_ARGS__)

static void nanpa2MemRestore() {
	memSetSysvar16Ptr(kSysvar16Off);
	memSetSysvar32(kSysVarFileData, offsetof(Memory, fileData));
	memSetSysvar32(kSysVarMenuEntryAddresses, offsetof(Memory, menuEntryAddresses));
	memSetSysvar32(kSysVarMenuEntryNumbers, offsetof(Memory, menuEntryNumbers));
	memSetSysvar32(kSysVarMapData, offsetof(Memory, mapData));
	memSetSysvar16((uint)0, kHeapOff);
}

static void nanpa2MemInit() {
	// pointer table for memory access
	g_memPtr.mesName = memRaw();
	g_memPtr.var4 = memRaw() + kVar4Off;
	g_memPtr.systemVar16Ptr = memRaw() + kSv16PtrOff;
	g_memPtr.var16 = memRaw() + kVar16Off;
	g_memPtr.systemVar16 = memRaw() + kSysvar16Off;
	g_memPtr.var32 = memRaw() + kVar32Off;
	g_memPtr.systemVar32 = memRaw() + kSysvar32Off;

	memSetSysvar16(kSysVarFlags, 0x27);
	memSetSysvar16(kSysVarTextStartX, 0);
	memSetSysvar16(kSysVarTextStartY, 0);
	memSetSysvar16(kSysVarTextEndX, 640);
	memSetSysvar16(kSysVarTextEndY, 480);
	memSetSysvar16(kSysVarBgColor, 7);
	memSetSysvar16(kSysVarFontWidth, 16);
	memSetSysvar16(kSysVarFontHeight, 16);
	memSetSysvar16(kSysVarFontWeight, 1);
	memSetSysvar16(kSysVarCharSpace, 16);
	memSetSysvar16(kSysVarLineSpace, 16);
	memSetSysvar16(19, 0xffff);
	memSetSysvar16(20, 0xffff);
	memSetSysvar16(kSysVarMaskColor, 8);
	memSetSysvar32(kSysVarCgOffset, 0x20000);
	nanpa2MemRestore();
}

static void nanpa2MenuExec() {
	uint16 savedFlags = memGetSysvar16(kSysVarFlags);
	vmFlagOff(kFlagLogEnable);
	menuExec();
	memSetSysvar16(kSysVarFlags, savedFlags);
}

static void nanpa2Cursor(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: cursorShow(); break;
	case 1: cursorHide(); break;
	case 3: sysCursorSavePos(params); break;
	case 4: cursorSetPos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 5: cursorLoad(vmExprParam(params, 1) * 2, 2, nullptr); break;
	default: error("System.Cursor.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void nanpa2Anim(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: animInitStream(vmExprParam(params, 1), vmExprParam(params, 1)); break;
	case 1: animStart(vmExprParam(params, 1)); break;
	case 2: animStop(vmExprParam(params, 1)); break;
	case 3: animHalt(vmExprParam(params, 1)); break;
	case 4: animWait(vmExprParam(params, 1)); break;
	case 5: animStopAll(); break;
	case 6: animHaltAll(); break;
	case 7: animRestartAll(); break;
	default: error("System.Anim.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void nanpa2Savedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: savedataResumeLoad(sysSaveName(params)); break;
	case 1: savedataResumeSave(sysSaveName(params)); break;
	case 2: savedataLoad(sysSaveName(params), kVar4Off); break;
	default: error("System.SaveData.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void nanpa2Audio(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: audioBgmPlay(vmStringParam(params, 1), true); break;
	case 1: audioFade(kAudioChBgm, kAudioVolumeMin, 1000, true, false); break;
	case 2: audioFade(kAudioChBgm, kAudioVolumeMin, 250, true, false); break;
	case 3: audioSePlay(vmStringParam(params, 1), 0); break;
	case 4: audioSeFade(kAudioVolumeMin, 250, true, false, 0); break;
	default: error("System.Audio.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void nanpa2Voice(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: audioVoicePlay(vmStringParam(params, 1), 0); break;
	case 1: audioVoiceStop(0); break;
	case 2:
		audioVoicePlay(vmStringParam(params, 1), 0);
		while (audioIsPlaying(kAudioChVoice0) && !g_engine->shouldQuit())
			vmPeek();
		break;
	default: error("System.Voice.function[%u] not implemented",
				 params.params[0].val);
	}
}

static bool nanpa2FixedCrossfadeTick(GfxColor newPal[236]) {
	bool changed = false;
	for (int i = 0; i < 236; i++) {
		GfxColor *curC = &g_gfx.palette[i];
		GfxColor *newC = &newPal[i];
		if (curC->r < newC->r) {
			curC->r++;
			changed = true;
		} else if (curC->r > newC->r) {
			curC->r--;
			changed = true;
		}
		if (curC->g < newC->g) {
			curC->g++;
			changed = true;
		} else if (curC->g > newC->g) {
			curC->g--;
			changed = true;
		}
		if (curC->b < newC->b) {
			curC->b++;
			changed = true;
		} else if (curC->b > newC->b) {
			curC->b--;
			changed = true;
		}
	}
	if (changed)
		gfxUpdatePalette(0, 236);
	return changed;
}

static void memToSdlPalette(GfxColor newPal[236]) {
	for (int i = 0; i < 236; i++) {
		newPal[i].b = g_mem->palette[i*4];
		newPal[i].g = g_mem->palette[i*4+1];
		newPal[i].r = g_mem->palette[i*4+2];
	}
}

/*
 * This is a crossfade in which each color moves at a fixed velocity towards the target
 * color (so some colors will reach their target before others).
 */
static void nanpa2FixedCrossfade() {
	GfxColor newPal[236];
	memToSdlPalette(newPal);

	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < 256; i++) {
		if (!nanpa2FixedCrossfadeTick(newPal))
			break;
		vmPeek();
		vmTimerTick(timer, 20);
	}
}

static bool asyncCrossfadeActive = false;
static GfxColor asyncCrossfadePalette[236];
static VMTimer asyncCrossfadeT;

static void nanpa2Palette(ParamList &params) {
	if (vmFlagIsOn(kFlagSavePalette)) {
		PALETTE_LOG("(Palette saved)");
		memcpy(g_mem->savedPalette, g_mem->palette, 256 * 4);
	}
	switch (vmExprParam(params, 0)) {
	case 1:
		if (params.nrParams > 1) {
			memset(g_mem->palette, (byte)vmExprParam(params, 1), 236 * 4);
			PALETTE_LOG("Palette.crossfade(%u)", params.params[1].val);
		} else {
			PALETTE_LOG("Palette.crossfade()");
		}
		gfxPaletteCrossfade(g_mem->palette, 0, 236, 400);
		gfxPaletteCopy(g_mem->palette, 0, 256);
		break;
	case 2:
		PALETTE_LOG("Palette.crossfadeRange(%d, %d)", vmExprParam(params, 1),
				vmExprParam(params, 2));
		gfxPaletteCrossfade(g_mem->palette, vmExprParam(params, 1),
				vmExprParam(params, 2), 400);
		gfxPaletteCopy(g_mem->palette, 0, 256);
		break;
	case 5: {
		int start = vmExprParam(params, 1);
		int n = vmExprParam(params, 2);
		PALETTE_LOG("Palette.set(%d, %d)", start, n);
		gfxPaletteSetNoUpdate(g_mem->palette + start * 4, start, n);
		gfxUpdatePaletteNoDirty(start, n);
		break;
	}
	case 6:
		// Util 102/114 change whether this is a normal or 'fixed'-style crossfade.
		// In practice I think it's always a 'fixed'-style crossfade
		// Range of crossfade is always 16-236
		memToSdlPalette(asyncCrossfadePalette);
		asyncCrossfadeT = vmTimerCreate();
		asyncCrossfadeActive = true;
		break;
	case 7:
		if (params.nrParams > 2) {
			memset(g_mem->palette, (byte)vmExprParam(params, 2), 236 * 4);
			PALETTE_LOG("Palette_crossfade2(%u,%u)", params.params[1].val,
					params.params[2].val);
		}
		else {
			PALETTE_LOG("Palette_crossfade2(%u)", params.params[1].val);
		}
		if (vmExprParam(params, 1) == 0xff) {
			nanpa2FixedCrossfade();
		} else {
			gfxPaletteCrossfade(g_mem->palette, 0, 236, vmExprParam(params, 1) * 50);
		}
		gfxPaletteCopy(g_mem->palette, 0, 256);
		break;
	default: error("System.Palette.function[%u] not implemented",
				 params.params[0].val);
	}
	if (vmFlagIsOn(kFlagLoadPalette)) {
		PALETTE_LOG("(Palette restored)");
		memcpy(g_mem->palette, g_mem->savedPalette, 256 * 4);
	}
}

static void nanpa2PixelCrossfade(ParamList &params) {
	int srcX = vmExprParam(params, 1);
	int srcY = vmExprParam(params, 2);
	int srcW = (vmExprParam(params, 3) - srcX) + 1;
	int srcH = (vmExprParam(params, 4) - srcY) + 1;
	uint srcI = vmExprParam(params, 5);
	int dstX = vmExprParam(params, 6);
	int dstY = vmExprParam(params, 7);
	uint dstI = vmExprParam(params, 8);
	gfxPixelCrossfadeMaskedIndexed8x8(srcX, srcY, srcW, srcH, srcI,
			dstX, dstY, dstI, 255, 10);
}

static void nanpa2Graphics(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: sysGraphicsCopy(params); break;
	case 1: sysGraphicsCopyMasked(params); break;
	case 2: sysGraphicsFillBg(params); break;
	case 4: sysGraphicsSwapBgFg(params); break;
	case 5: nanpa2PixelCrossfade(params); break;
	case 6: sysGraphicsCompose(params); break;
	default: error("System.Graphics.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void nanpa2MapDrawWaku() {
	int srcX, srcY;
	int month = memGetVar16(7) / 100;
	int day = memGetVar16(7) % 100;
	int dayOfWeek = memGetVar16(4);
	int hour = memGetVar16(19) / 100;
	int minute = memGetVar16(19) % 100;
	int money = memGetVar16(12);
	int location = memGetVar4(2047);

	gfxCopy(0, 8, 640, 80, 0, 0, 0, 4); // save map bg to surface 4
	gfxCopy(0, 80, 88, 80, 5, 8, 8, 0); // draw datetime frame
	gfxCopy(92, 80, 88, 80, 5, 544, 8, 0); // draw location/money frame

	// month
	if (month == 12) {
		srcX = 546;
	} else {
		srcX = 570;
	}
	gfxCopy(srcX, 144, 24, 36, 5, 12, 12, 0);

	// day (digit)
	int dayI = day;
	if (dayI > 7)
		dayI -= 8;
	dayI--;
	gfxCopy(432 + (dayI % 5) * 38, (dayI / 5) * 36, 38, 36, 5, 36, 12, 0);

	// day of week
	if (day == 1) {
		// new year's day
		srcX = 594;
		srcY = 144;
	} else if (day == 23) {
		// emperor's birthday
		srcX = 608;
		srcY = 144;
	} else if (dayOfWeek == 0) {
		// sunday
		srcX = 608;
		srcY = 180;
	} else {
		// normal day
		srcX = 622;
		srcY = (dayOfWeek - 1) * 36;
	}
	gfxCopy(srcX, srcY, 14, 36, 5, 78, 12, 0);

	// AM/PM
	if (hour / 12 == 1) {
		srcX = 326;
	} else {
		srcX = 312;
	}
	gfxCopy(srcX, 132, 14, 32, 5, 12, 51, 0);

	// hour
	if (hour > 12)
		hour -= 12;
	if (hour >= 10) {
		gfxCopy(342, 80, 4, 28, 5, 32, 54, 0);
		hour -= 10;
	}
	gfxCopy(198 + hour * 14, 80, 14, 28, 5, 38, 54, 0);

	// minute
	gfxCopy(198 + (minute / 10) * 14, 80, 14, 28, 5, 60, 54, 0);
	gfxCopy(198 + (minute % 10) * 14, 80, 14, 28, 5, 76, 54, 0);

	// money
	if (money >= 1000)
		gfxCopy(328, 108, 4, 24, 5, 586, 58, 0);

	int place = 4;
	do {
		int dstX = 562 + place * 12 + (place > 1 ? 4 : 0);
		gfxCopy(208 + (money % 10) * 12, 108, 12, 24, 5, dstX, 56, 0);
		money /= 10;
		place--;
	} while (money && place >= 0);

	// location
	if (location == 0) {
		srcX = 448;
		srcY = 216;
	} else if (location == 6) {
		srcX = 448;
		srcY = 252;
	} else {
		srcX = 528;
		srcY = 216 + (location - 1) * 36;
	}
	gfxCopy(srcX, srcY, 80, 36, 5, 548, 12, 0);
}

static void nanpa2MapDrawTiles() {
	mapDrawTiles();
	nanpa2MapDrawWaku();
}

static void nanpa2MapDrawTiles2() {
	gfxCopy(0, 0, 640, 80, 4, 0, 8, 0);
	nanpa2MapDrawTiles();
}

static void nanpa2MapExecSpritesAndRedraw() {
	mapExecSprites();
	mapLoadTiles();
	mapPlaceSprites();
	nanpa2MapDrawTiles2();
	if (memGetVar16(18) == 0xfffe) {
		// XXX: bug (misfeature?) in game: single click can open and close map
		//      if held too long
		inputWaitUntilUp(kInputCancel);
	}
}

static void nanpa2MapSetSpriteScript(int spNo, int scriptNo) {
	if (!mapIsPathing())
		mapSetSpriteScript(spNo, scriptNo);
}

static void nanpa2Map(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: mapLoadTilemap(); break;
	case 1: mapSpawnSprite(vmExprParam(params, 1), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	case 2: mapLoadTiles(); break;
	case 4: mapLoadSpriteScripts(); break;
	case 5: nanpa2MapSetSpriteScript(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 6: mapPlaceSprites(); break;
	case 7: mapSetSpriteState(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 8: nanpa2MapExecSpritesAndRedraw(); break;
	case 9: mapExecSprites(); break;
	case 10: nanpa2MapDrawTiles(); break;
	case 11: nanpa2MapDrawTiles2(); break;
	case 12: mapSetLocationMode((MapLocationMode)vmExprParam(params, 1)); break;
	case 13: mapGetLocation(); break;
	case 14: mapMoveSprite(vmExprParam(params, 1), (MapDirection)vmExprParam(params, 2)); break;
	case 15: mapPathSprite(vmExprParam(params, 1), vmExprParam(params, 2),
				 vmExprParam(params, 3)); break;
	case 16: mapStopPathing(); break;
	case 17: mapGetPathing(); break;
	case 20: mapRewindSpritePos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 21: mapSkipPathing(0); break;
	case 22: mapSetSpriteAnim(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	default: error("System.Map.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void nanpa2Backlog(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: backlogClear(); break;
	case 1: backlogPrepareOld(); break;
	case 2: backlogCommitOld(); break;
	case 3: {
		uint count = backlogCount();
		memSetVar16(18, count ? count - 1 : 0xffff);
		break;
	}
	case 4: memSetVar32(18, backlogGetPointer(vmExprParam(params, 1))); break;
	default: error("System.Backlog.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void nanpa2Checkdisc(ParamList &params) {
	// XXX: We don't support reading from CD-ROM, so we treat all discs as being present
	//      at all times (the user must manually install all .ARC files into the game
	//      folder).
	memSetVar16(18, 1);
}

static void nanpa2Sys20(ParamList &params) {
	while (!inputDown(kInputActivate) && !inputDown(kInputCancel) && !g_engine->shouldQuit())
		vmPeek();
	while ((inputDown(kInputActivate) || inputDown(kInputCancel)) && !g_engine->shouldQuit())
		vmPeek();
}

static byte savedMemory[kMem16Size];

static void nanpa2SaveMemory(ParamList &params) {
	memcpy(savedMemory, memRaw(), kMem16Size);
}

static void nanpa2RestoreMemory(ParamList &params) {
	memcpy(memRaw() + 0x83a, savedMemory + 0x83a, 29);
	memcpy(memRaw() + 0xef8, savedMemory + 0xef8, 36);
	memRaw()[kVar4Off + 2030] = savedMemory[kVar4Off + 2030];
}

static void nanpa2SavePalette(ParamList &params) {
	int start = vmExprParam(params, 1);
	int n = vmExprParam(params, 2);
	PALETTE_LOG("nanpa2SavePalette(%d, %d)", start, n);
	if (n < 0)
		return;
	if (start + n > 256) {
		warning("Invalid range in restorePalette");
		n = 256 - start;
	}
	memcpy(g_mem->savedPalette + start * 4, g_mem->palette + start * 4, n * 4);
}

static void nanpa2RestorePalette(ParamList &params) {
	PALETTE_LOG("nanpa2RestorePalette()");
	memcpy(g_mem->palette, g_mem->savedPalette, 256 * 4);
}

static void nanpa2LoadLowPalette(ParamList &params) {
	PALETTE_LOG("nanpa2LoadLowPalette[%d]", memGetVar4(2024));
	int n = memGetVar4(2024) == 1 ? 10 : 16;
	memcpy(g_mem->palette, nanpa2LowPalette, n * 4);
}

static void nanpa2CtrlIsDown(ParamList &params) {
	memSetVar16(18, inputDown(kInputCtrl));
}

static void nanpa2ActivateIsDown(ParamList &params) {
	memSetVar16(18, inputDown(kInputActivate));
}

static void nanpa2WaitForActivateCancelUp(ParamList &params) {
	while (inputDown(kInputActivate) && !g_engine->shouldQuit())
		vmPeek();
	while (inputDown(kInputCancel) && !g_engine->shouldQuit())
		vmPeek();
}

#define MAP_FRAME_TIME 54

static VMTimer nanpa2MapTimer = 0;

static void nanpa2MapWait(ParamList &params) {
	vmTimerTick(nanpa2MapTimer, inputDown(kInputShift) ? MAP_FRAME_TIME/2 : MAP_FRAME_TIME);
}

static void nanpa2LoadMidPalette(ParamList &params) {
	memcpy(g_mem->palette + 16*4, nanpa2MidPalette, 48 * 4);
}

static void nanpa2Util105(ParamList &params) {
	PALETTE_LOG("nanpa2Util105");
	int n = memGetVar4(2024) == 1 ? 10 : 16;
	gfxPaletteCopy(g_mem->savedPalette, 0, 236);
	memcpy(g_mem->palette + n*4, g_mem->savedPalette + n*4, (256 - n) * 4);
}

static void nanpa2Util106(ParamList &params) {
	PALETTE_LOG("nanpa2Util106");
	int n = memGetVar4(2024) == 1 ? 10 : 16;
	memcpy(g_mem->palette + n*4, g_mem->savedPalette + n*4, (256 - n) * 4);
}

static void nanpa2SpLoad(ParamList &params) {
	savedataRead("flag06", memRaw(), 0xf5c, 14);
}

static void nanpa2CancelIsDown(ParamList &params) {
	memSetVar16(18, inputDown(kInputCancel));
}

static void nanpa2CrossfadeStaticPalette(ParamList &params) {
	PALETTE_LOG("nanpa2CrossfadeStaticPalette()");
	memcpy(g_mem->palette, nanpa2StaticPalette, 256 * 4);
	nanpa2FixedCrossfade();
	gfxPaletteCopy(g_mem->palette, 0, 256);
}

static void nanpa2Util110(ParamList &params) {
	uint i = vmExprParam(params, 1) * 4 + vmExprParam(params, 2);
	if (i >= 16)
		error("Invalid palette number: %u", i);

	memcpy(g_mem->palette + 16 * 4, nanpa2MapPalettes[i], 172 * 4);
}

static void nanpa2Util111(ParamList &params) {
	// TODO: add number to backlog
}

static void nanpa2LoadEndSepiaPalette(ParamList &params) {
	uint n = vmExprParam(params, 1) * 4 + vmExprParam(params, 2);
	if (n >= 64) {
		warning("Invalid palette number");
		return;
	}
	memcpy(g_mem->palette + 16 * 4, nanpa2EndSepiaPalette[n], 220 * 4);
}

static void nanpa2LoadEndColorPalette(ParamList &params) {
	uint n = vmExprParam(params, 1) * 4 + vmExprParam(params, 2);
	if (n >= 64) {
		warning("Invalid palette number");
		return;
	}
	memcpy(g_mem->palette + 16 * 4, nanpa2EndColorPalette[n], 220 * 4);
}

static void nanpa2InitEndCrossfade(ParamList &params) {
	memcpy(g_mem->palette, nanpa2EndLowPalette, 16 * 4);
}

static void nanpa2ClearHighPalette(ParamList &params) {
	PALETTE_LOG("nanpa2ClearHighPalette()");
	memset(g_mem->palette + 16 * 4, 0, 220 * 4);
}

static void nanpa2Wait(ParamList &params) {
	int n = vmExprParam(params, 1);
	VMTimer t = vmTimerCreate();
	for (int i = 0; i < n; i++) {
		vmPeek();
		vmTimerTick(t, 15);
	}
}

static bool paletteEqual(byte *a, byte *b, int start, int end) {
	for (int i = start; i < end; i++) {
		if (memcmp(a + i * 4, b + i * 4, 3))
			return false;
	}
	return true;
}

static void nanpa2HanaCrossfade(ParamList &params) {
	memcpy(g_mem->palette, nanpa2HanaPalette, 256 * 4);
	nanpa2FixedCrossfade();
	gfxPaletteCopy(g_mem->palette, 0, 256);
}

static void nanpa2Util120(ParamList &params) {
	PALETTE_LOG("nanpaUtil120()");
	byte pal[236 * 4];
	gfxPaletteCopy(pal, 0, 236);
	memcpy(g_mem->palette, g_mem->savedPalette, 236 * 4);

	// if background palette (128-236) is equal, return 0
	if (paletteEqual(g_mem->palette, pal, 128, 236)) {
		PALETTE_LOG("(equal)");
		memSetVar16(18, 0);
		return;
	}

	// otherwise crossfade to black and return 1
	PALETTE_LOG("(not equal)");
	memset(g_mem->palette + 16 * 4, 0, 220 * 4);
	gfxPaletteCrossfade(g_mem->palette, 0, 236, 400);
	gfxPaletteCopy(g_mem->palette, 0, 256);
	memSetVar16(18, 1);
}

static void nanpa2Util122(ParamList &params) {
	PALETTE_LOG("nanpaUtil122()");
	gfxPaletteCopy(g_mem->palette, 0, 236);
	memcpy(g_mem->palette + 16 * 4, g_mem->savedPalette + 16 * 4, 112 * 4);
}

static void nanpa2Util123(ParamList &params) {
	PALETTE_LOG("nanpaUtil123()");
	byte pal[236 * 4];
	gfxPaletteCopy(pal, 0, 236);

	int n = memGetVar4(2024) == 1 ? 10 : 16;
	memcpy(g_mem->savedPalette, nanpa2LowPalette, n * 4);
	memcpy(g_mem->palette, g_mem->savedPalette, 236 * 4);

	// if background and character palettes (10 - 236) are equal, return 0
	if (paletteEqual(g_mem->palette, pal, 10, 236)) {
		PALETTE_LOG("(equal)");
		memSetVar16(18, 0);
		return;
	}

	// otherwise crossfade to black and return 1
	PALETTE_LOG("(not equal)");
	memset(g_mem->palette + 10 * 4, 0, 226 * 4);
	gfxPaletteCrossfade(g_mem->palette, 0, 236, 400);
	gfxPaletteCopy(g_mem->palette, 0, 256);
	gfxFill(0, 0, 640, 400, 0, kSysVarBgColor >> 4);
	g_gfx.surface[g_gfx.screen].dirty = false;
	memSetVar16(18, 1);
}

static void nanpa2AnimLoadPalette(byte *src) {
	byte pal[256 * 4];
	byte *dst = pal;
	for (int i = 0; i < 236; i++, src += 3, dst += 4) {
		// XXX: BRG -> BGR
		dst[0] = src[0];
		dst[1] = src[2];
		dst[2] = src[1];
		dst[3] = 1;
	}
	gfxPaletteSet(pal, 0, 236);
}

static void nanpa2Init() {
	bool haveBoy = assetSetVoiceArchive("boy.arc");
	bool haveGirl = assetSetVoice2Archive("girl.arc");
	bool havePr = assetSetVoice3Archive("pr.arc");
	bool haveSp = assetSetVoice4Archive("sp.arc");
	if (!haveBoy || !haveGirl || !havePr || !haveSp) {
		Common::String msg = Common::String::format("Missing voice archives:\n%s%s%s%s"
				"Voice archives from both game discs must be copied into "
				"the game directory,\notherwise some voices may be missing.",
				haveBoy ? "" : "\t\"boy.arc\"\n",
				haveGirl ? "" : "\t\"girl.arc\"\n",
				havePr ? "" : "\t\"pr.arc\"\n",
				haveSp ? "" : "\t\"sp.arc\"\n");
		gfxWarningMessage(msg.c_str());
	}

	g_textShadow = kTextShadowA;
	gfxUpdatePalette(0, 256);
	g_animLoadPalette = nanpa2AnimLoadPalette;
}

enum {
	kMaxPetals = 30,
	kMaxFlakes = 200
};

struct Petal {
	bool active;
	bool altAnim;
	int size;
	int speed;
	struct { int x, y; } pos;
	int frame;
	int frameTick;
};

struct {
	bool enabled;
	uint nrPetals;
	Petal petals[kMaxPetals];
	bool needSpawn;
	VMTimer timer;
	int tick;
} hana;

static uint rnd() {
	return g_ai5->getRandom().getRandomNumber(0x7fff);
}

static void hanaSpawn() {
	assert(hana.nrPetals < kMaxPetals);
	uint i = hana.nrPetals++;
	hana.petals[i].active = true;
	hana.petals[i].size = rnd() % 3;
	hana.petals[i].speed = hana.petals[i].size;
	hana.petals[i].frameTick = 0;
	if (rnd() % 2) {
		if (hana.petals[i].size == 0 || hana.petals[i].size == 2)
			hana.petals[i].size = 1;
	}

	hana.petals[i].pos.x = (rnd() % 34) * 16;
	hana.petals[i].pos.y = 0;
	hana.petals[i].altAnim = false;
	hana.petals[i].frame = 9; // XXX: last frame of normal animation
}

static void doHanaTick(Petal *p) {
	// move
	if (p->frameTick == 0) {
		p->pos.x--;
		p->pos.y++;
		p->frameTick = p->speed;
		if (++p->frame >= (p->altAnim ? 6 : 10)) {
			p->frame = 0;
			// 30% chance for alternate animation
			p->altAnim = (rnd() % 3) == 0;
		}
	} else {
		p->frameTick--;
	}

	// despawn
	if (p->pos.x <= -16 || p->pos.y >= 280) {
		p->active = false;
		return;
	}

	// calculate coordinates/dimensions
	int frameX = (p->size + (p->altAnim ? 0 : 3)) * 16;
	int frameY = p->frame * 16;
	int x = p->pos.x;
	int y = p->pos.y;
	int w = 16;
	int h = 16;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y > 264) {
		h -= y - 264;
	}
	assert(w > 0 && h > 0);

	// draw petal
	if (x < 448)
		gfxIndexedCopyMaskedDstGt(frameX, frameY, w, h, 3, x + 96, y + 60, 0, 0, 155);
}

static void hanaErase(Petal *p) {
	int x = p->pos.x + 96;
	int y = p->pos.y + 60;
	gfxIndexedCopyMaskedDstGt(x, y, 16, 16, 2, x, y, 0, 0, 155);
}

static void hanaTick() {
	if (hana.nrPetals < kMaxPetals && hana.needSpawn) {
		hanaSpawn();
		hana.needSpawn = false;
	}

	for (int i = 0; i < kMaxPetals; i++) {
		if (!hana.petals[i].active)
			continue;
		hanaErase(&hana.petals[i]);
		doHanaTick(&hana.petals[i]);
		gfxScreenDirty();
	}

	if (++hana.tick >= 16) {
		hana.needSpawn = true;
		hana.tick = 0;
	}
}

static void nanpa2HanaStart(ParamList &params) {
	memset(&hana, 0, sizeof(hana));
	hana.enabled = true;
	hana.needSpawn = true;
	hana.timer = vmTimerCreate();
}

static void nanpa2HanaEnd(ParamList &params) {
	hana.enabled = false;
}

struct Flake {
	// static
	bool active;
	int size;
	int speed;
	bool bgLayer;
	// dynamic
	struct { int x, y; } pos;
	bool left;
	int tick;
	int spawnTicks;
};

struct {
	bool enabled;
	int nrFlakes;
	Flake flakes[kMaxFlakes];
	bool needSpawn;
	VMTimer timer;
	int tick;
} yuki;

static void doYukiTick(Flake *p) {
	// draw flake
	int srcX = p->size * 8;
	int dstX = p->pos.x;
	int dstY = p->pos.y;
	int threshold = p->bgLayer ? 109 : 9;
	if (dstX > -8 && dstX < 640 && dstY < 400) {
		gfxIndexedCopyMaskedDstGt(srcX, 0, 8, 8, 3, dstX, dstY, 0, 0, threshold);
		gfxIndexedCopyMaskedDstGt(srcX, 0, 8, 8, 3, dstX, dstY, 2, 0, threshold);
	}

	// speed 0: tick, tick, tock
	// speed 1: tick, tock
	// speed 2: tick, tock, tock
	int spawnTick = p->spawnTicks++;
	if (p->speed == 0 && (spawnTick % 3) > 2) {
		return;
	} else if (p->speed == 1 && (spawnTick % 2) > 0) {
		return;
	} else if (p->speed == 2 && (spawnTick % 3) > 1) {
		return;
	}

	// despawn
	if (dstY + 1 > 400) {
		yuki.nrFlakes--;
		p->active = false;
		return;
	}

	// tick
	if (p->tick == 0) {
		p->left = !p->left;
		p->tick = p->left ? 8 : 12;
	} else {
		p->tick--;
	}

	p->pos.x += p->left ? -1 : 1;
	p->pos.y++;
}

static void yukiErase(Flake *p) {
	if (p->pos.x <= -8 || p->pos.x >= 640 || p->pos.y >= 400)
		return;
	gfxIndexedCopyMaskedDstGt(p->pos.x, p->pos.y, 8, 8, 1,
			p->pos.x, p->pos.y, 0, 0, 109);
	gfxIndexedCopyMaskedDstGt(p->pos.x, p->pos.y, 8, 8, 1,
			p->pos.x, p->pos.y, 0, 2, 109);
}

static void yukiSpawn() {
	int i;
	for (i = 0; i < kMaxFlakes; i++) {
		if (!yuki.flakes[i].active)
			break;
	}
	if (i >= kMaxFlakes)
		return;

	Flake *flake = &yuki.flakes[i];
	flake->active = true;
	flake->size = rnd() % 3;
	flake->speed = flake->size;
	flake->bgLayer = false;
	if (flake->size == 0 && rnd() % 2) {
		// large flakes: 50% chance to be downsized and put into bg
		flake->size = 1;
		flake->bgLayer = true;
	} else if (flake->size == 1 && rnd() % 2) {
		// medium flakes: 50% chance to be put into bg
		flake->bgLayer = true;
	} else if (flake->size == 2) {
		// small flakes: 50/50 either upsized, or else put into bg
		if (rnd() % 2)
			flake->size = 1;
		else
			flake->bgLayer = true;
	}

	// flakes spawn from left to right, one each per 64-pixel chunk, resetting after 25
	// spawns. The x position within a chunk changes each pass.
	int chunk = (i % 25) % 10;
	int pass = i / 10;
	int x = chunk * 64;
	if (pass % 2 == 0) {
		if (chunk % 2 == 0)
			x += 8;
		else
			x += 40;
	} else {
		if (chunk % 2 == 0)
			x += 24;
		else
			x += 56;
	}
	flake->pos.x = x;
	flake->pos.y = -7;
	flake->left = true;
	flake->tick = 4;
	flake->spawnTicks = 0;
	yuki.nrFlakes++;
}

static void yukiTick() {
	if (yuki.nrFlakes < kMaxFlakes && yuki.needSpawn) {
		yukiSpawn();
		yuki.needSpawn = false;
	}

	for (int i = 0; i < kMaxFlakes; i++) {
		if (!yuki.flakes[i].active)
			continue;
		yukiErase(&yuki.flakes[i]);
		doYukiTick(&yuki.flakes[i]);
		gfxScreenDirty();
	}

	if (++yuki.tick >= 32) {
		yuki.needSpawn = true;
		yuki.tick = 0;
	}
}

static void nanpa2YukiStart(ParamList &params) {
	memset(&yuki, 0, sizeof(yuki));
	yuki.enabled = true;
	yuki.needSpawn = true;
	yuki.timer = vmTimerCreate();
}

static void nanpa2YukiEnd(ParamList &params) {
	yuki.enabled = false;
}

static void nanpa2YukiResume(ParamList &params) {
	yuki.enabled = true;
}

static void nanpa2Update() {
	if (asyncCrossfadeActive && vmTimerTickAsync(asyncCrossfadeT, 15)) {
		if (!nanpa2FixedCrossfadeTick(asyncCrossfadePalette)) {
			asyncCrossfadeActive = false;
		}
	}
	if (hana.enabled && vmTimerTickAsync(hana.timer, 25))
		hanaTick();
	if (yuki.enabled && vmTimerTickAsync(yuki.timer, 25))
		yukiTick();
}

void initGameDoukyuusei2(Game &g) {
	static const struct { uint16 w, h; } sizes[] = {
		{ 640, 400 },
		{ 640, 1800 },
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 2800 }
	};
	g.id = GID_DOUKYUUSEI2;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 8;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.init = nanpa2Init;
	g.update = nanpa2Update;
	g.memInit = nanpa2MemInit;
	g.memRestore = nanpa2MemRestore;
	g.unprefixedZen = vmStmtTxtNoLog;
	g.unprefixedHan = vmStmtStrNoLog;

	g.vm.exec = vmExec;
	g.vm.eval = vmEval;
	g.vm.readParams = vmReadParams;
	g.vm.endCode = 0x00;
	vmSetDefaultOps(g);
	g.exprOp[0xe5] = vmExprRandWithImmRange;
	g.stmtOp[0x01] = vmStmtTxtOldLog;
	g.stmtOp[0x02] = vmStmtStrOldLog;
	g.stmtOp[0x03] = vmStmtSetFlagConst16_4bitSaturate;
	g.stmtOp[0x0b] = vmStmtSysOldLog;
	g.stmtOp[0x0f] = vmStmtCallOldLog;
	g.stmtOp[0x11] = vmStmtLineOldLog;
	g.stmtOp[0x13] = nanpa2MenuExec;

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = nanpa2Cursor;
	g.sys[3] = nanpa2Anim;
	g.sys[4] = nanpa2Savedata;
	g.sys[5] = nanpa2Audio;
	g.sys[6] = nanpa2Voice;
	g.sys[7] = sysLoadFile;
	g.sys[8] = sysLoadImage;
	g.sys[9] = nanpa2Palette;
	g.sys[10] = nanpa2Graphics;
	g.sys[11] = sysWait;
	g.sys[12] = sysSetTextColorsIndexedWithSysvar;
	g.sys[13] = sysFarcall;
	g.sys[14] = sysGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[16] = sysGetTime;
	g.sys[17] = nanpa2Map;
	g.sys[18] = nanpa2Backlog;
	g.sys[19] = nanpa2Checkdisc;
	g.sys[20] = nanpa2Sys20;

	g.util[90] = nanpa2SaveMemory;
	g.util[91] = nanpa2RestoreMemory;
	g.util[92] = nanpa2YukiStart;
	g.util[93] = nanpa2YukiEnd;
	g.util[94] = nanpa2SavePalette;
	g.util[95] = nanpa2RestorePalette;
	g.util[96] = nanpa2LoadLowPalette;
	g.util[97] = nanpa2CtrlIsDown;
	g.util[99] = nanpa2ActivateIsDown;
	g.util[100] = nanpa2WaitForActivateCancelUp;
	g.util[101] = utilWarnUnimplemented;
	g.util[102] = utilWarnUnimplemented;
	g.util[103] = nanpa2MapWait;
	g.util[104] = nanpa2LoadMidPalette;
	g.util[105] = nanpa2Util105;
	g.util[106] = nanpa2Util106;
	g.util[107] = nanpa2SpLoad;
	g.util[108] = nanpa2CancelIsDown;
	g.util[109] = nanpa2CrossfadeStaticPalette;
	g.util[110] = nanpa2Util110;
	g.util[111] = nanpa2Util111;
	g.util[112] = nanpa2LoadEndSepiaPalette;
	g.util[113] = nanpa2LoadEndColorPalette;
	g.util[114] = nanpa2InitEndCrossfade;
	g.util[115] = nanpa2ClearHighPalette;
	g.util[116] = nanpa2Wait;
	g.util[117] = nanpa2HanaCrossfade;
	g.util[118] = nanpa2HanaStart;
	g.util[119] = nanpa2HanaEnd;
	g.util[120] = nanpa2Util120;
	g.util[122] = nanpa2Util122;
	g.util[123] = nanpa2Util123;
	g.util[124] = nanpa2YukiEnd;
	g.util[125] = nanpa2YukiResume;

	g.flags[kFlagLoadPalette] = 0x0001;
	g.flags[kFlagSavePalette] = 0x0002;
	g.flags[kFlagAnimEnable] = 0x0004;
	g.flags[kFlagMenuReturn] = 0x0008;
	g.flags[kFlagReturn] = 0x0010;
	g.flags[kFlagLogText] = 0x0040;
	g.flags[kFlagLog] = 0x0080;
	g.flags[kFlagVoiceEnable] = 0x0200;
	g.flags[kFlagLogEnable] = 0x0400;
	g.flags[kFlagAudioEnable] = kFlagAlwaysOn;
	g.flags[kFlagWaitKeyup] = kFlagAlwaysOn;

	// TODO: after the startup logo the title script only repeats a
	//       System.Graphics copy loop and never loads the title art, so
	//       the game stays marked unstable.
}

} // End of namespace AI5
