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

#include "common/ptr.h"
#include "common/textconsole.h"

#include "ai5/ai5.h"
#include "ai5/animplay.h"
#include "ai5/archive.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/backlog.h"
#include "ai5/cg.h"
#include "ai5/cursor.h"
#include "ai5/effect.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/map.h"
#include "ai5/movie.h"
#include "ai5/savedata.h"
#include "ai5/sys.h"
#include "ai5/vm.h"
#include "ai5/games/doukyuusei.h"

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
	kVar32Off = kSysvar16Off + 26 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	kHeapOff = kSysvar32Off + 211 * 4
};
static_assert(kHeapOff == 0x14a0, "unexpected Doukyuusei heap offset");

} // End of anonymous namespace

static void doukyuuseiMemRestore() {
	memSetSysvar16Ptr(kSysvar16Off);
	memSetSysvar32(kSysVarMemory, offsetof(Memory, mem16));
	memSetSysvar32(kSysVarFileData, offsetof(Memory, fileData));
	memSetSysvar32(kSysVarMenuEntryAddresses, offsetof(Memory, menuEntryAddresses));
	memSetSysvar32(kSysVarMenuEntryNumbers, offsetof(Memory, menuEntryNumbers));
	memSetSysvar32(kSysVarMapData, offsetof(Memory, mapData));

	uint16 flags = memGetSysvar16(kSysVarFlags);
	memSetSysvar16(kSysVarFlags, flags | 0x20);
	memSetSysvar16((uint)0, kHeapOff);
}

static void doukyuuseiMemInit() {
	// pointer table for memory access
	g_memPtr.mesName = memRaw();
	g_memPtr.var4 = memRaw() + kVar4Off;
	g_memPtr.systemVar16Ptr = memRaw() + kSv16PtrOff;
	g_memPtr.var16 = memRaw() + kVar16Off;
	g_memPtr.systemVar16 = memRaw() + kSysvar16Off;
	g_memPtr.var32 = memRaw() + kVar32Off;
	g_memPtr.systemVar32 = memRaw() + kSysvar32Off;

	memSetSysvar16(kSysVarFlags, 0xf);
	memSetSysvar16(kSysVarTextStartX, 0);
	memSetSysvar16(kSysVarTextStartY, 0);
	memSetSysvar16(kSysVarTextEndX, 640);
	memSetSysvar16(kSysVarTextEndY, 480);
	memSetSysvar16(kSysVarBgColor, 0);
	memSetSysvar16(kSysVarFgColor, 0x7fff);
	memSetSysvar16(kSysVarFontWidth, 16);
	memSetSysvar16(kSysVarFontHeight, 16);
	memSetSysvar16(kSysVarFontWeight, 1);
	memSetSysvar16(kSysVarCharSpace, 16);
	memSetSysvar16(kSysVarLineSpace, 16);
	memSetSysvar16(kSysVarMaskColor, 0);

	memSetSysvar32(kSysVarCgOffset, 0x20000);
	doukyuuseiMemRestore();

	// HACK: Map.loadTilemap and Map.loadSpriteScripts are sometimes called
	//       without first loading an mpx/ccd file (this happens when loading
	//       certain scenes from OMOIDE.MES, e.g. Yoshiko's ending scene).
	//       We initialize with fake empty files.
	byte *mpx = g_mem->fileData + 0x3e000;
	WRITE_LE_UINT16(mpx + 0, 0);
	WRITE_LE_UINT16(mpx + 2, 0);

	byte *ccd = g_mem->fileData + 0x34000;
	WRITE_LE_UINT16(ccd + 0, 2);
	ccd[2] = 0xff;
}

static void doukyuuseiCursor(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: cursorShow(); break;
	case 1: cursorHide(); break;
	case 2: sysCursorSavePos(params); break;
	case 3: cursorSetPos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 4: cursorLoad((vmExprParam(params, 1) + 0) * 2, 2, nullptr); break;
	default: error("System.Cursor.function[%u] not implemented",
				 params.params[0].val);
	}
}

static uint vmAnimParam(ParamList &params, uint i) {
	uint a = vmExprParam(params, i);
	uint b = vmExprParam(params, i+1);
	uint stream = a * 10 + b;
	if (a >= kAnimMaxStreams)
		error("Invalid animation stream index: %u:%u", a, b);
	return stream;
}

static void doukyuuseiAnimHaltGroup(uint no) {
	for (uint i = 0; i < 10; i++) {
		animHalt(no * 10 + i);
	}
}

static void doukyuuseiAnim(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: animInitStream(vmAnimParam(params, 1), vmAnimParam(params, 1)); break;
	case 1: animStart(vmAnimParam(params, 1)); break;
	case 2: animStop(vmAnimParam(params, 1)); break;
	case 3: animHalt(vmAnimParam(params, 1)); break;
	case 4: animWait(vmAnimParam(params, 1)); break;
	case 5: animStopAll(); break;
	case 6: animHaltAll(); break;
	case 7: animResetAll(); break;
	case 8: animExecCopyCall(vmAnimParam(params, 1)); break;
	case 9: doukyuuseiAnimHaltGroup(vmExprParam(params, 1)); break;
	case 10: animWait(vmAnimParam(params, 1)); break; // TODO: activate skips wait
	case 13: animWait(vmAnimParam(params, 1)); break;
	case 11: animPauseAllSync(); break;
	case 12: animUnpauseAll(); break;
	default: error("System.Anim.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void doukyuuseiSavedataLoadExtraVar32(const char *saveName) {
	// sysvar32[11] -> sysvar32[210]
	savedataRead(saveName, memRaw(), kSysvar32Off + (11 * 4), 200 * 4);
}

static void doukyuuseiSavedataSaveExtraVar32(const char *saveName) {
	// sysvar32[11] -> sysvar32[210]
	savedataWrite(saveName, memRaw(), kSysvar32Off + (11 * 4), 200 * 4);
}

static void doukyuuseiSavedataLoadSpecialFlags() {
	byte save[kVar4Off + kVar4Size];
	savedataRead(sysSaveNameNo(0), save, kVar4Off, kVar4Size);

	for (int i = kVar4Off + 2001; i < kVar4Off + 3500; i++) {
		if ((save[i] && !memRaw()[i]) || save[i] > 5)
			memRaw()[i] = save[i];
	}
	memSetVar4(1896, save[kVar4Off + 1896]);
	memSetVar4(1897, save[kVar4Off + 1897]);
}

static void doukyuuseiSavedataSaveSpecialFlags() {
	byte save[kVar4Off + kVar4Size];
	const char *saveName = sysSaveNameNo(0);
	savedataRead(saveName, save, kVar4Off, kVar4Size);

	for (int i = kVar4Off + 2001; i < kVar4Off + 3500; i++) {
		if (memRaw()[i])
			save[i] = memRaw()[i];
	}

	savedataWrite(saveName, save, kVar4Off, kVar4Size);
}

static void doukyuuseiSavedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: savedataResumeLoad(sysSaveName(params)); break;
	case 1: savedataResumeSave(sysSaveName(params)); break;
	case 2: savedataLoadVar4Restore(sysSaveName(params)); break;
	case 3: savedataSaveUnionVar4(sysSaveName(params)); break;
	case 4: doukyuuseiSavedataLoadExtraVar32(sysSaveName(params)); break;
	case 5: doukyuuseiSavedataSaveExtraVar32(sysSaveName(params)); break;
	case 6: memset(memRaw() + kVar4Off, 0, kVar4Size); break;
	case 7: savedataLoadVariables(sysSaveName(params), vmStringParam(params, 2)); break;
	case 8: doukyuuseiSavedataLoadSpecialFlags(); break;
	case 9: doukyuuseiSavedataSaveSpecialFlags(); break;
	case 10: savedataSaveVar4(sysSaveName(params)); break;
	default: error("System.SaveData.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void doukyuuseiAudioSeStop(ParamList &params) {
	uint no = vmExprParam(params, 1);
	if (no < 3) {
		audioStop((AudioChannel)(kAudioChSe0 + no));
	} else {
		audioStop(kAudioChSe0);
		audioStop((AudioChannel)(kAudioChSe0 + 1));
		audioStop((AudioChannel)(kAudioChSe0 + 2));
	}
}

static void doukyuuseiAudioSePlaySync(const char *name, uint ch) {
	if (!audioSeChannelValid(ch)) {
		warning("Invalid SE channel: %u", ch);
		return;
	}

	audioSePlay(name, ch);
	while (audioIsPlaying((AudioChannel)(kAudioChSe0 + ch)) && !g_engine->shouldQuit()) {
		vmPeek();
		if (inputDown(kInputShift)) {
			audioSeStop(ch);
		}
	}
}

static void doukyuuseiAudio(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: audioBgmPlay(vmStringParam(params, 1), true); break;
	case 1: audioFade(kAudioChBgm, kAudioVolumeMin, 2000, true, false); break;
	case 2: audioStop(kAudioChBgm); break;
	case 3: audioSePlay(vmStringParam(params, 1), vmExprParam(params, 2)); break;
	case 4: doukyuuseiAudioSeStop(params); break;
	case 5: audioSeFade(kAudioVolumeMin, 3000, true, false, vmExprParam(params, 1)); break;
	case 7: doukyuuseiAudioSePlaySync(vmStringParam(params, 1), vmExprParam(params, 2)); break;
	default: error("System.Audio.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void doukyuuseiVoicePlay(ParamList &params) {
	const char *name = vmStringParam(params, 1);
	char c = *name;
	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';

	if (c < 'L') {
		assetSetVoiceArchive("BSIDE.ARC");
	} else if (c < 'S') {
		assetSetVoiceArchive("LSIDE.ARC");
	} else {
		assetSetVoiceArchive("SSIDE.ARC");
	}
	audioVoicePlay(name, vmExprParam(params, 2));
}

static void doukyuuseiVoice(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: doukyuuseiVoicePlay(params); break;
	case 1: audioVoiceStop(vmExprParam(params, 1)); break;
	//case 2: audioVoicePlaySync(vmStringParam(params, 1)); break;
	case 3: memSetVar32(18, audioIsPlaying(kAudioChVoice0)); break;
	default: error("System.Voice.function[%u] not implemented",
				 params.params[0].val);
	}
}

static bool skipOnShift() {
	return !inputDown(kInputShift);
}

static void doukyuuseiDisplay(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:
		if (params.nrParams > 1) {
			gfxDisplayHide(vmExprParam(params, 1));
		} else {
			gfxDisplayUnhide();
		}
		break;
	case 1:
		if (params.nrParams > 1) {
			gfxDisplayFadeOut(vmExprParam(params, 1), 1000, skipOnShift);
		} else {
			gfxDisplayFadeIn(1000, skipOnShift);
		}
		break;
	default: error("System.Display.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void doukyuuseiGraphicsDarken(ParamList &params) {
	int x = vmExprParam(params, 1);
	int y = vmExprParam(params, 2);
	int w = (vmExprParam(params, 3) - x) + 1;
	int h = (vmExprParam(params, 4) - y) + 1;
	uint dstI = memGetSysvar16(kSysVarDstSurface);
	gfxBlendFill(x, y, w, h, dstI, 0, 127);
}

static void doukyuuseiGraphicsBlendTo(ParamList &params) {
	// System.Graphics.blendTo(aX, aY, aBrX, aBrY, aI, bX, bY, bI, dstX, dstY, dstI)
	int aX = vmExprParam(params, 1);
	int aY = vmExprParam(params, 2);
	int w = (vmExprParam(params, 3) - aX) + 1;
	int h = (vmExprParam(params, 4) - aY) + 1;
	uint aI = vmExprParam(params, 5);
	int bX = vmExprParam(params, 6);
	int bY = vmExprParam(params, 7);
	uint bI = vmExprParam(params, 8);
	int dstX = vmExprParam(params, 9);
	int dstY = vmExprParam(params, 10);
	uint dstI = vmExprParam(params, 11);
	uint rate = vmExprParam(params, 12);

	// XXX: This function is always called with (bX,bY)=(dstX,dstY)=(0,0), and
	//      bI=dstI. We just blend surface 'a' onto 'dst'; no need to implement the
	//      full semantics.
	if (bX || bY || dstX || dstY || w != 640 || h != 480 || bI != dstI) {
		warning("Unexpected parameters to System.Graphics.blendTo");
		return;
	}

	// rate is a number between 0 - 0x40000, and neither end of that range is fully
	// transparent.
	rate = CLIP<uint>(rate / 2048, 1, 254);
	gfxBlend(aX, aY, 640, 480, aI, 0, 0, dstI, rate);
}

static void doukyuuseiGraphicsPixelCrossfade(ParamList &params) {
	// System.Grahpics.pixelCrossfade(srcX, srcY, srcBrX, srcBrY, srcI, dstX, dstY, dstI)
	int srcX = vmExprParam(params, 1);
	int srcY = vmExprParam(params, 2);
	int srcW = (vmExprParam(params, 3) - srcX) + 1;
	int srcH = (vmExprParam(params, 4) - srcY) + 1;
	uint srcI = vmExprParam(params, 5);
	int dstX = vmExprParam(params, 6);
	int dstY = vmExprParam(params, 7);
	uint dstI = vmExprParam(params, 8);
	gfxPixelCrossfade(srcX, srcY, srcW, srcH, srcI, dstX, dstY, dstI, 30, nullptr, nullptr);
}

static void doukyuuseiGraphicsPixelCrossfadeMasked(ParamList &params) {
	// System.Grahpics.pixelCrossfadeMasked(srcX, srcY, srcBrX, srcBrY, srcI, dstX, dstY, dstI)
	int srcX = vmExprParam(params, 1);
	int srcY = vmExprParam(params, 2);
	int srcW = (vmExprParam(params, 3) - srcX) + 1;
	int srcH = (vmExprParam(params, 4) - srcY) + 1;
	uint srcI = vmExprParam(params, 5);
	int dstX = vmExprParam(params, 6);
	int dstY = vmExprParam(params, 7);
	uint dstI = vmExprParam(params, 8);
	gfxPixelCrossfadeMasked(srcX, srcY, srcW, srcH, srcI, dstX, dstY, dstI,
			memGetSysvar16(kSysVarMaskColor));
}

static void doukyuuseiGraphics(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: sysGraphicsCopy(params); break;
	case 1: sysGraphicsCopyMasked(params); break;
	case 2: sysGraphicsFillBg(params); break;
	case 3: sysGraphicsCopySwap(params); break;
	case 4: sysGraphicsSwapBgFg(params); break;
	case 5: doukyuuseiGraphicsPixelCrossfade(params); break;
	case 6: sysGraphicsCompose(params); break;
	case 9: doukyuuseiGraphicsPixelCrossfadeMasked(params); break;
	case 11: doukyuuseiGraphicsDarken(params); break;
	case 14: doukyuuseiGraphicsBlendTo(params); break;
	default: error("System.Graphics.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void doukyuuseiWait(ParamList &params) {
	// XXX: crossfade to/from title has wait of 1, probably because fadeTo
	//      operation is slow. We increase it.
	if (params.nrParams == 1 && vmExprParam(params, 0) == 1) {
		params.params[0].val = 20;
	}
	sysWait(params);
}

static void drawStatusbar() {
	// copy area hidden by status bar to surface 7
	gfxCopy(0, 448, 640, 32, 0, 0, 1248, 7);
	// draw status bar
	gfxCopy(0, 106, 640, 32, 7, 0, 448, 0);
}

static void doukyuuseiMapExecSpritesAndRedraw() {
	mapExecSpritesAndRedraw();
	drawStatusbar();
}

static void doukyuuseiMapDrawTiles() {
	mapDrawTiles();
	drawStatusbar();
}

static void doukyuuseiMap(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: mapLoadTilemap(); break;
	case 1: mapSpawnSprite(vmExprParam(params, 1), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	case 2: mapLoadTiles(); break;
	case 4: mapLoadSpriteScripts(); break;
	case 5: mapSetSpriteScript(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 6: mapPlaceSprites(); break;
	case 7: mapSetSpriteState(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 8: doukyuuseiMapExecSpritesAndRedraw(); break;
	case 9: mapExecSprites(); break;
	case 10:
	case 11: doukyuuseiMapDrawTiles(); break;
	case 12: mapSetLocationMode((MapLocationMode)vmExprParam(params, 1)); break;
	case 13: mapGetLocation(); break;
	case 14: mapMoveSprite(vmExprParam(params, 1), (MapDirection)vmExprParam(params, 2)); break;
	case 15: mapPathSprite(vmExprParam(params, 1), vmExprParam(params, 2),
				 vmExprParam(params, 3)); break;
	case 16: mapStopPathing(); break;
	case 17: mapGetPathing(); break;
	case 20: mapRewindSpritePos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 22: mapSetSpriteAnim(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 24: mapLoadPalette(vmStringParam(params, 1), vmExprParam(params, 2)); break;
	case 25: mapLoadBitmap(vmStringParam(params, 1), vmExprParam(params, 2),
				 vmExprParam(params, 3), vmExprParam(params, 4)); break;
	default: error("System.Map.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void doukyuuseiBacklog(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: backlogClear(); break;
	case 1: backlogPrepare(); break;
	case 2: backlogCommit(); break;
	case 3: memSetVar32(18, backlogCount()); break;
	case 4: memSetVar32(18, backlogGetPointer(vmExprParam(params, 1))); break;
	default: error("System.Backlog.function[%u] not implemented",
				 params.params[0].val);
	}
}

/*
 * Redraw the message box when it's clobbered by an animation.
 */
static void doukyuuseiAfterAnimDraw(AnimDrawCall *call) {
	if (!memGetVar4(4046))
		return;

	int srcTopY = 106;
	int dstTopY = 360;
	int maxH = 106;
	if (memGetVar4(4084)) {
		srcTopY += 32;
		dstTopY += 32;
		maxH -= 32;
	}

	int dstX, dstY, w, h;
	animDecomposeDrawCall(*call, dstX, dstY, w, h);
	if (dstY + h <= dstTopY)
		return;

	if (dstY < dstTopY) {
		h -= dstTopY - dstY;
		dstY = dstTopY;
	}
	h = MIN(h, maxH);

	// draw call clobbered message box: redraw it
	int srcX = dstX;
	int srcY = srcTopY + (dstY - dstTopY);
	// darken area under message box
	gfxBlendFill(dstX, dstY, w, h, 0, 0, 127);
	// compose message box on top
	gfxCopyMasked(srcX, srcY, w, h, 7, dstX, dstY, 0, memGetSysvar16(kSysVarMaskColor));
}

static void doukyuuseiStrlen(ParamList &params) {
	vmFlagOn(kFlagStrlen);
	memSetVar32(11, 0);
	sysFarcall(params);
	vmFlagOff(kFlagStrlen);
}

static void doukyuuseiSys25(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:
		g_game->afterAnimDraw = doukyuuseiAfterAnimDraw;
		break;
	case 1:
	case 3:
		g_game->afterAnimDraw = nullptr;
		break;
	default:
		warning("System.function[25].function[%u] not implemented",
				params.params[0].val);
	}
}

static void doukyuuseiSys26(ParamList &params) {
	error("System.function[26] not implemented");
}

/*
 * Copy a set of var4s to a particular location in the var4 array.
 */
static void utilCopyVar4(ParamList &params) {
	uint16 dst = memGetVar16(2);
	uint count = vmExprParam(params, 1);
	if (dst + count >= kVar4Size) {
		error("Tried to write past end of var4 array: %u+%u", dst, count);
	}
	for (uint i = 0; i < count; i++) {
		uint16 src = 0;
		if (i + 2 >= params.nrParams) {
			warning("Tried to read past the end of parameter list (%u/%u)",
					count + 2, params.nrParams);
		} else {
			src = vmExprParam(params, i + 2);
		}
		if (src >= kVar4Size) {
			error("Tried to read past end of var4 array: %u", src);
		}
		memSetVar4(dst + i, memGetVar4(src));
	}
}

static void utilGetCtrl(ParamList &params) {
	memSetVar16(18, inputDown(kInputCtrl));
}

static void utilResumeLoadWithSpecialFlags(ParamList &params) {
	savedataResumeLoad(sysSaveName(params));
	doukyuuseiSavedataLoadSpecialFlags();
}

// Animate date bar sliding up from bottom of screen to top of message box.
static void utilDatebarSlideUp(ParamList &params) {
	const uint s7 = 7;
	const uint s0 = 0;

	// dimensions of bar
	const uint w = 640;
	const uint h = 32;
	// location of bar (surface 7)
	const uint barY = 106;
	// location of scratch area (surface 7)
	const uint scratchY = 1216;
	// location of hidden area (surface 7)
	const uint hideY = 1248;
	// the new bar location, updated every iteration (surface 0)
	uint dstY = 440;

	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < 11; i++) {
		// restore bottom 8 lines under bar
		gfxCopy(0, (hideY + h) - 8, w, 8, s7, 0, dstY + h, s0);
		// update hidden area
		gfxCopy(0, hideY, w, h - 8, s7, 0, scratchY, s7);
		gfxCopy(0, dstY, w, 8, s0, 0, hideY, s7);
		gfxCopy(0, scratchY, w, h - 8, s7, 0, hideY+8, s7);
		// draw bar at new location (8 lines up)
		gfxCopy(0, barY, w, h, s7, 0, dstY, s0);
		// update
		gfxDirty(s0, 0, dstY, 640, 480 - dstY);
		vmPeek();
		vmTimerTick(timer, 16);
		dstY -= 8;
	}
}

// Animate date bar sliding down from top of message box to bottom of screen.
static void utilDatebarSlideDown(ParamList &params) {
	const uint s7 = 7;
	const uint s0 = 0;

	// dimensions of bar
	const uint w = 640;
	const uint h = 32;
	// location of bar (surface 7)
	const uint barY = 1216;
	// location of hidden area (surface 7)
	const uint hideY = 1248;
	// the new bar location, updated every iteration (surface 0)
	uint dstY = 368;

	// make working copy of bar (why?)
	gfxCopy(0, 106, w, h, s7, 0, barY, s7);

	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < 11; i++) {
		// restore top 8 lines under bar
		gfxCopy(0, hideY, w, 8, s7, 0, dstY - 8, s0);
		// update hidden area
		gfxCopy(0, hideY + 8, w, h - 8, s7, 0, hideY, s7);
		gfxCopy(0, (dstY + h) - 8, w, 8, s0, 0, (hideY + h) - 8, s7);
		// draw bar at new location (8 lines down)
		gfxCopy(0, barY, w, h, s7, 0, dstY, s0);
		// update
		gfxDirty(s0, 0, dstY - 8, 640, 480 - (dstY - 8));
		vmPeek();
		vmTimerTick(timer, 16);
		dstY += 8;
	}
}

// Animate cursor description box sliding up.
static void utilCursorDescriptionSlideUp(ParamList &params) {
	const uint s7 = 7;
	const uint s0 = 0;
	const uint16 maskColor = memGetSysvar16(kSysVarMaskColor);

	// dimensions of box
	const uint w = 144;
	const uint h = 34;
	// location of area hidden behind box (surface 7)
	const uint hideY = 1178;
	// location of box (surface 7)
	const uint boxY = 1212;
	// location of scratch area (surface 7)
	const uint scratchY = 1246;
	// the new box location, updated every iteration (surface 0)
	const uint dstX = 248;
	uint dstY = 430;

	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < 5; i++) {
		// restore row below dst
		gfxCopy(0, (hideY + h) - 8, w, 8, s7, dstX, dstY + h, s0);
		// create new hidden area in scratch
		gfxCopy(dstX, dstY, w, 8, s0, 0, scratchY, s7);
		gfxCopy(0, hideY, w, h - 8, s7, 0, scratchY + 8, s7);
		// copy scratch to hidden area
		gfxCopy(0, scratchY, w, h, s7, 0, hideY, s7);
		// compose box with scratch
		gfxCopyMasked(0, boxY, w, h, s7, 0, scratchY, s7, maskColor);
		// copy from scratch to destination
		gfxCopy(0, scratchY, w, h, s7, dstX, dstY, s0);
		// update
		gfxDirty(s0, dstX, dstY, w, h + 8);
		vmPeek();
		vmTimerTick(timer, 16);
		dstY -= 8;
	}
}

// Animate cursor description box sliding down.
static void utilCursorDescriptionSlideDown(ParamList &params) {
	const uint s7 = 7;
	const uint s0 = 0;
	const uint16 maskColor = memGetSysvar16(kSysVarMaskColor);

	// dimensions of box
	const uint w = 144;
	const uint h = 34;
	// location of area hidden behind box (surface 7)
	const uint hideY = 1178;
	// location of box (surface 7)
	const uint boxY = 1212;
	// location of scratch area (surface 7)
	const uint scratchY = 1246;
	// the new box location, updated every iteration (surface 0)
	const uint dstX = 248;
	uint dstY = 406;

	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < 5; i++) {
		// restore row above dst
		gfxCopy(0, hideY, w, 8, s7, dstX, dstY - 8, s0);
		// create new hidden area in scratch
		gfxCopy(0, hideY + 8, w, h - 8, s7, 0, scratchY, s7);
		gfxCopy(dstX, (dstY + h) - 8, w, 8, s0, 0, (scratchY + h) - 8, s7);
		// copy scratch to hidden area
		gfxCopy(0, scratchY, w, h, s7, 0, hideY, s7);
		// compose box with scratch
		gfxCopyMasked(0, boxY, w, h, s7, 0, scratchY, s7, maskColor);
		// copy from scratch to destination
		gfxCopy(0, scratchY, w, h, s7, dstX, dstY, s0);
		// update
		gfxDirty(s0, dstX, dstY - 8, w, h + 8);
		vmPeek();
		vmTimerTick(timer, 16);
		dstY += 8;
	}
}

static void utilScroll(ParamList &params) {
	// XXX: always called with the same parameters
	if (vmExprParam(params, 1) != 0 || vmExprParam(params, 2) != 480
			|| vmExprParam(params, 3) != 640
			|| vmExprParam(params, 4) != 960
			|| vmExprParam(params, 5) != 5) {
		warning("Unexpected parameters to Util.scroll");
	}

	VMTimer timer = vmTimerCreate();
	for (int y = 480; y > 0 && !g_engine->shouldQuit(); y -= 4) {
		gfxCopy(0, y, 640, 480, 9, 0, 0, 0);
		gfxWholeSurfaceDirty(0);
		vmPeek();
		vmTimerTick(timer, 16);
	}
	gfxCopy(0, 0, 640, 480, 9, 0, 0, 0);
	gfxWholeSurfaceDirty(0);
	vmPeek();
	vmTimerTick(timer, 16);
}

/*
 * Takes a variable number of string parameters and compares them against a
 * string on the heap. If any of the strings match, returns true. (Used to
 * prevent the player from entering certain names.)
 */
static void utilMultiStrcmp(ParamList &params) {
	char *str = memGetCString(kHeapOff);
	if (!str) {
		memSetVar32(18, 0);
		return;
	}
	for (uint i = 1; i < params.nrParams; i++) {
		if (!strcmp(str, vmStringParam(params, i))) {
			memSetVar32(18, 1);
			return;
		}
	}
	memSetVar32(18, 0);
}

/*
 * Save the player's name to FLAG00.
 */
static void utilSaveName(ParamList &params) {
	savedataWrite(sysSaveNameNo(0), memRaw(), kHeapOff, 20);
}

/*
 * Load the player's name from FLAG00.
 */
static void utilLoadName(ParamList &params) {
	savedataRead(sysSaveNameNo(0), memRaw(), kHeapOff, 20);
}

static void utilImeEnable(ParamList &params) {
	// TODO
	warning("Util.IME_enable not implemented");
}

static void utilImeDisable(ParamList &params) {
	// TODO
	warning("Util.IME_disable not implemented");
}

static void utilImeGetCompositionStarted(ParamList &params) {
	// TODO
	memSetVar32(18, 0);
}

static void utilImeGetOpen(ParamList &params) {
	// TODO
	memSetVar32(18, 0);
}

static void utilImeSetOpen(ParamList &params) {
	// TODO
	//warning("Util.IME_set_open not implemented");
}

static void utilGetBackspace(ParamList &params) {
	memSetVar32(18, inputDown(kInputBackspace));
}

static void utilGetBackspace2(ParamList &params) {
	uint32 prev = memGetVar32(4);
	bool bsDown = inputDown(kInputBackspace);
	if (!prev && bsDown) {
		memSetVar32(20, 1);
		memSetVar32(4, 0xffffffff);
	} else if (prev && !bsDown) {
		memSetVar32(20, 0);
		memSetVar32(4, 0);
	} else {
		memSetVar32(20, 0);
	}
}

/*
 * Save a particular set of var4s to FLAG00.
 */
static void utilSaveVar4(ParamList &params) {
	const char *saveName = sysSaveNameNo(0);
	byte save[kVar4Off + kVar4Size];
	savedataRead(saveName, save, kVar4Off, kVar4Size);

	for (int i = kVar4Off; i < kVar4Off + 1900; i++) {
		save[i] |= memRaw()[i];
	}
	byte flag;
	if ((flag = memGetVar4(1834)))
		save[kVar4Off + 1834] = flag;
	if ((flag = memGetVar4(1721)))
		save[kVar4Off + 1721] = flag;
	if (memGetVar4(1859))
		save[kVar4Off + 1859] = flag;
	if ((flag = memGetVar4(1789)))
		save[kVar4Off + 1789] = flag;
	if ((flag = memGetVar4(1860)))
		save[kVar4Off + 1860] = flag;
	if ((flag = memGetVar4(1863)))
		save[kVar4Off + 1863] = flag;

	savedataWrite(saveName, save, kVar4Off, kVar4Size);
}

/*
 * Animation during Misa's ending where the train pulls into the station.
 */
static void utilMisaTrainIn(ParamList &params) {
	sysLoadImageAt("Y04BTR.G16", 11, 1);

	uint16 maskColor = memGetSysvar16(kSysVarMaskColor);
	VMTimer timer = vmTimerCreate();

	for (int dstX = 640 - 8; dstX >= 0; dstX -= 8) {
		gfxCopy(0, 0, 640, 480, 2, 0, 0, 0);
		gfxCopyMasked(0, 0, 640 - dstX, 480, 11, dstX, 0, 0, maskColor);
		vmPeek();
		vmTimerTick(timer, 30);
	}
	for (int srcX = 8; srcX <= 2080; srcX += 8) {
		gfxCopy(0, 0, 640, 480, 2, 0, 0, 0);
		gfxCopyMasked(srcX, 0, 640, 480, 11, 0, 0, 0, maskColor);
		vmPeek();
		vmTimerTick(timer, 30);
	}
}

/*
 * Animation during Misa's ending where the train pulls out of the station.
 */
static void utilMisaTrainOut(ParamList &params) {
	uint16 maskColor = memGetSysvar16(kSysVarMaskColor);
	VMTimer timer = vmTimerCreate();

	for (int srcX = 2088; srcX < 2672; srcX += 8) {
		gfxCopy(0, 0, 640, 480, 2, 0, 0, 0);
		gfxCopyMasked(srcX, 0, 640, 480, 11, 0, 0, 0, maskColor);
		vmPeek();
		vmTimerTick(timer, 30);
	}
	gfxCopy(0, 0, 640, 480, 2, 0, 0, 0);
}

namespace {

struct MovieOverlay {
	int w, h;
	Common::Array<byte> pixels; // RGBA
};

struct MovieState {
	Common::ScopedPtr<ArcArchive> arc;
	Common::ScopedPtr<Movie> movie;
	bool isEnding;
	MovieOverlay credits;
	MovieOverlay chara;
} movieState;

} // End of anonymous namespace

static void movieEnd() {
	movieState.movie.reset();
	movieState.isEnding = false;
	movieState.credits.pixels.clear();
	movieState.chara.pixels.clear();
}

static const char *charaFileName(uint i) {
	static const char *const names[] = {
		"mai.g16", "misa.g16", "miho.g16", "satomi.g16", "kurumi.g16", "chiharu.g16", "yoshiko.g16",
		"mako.g16", "ako.g16", "hiromi.g16", "reiko.g16", "kaori.g16", "yayoi.g16", "natuko.g16"
	};
	return i < ARRAYSIZE(names) ? names[i] : nullptr;
}

static bool loadMovieOverlay(const char *name, MovieOverlay &out) {
	// load file from movie archive
	Common::ScopedPtr<Common::SeekableReadStream> file(movieState.arc->createReadStreamForMember(Common::Path(name)));
	if (!file) {
		warning("Failed to open %s", name);
		return false;
	}
	Common::Array<byte> data(file->size());
	file->read(data.data(), data.size());

	// decode CG
	Common::SharedPtr<CG> cg = assetCgDecode(name, data);
	if (!cg || cg->isIndexed()) {
		warning("Failed to decode CG \"%s\"", name);
		return false;
	}

	// convert color key to alpha
	out.w = cg->w;
	out.h = cg->h;
	out.pixels = cg->pixels;
	for (uint i = 0; i + 3 < out.pixels.size(); i += 4) {
		byte *p = &out.pixels[i];
		if (p[0] == 0 && p[1] == 248 && p[2] == 0)
			p[3] = 0;
	}
	return true;
}

static void drawMovieOverlay(const MovieOverlay &o, int srcY, int dstX, int dstY, int w, int h, GfxSurface *dst) {
	for (int row = 0; row < h; row++) {
		if (srcY + row >= o.h || dstY + row >= dst->h)
			break;
		const byte *src = &o.pixels[((srcY + row) * o.w) * 4];
		byte *out = gfxPixel(dst, dstX, dstY + row);
		for (int col = 0; col < w && col < o.w && dstX + col < dst->w; col++, src += 4, out += 3) {
			if (src[3])
				memcpy(out, src, 3);
		}
	}
}

static void utilMovieLoad(ParamList &params) {
	if (!movieState.arc) {
		movieState.arc.reset(ArcArchive::open(Common::Path("STREAM.DAT"), GID_DOUKYUUSEI, 0));
		if (!movieState.arc) {
			warning("Failed to open archive: STREAM.DAT");
			movieEnd();
			return;
		}
	}

	const char *videoName = vmStringParam(params, 4);
	Common::SeekableReadStream *video = movieState.arc->createReadStreamForMember(Common::Path(videoName));
	if (!video) {
		warning("Failed to open video file: %s", videoName);
		movieEnd();
		return;
	}
	Common::SeekableReadStream *audio = nullptr;
	if (params.nrParams > 8) {
		assetSetVoiceArchive("SSIDE.ARC");
		if (!(audio = assetVoiceLoad(vmStringParam(params, 8))))
			warning("Failed to open audio file: %s", vmStringParam(params, 8));
	}

	movieState.movie.reset(new Movie());
	if (!movieState.movie->load(video, audio)) {
		warning("Failed to load movie");
		movieEnd();
		return;
	}

	if (!scumm_stricmp(videoName, "end.avi")) {
		// load overlay images
		const char *charaName = charaFileName(vmExprParam(params, 1));
		if (!charaName || !loadMovieOverlay(charaName, movieState.chara)
				|| !loadMovieOverlay("staff.g16", movieState.credits)) {
			movieEnd();
			return;
		}
		movieState.isEnding = true;
	}
}

static bool movieCancelled() {
	vmPeek();
	if (g_engine->shouldQuit())
		return true;
	if (memGetVar4(4047) && inputDown(kInputCancel)) {
		memSetVar32(18, 1);
		return true;
	}
	return false;
}

#define MS(minutes, seconds, ms) ((((minutes) * 60) + (seconds)) * 1000 + (ms))

static void playEnding(GfxSurface *frame, GfxSurface *s0) {
	struct MovieSeek {
		int t;
		uint frame;
	};
	static const MovieSeek seek[] = {
		{ MS(0, 24,   0), 185 },
		{ MS(0, 29, 400), 185 },
		{ MS(0, 34, 900),  95 },
		{ MS(0, 49, 300), 185 },
		{ MS(0, 54, 700), 185 },
		{ MS(1,  0, 100), 185 },
		{ MS(1,  5, 500),  95 },
		{ MS(1, 14, 700),  95 },
		{ MS(1, 29, 100), 185 },
		{ MS(1, 34, 500), 185 },
		{ MS(1, 40,   0),  95 },
		{ MS(1, 54, 400), 185 },
		{ MS(1, 59, 800), 185 },
		{ MS(2,  5, 200), 185 },
		{ MS(2, 10, 600),  95 },
		{ MS(2, 25, 100),  95 },
		// transition to evening
		{ MS(2, 46, 500), 400 },
		{ MS(2, 51, 300), 310 },
		{ MS(3,  5,   0), 400 },
		{ MS(3,  9, 800), 310 },
		{ MS(3, 23, 500), 400 },
		{ MS(3, 28, 300), 400 },
		{ MS(3, 33, 100), 310 },
		{ MS(3, 46, 800), 400 },
		{ MS(3, 51, 600), 310 },
		{ MS(4,  5, 300), 400 },
		{ MS(4, 10, 100), 310 },
		{ MS(4, 23, 800), 310 }
	};
	uint seekI = 0;

	struct MovieCreditsFrame {
		int t;
		int srcY;
		int dstY;
		int h;
	};
	static const MovieCreditsFrame credits[] = {
		//  frame time    srcY   dstY    h
		{ MS(0,  0,   0),    0,     0,   0 },
		{ MS(0, 10,   0),    0,   128,  53 },
		{ MS(0, 20,   0),    0,     0,   0 },
		{ MS(0, 21, 500),   57,   113,  89 },
		{ MS(0, 31, 500),    0,     0,   0 },
		{ MS(0, 33,   0),  158,   134,  47 },
		{ MS(0, 43,   0),    0,     0,   0 },
		{ MS(0, 44, 500),  214,   134,  47 },
		{ MS(0, 54, 500),    0,     0,   0 },
		{ MS(0, 56,   0),  264,    88, 139 },
		{ MS(1,  6,   0),    0,     0,   0 },
		{ MS(1,  7, 500),  413,   101, 114 },
		{ MS(1, 17, 500),    0,     0,   0 },
		{ MS(1, 19,   0),  529,   121,  72 },
		{ MS(1, 29,   0),    0,     0,   0 },
		{ MS(1, 30, 500),  611,   122,  69 },
		{ MS(1, 40, 500),    0,     0,   0 },
		{ MS(1, 42,   0),  684,   100, 114 },
		{ MS(1, 52,   0),    0,     0,   0 },
		{ MS(1, 53, 500),  804,   100, 114 },
		{ MS(2,  3, 500),    0,     0,   0 },
		{ MS(2,  5,   0),  924,   100, 114 },
		{ MS(2, 15,   0),    0,     0,   0 },
		{ MS(2, 16, 500), 1044,   100, 114 },
		{ MS(2, 26, 500),    0,     0,   0 },
		{ MS(2, 28,   0), 1162,   122,  70 },
		{ MS(2, 38,   0),    0,     0,   0 },
		{ MS(2, 45,   0), 1236,   100, 114 },
		{ MS(2, 55,   0),    0,     0,   0 },
		{ MS(2, 56, 500), 1356,   100, 114 },
		{ MS(3,  6, 500),    0,     0,   0 },
		{ MS(3,  8,   0), 1479,   111,  93 },
		{ MS(3, 18,   0),    0,     0,   0 },
		{ MS(3, 19, 500), 1580,    99, 115 },
		{ MS(3, 29, 500),    0,     0,   0 },
		{ MS(3, 31,   0), 1700,   100, 114 },
		{ MS(3, 41,   0),    0,     0,   0 },
		{ MS(3, 42, 500), 1820,   100, 114 },
		{ MS(3, 52, 500),    0,     0,   0 },
		{ MS(3, 54,   0), 1940,   100, 114 },
		{ MS(4,  4,   0),    0,     0,   0 },
		{ MS(4,  5, 500), 2060,   100, 114 },
		{ MS(4, 15, 500),    0,     0,   0 },
	};
	uint creditsI = 0;

	static const int chara[] = {
		MS(0,  0,   0),
		MS(2, 42,   0), MS(2, 42, 100), MS(2, 42, 200), MS(2, 42, 300), MS(2, 42, 400),
		MS(2, 42, 500), MS(2, 42, 600), MS(2, 42, 700), MS(2, 42, 800), MS(2, 42, 900),
		MS(2, 43,   0), MS(2, 43, 100), MS(2, 43, 200), MS(2, 43, 300), MS(2, 43, 400),
		MS(2, 43, 500), MS(2, 43, 600), MS(2, 43, 700), MS(2, 43, 800), MS(2, 43, 900),
		MS(4, 17, 100), MS(4, 17, 400), MS(4, 17, 600), MS(4, 17, 800), MS(4, 18,   0),
		MS(4, 18, 200), MS(4, 18, 400), MS(4, 18, 600), MS(4, 18, 800), MS(4, 19, 300),
		MS(4, 19, 500), MS(4, 37, 600), MS(4, 37, 700), MS(4, 37, 800), MS(4, 37, 900),
		MS(4, 38,   0), MS(4, 38, 100), MS(4, 38, 200),
	};
	int charaSrcY = 0;
	uint charaI = 0;

	Movie *movie = movieState.movie.get();
	while (!movie->isEnd()) {
		int pos = movie->getPosition();
		if (seekI < ARRAYSIZE(seek) && pos - seek[seekI].t > -2) {
			movie->seekVideo(seek[seekI].frame);
			seekI++;
		}
		int r = movie->draw(frame);
		if (r < 0)
			break;
		if (r > 0) {
			// draw credits/characters on top of video
			if (creditsI + 1 < ARRAYSIZE(credits) && pos - credits[creditsI + 1].t >= 0)
				creditsI++;
			if (charaI + 1 < ARRAYSIZE(chara) && pos - chara[charaI + 1] >= 0) {
				charaI++;
				charaSrcY += 200;
			}
			memcpy(s0->pixels, frame->pixels, frame->pitch * frame->h);
			const MovieCreditsFrame &c = credits[creditsI];
			if (c.h)
				drawMovieOverlay(movieState.credits, c.srcY, 208, c.dstY, 224, c.h, s0);
			if (pos < MS(4, 38, 300))
				drawMovieOverlay(movieState.chara, charaSrcY, 160, 265, 205, 200, s0);
			gfxWholeSurfaceDirty(0);
		}
		if (movieCancelled())
			break;
	}
}

#undef MS

static void utilMoviePlay(ParamList &params) {
	if (!movieState.movie) {
		warning("No movie loaded");
		return;
	}

	GfxSurface *s0 = gfxGetSurface(0);
	Common::Array<byte> frameBuf(s0->pitch * s0->h);
	GfxSurface frame = { frameBuf.data(), s0->w, s0->h, s0->pitch, s0->bytesPerPixel };

	movieState.movie->play(18);
	if (movieState.isEnding) {
		playEnding(&frame, s0);
	} else {
		Movie *movie = movieState.movie.get();
		while (!movie->isEnd()) {
			int r = movie->draw(s0);
			if (r < 0)
				break;
			if (r > 0)
				gfxWholeSurfaceDirty(0);
			if (movieCancelled())
				break;
		}
	}

	// leave the last video frame (without overlays) on surface 0
	if (movieState.isEnding)
		memcpy(s0->pixels, frame.pixels, frame.pitch * frame.h);
	gfxWholeSurfaceDirty(0);
	movieEnd();
}

static void util500(ParamList &params) {
	// TODO
	//warning("Util.function[500] not implemented");
}

static void util501(ParamList &params) {
	// TODO
	//warning("Util.function[501] not implemented");
	memSetVar32(25, 0);
}

static void utilEndWait(ParamList &params) {
	uint32 ticks = vmExprParam(params, 1);
	VMTimer start = vmTimerCreate();
	VMTimer timer = start;

	bool allowSkip = memGetVar4(4047);
	while (timer - start < ticks && !g_engine->shouldQuit()) {
		if (allowSkip && inputDown(kInputCancel))
			break;
		vmTimerTick(timer, 16);
	}
}

static void doukyuuseiInit() {
	g_mapVersion = kMapVersionNew;
	mapInit();
	movieState.arc.reset();
	movieEnd();
	audioSetVolume(kAudioChBgm, -1500);
	audioSetVolume(kAudioChSe0, -1500);
	audioSetVolume(kAudioChVoice0, -500);
}

static void doukyuuseiDrawText(const char *text) {
	if (vmFlagIsOn(kFlagStrlen)) {
		memSetVar32(11, memGetVar32(11) + strlen(text));
	} else {
		vmDrawText(text, 1);
	}
}

void initGameDoukyuusei(Game &g) {
	static const struct { uint16 w, h; } sizes[] = {
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 992, 832 },
		{ 640, 480 },
		{ 640, 1280 },
		{ 640, 480 },
		{ 1280, 1280 },
		{ 264, 532 },
		{ 2672, 480 }
	};
	g.id = GID_DOUKYUUSEI;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 16;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.init = doukyuuseiInit;
	g.memInit = doukyuuseiMemInit;
	g.memRestore = doukyuuseiMemRestore;
	g.drawTextZen = doukyuuseiDrawText;
	g.drawTextHan = doukyuuseiDrawText;

	g.vm.exec = vmExec;
	g.vm.eval = vmEval;
	g.vm.readParams = vmReadParams;
	g.vm.endCode = 0x00;
	vmSetDefaultOps(g);
	g.exprOp[0xe5] = vmExprRandWithImmRange;
	g.stmtOp[0x03] = vmStmtSetFlagConst16_4bitSaturate;
	g.stmtOp[0x05] = vmStmtSetFlagExpr4bitSaturate;

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = doukyuuseiCursor;
	g.sys[3] = doukyuuseiAnim;
	g.sys[4] = doukyuuseiSavedata;
	g.sys[5] = doukyuuseiAudio;
	g.sys[6] = doukyuuseiVoice;
	g.sys[7] = sysLoadFile;
	g.sys[8] = sysLoadImage;
	g.sys[9] = doukyuuseiDisplay;
	g.sys[10] = doukyuuseiGraphics;
	g.sys[11] = doukyuuseiWait;
	g.sys[12] = sysSetTextColorsDirect;
	g.sys[13] = sysFarcall;
	g.sys[14] = sysGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[16] = sysGetTime;
	g.sys[17] = doukyuuseiMap;
	g.sys[18] = sysCheckInput;
	g.sys[19] = doukyuuseiBacklog;
	g.sys[24] = doukyuuseiStrlen;
	g.sys[25] = doukyuuseiSys25;
	g.sys[26] = doukyuuseiSys26;
	g.sys[255] = utilNoop;

	g.util[5] = utilCopyVar4;
	g.util[6] = utilResumeLoadWithSpecialFlags;
	g.util[7] = utilGetCtrl;
	g.util[11] = utilDatebarSlideUp;
	g.util[12] = utilDatebarSlideDown;
	g.util[13] = utilCursorDescriptionSlideUp;
	g.util[14] = utilCursorDescriptionSlideDown;
	g.util[16] = utilScroll;
	g.util[17] = utilMultiStrcmp;
	g.util[18] = utilSaveName;
	g.util[19] = utilImeEnable;
	g.util[20] = utilImeDisable;
	g.util[21] = utilImeGetOpen;
	g.util[22] = utilImeSetOpen;
	g.util[25] = utilImeGetCompositionStarted;
	g.util[69] = utilGetBackspace;
	g.util[70] = utilGetBackspace2;
	g.util[100] = utilSaveVar4;
	g.util[200] = utilMisaTrainIn;
	g.util[201] = utilMisaTrainOut;
	g.util[300] = utilMovieLoad;
	g.util[301] = utilMoviePlay;
	g.util[350] = utilWarnUnimplemented;
	g.util[351] = utilWarnUnimplemented;
	g.util[400] = utilLoadName;
	g.util[500] = util500;
	g.util[501] = util501;
	g.util[600] = utilEndWait;

	g.flags[kFlagAnimEnable] = 0x0004;
	g.flags[kFlagMenuReturn] = 0x0008;
	g.flags[kFlagReturn] = 0x0010;
	g.flags[kFlagLogEnable] = 0x0020;
	g.flags[kFlagLogText] = 0x0040;
	g.flags[kFlagLog] = 0x0080;
	g.flags[kFlagVoiceEnable] = 0x0100;
	g.flags[kFlagAudioEnable] = 0x0200;
	g.flags[kFlagStrlen] = 0x0400;
	g.flags[kFlagLogSys] = 0x1000;
	g.flags[kFlagWaitKeyup] = kFlagAlwaysOn;
}

} // End of namespace AI5
