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
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/effect.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/savedata.h"
#include "ai5/sys.h"
#include "ai5/text.h"
#include "ai5/vm.h"
#include "ai5/games/beyond.h"

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
	kHeapOff = kSysvar32Off + 61 * 4
};
static_assert(kHeapOff == 0x1244, "unexpected Be-Yond heap offset");

enum {
	kScreenW = 640,
	kScreenH = 480
};

} // End of anonymous namespace

static void beyondMemRestore() {
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

static void beyondMemInit() {
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
	beyondMemRestore();
}

static void beyondCursor(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: cursorShow(); break;
	case 1: cursorHide(); break;
	case 2: sysCursorSavePos(params); break;
	case 3: cursorSetPos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	//case 4: break; // TODO
	//case 5: break; // TODO
	//case 6: break; // TODO
	//case 7: break; // TODO
	//case 8: break; // TODO
	case 9: memSetVar32(18, 0); break; // TODO: ???
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

static void beyondAnim(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: animInitStream(vmAnimParam(params, 1), vmAnimParam(params, 1)); break;
	case 1: animStart(vmAnimParam(params, 1)); break;
	case 2: animStop(vmAnimParam(params, 1)); break;
	case 3: animHalt(vmAnimParam(params, 1)); break;
	case 4: animWait(vmAnimParam(params, 1)); break;
	case 5: animStopAll(); break;
	case 6: animHaltAll(); break;
	case 7: animResetAll(); break;
	case 8: animUnpauseAll(); break;
	case 9: animInitStreamFrom(vmAnimParam(params, 1), vmAnimParam(params, 1),
				memGetVar32(2)); break;
	case 10: animInitStreamFrom(vmAnimParam(params, 1), vmAnimParam(params, 1),
				 vmExprParam(params, 3)); break;
	//case 11:
	case 12: memSetVar16(18, !animStreamRunning(vmAnimParam(params, 1))); break;
	case 13: memSetVar16(18, animRunning()); break;
	default: error("System.Anim.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void beyondResumeLoad(const char *saveName) {
	byte buf[kVar4Off + kVar4Size];
	byte *var4 = buf + kVar4Off;
	byte *memVar4 = memRaw() + kVar4Off;
	savedataRead(saveName, memRaw(), 0, kMem16Size);
	savedataRead("FLAG00", buf, kVar4Off, kVar4Size);

	memcpy(memVar4 +  200, var4 +  200, 800);
	memcpy(memVar4 + 1200, var4 + 1200, 800);
	memcpy(memVar4 + 2030, var4 + 2030, 12);
	memcpy(memVar4 + 2100, var4 + 2100, 1900);
	memVar4[2015] = 1;

	beyondMemRestore();
	vmLoadMes(memMesName());
	vmFlagOn(kFlagReturn);
}

static void beyondLoadExtraVar32(const char *saveName) {
	savedataRead(saveName, memRaw(), kSysvar32Off + (11 * 4), 50 * 4);
}

static void beyondSaveExtraVar32(const char *saveName) {
	savedataWrite(saveName, memRaw(), kSysvar32Off + (11 * 4), 50 * 4);
}

static void beyondLoadHeap(const char *saveName, int start, int count) {
	if (count <= 0 || start < 0 || start + count > 3516) {
		warning("Invalid heap load: %d+%d", start, count);
		return;
	}
	savedataRead(saveName, memRaw(), kHeapOff + start, count);
}

static void beyondSaveHeap(const char *saveName, int start, int count) {
	if (count <= 0 || start < 0 || start + count > 3516) {
		warning("Invalid heap save: %d+%d", start, count);
		return;
	}
	savedataWrite(saveName, memRaw(), kHeapOff + start, count);
}

static void beyondSavedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: beyondResumeLoad(sysSaveName(params)); break;
	case 1: savedataResumeSave(sysSaveName(params)); break;
	case 2: savedataLoadVar4Restore(sysSaveName(params)); break;
	case 3: savedataSaveUnionVar4(sysSaveName(params)); break;
	case 4: beyondLoadExtraVar32(sysSaveName(params)); break;
	case 5: beyondSaveExtraVar32(sysSaveName(params)); break;
	case 6: memset(memRaw() + kVar4Off, 0, kVar4Size); break;
	case 7: beyondLoadHeap(sysSaveName(params), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	case 8: beyondSaveHeap(sysSaveName(params), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	default: error("System.SaveData.function[%u] not implemented",
				 params.params[0].val);
	}
}

enum {
	kVar4BgmFading = 4020,
	kVar4SeFading = 4021
};

static void beyondAudio(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:
		memSetVar4(kVar4BgmFading, 0);
		audioBgmPlay(vmStringParam(params, 1), true);
		break;
	case 1:
		memSetVar4(kVar4BgmFading, 0);
		audioStop(kAudioChBgm);
		break;
	case 2:
		memSetVar4(kVar4BgmFading, 1);
		audioFade(kAudioChBgm, kAudioVolumeMin, 3000, true, false);
		break;
	case 3:
		audioFade(kAudioChBgm, kAudioVolumeMin, 3000, true, false);
		while (audioIsPlaying(kAudioChBgm) && !g_engine->shouldQuit()) {
			vmPeek();
			if (memGetVar4(2019) == 1 || memGetVar4(2020) == 1)
				break;
			if (inputDown(kInputActivate) || inputDown(kInputCtrl))
				break;
		}
		audioStop(kAudioChBgm);
		break;
	case 4:
		memSetVar4(kVar4SeFading, 0);
		audioSePlay(vmStringParam(params, 1), vmExprParam(params, 2));
		break;
	case 5:
		memSetVar4(kVar4SeFading, 0);
		audioSeStop(vmExprParam(params, 1));
		break;
	case 6:
		memSetVar4(kVar4SeFading, 1);
		audioSeFade(kAudioVolumeMin, 3000, true, false, vmExprParam(params, 1));
		break;
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
		int vol = CLIP<int>(vmExprParam(params, 1), -5000, 0);
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

static void beyondVoice(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	default: error("System.Voice.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void beyondDisplay(ParamList &params) {
	switch (vmExprParam(params, 0)) {
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

static void beyondGraphicsBlendMasked(ParamList &params) {
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

static void beyondGraphicsCrossfade(ParamList &params) {
	int srcX = vmExprParam(params, 1);
	int srcY = vmExprParam(params, 2);
	int w = (vmExprParam(params, 3) - srcX) + 1;
	int h = (vmExprParam(params, 4) - srcY) + 1;
	uint srcI = vmExprParam(params, 5);
	int newX = vmExprParam(params, 6);
	int newY = vmExprParam(params, 7);
	uint newI = vmExprParam(params, 8);
	int dstX = vmExprParam(params, 9);
	int dstY = vmExprParam(params, 10);
	uint dstI = vmExprParam(params, 11);

	VMTimer timer = vmTimerCreate();
	for (uint a = 0; a < 255; a += 8) {
		gfxCopy(srcX, srcY, w, h, srcI, dstX, dstY, dstI);
		gfxBlend(newX, newY, w, h, newI, dstX, dstY, dstI, a);
		vmPeek();
		vmTimerTick(timer, 33);
	}
	gfxCopy(newX, newY, w, h, newI, dstX, dstY, dstI);
}

static void beyondGraphics(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: sysGraphicsCopy(params); break;
	case 1: sysGraphicsCopyMasked(params); break;
	case 2: sysGraphicsFillBg(params); break;
	case 3: sysGraphicsCopySwap(params); break;
	case 4: sysGraphicsSwapBgFg(params); break;
	case 5: sysGraphicsCopyProgressive(params); break;
	case 6: sysGraphicsCompose(params); break;
	case 10: beyondGraphicsBlendMasked(params); break;
	case 11: beyondGraphicsCrossfade(params); break;
	default: error("System.Graphics.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void beyondBacklog(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: backlogClear(); break;
	case 1: backlogPrepare(); break;
	case 2: backlogCommit(); break;
	case 3: memSetVar32(18, backlogCount()); break;
	case 4: memSetVar32(18, backlogGetPointer(vmExprParam(params, 1))); break;
	case 5: memSetVar16(18, backlogHasVoice(vmExprParam(params, 1))); break;
	default: error("System.Backlog.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void beyondOverlay(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	default: warning("System.Overlay.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void util6(ParamList &params) {
	char *heap = (char*)memRaw() + kHeapOff;
	switch (vmExprParam(params, 1)) {
	case 1:
		memSetVar4(4000, 1);
		Common::strlcpy(heap + 100, vmStringParam(params, 2), 16);
		break;
	case 2:
		memSetVar4(4005, 1);
		Common::strlcpy(heap + 116, vmStringParam(params, 2), 16);
		Common::strlcpy(heap + 132, vmStringParam(params, 3), 16);
		break;
	case 4:
		memSetVar4(4003, 1);
		Common::strlcpy(heap + 180, vmStringParam(params, 2), 16);
		break;
	case 5:
		memSetVar4(4004, 1);
		Common::strlcpy(heap + 196, vmStringParam(params, 2), 16);
		break;
	}
}

static void beyondSetVolume(ParamList &params) {
	int which = vmExprParam(params, 1);
	int vol = vmExprParam(params, 2);
	if (vol < 0 || vol > 4) {
		warning("Invalid volume: %d", vol);
		vol = CLIP(vol, 0, 4);
	}
	switch (which) {
	case 0:
		which = kAudioChBgm;
		g_config.volume.music = vol;
		break;
	case 1:
		which = kAudioChVoice0;
		g_config.volume.voice = vol;
		break;
	default:
		which = kAudioChSe0;
		g_config.volume.effect = vol;
		break;
	}

	audioSetVolume((AudioChannel)which, (vol - 4) * 1000);
	// TODO: save to .ini
}

static void beyondGetIniValues(ParamList &params) {
	memSetVar16(0, g_config.volume.music);
	memSetVar16(1, g_config.volume.effect);
	memSetVar16(2, g_config.volume.voice);
	memSetVar16(3, 4); // SPEED
	memSetVar16(4, 1); // SKIP
}

void initGameBeyond(Game &g) {
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
	g.id = GID_BEYOND;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 16;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.memInit = beyondMemInit;
	g.memRestore = beyondMemRestore;

	g.vm.exec = vmExec;
	g.vm.eval = vmEval;
	g.vm.readParams = vmReadParams;
	g.vm.endCode = 0x00;
	vmSetDefaultOps(g);

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = beyondCursor;
	g.sys[3] = beyondAnim;
	g.sys[4] = beyondSavedata;
	g.sys[5] = beyondAudio;
	g.sys[6] = beyondVoice;
	g.sys[7] = sysLoadFile;
	g.sys[8] = sysLoadImage;
	g.sys[9] = beyondDisplay;
	g.sys[10] = beyondGraphics;
	g.sys[11] = sysWait;
	g.sys[12] = sysSetTextColorsDirect;
	g.sys[13] = sysFarcall;
	g.sys[14] = sysGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[16] = sysGetTime;
	g.sys[17] = utilNoop;
	g.sys[18] = sysCheckInput;
	g.sys[19] = beyondBacklog;
	g.sys[20] = utilNoop;
	g.sys[21] = sysStrlen;
	g.sys[22] = beyondOverlay;

	g.util[6] = util6;
	g.util[11] = beyondSetVolume;
	g.util[12] = beyondGetIniValues;
	// The retail scripts call Utils 20, 25, 30, 31 during the prologue; upstream
	// doesn't register it either, so no-op them to get past the calls.
	g.util[20] = utilNoop;
	g.util[25] = utilNoop;
	g.util[30] = utilNoop;
	g.util[31] = utilNoop;

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
}

} // End of namespace AI5
