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
#include "ai5/audio.h"
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/map.h"
#include "ai5/savedata.h"
#include "ai5/sys.h"
#include "ai5/vm.h"
#include "ai5/games/allstars.h"

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
	kVar32Off = kSysvar16Off + 24 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	// System.SaveData keeps 50 dwords at &System.var32[12], i.e. the
	// indices 12-61, so the system var32 area needs 62 dwords. This
	// places the heap 4 bytes after Be-Yond's (61 dwords).
	kSysvar32Size = 12 + 50,
	kHeapOff = kSysvar32Off + kSysvar32Size * 4,
	kHeapSize = kMem16Size - kHeapOff
};
static_assert(kHeapOff == 0x1248, "unexpected Elf All Stars heap offset");

enum {
	kScreenW = 640,
	kScreenH = 480
};

} // End of anonymous namespace

static void allstarsMemRestore() {
	// XXX: In AI5WIN.EXE, these are 32-bit pointers into the VM's own
	//      address space. Since we support 64-bit systems, we treat
	//      32-bit pointers as offsets into the `memory` struct (similar
	//      to how AI5WIN.EXE treats 16-bit pointers).
	memSetSysvar16Ptr(kSysvar16Off);
	memSetSysvar32(kSysVarMemory, offsetof(Memory, mem16));
	memSetSysvar32(kSysVarFileData, offsetof(Memory, fileData));
	memSetSysvar32(kSysVarMenuEntryAddresses, offsetof(Memory, menuEntryAddresses));
	memSetSysvar32(kSysVarMenuEntryNumbers, offsetof(Memory, menuEntryNumbers));

	uint16 flags = memGetSysvar16(kSysVarFlags);
	memSetSysvar16(kSysVarFlags, flags | 4);
	memSetSysvar16((uint)0, kHeapOff);
	memSetSysvar32(10, 0);
}

static void allstarsMemInit() {
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
	memSetSysvar16(kSysVarTextEndX, kScreenW);
	memSetSysvar16(kSysVarTextEndY, kScreenH);
	memSetSysvar16(kSysVarFontWidth, 16);
	memSetSysvar16(kSysVarFontHeight, 16);
	memSetSysvar16(kSysVarCharSpace, 16);
	memSetSysvar16(kSysVarLineSpace, 16);
	memSetSysvar16(kSysVarMaskColor, 0x3e0);

	memSetSysvar32(kSysVarCgOffset, 0x20000);
	allstarsMemRestore();
}

static void allstarsCursor(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: cursorShow(); break;
	case 1: cursorHide(); break;
	case 2: sysCursorSavePos(params); break;
	case 3: cursorSetPos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	// Same frame selection as the other AI5WIN games.
	case 4: cursorLoad(vmExprParam(params, 1) * 2, 2, nullptr); break;
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

static void allstarsAnim(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: animInitStream(vmAnimParam(params, 1), vmAnimParam(params, 1)); break;
	case 1: animStart(vmAnimParam(params, 1)); break;
	case 2: animStop(vmAnimParam(params, 1)); break;
	case 3: animHalt(vmAnimParam(params, 1)); break;
	case 4: animWait(vmAnimParam(params, 1)); break;
	case 5: animStopAll(); break;
	case 6: animHaltAll(); break;
	case 7: animResetAll(); break;
	default: error("System.Anim.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void allstarsLoadVar32Slice(const char *saveName) {
	savedataRead(saveName, memRaw(), kSysvar32Off + (12 * 4), 50 * 4);
}

static void allstarsSaveVar32Slice(const char *saveName) {
	savedataWrite(saveName, memRaw(), kSysvar32Off + (12 * 4), 50 * 4);
}

static void allstarsLoadVar4AndVar32(const char *saveName) {
	savedataLoadVar4(saveName);
	allstarsLoadVar32Slice(saveName);
	allstarsMemRestore();
}

static void allstarsLoadHeap(const char *saveName, int start, int count) {
	if (count <= 0 || start < 0 || start + count > kHeapSize) {
		warning("Invalid heap load: %d+%d", start, count);
		return;
	}
	savedataRead(saveName, memRaw(), kHeapOff + start, count);
}

static void allstarsSaveHeap(const char *saveName, int start, int count) {
	if (count <= 0 || start < 0 || start + count > kHeapSize) {
		warning("Invalid heap save: %d+%d", start, count);
		return;
	}
	savedataWrite(saveName, memRaw(), kHeapOff + start, count);
}

static void allstarsSavedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: savedataResumeLoad(sysSaveName(params)); break;
	case 1: savedataResumeSave(sysSaveName(params)); break;
	case 2: allstarsLoadVar4AndVar32(sysSaveName(params)); break;
	case 3: savedataSaveUnionVar4(sysSaveName(params)); break;
	case 4: allstarsLoadVar32Slice(sysSaveName(params)); break;
	case 5: allstarsSaveVar32Slice(sysSaveName(params)); break;
	case 6: memset(memRaw() + kVar4Off, 0, kVar4Size); break;
	case 7: allstarsLoadHeap(sysSaveName(params), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	case 8: allstarsSaveHeap(sysSaveName(params), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	// TODO: System.SaveData.function 9/10 (in-memory save/restore of var4
	//       plus the 50 dwords at var32[12]) are unimplemented upstream.
	//       The scripts call 9; accept and skip it until it is known.
	case 9:
	case 10: {
		static bool warned = false;
		if (!warned) {
			warned = true;
			warning("Elf All Stars: System.SaveData.function[%u] is not implemented",
					params.params[0].val);
		}
		break;
	}
	default: error("System.SaveData.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void allstarsAudio(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: audioBgmPlay(vmStringParam(params, 1), true); break;
	case 1: audioStop(kAudioChBgm); break;
	case 2: audioFade(kAudioChBgm, kAudioVolumeMin, 3000, true, false); break;
	case 3:
		audioFade(kAudioChBgm, kAudioVolumeMin, 3000, true, false);
		while (audioIsPlaying(kAudioChBgm) && !g_engine->shouldQuit()) {
			vmPeek();
			if (inputDown(kInputActivate) || inputDown(kInputCtrl))
				break;
		}
		audioStop(kAudioChBgm);
		break;
	// TODO: functions 4-7 use auxiliary music channels. NovelVM has no
	//       separate aux channels, so they are played on the SE channels.
	case 4: audioSePlay(vmStringParam(params, 1), vmExprParam(params, 2)); break;
	case 5: audioSeStop(vmExprParam(params, 1)); break;
	case 6: audioSeFade(kAudioVolumeMin, 3000, true, false, vmExprParam(params, 1)); break;
	case 7: {
		int ch = vmExprParam(params, 1);
		if (!audioSeChannelValid(ch)) {
			warning("Invalid SE channel: %u", ch);
			break;
		}
		while (audioIsPlaying((AudioChannel)(kAudioChSe0 + ch)) && !g_engine->shouldQuit()) {
			vmPeek();
			if (inputDown(kInputActivate) || inputDown(kInputCtrl))
				break;
		}
		break;
	}
	case 8: {
		int vol = CLIP<int>(vmExprParam(params, 1), kAudioVolumeMin, kAudioVolumeMax);
		audioSetVolume(kAudioChBgm, vol);
		break;
	}
	case 9: {
		int vol = CLIP(g_config.volume.music, 0, 4);
		audioSetVolume(kAudioChBgm, (vol - 4) * 1000);
		break;
	}
	case 10:
		memSetVar16(18, audioIsPlaying(kAudioChBgm));
		break;
	default: error("System.Audio.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void allstarsVoice(ParamList &params) {
	if (!vmFlagIsOn(kFlagVoiceEnable))
		return;
	switch (vmExprParam(params, 0)) {
	case 0: audioVoicePlay(vmStringParam(params, 1), 0); break;
	case 1: audioVoiceStop(0); break;
	case 2:
		audioVoicePlay(vmStringParam(params, 1), 0);
		while (audioIsPlaying(kAudioChVoice0) && !g_engine->shouldQuit()) {
			vmPeek();
			if (inputDown(kInputActivate) || inputDown(kInputCtrl))
				break;
		}
		break;
	case 3: memSetVar16(18, audioIsPlaying(kAudioChVoice0)); break;
	case 4:
		audioSetVolume(kAudioChVoice0,
				CLIP<int>(vmExprParam(params, 1), kAudioVolumeMin, kAudioVolumeMax));
		break;
	case 5: {
		int vol = CLIP(g_config.volume.voice, 0, 4);
		audioSetVolume(kAudioChVoice0, (vol - 4) * 1000);
		break;
	}
	default: error("System.Voice.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void allstarsDisplay(ParamList &params) {
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
			gfxDisplayFadeOut(vmExprParam(params, 1), 250);
		} else {
			gfxDisplayFadeIn(250);
		}
		break;
	default: error("System.Display.function[%u] not implemented",
				 params.params[0].val);
	}
}

struct CopyArgs {
	int srcX, srcY, w, h;
	uint srcI;
	int dstX, dstY;
	uint dstI;
};

// (src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
static CopyArgs allstarsReadCopyArgs(ParamList &params) {
	CopyArgs a;
	a.srcX = vmExprParam(params, 1);
	a.srcY = vmExprParam(params, 2);
	a.w = (int)vmExprParam(params, 3) - a.srcX + 1;
	a.h = (int)vmExprParam(params, 4) - a.srcY + 1;
	a.srcI = vmExprParam(params, 5);
	a.dstX = vmExprParam(params, 6);
	a.dstY = vmExprParam(params, 7);
	a.dstI = vmExprParam(params, 8);
	return a;
}

// TODO: the parameter list of System.Image.pixel_fade is unverified; it is
//       assumed to be (x, y, br_x, br_y, dst_i, rate) and to fade the
//       destination rectangle towards black.
static void allstarsPixelFade(ParamList &params) {
	int x = vmExprParam(params, 1);
	int y = vmExprParam(params, 2);
	int w = (int)vmExprParam(params, 3) - x + 1;
	int h = (int)vmExprParam(params, 4) - y + 1;
	uint dstI = params.nrParams > 5 ? vmExprParam(params, 5)
			: memGetSysvar16(kSysVarDstSurface);
	byte rate = params.nrParams > 6 ? CLIP<int>(vmExprParam(params, 6), 0, 255) : 127;
	gfxBlendFill(x, y, w, h, dstI, 0, rate);
}

// TODO: blend_half is assumed to use a fixed 50% alpha and the same
//       arguments as copy (no alpha parameter).
static void allstarsBlendHalf(ParamList &params) {
	CopyArgs a = allstarsReadCopyArgs(params);
	gfxBlend(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI, 128);
}

static void allstarsBlendWithMaskColor(ParamList &params) {
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
	byte *mask = memRaw() + vmExprParam(params, 12);
	gfxBlendWithMaskColorTo(aX, aY, w, h, aI, bX, bY, bI, dstX, dstY, dstI,
			READ_LE_UINT16(mask + 0), READ_LE_UINT16(mask + 2), mask + 4);
}

static void allstarsImage(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: sysGraphicsCopy(params); break;
	case 1: sysGraphicsCopyMasked(params); break;
	case 2: sysGraphicsFillBg(params); break;
	case 3: sysGraphicsCopySwap(params); break;
	case 5: allstarsPixelFade(params); break;
	case 7: sysGraphicsBlend(params); break;
	case 8: allstarsBlendHalf(params); break;
	case 10: allstarsBlendWithMaskColor(params); break;
	// TODO: System.Image.function 4/6/9 are unused upstream.
	default: error("System.Image.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void allstarsMap(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: mapLoadTilemap(); break;
	case 1: mapSpawnSprite(vmExprParam(params, 1), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	case 2: mapLoadTiles(); break;
	case 4: mapLoadSpriteScripts(); break;
	case 5: mapSetSpriteScript(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 6: mapPlaceSprites(); break;
	case 7: mapSetSpriteState(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 8: mapExecSpritesAndRedraw(); break;
	case 9: mapExecSprites(); break;
	case 10:
	case 11: mapDrawTiles(); break;
	case 12: mapSetLocationMode((MapLocationMode)vmExprParam(params, 1)); break;
	case 13: mapGetLocation(); break;
	case 14: mapMoveSprite(vmExprParam(params, 1), (MapDirection)vmExprParam(params, 2)); break;
	case 15: mapPathSprite(vmExprParam(params, 1), vmExprParam(params, 2),
				 vmExprParam(params, 3)); break;
	case 16: mapStopPathing(); break;
	case 20: mapRewindSpritePos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 24: mapLoadPalette(vmStringParam(params, 1), vmExprParam(params, 2)); break;
	case 25: mapLoadBitmap(vmStringParam(params, 1), vmExprParam(params, 2),
				 vmExprParam(params, 3), vmExprParam(params, 4)); break;
	// TODO: System.Map.function 3 and 17-19/21-23 are unassigned upstream.
	default: error("System.Map.function[%u] not implemented",
				 params.params[0].val);
	}
}

// TODO: System.overlay (function 22) is unimplemented. libai5 leaves
//       it commented out, and AI5WIN.EXE gives no reference for its
//       parameters or its drawing, so it is accepted and skipped.
static void allstarsOverlay(ParamList &params) {
	static bool warned = false;
	if (!warned) {
		warned = true;
		warning("Elf All Stars: System.overlay is not implemented");
	}
	if (params.nrParams > 0 && params.params[0].type == kMesParamExpression)
		vmExprParam(params, 0);
}

// TODO: Elf All Stars' game-specific util calls (the numbers below) live in
//       the retail runtime and are unimplemented. They carry the game's own
//       logic: message page flush/close, scene and menu setup, character and
//       scene drawing, CG unlocking, the shop/status screens and the mahjong
//       setup. The scripts can run with these accepted and skipped, but the
//       game stays incomplete until their behaviour is reverse engineered.
#define ALLSTARS_UTIL_STUB(nr, what) \
	static void allstarsUtil##nr(ParamList &) { \
		static bool warned = false; \
		if (!warned) { \
			warned = true; \
			warning("Elf All Stars: Util %u is not implemented (%s)", nr, what); \
		} \
	}

ALLSTARS_UTIL_STUB(1, "message page flush")
ALLSTARS_UTIL_STUB(2, "message page close")
ALLSTARS_UTIL_STUB(43, "mahjong state init")
ALLSTARS_UTIL_STUB(44, "UI mode enter/leave")
ALLSTARS_UTIL_STUB(45, "character selection commit")
ALLSTARS_UTIL_STUB(46, "history screen draw")
ALLSTARS_UTIL_STUB(47, "idle timer")
ALLSTARS_UTIL_STUB(48, "backlog clear")
ALLSTARS_UTIL_STUB(50, "score/cash update")
ALLSTARS_UTIL_STUB(51, "mahjong players init")
ALLSTARS_UTIL_STUB(52, "mahjong asset init")
ALLSTARS_UTIL_STUB(53, "sound effect control")
ALLSTARS_UTIL_STUB(55, "opponent route select")
ALLSTARS_UTIL_STUB(56, "opponent restore")
ALLSTARS_UTIL_STUB(57, "character parts draw")
ALLSTARS_UTIL_STUB(58, "status bars draw")
ALLSTARS_UTIL_STUB(59, "list row highlight")
ALLSTARS_UTIL_STUB(60, "list row highlight")
ALLSTARS_UTIL_STUB(61, "animation pause/resume")
ALLSTARS_UTIL_STUB(64, "number/digit block draw")
ALLSTARS_UTIL_STUB(65, "CG unlock")
ALLSTARS_UTIL_STUB(66, "console image load")
ALLSTARS_UTIL_STUB(67, "pre-match dialogue setup")
ALLSTARS_UTIL_STUB(68, "menu branch action")
ALLSTARS_UTIL_STUB(69, "menu branch action")
ALLSTARS_UTIL_STUB(90, "installer dialog control")
ALLSTARS_UTIL_STUB(91, "title transition")
ALLSTARS_UTIL_STUB(99, "profile view open")

// TODO: System.FaceWindow (function 23) is unimplemented. Neither
//       AI5WIN.EXE's behaviour nor upstream's table (all children NULL)
//       provides the parameter list or the window's drawing semantics.
static void allstarsFaceWindow(ParamList &params) {
	static bool warned = false;
	if (!warned) {
		warned = true;
		warning("Elf All Stars: System.FaceWindow is not implemented");
	}
	if (params.nrParams > 0 && params.params[0].type == kMesParamExpression)
		vmExprParam(params, 0);
}

// TODO: System.run_mahjong (function 24) is a stub. The whole mahjong
//       match is unimplemented: dealing, turn order, calls (chi/pon/kan/
//       riichi), yaku scoring, the opponent AI, board/hand rendering and
//       input all live behind this call. The script continues with a
//       neutral (zero) result.
static void allstarsRunMahjong(ParamList &params) {
	static bool warned = false;
	if (!warned) {
		warned = true;
		warning("Elf All Stars: mahjong is not implemented");
	}
	if (params.nrParams > 0 && params.params[0].type == kMesParamExpression)
		vmExprParam(params, 0);
	// The VM has no stack return for System calls; games read results from
	// var16[18], so zero that to look like a harmless draw/loss.
	memSetVar16(18, 0);
}

void initGameAllstars(Game &g) {
	static const struct { uint16 w, h; } sizes[] = {
		{ 640, 480 },
		{ 1280, 1280 },
		{ 640, 480 },
		{ 640, 960 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 768, 440 },
		{ 472, 104 },
		{ 32, 480 },
		{ 640, 240 },
		{ 320, 240 }
	};
	g.id = GID_ALLSTARS;
	g.view.w = kScreenW;
	g.view.h = kScreenH;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 16;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.memInit = allstarsMemInit;
	g.memRestore = allstarsMemRestore;

	g.vm.exec = vmExec;
	g.vm.eval = vmEval;
	g.vm.readParams = vmReadParams;
	g.vm.endCode = 0x00;
	vmSetDefaultOps(g);

	mapInit();

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = allstarsCursor;
	g.sys[3] = allstarsAnim;
	g.sys[4] = allstarsSavedata;
	g.sys[5] = allstarsAudio;
	g.sys[6] = allstarsVoice;
	g.sys[7] = sysLoadFile;
	g.sys[8] = sysLoadImage;
	g.sys[9] = allstarsDisplay;
	g.sys[10] = allstarsImage;
	g.sys[11] = sysWait;
	g.sys[12] = sysSetTextColorsDirect;
	g.sys[13] = sysFarcall;
	g.sys[14] = sysGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[16] = sysGetTime;
	g.sys[17] = allstarsMap;
	g.sys[18] = sysCheckInput;
	// System.function 19/20 are unused upstream and left unset.
	g.sys[21] = sysStrlen;
	g.sys[22] = allstarsOverlay;
	g.sys[23] = allstarsFaceWindow;
	g.sys[24] = allstarsRunMahjong;

	// TODO: The game-specific util calls are accepted and skipped; see the
	//       ALLSTARS_UTIL_STUB block above. Their behaviour lives in the
	//       retail runtime and is not implemented.
	static const struct {
		int nr;
		SysFunc fn;
	} utilStubs[] = {
		{ 1, allstarsUtil1 },
		{ 2, allstarsUtil2 },
		{ 43, allstarsUtil43 },
		{ 44, allstarsUtil44 },
		{ 45, allstarsUtil45 },
		{ 46, allstarsUtil46 },
		{ 47, allstarsUtil47 },
		{ 48, allstarsUtil48 },
		{ 50, allstarsUtil50 },
		{ 51, allstarsUtil51 },
		{ 52, allstarsUtil52 },
		{ 53, allstarsUtil53 },
		{ 55, allstarsUtil55 },
		{ 56, allstarsUtil56 },
		{ 57, allstarsUtil57 },
		{ 58, allstarsUtil58 },
		{ 59, allstarsUtil59 },
		{ 60, allstarsUtil60 },
		{ 61, allstarsUtil61 },
		{ 64, allstarsUtil64 },
		{ 65, allstarsUtil65 },
		{ 66, allstarsUtil66 },
		{ 67, allstarsUtil67 },
		{ 68, allstarsUtil68 },
		{ 69, allstarsUtil69 },
		{ 90, allstarsUtil90 },
		{ 91, allstarsUtil91 },
		{ 99, allstarsUtil99 }
	};
	for (uint i = 0; i < ARRAYSIZE(utilStubs); i++)
		g.util[utilStubs[i].nr] = utilStubs[i].fn;

	g.flags[kFlagMenuReturn] = 0x0008;
	g.flags[kFlagReturn] = 0x0010;
	g.flags[kFlagLogEnable] = 0x0020;
	g.flags[kFlagLogText] = 0x0040;
	g.flags[kFlagLog] = 0x0080;
	g.flags[kFlagVoiceEnable] = 0x0100;
	g.flags[kFlagAudioEnable] = kFlagAlwaysOn;
	g.flags[kFlagStrlen] = 0x0400;
	g.flags[kFlagWaitKeyup] = kFlagAlwaysOn;
	g.flags[kFlagSkipKeyup] = 0x0800;
	g.flags[kFlagLogSys] = 0x1000;

	// TODO: only the mahjong match is missing; keep the game marked as
	//       incomplete until it is implemented.
	//
	// TODO: the detection table entry is deferred: no retail MES.ARC is
	//       available to compute its md5. When it is added, mark it
	//       ADGF_UNSTABLE because of the missing mahjong match.
	warning("Elf All Stars: the mahjong match is not implemented");
}

} // End of namespace AI5
