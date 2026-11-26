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
#include "ai5/games/kakyuusei.h"

namespace AI5 {

namespace {

enum {
	kMesNameSize = 128,
	kVar4Size = 4096,
	kMem16Size = 8192,
	kVar4Off = kMesNameSize,
	kSv16PtrOff = kVar4Off + kVar4Size,
	kVar16Off = kSv16PtrOff + 4,
	kSysvar16Off = kVar16Off + 26 * 2,
	kVar32Off = kSysvar16Off + 28 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	kHeapOff = kSysvar32Off + 210 * 4
};
static_assert(kHeapOff == 0x14a0, "unexpected Kakyuusei heap offset");

} // End of anonymous namespace
#define PALETTE_LOG(...) debug(5, __VA_ARGS__)

static void kakyuuseiMemRestore() {
	memSetSysvar16Ptr(kSysvar16Off);
	memSetSysvar32(kSysVarFileData, offsetof(Memory, fileData));
	memSetSysvar32(kSysVarMenuEntryAddresses, offsetof(Memory, menuEntryAddresses));
	memSetSysvar32(kSysVarMenuEntryNumbers, offsetof(Memory, menuEntryNumbers));
	memSetSysvar32(kSysVarMapData, offsetof(Memory, mapData));
	memSetSysvar16((uint)0, kHeapOff);
}

static void kakyuuseiMemInit() {
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
	kakyuuseiMemRestore();
}

static void kakyuuseiMenuExec() {
	uint16 savedFlags = memGetSysvar16(kSysVarFlags);
	vmFlagOff(kFlagLogEnable);
	menuExec();
	memSetSysvar16(kSysVarFlags, savedFlags);
}

static void kakyuuseiCursor(ParamList &params) {
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

static void kakyuuseiAnim(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: animInitStream(vmExprParam(params, 1), vmExprParam(params, 1)); break;
	case 1: animStart(vmExprParam(params, 1)); break;
	case 2: animStop(vmExprParam(params, 1)); break;
	case 3: animHalt(vmExprParam(params, 1)); break;
	case 4: animWait(vmExprParam(params, 1)); break;
	case 5: animStopAll(); break;
	case 6: animHaltAll(); break;
	case 7: animResetAll(); break;
	case 8: animWaitAll(); break;
	default: error("System.Anim.function[%u] not implemented",
				 params.params[0].val);
	}
}

/*
 * Redraw message box if clobbered by animation.
 */
static void kakyuuseiAfterAnimCopy(AnimDrawCall *call) {
	int dstX, dstY, w, h;
	animDecomposeDrawCall(*call, dstX, dstY, w, h);
	if (dstY + h <= 280 || dstX + w <= 16 || dstX >= 624)
		return;

	if (dstY < 280) {
		h -= 280 - dstY;
		dstY = 280;
	}
	if (dstX < 16) {
		w -= 16 - dstX;
		dstX = 16;
	}
	if (dstX + w > 624) {
		w = 624 - dstX;
	}

	// update clean bg on surface 7
	gfxCopy(dstX, dstY, w, h, 0, dstX - 16, dstY - 280, 7);

	if (memGetVar4(2829) || memGetVar4(2808) != 1)
		return;

	// draw message box
	gfxCopyMasked(dstX - 16, 120 + (dstY - 280), w, h, 7, dstX, dstY, 0, 0);
}

/*
 * Redraw item window if clobbered by animation.
 */
static void kakyuuseiAfterAnimCopyMasked(AnimDrawCall *call) {
	if (memGetVar4(3020) != 1)
		return;

	int dstX, dstY, w, h;
	animDecomposeDrawCall(*call, dstX, dstY, w, h);
	if (dstX + w <= 152 || dstX > 152 + 336 || dstY + h <= 128 || dstY > 128 + 144)
		return;

	if (dstX < 152) {
		w -= 152 - dstX;
		dstX = 152;
	}
	if (dstX + w > 152 + 336) {
		w = (152 + 336) - dstX;
	}
	if (dstY < 128) {
		h -= 128 - dstY;
		dstY = 128;
	}
	if (dstY + h > 128 + 144) {
		h = (128 + 144) - dstY;
	}

	int s8X = 240 + (dstX - 152);

	// update clean bg on surface 8
	gfxCopy(dstX, dstY, w, h, 0, s8X, dstY - 128, 8);

	if (memGetVar4(2829))
		return;

	// draw item box
	gfxCopyMasked(s8X, 288 + (dstY - 128), w, h, 8, dstX, dstY, 0, 0);
}

static void kakyuuseiAfterAnimDraw(AnimDrawCall *call) {
	if (call->op == kAnimDrawCopy)
		kakyuuseiAfterAnimCopy(call);
	else if (call->op == kAnimDrawCopyMasked)
		kakyuuseiAfterAnimCopyMasked(call);
}

static void kakyuuseiResumeLoad(const char *saveName) {
	savedataResumeLoad(saveName);
	// load player name
	savedataRead("FLAG08", memRaw(), kHeapOff + 2880, 16);
}

static void kakyuuseiSave(const char *saveName) {
	byte save[kMem16Size];
	byte *var4 = save + kVar4Off;
	byte *memVar4 = memRaw() + kVar4Off;
	savedataRead(saveName, save, 0, kMem16Size);
	for (int i = 0; i < kVar4Size; i++) {
		if (memVar4[i])
			var4[i] = memVar4[i];
	}
	uint off = kSysvar32Off + 240;
	memcpy(save+off, memRaw()+off, 200);
	savedataWrite(saveName, save, 0, kMem16Size);
}

static void kakyuuseiSavedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: kakyuuseiResumeLoad(sysSaveName(params)); break;
	case 1: savedataResumeSave(sysSaveName(params)); break;
	case 2: savedataLoad(sysSaveName(params), kVar4Off); break;
	case 3: kakyuuseiSave(sysSaveName(params)); break;
	case 4: savedataLoadVariables(sysSaveName(params), vmStringParam(params, 2)); break;
	default: error("System.SaveData.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void kakyuuseiAudio(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: audioBgmPlay(vmStringParam(params, 1), true); break;
	case 1: audioFade(kAudioChBgm, kAudioVolumeMin, 250, true, false); break;
	case 2: audioFade(kAudioChBgm, kAudioVolumeMin, 3000, true, false); break;
	case 3: audioSePlay(vmStringParam(params, 1), vmExprParam(params, 2)); break;
	case 4: audioSeFade(kAudioVolumeMin, 3000, true, false, vmExprParam(params, 1)); break;
	case 5: audioSeStop(vmExprParam(params, 1)); break;
	default: error("System.Audio.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void kakyuuseiVoice(ParamList &params) {
	if (!vmFlagIsOn(kFlagVoiceEnable))
		return;
	switch (vmExprParam(params, 0)) {
	case 0: audioVoicePlay(vmStringParam(params, 1), 0); break;
	case 1: audioVoiceStop(0); break;
	case 2: memSetVar16(18, audioIsPlaying(kAudioChVoice0)); break;
	default: error("System.Voice.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void kakyuuseiLoadImage(ParamList &params) {
	int dstSurface = memGetSysvar16(kSysVarDstSurface);
	if (dstSurface < 2)
		animHaltAll();
	sysLoadImageAt(vmStringParam(params, 0), dstSurface, 1);
}

static byte extraPalette256[0x400] = {0};
static byte extraPalette16[0x40] = {0};

static void kakyuuseiPalette(ParamList &params) {
	if (vmFlagIsOn(kFlagSavePalette)) {
		PALETTE_LOG("(Palette saved)");
		memcpy(extraPalette256, g_mem->palette, 256 * 4);
	}
	switch (vmExprParam(params, 0)) {
	case 1:
		if (params.nrParams > 1) {
			memset(g_mem->palette, (byte)vmExprParam(params, 1), 236 * 4);
			PALETTE_LOG("Palette.crossfade(%u)", params.params[1].val);
		} else {
			PALETTE_LOG("Palette.crossfade()");
		}
		gfxPaletteCrossfade(g_mem->palette, 0, 236, 1000);
		gfxPaletteCopy(g_mem->palette, 0, 256);
		break;
	case 5: {
		int start = vmExprParam(params, 1);
		int n = vmExprParam(params, 2);
		PALETTE_LOG("Palette.set(%d, %d)", start, n);
		gfxPaletteSetNoUpdate(g_mem->palette, 0, 236);
		gfxUpdatePalette(start, n);
		break;
	}
	case 7:
		if (params.nrParams > 2) {
			memset(g_mem->palette, (byte)vmExprParam(params, 2), 236 * 4);
			PALETTE_LOG("Palette_crossfade2(%u,%u)", params.params[1].val,
					params.params[2].val);
		}
		else {
			PALETTE_LOG("Palette_crossfade2(%u)", params.params[1].val);
		}
		gfxPaletteCrossfade(g_mem->palette, 0, 236, vmExprParam(params, 1) * 50);
		gfxPaletteCopy(g_mem->palette, 0, 256);
		break;
	default: error("System.Palette.function[%u] not implemented",
				 params.params[0].val);
	}
	if (vmFlagIsOn(kFlagLoadPalette)) {
		PALETTE_LOG("(Palette restored)");
		memcpy(g_mem->palette, extraPalette256, 256 * 4);
	}
}

static void kakyuuseiPixelCrossfade(ParamList &params, bool slow) {
	int srcX = vmExprParam(params, 1);
	int srcY = vmExprParam(params, 2);
	int srcW = (vmExprParam(params, 3) - srcX) + 1;
	int srcH = (vmExprParam(params, 4) - srcY) + 1;
	uint srcI = vmExprParam(params, 5);
	int dstX = vmExprParam(params, 6);
	int dstY = vmExprParam(params, 7);
	uint dstI = vmExprParam(params, 8);
	if (slow)
		gfxPixelCrossfadeMaskedIndexed8x8(srcX, srcY, srcW, srcH, srcI,
				dstX, dstY, dstI, 0, 30);
	else
		gfxPixelCrossfadeMaskedIndexed(srcX, srcY, srcW, srcH, srcI, dstX,
				dstY, dstI, 0);
}

static void kakyuuseiGraphics(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: sysGraphicsCopy(params); break;
	case 1: sysGraphicsCopyMasked(params); break;
	case 2: sysGraphicsFillBg(params); break;
	case 5: kakyuuseiPixelCrossfade(params, false); break;
	case 7: kakyuuseiPixelCrossfade(params, true); break;
	default: error("System.Graphics.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void kakyuuseiWait(ParamList &params) {
	if (params.nrParams > 0 && vmExprParam(params, 0) == 0) {
		params.params[0].val = 1;
	}
	sysWait(params);
}

static void drawDatetime() {
	int buffer = 8;
	int screen = 0;
	int w = 120;
	int h = 128;
	gfxCopy(16, 16, w, h, screen, 0, 0, buffer);
	gfxCopyMasked(0, 256, w, h, buffer, 16, 16, screen, 0);
	gfxCopy(504, 16, w, h, screen, 120, 0, buffer);
	gfxCopyMasked(120, 256, w, h, buffer, 504, 16, screen, 0);
}

static void kakyuuseiMapExecSpritesAndRedraw() {
	mapExecSprites();
	mapLoadTiles();
	mapPlaceSprites();
	mapDrawTiles();
	drawDatetime();
}

static void kakyuuseiMapDrawTiles() {
	mapDrawTiles();
	drawDatetime();
}

static void kakyuuseiMap(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: mapLoadTilemap(); break;
	case 1: mapSpawnSprite(vmExprParam(params, 1), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	case 2: mapLoadTiles(); break;
	case 4: mapLoadSpriteScripts(); break;
	case 5: mapSetSpriteScript(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 6: mapPlaceSprites(); break;
	case 7: mapSetSpriteState(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 8: kakyuuseiMapExecSpritesAndRedraw(); break;
	case 9: mapExecSprites(); break;
	case 10:
	case 11: kakyuuseiMapDrawTiles(); break;
	case 12: mapSetLocationMode((MapLocationMode)vmExprParam(params, 1)); break;
	case 13: mapGetLocation(); break;
	case 14: mapMoveSprite(vmExprParam(params, 1), (MapDirection)vmExprParam(params, 2)); break;
	case 15: mapPathSprite(vmExprParam(params, 1), vmExprParam(params, 2),
				 vmExprParam(params, 3)); break;
	case 16: mapStopPathing(); break;
	case 17: mapGetPathing(); break;
	case 20: mapRewindSpritePos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 21: mapSkipPathing(vmExprParam(params, 1)); break;
	default: error("System.Map.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void kakyuuseiBacklog(ParamList &params) {
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

/*
 * Copy from main to extra palette bank.
 */
static void kakyuuseiSavePalette(ParamList &params) {
	int start = vmExprParam(params, 1);
	int n = vmExprParam(params, 2);
	PALETTE_LOG("Palette.save(%d, %d)", start, n);
	if (start + n < 0 || start + n > 256)
		error("Invalid palette range: %d+%d", start, n);
	memcpy(extraPalette256 + start*4, g_mem->palette + start*4, n * 4);
}

/*
 * Copy from extra to main palette bank.
 */
static void kakyuuseiRestorePalette(ParamList &params) {
	PALETTE_LOG("Palette.restore()");
	memcpy(g_mem->palette, extraPalette256, 256 * 4);
}

/*
 * Copy from extra to main palette bank (system colors).
 */
static void kakyuuseiResetLowPalette(ParamList &params) {
	PALETTE_LOG("Palette.restoreLow()");
	memcpy(g_mem->palette, extraPalette16, 16 * 4);
}

static void kakyuuseiCtrlIsDown(ParamList &params) {
	memSetVar16(18, inputDown(kInputCtrl));
}

static void kakyuuseiActivateIsDown(ParamList &params) {
	memSetVar16(18, inputDown(kInputActivate));
}

static void kakyuuseiWaitUntilActivateIsUp(ParamList &params) {
	while (inputDown(kInputActivate) && !g_engine->shouldQuit())
		vmPeek();
	while (inputDown(kInputCancel) && !g_engine->shouldQuit())
		vmPeek();
}

static VMTimer ticks;

static void kakyuuseiTimerInit(ParamList &params) {
	ticks = vmTimerCreate();
}

static void kakyuuseiTimerSet(ParamList &params) {
	VMTimer now = vmTimerCreate();
	ticks = ((ticks + 800) - now) / 200;
}

static void kakyuuseiTimerWait(ParamList &params) {
	if (ticks > 20000) {
		warning("Util.timerWait called with t > 20s");
		ticks = 20000;
	}
	vmDelay(ticks);
}

static int moveSpeed = 2;

/*
 * Delay to control movement speed on map.
 */
static void kakyuuseiMoveTick(ParamList &params) {
	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < moveSpeed; i++) {
		if (inputDown(kInputShift))
			return;
		vmTimerTick(timer, 30);
	}
}

/*
 * Sepia-ish effect. Used when viewing backlog.
 */
static void kakyuuseiCrossfadeSepia(ParamList &params) {
	gfxPaletteCopy(extraPalette256, 0, 236);
	for (int i = 16; i < 256; i++) {
		float fR = extraPalette256[i * 4 + 2];
		float fG = extraPalette256[i * 4 + 1];
		float fB = extraPalette256[i * 4 + 0];
		float avg = (fR + fG + fB) / 3.f;
		byte col[3] = {
			(byte)avg,
			(byte)MIN<uint>(255, (uint)(avg * 1.02f)),
			(byte)MIN<uint>(255, (uint)(avg * 1.2f))
		};
		memcpy(g_mem->palette + i * 4, col, 3);
	}
	gfxPaletteCrossfade(g_mem->palette, 0, 236, 1000);
}

/*
 * Palette crossfade (excluding system colors).
 */
static void kakyuuseiCrossfadeHighPalette(ParamList &params) {
	memcpy(g_mem->palette + 16 * 4, extraPalette256 + 16 * 4, 240 * 4);
	gfxPaletteCrossfade(g_mem->palette, 0, 236, 1000);
}

/*
 * Load the player name from disk.
 */
static void kakyuuseiLoadPlayerName(ParamList &params) {
	savedataRead("FLAG08", memRaw(), kHeapOff + 2880, 32);
}

/*
 * Push a number to the backlog.
 */
static void kakyuuseiBacklogAddNumber(ParamList &params) {
	const char *str = sysNumberToString(vmExprParam(params, 1));
	backlogPushByte(1);
	while (*str) {
		backlogPushByte(*str);
		str++;
	}
	backlogPushByte(0);
}

enum {
	kScrollDelta = 2,
	kMoveDelta = 4
};

static void scrollTick(int x, int y, VMTimer &timer) {
	gfxCopy(x, y, 640, 400, 9, 0, 0, 0);
	gfxUpdate();
	vmPeek();
	vmTimerTick(timer, 30);
}

/*
 * Interactive scrolling effect. Used for viewing posters and a few character events.
 */
static void kakyuuseiScroll(ParamList &params) {
	//int x = vmExprParam(params, 1); // XXX: always 0
	//int y = vmExprParam(params, 2); // XXX: always 0
	int w = vmExprParam(params, 3);
	int h = vmExprParam(params, 4);
	uint flags = vmExprParam(params, 5);

	if (!flags) {
		gfxCopy(0, 0, 640, 400, 9, 0, 0, 0);
	} else if (flags & 8) {
		// scroll with arrow keys
		GfxPoint cur = { 0, 0 };
		GfxPoint limit = { w - 640, h - 400 };
		VMTimer timer = vmTimerCreate();
		while (!g_engine->shouldQuit()) {
			bool dirty = false;
			if (inputDown(kInputActivate)) {
				inputWaitUntilUp(kInputActivate);
				break;
			}
			if (inputDown(kInputLeft) && cur.x > 0) {
				cur.x = MAX<float>(0, cur.x - kMoveDelta);
				dirty = true;
			}
			if (inputDown(kInputRight) && cur.x < limit.x) {
				cur.x = MIN<float>(limit.x, cur.x + kMoveDelta);
				dirty = true;
			}
			if (inputDown(kInputUp) && cur.y > 0) {
				cur.y = MAX<float>(0, cur.y - kMoveDelta);
				dirty = true;
			}
			if (inputDown(kInputDown)) {
				cur.y = MIN<float>(limit.y, cur.y + kMoveDelta);
				dirty = true;
			}
			if (dirty) {
				scrollTick(cur.x, cur.y, timer);
			} else {
				vmPeek();
				vmTimerTick(timer, 30);
			}
		}

		// return to origin
		int dx = 0;
		float dy = 0.f;
		float startY = cur.y;
		if (cur.x && cur.y) {
			float fy = (float)cur.y / (float)cur.x;
			dx = -kScrollDelta;
			dy = fy * -kScrollDelta;
		} else if (cur.x) {
			dx = -kScrollDelta;
		} else if (cur.y) {
			dy = -kScrollDelta;
		}

		for (int frame = 1; cur.x || cur.y; frame++) {
			cur.x = MAX<float>(0, cur.x + dx);
			cur.y = MAX<float>(0, startY + dy * frame);
			scrollTick(cur.x, cur.y, timer);
		}
	} else {
		// scroll to opposite corner and back
		if (w <= 640)
			flags &= 0xd;
		if (h <= 400)
			flags &= 0xb;
		GfxPoint end = { flags & 2 ? w - 640 : 0, flags & 4 ? h - 400 : 0 };
		int dx = 0;
		float dy = 0.f;
		if ((flags & 2) && (flags & 4)) {
			float fy = (float)(h - 400) / (float)(w - 640);
			dx = kScrollDelta;
			dy = fy * kScrollDelta;
		} else if (flags & 2) {
			dx = kScrollDelta;
		} else if (flags & 4) {
			dy = kScrollDelta;
		}

		VMTimer timer = vmTimerCreate();
		GfxPoint cur = { 0, 0 };
		int startY = 0;
		for (int frame = 1; cur.x != end.x || cur.y != end.y; frame++) {
			cur.x = MIN<float>(end.x, cur.x + dx);
			cur.y = MIN<float>(end.y, startY + dy * frame);
			scrollTick(cur.x, cur.y, timer);
		}
		vmTimerTick(timer, 250);
		startY = cur.y;
		for (int frame = 1; cur.x || cur.y; frame++) {
			cur.x = MAX<float>(0, cur.x - dx);
			cur.y = MAX<float>(0, startY - dy * frame);
			scrollTick(cur.x, cur.y, timer);
		}
	}
}

/*
 * Screen quake effect.
 */
static void kakyuuseiQuake(ParamList &params) {
	uint param1 = vmExprParam(params, 1);
	uint param2 = vmExprParam(params, 2);
	uint nrQuakes = param1 & 0xf;
	uint quakeT = ((param1 >> 4) & 0xf) + 1;
	uint flags = param2 & 0xf;
	uint quakeSize = ((param2 >> 4) & 0xf) + 1;

	if (quakeSize > 9)
		quakeSize *= 2;
	quakeT = MIN<uint>(120, quakeT * 30);

	VMTimer timer = vmTimerCreate();
	GfxSurfaceState *s = &g_gfx.surface[0];
	s->scaled = true;
	for (uint i = 0; i < nrQuakes; i++) {
		if (i & 1) {
			if (flags & 1)
				s->dst.x = -quakeSize;
			if (flags & 2)
				s->dst.y = -quakeSize;
		} else {
			if (flags & 1)
				s->dst.x = quakeSize;
			if (flags & 2)
				s->dst.y = quakeSize;
		}
		gfxScreenDirty();
		gfxUpdate();
		vmPeek();
		vmTimerTick(timer, quakeT);
	}
	s->scaled = false;
	s->dst.x = 0;
	s->dst.y = 0;
	gfxScreenDirty();
	gfxUpdate();
	vmTimerTick(timer, quakeT);
}

static bool mahokoSpinActive = false;
static int mahokoSpinY = 0;
static int mahokoSpinSpeed = 1;
VMTimer mahokoSpinTimer = 0;
static bool mahokoSpinMsgboxVisible = true;
enum { WAITING_NONE, WAITING_ACTIVATE, WAITING_CANCEL } mahokoSpinWait = WAITING_NONE;

/*
 * Spinning background effect used in the Mahoko amusement park date.
 * Runs asynchronously.
 */
static void mahokoSpinTick() {
	if (!vmTimerTickAsync(mahokoSpinTimer, 50))
		return;

	// spinning bg left/right
	if (mahokoSpinY <= 1400) {
		gfxCopy(0, mahokoSpinY, 144, 400, 9, 0, 0, 0);
		gfxCopy(487, mahokoSpinY, 153, 400, 9, 487, 0, 0);
	} else {
		int remH = 1800 - mahokoSpinY;
		int loopH = 400 - remH;
		gfxCopy(0,   mahokoSpinY, 144, remH,  9, 0,   0,     0);
		gfxCopy(0,   0,             144, loopH, 9, 0,   remH, 0);
		gfxCopy(487, mahokoSpinY, 153, remH,  9, 487, 0,     0);
		gfxCopy(487, 0,             153, loopH, 9, 487, remH, 0);
	}

	// static ride left/right
	gfxCopyMasked(92, 0, 52, 400, 4, 92, 0, 0, 0);
	gfxCopyMasked(487, 0, 52, 400, 4, 487, 0, 0, 0);

	gfxCopy(127, 0, 345, 104, 7, 143, 280, 0);
	if (mahokoSpinMsgboxVisible) {
		// restore messagebox
		gfxCopyMasked(0, 120, 608, 104, 7, 16, 280, 0, 0);
	}

	// ramp up speed, starting animation at max speed
	if (mahokoSpinSpeed < 65 && ++mahokoSpinSpeed == 65) {
		animHaltAll();
		animInitStream(1, 1);
		animStart(1);
	}

	// increment y
	mahokoSpinY += mahokoSpinSpeed;
	if (mahokoSpinY >= 1800) {
		mahokoSpinY = 0;
	}

	// message box visibility is handled here
	switch (mahokoSpinWait) {
	case WAITING_NONE:
		if (inputDown(kInputActivate)) {
			mahokoSpinWait = WAITING_ACTIVATE;
		} else if (inputDown(kInputCancel)) {
			mahokoSpinWait = WAITING_CANCEL;
		}
		break;
	case WAITING_ACTIVATE:
		if (!inputDown(kInputActivate)) {
			mahokoSpinWait = WAITING_NONE;
			mahokoSpinMsgboxVisible = true;
		}
		break;
	case WAITING_CANCEL:
		if (!inputDown(kInputCancel)) {
			mahokoSpinWait = WAITING_NONE;
			mahokoSpinMsgboxVisible = !mahokoSpinMsgboxVisible;
		}
		break;
	}
}

/*
 * Begin the spinning background effect (Mahoko amusement park date).
 */
static void kakyuuseiMahokoSpinStart(ParamList &params) {
	// XXX: This effect lowers the frame rate significantly, which
	//      affects the animation speed. We emulate this.
	g_animFrameT = 50;
	mahokoSpinActive = true;
	mahokoSpinY = 234;
	mahokoSpinMsgboxVisible = true;
	mahokoSpinWait = WAITING_NONE;
	// draw opaque portion in center
	gfxCopy(143, 0, 345, 400, 4, 143, 0, 0);
	mahokoSpinTick();
}

/*
 * End the spinning background effect (Mahoko amusement park date).
 */
static void kakyuuseiMahokoSpinEnd(ParamList &params) {
	mahokoSpinActive = false;
	g_animFrameT = 16;
}

/*
 * Save the active palette to the extra banks.
 */
static void kakyuuseiSaveCurrentPalette(ParamList &params) {
	PALETTE_LOG("Palette.util131()");
	gfxPaletteCopy(extraPalette256, 0, 236);
	memcpy(extraPalette16, extraPalette256, 16 * 4);
}

/*
 * Save the player name to disk.
 */
static void kakyuuseiSavePlayerName(ParamList &params) {
	savedataWrite("FLAG08", memRaw(), kHeapOff + 2880, 32);
}

/*
 * Blend a color into every color in the palette.
 */
static void kakyuuseiPaletteBlendColor(ParamList &params) {
	int start = vmExprParam(params, 1);
	int end = vmExprParam(params, 2) + 1;
	int r = vmExprParam(params, 3);
	int g = vmExprParam(params, 4);
	int b = vmExprParam(params, 5);
	int a = vmExprParam(params, 6);
	PALETTE_LOG("Palette.blend(%d,%d,%d,%d,%d,%d)", start, end, r, g, b, a);

	for (int i = start; i < end; i++) {
		int srcR = extraPalette256[i*4+2];
		int srcG = extraPalette256[i*4+1];
		int srcB = extraPalette256[i*4+0];
		extraPalette256[i*4+2] = (srcR * a + (100 - a) * r) / 100;
		extraPalette256[i*4+1] = (srcG * a + (100 - a) * g) / 100;
		extraPalette256[i*4+0] = (srcB * a + (100 - a) * b) / 100;
	}
}

/*
 * Unskippable wait.
 */
static void kakyuuseiForceWait(ParamList &params) {
	VMTimer timer = vmTimerCreate();
	VMTimer targetT = timer + vmExprParam(params, 1);
	while (timer < targetT && !g_engine->shouldQuit()) {
		vmPeek();
		vmTimerTick(timer, MIN<uint32>(targetT - timer, 15));
	}
}

// blend one row of the credits (four 320x864 columns of surface 1 stacked
// vertically) onto the frame
static void blendCreditsRow(GfxSurface *frame, int srcY, int dstY, int alpha) {
	if (srcY < 0 || srcY >= 864 * 4 || dstY < 0 || dstY >= frame->h)
		return;
	GfxSurface *src = gfxGetSurface(1);
	const byte *in = gfxPixel(src, 320 * (srcY / 864), srcY % 864);
	byte *out = gfxPixel(frame, 0, dstY);
	alpha = CLIP(alpha, 0, 255);
	for (int x = 0; x < 320; x++, out += 3) {
		if (in[x] == 0)
			continue;
		const GfxColor &c = g_gfx.palette[in[x]];
		out[0] = (c.r * alpha + out[0] * (255 - alpha)) / 255;
		out[1] = (c.g * alpha + out[1] * (255 - alpha)) / 255;
		out[2] = (c.b * alpha + out[2] * (255 - alpha)) / 255;
	}
}

/*
 * Ending credits roll animation. The credits text fades in/out at the bottom/top of screen.
 */
static void kakyuuseiEnding(ParamList &params) {
	// background (surface 2) converted to RGB once
	GfxSurface *bgS = gfxGetSurface(2);
	Common::Array<byte> bg(640 * 400 * 3);
	for (int y = 0; y < 400; y++) {
		const byte *in = gfxPixel(bgS, 0, y);
		byte *out = &bg[y * 640 * 3];
		for (int x = 0; x < 640; x++, out += 3) {
			const GfxColor &c = g_gfx.palette[in[x]];
			out[0] = c.r;
			out[1] = c.g;
			out[2] = c.b;
		}
	}
	Common::Array<byte> frameBuf(bg.size());
	GfxSurface frame = { frameBuf.data(), 640, 400, 640 * 3, 3 };

	int srcTopY = 0;
	int dstTopY = 399;
	VMTimer timer = vmTimerCreate();
	while (srcTopY < 2872 && !g_engine->shouldQuit()) {
		memcpy(frameBuf.data(), bg.data(), bg.size());

		// top fade
		for (int dstY = dstTopY; dstY < 64; dstY++)
			blendCreditsRow(&frame, srcTopY + (dstY - dstTopY), dstY, dstY * 4);
		// solid portion
		if (dstTopY < 337) {
			int srcY = srcTopY + MAX(0, 64 - dstTopY);
			int dstY = MAX(64, dstTopY);
			for (int i = 0; i < 337 - dstY; i++)
				blendCreditsRow(&frame, srcY + i, dstY + i, 255);
		}
		// bottom fade
		for (int dstY = MAX(dstTopY, 337); dstY < 400; dstY++)
			blendCreditsRow(&frame, srcTopY + (dstY - dstTopY), dstY, (400 - dstY) * 4);

		gfxPresentFrame(&frame);
		vmPeek();
		vmTimerTick(timer, 60);
		if (dstTopY > 0)
			dstTopY--;
		else
			srcTopY++;
	}

	// copy final frame to screen surface
	gfxCopyMasked(320 * 3, 279, 320, 400, 1, 0, 0, 0, 0);
	gfxWholeSurfaceDirty(0);
}

static void kakyuuseiBgmIsPlaying(ParamList &params) {
	memSetVar16(18, audioIsPlaying(kAudioChBgm));
}

static uint32 kakyuuseiClock = 0;

/*
 * Start the timer (for music mode).
 */
static void kakyuuseiClockStart(ParamList &params) {
	kakyuuseiClock = vmGetTicks();
}

/*
 * Get the minutes/seconds elapsed since calling kakyuuseiClockStart.
 */
static void kakyuuseiClockGet(ParamList &params) {
	uint32 diff = vmGetTicks() - kakyuuseiClock;
	uint minutes = diff / 60000;
	uint seconds = (diff % 60000) / 1000;
	if (minutes > 99) {
		minutes = 99;
		seconds = 59;
	}
	memSetVar4(18, minutes / 10);
	memSetVar4(19, minutes % 10);
	memSetVar4(20, seconds / 10);
	memSetVar4(21, seconds % 10);
}

static void kakyuuseiInit() {
	g_mapVersion = kMapVersionOld;
	mapInit();
	g_textShadow = kTextShadowA;
}

static void kakyuuseiUpdate() {
	if (mahokoSpinActive)
		mahokoSpinTick();
}

void initGameKakyuusei(Game &g) {
	static const struct { uint16 w, h; } sizes[] = {
		{ 640, 400 },
		{ 2688, 864 },
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 896 },
		{ 640, 512 },
		{ 988, 1800 }
	};
	g.id = GID_KAKYUUSEI;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 8;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.init = kakyuuseiInit;
	g.update = kakyuuseiUpdate;
	g.memInit = kakyuuseiMemInit;
	g.memRestore = kakyuuseiMemRestore;
	g.unprefixedZen = vmStmtTxtNoLog;
	g.unprefixedHan = vmStmtStrNoLog;
	g.afterAnimDraw = kakyuuseiAfterAnimDraw;

	g.vm.exec = vmExec;
	g.vm.eval = vmEval;
	g.vm.readParams = vmReadParams;
	g.vm.endCode = 0x00;
	vmSetDefaultOps(g);
	g.exprOp[0xe5] = vmExprRandWithImmRange;
	g.stmtOp[0x01] = vmStmtTxtOldLog;
	g.stmtOp[0x02] = vmStmtStrNoLog;
	g.stmtOp[0x0b] = vmStmtSysOldLog;
	g.stmtOp[0x0f] = vmStmtCallOldLog;
	g.stmtOp[0x13] = kakyuuseiMenuExec;

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = kakyuuseiCursor;
	g.sys[3] = kakyuuseiAnim;
	g.sys[4] = kakyuuseiSavedata;
	g.sys[5] = kakyuuseiAudio;
	g.sys[6] = kakyuuseiVoice;
	g.sys[7] = sysLoadFile;
	g.sys[8] = kakyuuseiLoadImage;
	g.sys[9] = kakyuuseiPalette;
	g.sys[10] = kakyuuseiGraphics;
	g.sys[11] = kakyuuseiWait;
	g.sys[12] = sysSetTextColorsIndexedWithSysvar;
	g.sys[13] = sysFarcall;
	g.sys[14] = sysGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[16] = sysGetTime;
	g.sys[17] = kakyuuseiMap;
	g.sys[18] = kakyuuseiBacklog;

	g.util[94] = kakyuuseiSavePalette;
	g.util[95] = kakyuuseiRestorePalette;
	g.util[96] = kakyuuseiResetLowPalette;
	g.util[97] = kakyuuseiCtrlIsDown;
	g.util[99] = kakyuuseiActivateIsDown;
	g.util[100] = kakyuuseiWaitUntilActivateIsUp;
	g.util[101] = kakyuuseiTimerInit;
	g.util[102] = kakyuuseiTimerSet;
	g.util[103] = kakyuuseiMoveTick;
	g.util[105] = kakyuuseiCrossfadeSepia;
	g.util[106] = kakyuuseiCrossfadeHighPalette;
	g.util[107] = kakyuuseiLoadPlayerName;
	g.util[111] = kakyuuseiBacklogAddNumber;
	g.util[127] = kakyuuseiScroll;
	g.util[128] = kakyuuseiQuake;
	g.util[129] = kakyuuseiMahokoSpinStart;
	g.util[130] = kakyuuseiMahokoSpinEnd;
	g.util[131] = kakyuuseiSaveCurrentPalette;
	g.util[133] = kakyuuseiSavePlayerName;
	g.util[134] = utilWarnUnimplemented;
	g.util[135] = utilWarnUnimplemented;
	g.util[136] = kakyuuseiPaletteBlendColor;
	g.util[137] = kakyuuseiForceWait;
	g.util[138] = kakyuuseiEnding;
	g.util[139] = kakyuuseiTimerWait;
	g.util[140] = kakyuuseiBgmIsPlaying;
	g.util[141] = kakyuuseiClockStart;
	g.util[142] = kakyuuseiClockGet;
	g.util[143] = utilWarnUnimplemented;
	g.util[144] = kakyuuseiActivateIsDown;

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
}

} // End of namespace AI5
