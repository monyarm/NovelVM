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
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/dungeon.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/popupmenu.h"
#include "ai5/savedata.h"
#include "ai5/sys.h"
#include "ai5/vm.h"
#include "ai5/games/isaku.h"

namespace AI5 {

enum {
	kMesNameSize = 128,
	kVar4Size = 2048,
	kMem16Size = 4096,
	kVar4Off = kMesNameSize,
	kSv16PtrOff = kVar4Off + kVar4Size,
	kVar16Off = kSv16PtrOff + 4,
	kSysvar16Off = kVar16Off + 26 * 2,
	kVar32Off = kSysvar16Off + 26 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	kHeapOff = kSysvar32Off + 61 * 4
};
static_assert(kHeapOff == 0xa48, "Isaku heap offset");

static bool g_overlayOn = false;

static void isakuMemRestore() {
	memSetSysvar16Ptr(kSysvar16Off);
	memSetSysvar32(kSysVarMemory, offsetof(Memory, mem16));
	memSetSysvar32(kSysVarPalette, offsetof(Memory, palette));
	memSetSysvar32(kSysVarFileData, offsetof(Memory, fileData));
	memSetSysvar32(kSysVarMenuEntryAddresses, offsetof(Memory, menuEntryAddresses));
	memSetSysvar32(kSysVarMenuEntryNumbers, offsetof(Memory, menuEntryNumbers));

	uint16 flags = memGetSysvar16(kSysVarFlags);
	memSetSysvar16(kSysVarFlags, flags | 4);
	memSetSysvar16((uint)0, kHeapOff);
	memSetVar16(22, 20);
}

static void isakuMemInit() {
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
	memSetSysvar16(kSysVarFontWidth, 16);
	memSetSysvar16(kSysVarFontHeight, 16);
	memSetSysvar16(kSysVarCharSpace, 16);
	memSetSysvar16(kSysVarLineSpace, 16);
	memSetSysvar16(kSysVarMaskColor, 0);

	memSetSysvar32(kSysVarCgOffset, 0x20000);
	isakuMemRestore();
}

static uint cursorNo(uint n) {
	switch (n) {
	case 0: return 30;
	case 1: return 32;
	case 2: return 34;
	case 3: return 36;
	case 4: return 38;
	case 5: return 40;
	case 6: return 42;
	// 7 is skipped
	case 8: return 44;
	// 9-12 are skipped
	case 13: return 46;
	case 14: return 48;
	case 15: return 50;
	case 16: return 52;
	default:
		warning("Invalid cursor number: %u", n);
	}
	return 30;
}

static void isakuCursor(ParamList &params) {
	static uint32 uk = 0;
	switch (vmExprParam(params, 0)) {
	case 0: cursorShow(); break;
	case 1: cursorHide(); break;
	case 2: sysCursorSavePos(params); break;
	case 3: cursorSetPos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 4: cursorLoad(cursorNo(vmExprParam(params, 1)), 2, nullptr); break;
	case 5: uk = 0; break;
	case 6: memSetVar16(18, 0); break;
	case 7: memSetVar32(18, uk); break;
	case 8: uk = vmExprParam(params, 1); break;
	default:
		error("System.Cursor.function[%u] not implemented", params.params[0].val);
	}
}

static uint vmAnimParam(ParamList &params, uint i) {
	uint a = vmExprParam(params, i);
	uint b = vmExprParam(params, i + 1);
	if (a >= kAnimMaxStreams)
		error("Invalid animation stream index: %u:%u", a, b);
	return a * 10 + b;
}

static void isakuAnim(ParamList &params) {
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
	default:
		error("System.Anim.function[%u] not implemented", params.params[0].val);
	}
}

static void isakuSavedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: savedataResumeLoad(sysSaveName(params)); break;
	case 1: savedataResumeSave(sysSaveName(params)); break;
	case 2: savedataLoad(sysSaveName(params), kVar4Off); break;
	case 3: savedataSaveUnionVar4(sysSaveName(params)); break;
	case 6: memset(memRaw() + kVar4Off, 0, kVar4Size); break;
	default:
		error("System.SaveData.function[%u] not implemented", params.params[0].val);
	}
}

static void fadeOutSync(AudioChannel ch) {
	audioFade(ch, kAudioVolumeMin, 500, true, false);
	while (audioIsPlaying(ch) && !g_engine->shouldQuit()) {
		if (inputDown(kInputShift))
			break;
		vmPeek();
		vmDelay(16);
	}
}

static void isakuSeWait() {
	while (audioIsPlaying(kAudioChSe0) && !g_engine->shouldQuit()) {
		vmPeek();
		vmDelay(16);
	}
}

static char g_bgmName[32];

static void isakuAudio(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:
		Common::strlcpy(g_bgmName, vmStringParam(params, 1), sizeof(g_bgmName));
		break;
	case 1:
	case 2:
	case 7:
		g_bgmName[0] = '\0';
		break;
	default:
		break;
	}
	if (!vmFlagIsOn(kFlagAudioEnable))
		return;
	switch (vmExprParam(params, 0)) {
	case 0: audioBgmPlay(vmStringParam(params, 1), true); break;
	case 1: audioFade(kAudioChBgm, kAudioVolumeMin, 2000, true, false); break;
	case 2: audioStop(kAudioChBgm); break;
	case 3: audioSePlay(vmStringParam(params, 1), 0); break;
	case 4: audioStop(kAudioChSe0); break;
	case 5: audioFade(kAudioChSe0, kAudioVolumeMin, 2000, true, false); break;
	// 6 (bgm_play_sync) is unused
	case 7: fadeOutSync(kAudioChBgm); break;
	case 8: fadeOutSync(kAudioChSe0); break;
	case 9: isakuSeWait(); break;
	default:
		error("System.Audio.function[%u] not implemented", params.params[0].val);
	}
}

static void isakuVoice(ParamList &params) {
	if (!vmFlagIsOn(kFlagVoiceEnable))
		return;
	switch (vmExprParam(params, 0)) {
	case 0:
		audioVoicePlay(vmStringParam(params, 1), 0);
		break;
	case 1:
		audioStop(kAudioChVoice0);
		break;
	case 2:
		audioVoicePlay(vmStringParam(params, 1), 0);
		while (audioIsPlaying(kAudioChVoice0) && !g_engine->shouldQuit()) {
			vmPeek();
			vmDelay(50);
		}
		break;
	default:
		warning("System.Voice.function[%u] not implemented", params.params[0].val);
	}
}

static bool skipOnShift() {
	return !inputDown(kInputShift);
}

static void isakuDisplay(ParamList &params) {
	animHaltAll();
	switch (vmExprParam(params, 0)) {
	case 0:
		// only unfreeze is used in Isaku
		if (params.nrParams > 1)
			gfxDisplayFreeze();
		else
			gfxDisplayUnfreeze();
		break;
	case 1:
	case 2:
		if (params.nrParams > 1)
			gfxDisplayFadeOut(vmExprParam(params, 1), 1000, skipOnShift);
		else
			gfxDisplayFadeIn(1000, skipOnShift);
		break;
	default:
		error("System.Display.function[%u] not implemented", params.params[0].val);
	}
}

static void isakuGraphicsCrossfade(ParamList &params) {
	// the parameters are always the same except for src/dst
	uint srcA = vmExprParam(params, 5);
	uint srcB = vmExprParam(params, 8);

	VMTimer timer = vmTimerCreate();
	for (uint a = 0; a < 256; a += 8) {
		if (inputDown(kInputCtrl) || g_engine->shouldQuit())
			break;
		gfxCopy(0, 0, 640, 480, srcB, 0, 0, 0);
		gfxBlend(0, 0, 640, 480, srcA, 0, 0, 0, a);
		vmPeek();
		vmTimerTick(timer, 33);
	}
	gfxCopy(0, 0, 640, 480, srcA, 0, 0, 0);
}

static void isakuGraphics(ParamList &params) {
	static int frame = 0;
	static VMTimer timer = 0;
	switch (vmExprParam(params, 0)) {
	case 0:
		sysGraphicsCopy(params);
		if (vmExprParam(params, 2) == vmExprParam(params, 4)) {
			if (!frame)
				vmTimerTick(timer, 4);
			frame = (frame + 1) % 4;
		}
		break;
	case 1:
		sysGraphicsCopyMasked(params);
		if (vmExprParam(params, 2) == vmExprParam(params, 4)) {
			if (!frame)
				vmTimerTick(timer, 4);
			frame = (frame + 1) % 4;
		}
		break;
	case 2: sysGraphicsFillBg(params); break;
	case 3: sysGraphicsCopySwap(params); break;
	case 4: sysGraphicsSwapBgFg(params); break;
	case 5: sysGraphicsCopyProgressive(params); break;
	case 6: sysGraphicsCompose(params); break;
	case 7: isakuGraphicsCrossfade(params); break;
	default:
		break;
	}
}

static void isakuWait(ParamList &params) {
	if (params.nrParams > 0 && vmExprParam(params, 0) == 0)
		vmDelay(16);
	else
		sysWait(params);
}

static void isakuDungeon(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:
		dungeonLoad(memRaw() + vmExprParam(params, 1),
				memRaw() + vmExprParam(params, 5),
				memRaw() + vmExprParam(params, 6),
				memRaw() + vmExprParam(params, 7),
				memRaw() + vmExprParam(params, 8),
				memRaw() + vmExprParam(params, 9));
		break;
	case 1:
		dungeonSetPos(vmExprParam(params, 1), vmExprParam(params, 2), (DungeonDirection)vmExprParam(params, 3));
		break;
	case 2:
		dungeonDraw();
		break;
	case 3:
		memSetVar16(18, dungeonMove((DungeonMoveCommand)vmExprParam(params, 1)));
		break;
	case 8:
		warning("System.Dungeon.function[8] not implemented");
		break;
	case 9:
		cursorLoad(vmExprParam(params, 1) + 25, 1, nullptr);
		break;
	default:
		error("System.Dungeon.function[%u] not implemented", params.params[0].val);
	}

	if (vmExprParam(params, 0) != 9) {
		uint16 x, y, dir;
		dungeonGetPos(&x, &y, &dir);
		memSetVar16(23, x);
		memSetVar16(24, y);
		memSetVar16(3, dir);
	}
}

static bool g_builtinSeEnabled = false;

void isakuBuiltinSePlay(const char *name) {
	if (g_builtinSeEnabled)
		audioSePlay(name, 0);
}

static void isakuItemWindow(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:
		isakuItemWindowCreate();
		break;
	case 1:
		isakuItemWindowToggle();
		break;
	case 2:
		memSetVar16(18, isakuItemWindowIsOpen());
		break;
	case 3: {
		int x, y, w, h;
		isakuItemWindowGetPos(x, y, w, h);
		WRITE_LE_UINT16(g_memPtr.systemVar32 + 44, x);
		WRITE_LE_UINT16(g_memPtr.systemVar32 + 46, y);
		WRITE_LE_UINT16(g_memPtr.systemVar32 + 48, w);
		WRITE_LE_UINT16(g_memPtr.systemVar32 + 50, h);
		break;
	}
	case 4: {
		int x, y;
		isakuItemWindowGetCursorPos(x, y);
		memSetSysvar16(kSysVarCursorX, x);
		memSetSysvar16(kSysVarCursorY, y);
		break;
	}
	case 5:
		isakuItemWindowEnable();
		break;
	case 6:
		isakuItemWindowDisable();
		break;
	case 7: {
		bool lmbDown, rmbDown;
		isakuItemWindowGetMouseState(lmbDown, rmbDown);
		WRITE_LE_UINT16(g_memPtr.systemVar32 + 52, lmbDown);
		WRITE_LE_UINT16(g_memPtr.systemVar32 + 54, rmbDown);
		break;
	}
	case 8:
		isakuItemWindowUpdate();
		break;
	case 9:
		memSetVar16(18, 0);
		break;
	case 10:
		warning("ItemWindow.function[10] not implemented");
		break;
	default:
		error("System.ItemWindow.function[%u] not implemented", params.params[0].val);
	}
}

static void clearOverlayRect(GfxSurface *s, int x, int y, int w, int h) {
	for (int row = y; row < y + h; row++)
		memset(gfxPixel(s, x, row), 0, w * 4);
}

static void disableOverlay() {
	clearOverlayRect(gfxGetOverlay(0), 0, 388, 640, 72);
	g_overlayOn = false;
	gfxOverlayDisable(0);
}

// Text overlay, used in (most?) scenes with a full-screen CG.
static void isakuOverlay(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:
		// always called with the same arguments
		g_overlayOn = true;
		gfxDirty(0, 0, 388, 640, 72);
		break;
	case 1:
		disableOverlay();
		break;
	case 3:
		disableOverlay();
		gfxDirty(0, 0, 388, 640, 72);
		break;
	default:
		warning("System.function[23].function[%u] not implemented", params.params[0].val);
	}
}

static void isakuStrlen(ParamList &params) {
	vmFlagOn(kFlagStrlen);
	memSetVar32(18, 0);
	sysFarcall(params);
	vmFlagOff(kFlagStrlen);
}

struct IsakuMenu {
	bool enabled;
	bool requested;
	const char *name;
};

static IsakuMenu g_saveMenu = { false, false, "SaveMenu" };
static IsakuMenu g_loadMenu = { false, false, "LoadMenu" };

static void menuOpen(IsakuMenu &menu) {
	if (!menu.enabled) {
		isakuBuiltinSePlay("error.wav");
		return;
	}
	isakuBuiltinSePlay("winopn.wav");
	menu.requested = true;
}

static void isakuMenu(ParamList &params, IsakuMenu &menu) {
	switch (vmExprParam(params, 0)) {
	case 0: menuOpen(menu); break;
	case 1: menu.enabled = vmExprParam(params, 1); break;
	case 2: menu.requested = false; break;
	case 3: memSetVar16(18, menu.requested); break;
	default:
		error("System.%s.function[%u] not implemented", menu.name, params.params[0].val);
	}
}

static void isakuSaveMenu(ParamList &params) {
	isakuMenu(params, g_saveMenu);
}

static void isakuLoadMenu(ParamList &params) {
	isakuMenu(params, g_loadMenu);
}

static bool g_messageClearEnabled = false;
static bool g_messageCleared = false;

static void messageClearWait() {
	while (g_messageCleared && !g_engine->shouldQuit()) {
		if (inputDown(kInputTab)) {
			inputWaitUntilUp(kInputTab);
			break;
		}
		vmDelay(16);
		vmPeek();
	}
	g_messageCleared = false;
}

static void messageClear() {
	if (!g_messageClearEnabled)
		return;

	if (g_overlayOn) {
		gfxOverlayDisable(0);
		g_messageCleared = true;
		messageClearWait();
		gfxOverlayEnable(0);
	} else {
		gfxCopy(0, 316, 640, 72, 5, 0, 388, 0);
		g_messageCleared = true;
		messageClearWait();
		gfxCopy(0, 388, 640, 72, 5, 0, 388, 0);
	}
}

static void isakuMessage(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:
		g_messageClearEnabled = true;
		g_messageCleared = false;
		break;
	case 1:
		// System.wait is not called, so handle the message skip delay here
		if (inputDown(kInputCtrl)) {
			vmPeek();
			vmDelay(g_config.msgSkipDelay);
		}
		g_messageClearEnabled = false;
		break;
	default:
		warning("System.function[27].function[%u] not implemented", params.params[0].val);
	}
}

static void utilOffsetScreen(ParamList &params) {
	int16 xOff = (uint16)vmExprParam(params, 1);
	int16 yOff = (uint16)vmExprParam(params, 2);
	GfxSurfaceState *s = &g_gfx.surface[0];
	if (!xOff && !yOff) {
		s->dst = s->src;
		s->scaled = false;
	} else {
		s->dst.x = xOff;
		s->dst.y = yOff;
		s->scaled = true;
	}
	gfxScreenDirty();
}

static void utilItemCursor(ParamList &params) {
	cursorLoad(vmExprParam(params, 1) - 100, 1, nullptr);
}

static void utilLoadHeap(ParamList &params) {
	savedataRead("FLAG08", memRaw(), 3132, 100);
}

static void utilSaveHeap(ParamList &params) {
	savedataWrite("FLAG08", memRaw(), 3132, 100);
}

// Left scroll animation, used during Jinpachi's confession.
static void utilScrollLeft(ParamList &params) {
	// always called with the same parameters
	const uint src = 1;
	const uint dst = 0;
	const uint d = 894 - 640;

	VMTimer timer = vmTimerCreate();
	for (uint x = 0; x < d && !g_engine->shouldQuit(); x += 4) {
		gfxCopy(x, 0, 640, 480, src, 0, 0, dst);
		vmPeek();
		vmTimerTick(timer, 16);
	}
}

static void utilDelay(ParamList &params) {
	VMTimer t = vmTimerCreate();
	uint32 stopT = t + vmExprParam(params, 1) * 16;
	memSetVar32(18, 0);
	while (t < stopT && !g_engine->shouldQuit()) {
		if (inputDown(kInputShift))
			return;
		if (inputDown(kInputCancel)) {
			memSetVar32(18, 1);
			return;
		}
		vmPeek();
		vmTimerTick(t, 16);
	}
}

// Crossfade with start/end alpha, used when looking behind the projector screen.
static void utilCrossfade(ParamList &params) {
	// the parameters are always the same except for the start/end alpha
	uint startA = vmExprParam(params, 12) * 8;
	uint endA = MIN<uint>(255, vmExprParam(params, 13) * 8);

	VMTimer timer = vmTimerCreate();
	for (uint a = startA; a < endA && !g_engine->shouldQuit(); a += 8) {
		// one surface is always solid black
		gfxFill(0, 0, 640, 480, 0, 0);
		gfxBlend(0, 0, 640, 480, 3, 0, 0, 0, a);
		vmPeek();
		vmTimerTick(timer, 33);
	}
	if (endA == 255)
		gfxCopy(0, 0, 640, 480, 3, 0, 0, 0);
}

static Common::SharedPtr<CG> g_badEndCg[13];

static void utilBadEndPrepare(ParamList &params) {
	char name[20];
	for (int i = 0; i < 13; i++) {
		snprintf(name, sizeof(name), "A30_%02d.G16", i + 1);
		if (!(g_badEndCg[i] = assetCgLoad(name)))
			warning("Failed to load CG: %s", name);
	}
}

static void utilBadEndPlay(ParamList &params) {
	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < 13; i++) {
		if (g_badEndCg[i]) {
			gfxDrawCG(0, *g_badEndCg[i]);
			g_badEndCg[i].reset();
		}
		vmPeek();
		vmTimerTick(timer, 50);
	}
}

static void utilEnableBuiltinSe(ParamList &params) {
	g_builtinSeEnabled = true;
}

static void utilDisableBuiltinSe(ParamList &params) {
	g_builtinSeEnabled = false;
}

// color-keyed copy of an RGB24 surface into an RGBA overlay
static void copyToOverlay(GfxSurface *src, GfxRect srcR, GfxSurface *dst, GfxPoint dstP, const GfxColor &mask) {
	if (!gfxCopyClip(src, &srcR, dst, &dstP))
		return;
	for (int row = 0; row < srcR.h; row++) {
		const byte *s = gfxPixel(src, srcR.x, srcR.y + row);
		byte *d = gfxPixel(dst, dstP.x, dstP.y + row);
		for (int col = 0; col < srcR.w; col++, s += 3, d += 4) {
			if (s[0] == mask.r && s[1] == mask.g && s[2] == mask.b)
				continue;
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
			d[3] = 255;
		}
	}
}

// Credits scroll upwards, with an alpha gradient at the top and bottom. The
// overlay is used to make the gradient easier; AI5WIN.EXE does not.
static void utilCreditsScroll(ParamList &params) {
	const GfxRect r = { 140, 160, 360, 160 };
	const int topFadeStart = r.y;
	const int botFadeStart = r.y + r.h - 32;

	GfxSurface *src = gfxGetSurface(1);
	GfxSurface *dst = gfxGetOverlay(0);
	GfxColor mask = gfxDecodeBgr555(memGetSysvar16(kSysVarMaskColor));

	gfxOverlayEnable(0);

	int dstY = r.y + r.h;
	VMTimer timer = vmTimerCreate();
	for (int srcY = 0; srcY < 1600 && !g_engine->shouldQuit();) {
		if (dstY > 0)
			dstY--;

		int dstH = r.y + r.h - dstY;
		GfxRect srcR = { r.x, srcY, r.w, dstH };
		GfxPoint dstP = { r.x, dstY };
		copyToOverlay(src, srcR, dst, dstP, mask);

		for (int i = 0; i < 32; i++) {
			byte *p = gfxPixel(dst, r.x, topFadeStart + i);
			for (int col = 0; col < r.w; col++, p += 4) {
				if (p[3])
					p[3] = i * 8;
			}
			p = gfxPixel(dst, r.x, botFadeStart + i);
			for (int col = 0; col < r.w; col++, p += 4) {
				if (p[3])
					p[3] = (31 - i) * 8;
			}
		}

		gfxDirty(0, r.x, r.y, r.w, r.h);
		vmPeek();
		vmTimerTick(timer, inputDown(kInputCtrl) ? 16 : 50);

		clearOverlayRect(dst, r.x, r.y, r.w, r.h);

		if (dstY == 0)
			srcY++;
	}

	gfxOverlayDisable(0);
}

static void itemWindowClicked() {
	isakuItemWindowToggle();
}

static void saveDataClicked() {
	menuOpen(g_saveMenu);
}

static void loadDataClicked() {
	menuOpen(g_loadMenu);
}

static void hideMessageClicked() {
	if (g_messageCleared)
		g_messageCleared = false;
	else
		messageClear();
}

static void musicEnabledClicked(int index) {
	if (index == 0) {
		vmFlagOn(kFlagAudioEnable);
		audioBgmPlay(g_bgmName, true);
	} else {
		audioStop(kAudioChBgm);
		audioStop(kAudioChSe0);
		vmFlagOff(kFlagAudioEnable);
	}
}

static void voiceEnabledClicked(int index) {
	if (index == 0) {
		vmFlagOn(kFlagVoiceEnable);
	} else {
		audioStop(kAudioChVoice0);
		vmFlagOff(kFlagVoiceEnable);
	}
}

static void moveSpeedClicked(int index) {
	dungeonSetMoveSpeed((DungeonSpeed)index);
}

static void quitGameClicked() {
	g_engine->quitGame();
}

static void openContextMenu() {
	PopupMenu m;
	int itemId = m.appendEntry(1, "Item Window", "Space", itemWindowClicked);
	int saveId = m.appendEntry(4, "Save Data", "S", saveDataClicked);
	int loadId = m.appendEntry(3, "Load Data", "L", loadDataClicked);
	m.appendSeparator();
	int msgId = m.appendEntry(-1, "Hide Message", "Tab", hideMessageClicked);
	m.appendSeparator();

	static const char *const onOff[] = { "On", "Off" };
	static const char *const fastNormalSlow[] = { "Fast", "Normal", "Slow" };

	PopupMenu *music = new PopupMenu();
	music->appendRadioGroup(onOff, 2, vmFlagIsOn(kFlagAudioEnable) ? 0 : 1, musicEnabledClicked);
	PopupMenu *voice = new PopupMenu();
	voice->appendRadioGroup(onOff, 2, vmFlagIsOn(kFlagVoiceEnable) ? 0 : 1, voiceEnabledClicked);
	PopupMenu *moveSpeed = new PopupMenu();
	moveSpeed->appendRadioGroup(fastNormalSlow, 3, dungeonGetMoveSpeed(), moveSpeedClicked);

	PopupMenu *options = new PopupMenu();
	options->appendSubmenu(-1, "Music", music);
	options->appendSubmenu(-1, "Voice", voice);
	options->appendSubmenu(-1, "Move Speed", moveSpeed);
	m.appendSubmenu(-1, "Options", options);

	m.appendSeparator();
	m.appendEntry(-1, "Quit Game", "Alt+F4", quitGameClicked);
	m.appendSeparator();
	m.appendEntry(-1, "Cancel", nullptr, nullptr);

	m.setActive(itemId, isakuItemWindowIsEnabled());
	m.setActive(saveId, g_saveMenu.enabled);
	m.setActive(loadId, g_loadMenu.enabled);
	m.setActive(msgId, g_messageClearEnabled);

	uint x, y;
	cursorGetPos(x, y);
	m.run(x, y);
}

static bool isakuHandleEvent(const Common::Event &e) {
	if (isakuItemWindowIsOpen() && isakuItemWindowEvent(e))
		return true;

	switch (e.type) {
	case Common::EVENT_RBUTTONDOWN:
		return true;
	case Common::EVENT_RBUTTONUP:
		openContextMenu();
		return true;
	default:
		return false;
	}
}

static void isakuDrawText(const char *text) {
	if (vmFlagIsOn(kFlagStrlen))
		memSetVar32(18, memGetVar32(18) + strlen(text));
	else
		vmDrawText(text, 1);
}

static void isakuInit() {
	audioSetVolume(kAudioChBgm, -1500);
	audioSetVolume(kAudioChSe0, -1500);
	audioSetVolume(kAudioChVoice0, -500);
}

static void isakuUpdate() {
	// don't update while the message is cleared
	if (g_messageCleared)
		return;

	if (inputDown(kInputSpace)) {
		inputWaitUntilUpAlways(kInputSpace);
		isakuItemWindowToggle();
	}
	if (inputDown(kInputS)) {
		inputWaitUntilUpAlways(kInputS);
		menuOpen(g_saveMenu);
	}
	if (inputDown(kInputL)) {
		inputWaitUntilUpAlways(kInputL);
		menuOpen(g_loadMenu);
	}
	if (inputDown(kInputTab)) {
		inputWaitUntilUpAlways(kInputTab);
		messageClear();
	}
	isakuItemWindowTick();

	if (!memGetVar4(2007) || !g_overlayOn)
		return;
	if (!gfxIsDirty(5))
		return;

	// copy the text to the overlay
	gfxOverlayEnable(0);
	GfxColor mask = gfxDecodeBgr555(memGetSysvar16(kSysVarMaskColor));
	GfxRect rect = { 0, 388, 640, 72 };
	GfxPoint p = { 0, 388 };
	copyToOverlay(gfxGetSurface(5), rect, gfxGetOverlay(0), p, mask);
	gfxClean(5);
}

void initGameIsaku(Game &g) {
	static const struct { uint16 w, h; } sizes[] = {
		{ 640, 480 },
		{ 1000, 1750 },
		{ 640, 480 }, // AI5WIN.EXE crashes when using this surface
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 352, 32 },
		{ 320, 32 },
		{ 640, 480 }
	};
	g.id = GID_ISAKU;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 16;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.handleEvent = isakuHandleEvent;
	g.memInit = isakuMemInit;
	g.memRestore = isakuMemRestore;
	g.drawTextZen = isakuDrawText;
	g.drawTextHan = isakuDrawText;
	g.init = isakuInit;
	g.update = isakuUpdate;

	g.vm.exec = vmExec;
	g.vm.eval = vmEval;
	g.vm.readParams = vmReadParams;
	g.vm.endCode = 0x00;
	vmSetDefaultOps(g);

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = isakuCursor;
	g.sys[3] = isakuAnim;
	g.sys[4] = isakuSavedata;
	g.sys[5] = isakuAudio;
	g.sys[6] = isakuVoice;
	g.sys[7] = sysLoadFile;
	g.sys[8] = sysLoadImage;
	g.sys[9] = isakuDisplay;
	g.sys[10] = isakuGraphics;
	g.sys[11] = isakuWait;
	g.sys[12] = sysSetTextColorsDirect;
	g.sys[13] = sysFarcall;
	g.sys[14] = sysGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[16] = sysGetTime;
	g.sys[18] = sysCheckInput;
	g.sys[20] = isakuDungeon;
	g.sys[22] = isakuItemWindow;
	g.sys[23] = isakuOverlay;
	g.sys[24] = isakuStrlen;
	g.sys[25] = isakuSaveMenu;
	g.sys[26] = isakuLoadMenu;
	g.sys[27] = isakuMessage;

	g.util[0] = utilOffsetScreen;
	g.util[2] = utilItemCursor;
	g.util[3] = utilLoadHeap;
	g.util[4] = utilSaveHeap;
	g.util[6] = utilScrollLeft;
	g.util[7] = utilDelay;
	g.util[8] = utilCrossfade;
	g.util[9] = utilBadEndPrepare;
	g.util[10] = utilBadEndPlay;
	g.util[11] = utilEnableBuiltinSe;
	g.util[12] = utilDisableBuiltinSe;
	g.util[13] = utilCreditsScroll;

	g.flags[kFlagAnimEnable] = 0x0004;
	g.flags[kFlagMenuReturn] = 0x0008;
	g.flags[kFlagReturn] = 0x0010;
	g.flags[kFlagProcClear] = 0x0040;
	g.flags[kFlagVoiceEnable] = 0x0100;
	g.flags[kFlagAudioEnable] = 0x0200;
	g.flags[kFlagStrlen] = 0x0400;
	g.flags[kFlagWaitKeyup] = 0x0800;
}

} // End of namespace AI5
