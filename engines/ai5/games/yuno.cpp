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

#include "common/file.h"
#include "common/textconsole.h"

#include "ai5/animplay.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/cg.h"
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/effect.h"
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
	kVar4Size = 4096,
	kMem16Size = 8192,

	kVar4Off = kMesNameSize,
	kSv16PtrOff = kVar4Off + kVar4Size,
	kVar16Off = kSv16PtrOff + 4,
	kSysvar16Off = kVar16Off + 26 * 2,
	kVar32Off = kSysvar16Off + 26 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	kHeapOff = kSysvar32Off + 161 * 4
};
static_assert(kHeapOff == 0x13d8, "unexpected YU-NO heap offset");

byte stashedMesName[kMesNameSize];
Common::String savedCgName;
Common::String savedDataName;
bool savedAnimRunning[10];

} // End of anonymous namespace

static void yunoMemRestore() {
	// XXX: In AI5WIN.EXE, these are 32-bit pointers into the VM's own
	//      address space. We treat them as offsets into Memory (similar
	//      to how AI5WIN.EXE treats 16-bit pointers).
	memSetSysvar16Ptr(kSysvar16Off);
	memSetSysvar32(kSysVarMemory, offsetof(Memory, mem16));
	memSetSysvar32(kSysVarPalette, offsetof(Memory, palette));
	memSetSysvar32(kSysVarFileData, offsetof(Memory, fileData));
	memSetSysvar32(kSysVarMenuEntryAddresses, offsetof(Memory, menuEntryAddresses));
	memSetSysvar32(kSysVarMenuEntryNumbers, offsetof(Memory, menuEntryNumbers));

	// this value is restored when loading a save via System.SaveData.resume_load...
	memSetSysvar16((uint)0, kHeapOff);
}

static void yunoMemInit() {
	// pointer table for memory access
	g_memPtr.mesName = memRaw();
	g_memPtr.var4 = memRaw() + kVar4Off;
	g_memPtr.systemVar16Ptr = memRaw() + kSv16PtrOff;
	g_memPtr.var16 = memRaw() + kVar16Off;
	g_memPtr.systemVar16 = memRaw() + kSysvar16Off;
	g_memPtr.var32 = memRaw() + kVar32Off;
	g_memPtr.systemVar32 = memRaw() + kSysvar32Off;

	memSetSysvar16(kSysVarFlags, 0x260d);
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
	yunoMemRestore();
}

static void yunoAnim(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:  animInitStream(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 1:  animStart(vmExprParam(params, 1)); break;
	case 2:  animStop(vmExprParam(params, 1)); break;
	case 3:  animHalt(vmExprParam(params, 1)); break;
	// TODO
	case 4:  warning("System.Anim.function[4] not implemented"); break;
	case 5:  animStopAll(); break;
	case 6:  animHaltAll(); break;
	case 20: animSetOffset(vmExprParam(params, 1), vmExprParam(params, 2) * 8, vmExprParam(params, 3)); break;
	default: error("System.Anim.function[%u] not implemented", params.params[0].val);
	}
}

static void yunoSavedataLoadJewelSave(const char *saveName) {
	byte buf[kMemMem16MaxSize];
	const byte *loadVar4 = buf + kVar4Off;
	byte *curVar4 = g_memPtr.var4;
	savedataRead(saveName, buf, 0, kVar4Off + kVar4Size);
	memcpy(memRaw(), buf, kMesNameSize);
	curVar4[18] = loadVar4[18];
	curVar4[21] = loadVar4[21];
	curVar4[29] = loadVar4[29];
	for (int i = 50; i < 90; i++)
		curVar4[i] = loadVar4[i];
	for (int i = 150; i < 2000; i++)
		curVar4[i] = loadVar4[i];
	g_game->memRestore();
	vmLoadMes(memMesName());
	vmFlagOn(kFlagReturn);
}

static void yunoSavedataSaveJewelSave(const char *saveName) {
	byte buf[kMemMem16MaxSize];
	byte *outVar4 = buf + kVar4Off;
	const byte *curVar4 = g_memPtr.var4;
	savedataRead(saveName, buf, 0, kVar4Off + kVar4Size);
	memcpy(buf, stashedMesName, kMesNameSize);
	for (int i = 50; i < 90; i++)
		outVar4[i] = curVar4[i];
	for (int i = 150; i < 2000; i++)
		outVar4[i] = curVar4[i];
	savedataWrite(saveName, buf, 0, kVar4Off + kVar4Size);
}

static void yunoSavedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:  savedataResumeLoad(sysSaveName(params)); break;
	case 1:  savedataResumeSave(sysSaveName(params)); break;
	case 2:  savedataLoad(sysSaveName(params), kVar4Off); break;
	case 3:  savedataSave(sysSaveName(params), kVar4Off); break;
	case 4:  savedataRead(sysSaveName(params), memRaw(), kVar4Off, kVar4Size); break;
	case 5:  savedataSaveVar4(sysSaveName(params)); break;
	case 6:  savedataSaveUnionVar4(sysSaveName(params)); break;
	case 7:  savedataLoadVar4Slice(sysSaveName(params), vmExprParam(params, 2), vmExprParam(params, 3)); break;
	case 8:  savedataSaveVar4Slice(sysSaveName(params), vmExprParam(params, 2), vmExprParam(params, 3)); break;
	case 9: {
		Common::String src = sysSaveName(params);
		savedataCopy(src.c_str(), sysSaveNameNo(vmExprParam(params, 2)));
		break;
	}
	case 11: yunoSavedataLoadJewelSave(sysSaveName(params)); break;
	case 12: yunoSavedataSaveJewelSave(sysSaveName(params)); break;
	case 13: savedataSetMesName(sysSaveName(params), vmStringParam(params, 2)); break;
	default: error("System.SaveData.function[%u] not implemented", params.params[0].val);
	}
}

static void yunoLoadImage(ParamList &params) {
	// XXX: animations are not halted when loading an image in YU-NO
	sysLoadImageAt(vmStringParam(params, 0), memGetSysvar16(kSysVarDstSurface), 8);
}

// The System.Graphics functions take x coordinates in units of 8 pixels and
// bottom-right corners instead of sizes.
struct YunoRect {
	int x, y, w, h;
};

static YunoRect yunoRectParam(ParamList &params, uint i) {
	YunoRect r;
	r.x = vmExprParam(params, i);
	r.y = vmExprParam(params, i + 1);
	r.w = (vmExprParam(params, i + 2) - r.x) + 1;
	r.h = (vmExprParam(params, i + 3) - r.y) + 1;
	return r;
}

static void yunoGraphics(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: {
		// copy(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
		YunoRect r = yunoRectParam(params, 1);
		gfxCopy(r.x * 8, r.y, r.w * 8, r.h, vmExprParam(params, 5), vmExprParam(params, 6) * 8,
				vmExprParam(params, 7), vmExprParam(params, 8));
		break;
	}
	case 1: {
		// copy_masked(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
		YunoRect r = yunoRectParam(params, 1);
		gfxCopyMasked(r.x * 8, r.y, r.w * 8, r.h, vmExprParam(params, 5), vmExprParam(params, 6) * 8,
				vmExprParam(params, 7), vmExprParam(params, 8), memGetSysvar16(kSysVarMaskColor));
		break;
	}
	case 2: {
		// fill_bg(x, y, br_x, br_y)
		YunoRect r = yunoRectParam(params, 1);
		gfxTextFill(r.x * 8, r.y, r.w * 8, r.h, memGetSysvar16(kSysVarDstSurface));
		break;
	}
	case 3: {
		// copy_swap(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
		YunoRect r = yunoRectParam(params, 1);
		gfxCopySwap(r.x * 8, r.y, r.w * 8, r.h, vmExprParam(params, 5), vmExprParam(params, 6) * 8,
				vmExprParam(params, 7), vmExprParam(params, 8));
		break;
	}
	case 4: {
		// swap_bg_fg(x, y, br_x, br_y)
		YunoRect r = yunoRectParam(params, 1);
		gfxTextSwapColors(r.x * 8, r.y, r.w * 8, r.h, memGetSysvar16(kSysVarDstSurface));
		break;
	}
	case 5: {
		// compose(fg_x, fg_y, fg_br_x, fg_br_y, fg_i, bg_x, bg_y, bg_i, dst_x, dst_y, dst_i)
		YunoRect r = yunoRectParam(params, 1);
		gfxCompose(r.x * 8, r.y, r.w * 8, r.h, vmExprParam(params, 5), vmExprParam(params, 6) * 8,
				vmExprParam(params, 7), vmExprParam(params, 8), vmExprParam(params, 9) * 8,
				vmExprParam(params, 10), vmExprParam(params, 11), memGetSysvar16(kSysVarMaskColor));
		break;
	}
	case 6: {
		// invert_colors(x, y, br_x, br_y)
		YunoRect r = yunoRectParam(params, 1);
		gfxInvertColors(r.x * 8, r.y, r.w * 8, r.h, memGetSysvar16(kSysVarDstSurface));
		break;
	}
	case 20: {
		// copy_progressive(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
		YunoRect r = yunoRectParam(params, 1);
		gfxCopyProgressive(r.x * 8, r.y, r.w * 8, r.h, vmExprParam(params, 5), vmExprParam(params, 6) * 8,
				vmExprParam(params, 7), vmExprParam(params, 8));
		break;
	}
	default:
		error("System.Image.function[%u] not implemented", params.params[0].val);
	}
}

static void sys22(ParamList &params) {
	warning("System.function[22] not implemented");
}

static void yunoSetScreenSurface(ParamList &params) {
	gfxSetScreenSurface(vmExprParam(params, 0));
}

static void utilBlinkFade(ParamList &params) {
	gfxBlinkFade(64, 0, 512, 288, 0);
}

static void utilScaleH(ParamList &params) {
	gfxScaleH(gfxCurrentSurface(), (int16)(uint16)vmExprParam(params, 1));
}

static void utilInvertColors(ParamList &params) {
	YunoRect r = yunoRectParam(params, 1);
	gfxInvertColors(r.x, r.y, r.w, r.h, 0);
}

static void utilFade(ParamList &params) {
	YunoRect r = yunoRectParam(params, 1);
	uint dstI = vmExprParam(params, 5);
	bool down = vmExprParam(params, 6) == 1;
	int srcI = vmExprParam(params, 7) == 0 ? -1 : 2;

	if (down)
		gfxFadeDown(r.x * 8, r.y, r.w * 8, r.h, dstI, srcI);
	else
		gfxFadeRight(r.x * 8, r.y, r.w * 8, r.h, dstI, srcI);
}

static void utilSavedataStashName(ParamList &params) {
	memcpy(stashedMesName, memRaw(), kMesNameSize);
}

static void utilPixelate(ParamList &params) {
	YunoRect r = yunoRectParam(params, 1);
	gfxPixelate(r.x * 8, r.y, r.w * 8, r.h, vmExprParam(params, 5), vmExprParam(params, 6));
}

static void utilGetTime(ParamList &params) {
	static uint32 startT = 0;
	if (!vmExprParam(params, 1)) {
		startT = vmGetTicks();
		return;
	}

	// return hours:minutes:seconds
	uint32 elapsed = (vmGetTicks() - startT) / 1000;
	memSetVar16(7, elapsed / 3600);
	memSetVar16(12, (elapsed % 3600) / 60);
	memSetVar16(18, elapsed % 60);
}

// wait for cursor to rest for a given interval
static void utilCheckCursor(ParamList &params) {
	static uint32 startT = 0, waitT = 0;
	static uint cursorX = 0, cursorY = 0;
	if (!vmExprParam(params, 1)) {
		startT = vmGetTicks();
		waitT = vmExprParam(params, 2);
		cursorGetPos(cursorX, cursorY);
		return;
	}

	// check timer
	uint32 currentT = vmGetTicks();
	memSetVar16(18, 0);
	if (currentT < startT + waitT)
		return;

	// return TRUE if cursor didn't move
	uint x, y;
	cursorGetPos(x, y);
	if (x == cursorX && y == cursorY) {
		memSetVar16(18, 1);
		return;
	}

	// otherwise restart timer
	startT = currentT;
	cursorX = x;
	cursorY = y;
}

static void utilDelay(ParamList &params) {
	uint nrTicks = vmExprParam(params, 1);
	VMTimer timer = vmTimerCreate();
	VMTimer targetT = timer + nrTicks * 15;
	while (timer < targetT && !g_engine->shouldQuit()) {
		vmPeek();
		vmTimerTick(timer, MIN<uint32>(targetT - timer, 15));
	}
}

static void utilSaveAnimation(ParamList &params) {
	savedCgName = g_assetCgName;
	savedDataName = g_assetDataName;
}

static void utilRestoreAnimation(ParamList &params) {
	if (savedCgName.empty() || savedDataName.empty())
		error("No saved animation in Util.restore_animation");
	sysLoadImageAt(savedCgName.c_str(), 1, 8);
	vmLoadDataFile(savedDataName.c_str(), memGetSysvar32(kSysVarDataOffset));
	for (int i = 0; i < 10; i++) {
		if (savedAnimRunning[i]) {
			animInitStream(i, i);
			animStart(i);
		}
	}
}

static void utilAnimSaveRunning(ParamList &params) {
	bool running = false;
	for (int i = 0; i < 10; i++) {
		savedAnimRunning[i] = animStreamRunning(i);
		running |= savedAnimRunning[i];
	}
	memSetVar16(18, running);
}

static void utilCopyProgressive(ParamList &params) {
	gfxCopyProgressive(64, 0, 512, 288, 2, 64, 0, vmExprParam(params, 1));
}

static void utilFadeProgressive(ParamList &params) {
	gfxFadeProgressive(64, 0, 512, 288, vmExprParam(params, 1));
}

static void utilAnimRunning(ParamList &params) {
	memSetVar16(18, animRunning());
}

// locations of dream text in the JP executable
static const uint32 yumeTextLoc[] = {
	0x60fdc, 0x60fc8, 0x60fa8, 0x60f90, 0x60f68, 0x60f48, 0x60f10, 0x60efc,
	0x60ec0, 0x60e94, 0x60e5c, 0x60e30, 0x60e04, 0x60de8, 0x60dcc
};

// locations of dream text in the EN executable
static const uint32 yumeTextLocEng[] = {
	0x60a00, 0x60a40, 0x60a80, 0x60ac0, 0x60b00, 0x60b40, 0x60b80, 0x60bc0, 0x60c00,
	0x60c40, 0x60c80, 0x60cc0, 0x60d00, 0x60d40, 0x60d80, 0x60dc0, 0x60e00
};

static int readYumeText(Common::String *dst) {
	Common::File exe;
	if (!exe.open(Common::Path(g_config.exeName)))
		return -1;

	const uint32 *addrs = yumeTextLoc;
	int nrLines = ARRAYSIZE(yumeTextLoc);
	if (g_yunoEng) {
		addrs = yumeTextLocEng;
		nrLines = ARRAYSIZE(yumeTextLocEng);
	}

	for (int i = 0; i < nrLines; i++) {
		if (addrs[i] + 64 >= exe.size())
			return -1;
	}
	for (int i = 0; i < nrLines; i++) {
		char buf[64];
		exe.seek(addrs[i]);
		exe.read(buf, 63);
		buf[63] = '\0';
		dst[i] = buf;
	}
	return nrLines;
}

static void yunoDrawText(const char *text);

// TODO: play effect from YUME.BIN
static void utilYume(ParamList &params) {
	Common::String text[17];
	int nrLines = readYumeText(text);

	const uint16 startX = memGetSysvar16(kSysVarTextStartX);
	const uint16 startY = memGetSysvar16(kSysVarTextStartY);

	// XXX: text colors are 0,7, but both are black
	g_gfx.palette[7].r = g_gfx.palette[7].g = g_gfx.palette[7].b = g_gfx.palette[7].a = 255;
	gfxUpdatePalette(0, 256);

	gfxFill(0, 0, 640, 400, 0, 0);
	gfxDisplayFreeze();
	for (int i = 0; i < nrLines && !g_engine->shouldQuit(); i++) {
		// draw text
		memSetSysvar16(kSysVarTextCursorX, startX);
		memSetSysvar16(kSysVarTextCursorY, startY);
		yunoDrawText(text[i].c_str());
		gfxDisplayFadeIn(1000);

		// wait for input
		ParamList waitParams;
		waitParams.nrParams = 0;
		sysWait(waitParams);

		// fade out
		gfxDisplayFadeOut(0, 1000);
		gfxFill(0, 0, 640, 400, 0, 0);
	}
	gfxDisplayUnfreeze();
}

static void utilCopy(ParamList &params) {
	YunoRect r = yunoRectParam(params, 1);
	gfxCopy(r.x, r.y, r.w, r.h, vmExprParam(params, 5), vmExprParam(params, 6), vmExprParam(params, 7),
			vmExprParam(params, 8));
}

static void utilBgmPlay(ParamList &params) {
	// XXX: lazy hack to fix issue where mixer volume remains at 0 in the
	//      music room (due to prior fade out).
	if (!strcmp(memMesName(), "MUSICMODE.MES"))
		audioSetVolume(kAudioChBgm, 0);
	audioBgmPlay(vmStringParam(params, 1), false);
}

static void utilBgmIsPlaying(ParamList &params) {
	memSetVar16(18, audioIsPlaying(kAudioChBgm));
}

static void utilSeIsPlaying(ParamList &params) {
	memSetVar16(18, audioIsPlaying(kAudioChSe0));
}

static void utilGetTicks(ParamList &params) {
	memSetVar32(16, vmGetTicks());
}

static void utilWaitUntil(ParamList &params) {
	if (!g_vm->procedures[110].code || !g_vm->procedures[111].code)
		error("procedures 110-111 not defined in Util.wait_until");

	uint32 stopT = vmExprParam(params, 1);
	VMTimer t = vmTimerCreate();
	do {
		vmPeek();
		if (inputDown(kInputActivate)) {
			vmCallProcedure(110);
			return;
		} else if (inputDown(kInputCancel)) {
			vmCallProcedure(111);
			return;
		}

		vmTimerTick(t, 16);
	} while (t < stopT && !g_engine->shouldQuit());
}

static void utilWaitUntil2(ParamList &params) {
	uint32 stopT = vmExprParam(params, 1);
	VMTimer t = vmTimerCreate();
	while (t < stopT && !g_engine->shouldQuit()) {
		vmPeek();
		vmTimerTick(t, 16);
	}
}

static void utilBgmIsFading(ParamList &params) {
	memSetVar32(13, audioIsFading(kAudioChBgm));
}

// reflector animation on the map screen

enum {
	kReflectorW = 21,
	kReflectorH = 17,
	// location of the base frame in MAPORB.GP8
	kMaporbX = 21,
	kMaporbY = 69,
	kReflectorDrawX = 581,
	kReflectorDrawY = 373,
	kReflectorFrameTime = 250
};

#define X 0xff
#define O 0x00
static const byte yunoReflectorMask[kReflectorW * kReflectorH] = {
	O,O,O,O,O,O,X,X,X,X,X,X,X,X,X,O,O,O,O,O,O,
	O,O,O,O,O,X,X,X,X,X,X,X,X,X,X,X,O,O,O,O,O,
	O,O,O,O,X,X,X,X,X,X,X,X,X,X,X,X,X,O,O,O,O,
	O,O,O,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,O,O,O,
	O,O,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,O,O,
	O,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,O,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	O,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,O,
	O,O,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,O,O,
	O,O,O,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,O,O,O,
	O,O,O,O,X,X,X,X,X,X,X,X,X,X,X,X,X,O,O,O,O,
	O,O,O,O,O,X,X,X,X,X,X,X,X,X,X,X,O,O,O,O,O,
	O,O,O,O,O,O,X,X,X,X,X,X,X,X,X,O,O,O,O,O,O,
};
#undef O
#undef X

static byte reflectorFrames[4][kReflectorW * kReflectorH];
// frame order: 0 1 2 3 2 1
static const byte reflectorSequence[6] = { 0, 1, 2, 3, 2, 1 };
static bool reflectorInitialized;
static uint32 reflectorT;
static uint reflectorFrame;

//  0 -> 11
// 11 -> 12
// 12 -> 7
static void generateFrame(const byte *prev, byte *frame) {
	for (int i = 0; i < kReflectorW * kReflectorH; i++) {
		if (!yunoReflectorMask[i])
			frame[i] = 1;
		else if (prev[i] == 0)
			frame[i] = 11;
		else if (prev[i] == 11)
			frame[i] = 12;
		else if (prev[i] == 12)
			frame[i] = 7;
		else if (prev[i] == 7)
			frame[i] = 7;
		else
			warning("Unexpected color: %u", prev[i]);
	}
}

static void generateReflectorFrames() {
	// load CG containing base frame
	Common::SharedPtr<CG> cg = assetCgLoad("maporb.gp8");
	if (!cg) {
		warning("Failed to decode CG: MAPORB.GP8");
		return;
	}
	if (cg->w < kMaporbX + kReflectorW || cg->h < kMaporbY + kReflectorH) {
		warning("Unexpected dimensions for CG: MAPORB.GP8");
		return;
	}

	// load base frame
	const byte *base = cg->pixels.data() + kMaporbY * cg->w + kMaporbX;
	for (int row = 0; row < kReflectorH; row++) {
		const byte *src = base + row * cg->w;
		byte *dst = reflectorFrames[0] + row * kReflectorW;
		for (int col = 0; col < kReflectorW; col++) {
			if (!yunoReflectorMask[row * kReflectorW + col])
				dst[col] = 1;
			else
				dst[col] = src[col];
		}
	}

	// generate subsequent frames from base frame
	for (int i = 1; i < 4; i++)
		generateFrame(reflectorFrames[i - 1], reflectorFrames[i]);
}

static void drawReflectorFrame(const byte *frame) {
	GfxSurface *s = gfxGetSurface(g_gfx.screen);
	for (int row = 0; row < kReflectorH; row++) {
		byte *dst = gfxPixel(s, kReflectorDrawX, kReflectorDrawY + row);
		const byte *src = &frame[row * kReflectorW];
		for (int i = 0; i < kReflectorW; i++) {
			if (src[i] != 1)
				dst[i] = src[i];
		}
	}
}

static void yunoReflectorAnimation() {
	if (!reflectorInitialized) {
		generateReflectorFrames();
		reflectorInitialized = true;
	}

	uint32 nowT = vmGetTicks();
	if (nowT - reflectorT < kReflectorFrameTime)
		return;

	drawReflectorFrame(reflectorFrames[reflectorSequence[reflectorFrame]]);
	reflectorFrame = (reflectorFrame + 1) % ARRAYSIZE(reflectorSequence);
	reflectorT = nowT;
	gfxScreenDirty();
}

// character widths of MS PGothic at size 16, from ' ' to '~'
static const byte charSizeP[95] = {
	6, 5, 9, 9, 9, 9, 11, 4, 6, 6, 9, 9, 4, 9, 4, 9,  // 0x20
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 4, 4, 9, 9, 9, 8,  // 0x30
	12, 11, 11, 12, 11, 10, 10, 12, 11, 5, 10, 11, 10, 13, 11, 12,  // 0x40
	11, 12, 11, 11, 10, 11, 11, 13, 11, 10, 10, 6, 9, 6, 8, 6,  // 0x50
	8, 9, 9, 9, 9, 9, 6, 8, 9, 4, 5, 8, 4, 13, 9, 9,  // 0x60
	9, 9, 7, 8, 7, 9, 9, 11, 8, 9, 8, 5, 5, 5, 8,  // 0x70
};

static uint enCharSize(uint32 ch) {
	if (g_gfx.text.size != 16)
		return gfxTextSizeChar(ch);

	if (ch >= 0x20 && ch < 0x7f)
		return charSizeP[ch - 0x20];
	// full-width ':'
	if (ch == 0xff1a)
		return 9;
	// full-width space
	if (ch == 0x3000)
		return 12;
	return gfxTextSizeChar(ch);
}

static void yunoEngDrawText(const char *text) {
	static uint16 xLast = 0;
	static uint16 xColLast = 0;
	static uint16 yLast = 0;

	// XXX: System.text_cursor_x stores the text position as a multiple of
	//      8, but AI5ENG.EXE continues from the precise position when
	//      drawing characters individually. Hence this hack.
	uint16 x = memGetSysvar16(kSysVarTextCursorX);
	uint16 y = memGetSysvar16(kSysVarTextCursorY);
	if (x == xColLast && y == yLast)
		x = xLast;
	else
		x *= 8;

	const uint surface = memGetSysvar16(kSysVarDstSurface);
	while (*text) {
		uint32 ch;
		text = sjisNextChar(text, ch);
		gfxTextDrawGlyph(x, y, surface, ch);
		x += enCharSize(ch);
	}

	xLast = x;
	xColLast = ((x + 7u) & ~7u) / 8;
	yLast = y;
	memSetSysvar16(kSysVarTextCursorX, xColLast);
	memSetSysvar16(kSysVarTextCursorY, yLast);
}

static void yunoDrawText(const char *text) {
	if (g_yunoEng) {
		yunoEngDrawText(text);
		return;
	}

	vmDrawText(text, 8);
}

static void yunoUpdate() {
	if (vmFlagIsOn(kFlagReflector)) {
		if (gfxCurrentSurface() != 1 || memGetVar4(21) != 1)
			yunoReflectorAnimation();
	}
}

static void yunoInit() {
	g_hanLineBreaks = true;
	g_assetEffectIsBgm = false;
}

void initGameYuno(Game &g) {
	static const struct { uint16 w, h; } sizes[] = {
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 768 },
		{ 640, 768 },
		{ 1696, 720 }
	};
	g.id = GID_YUNO;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 8;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.init = yunoInit;
	g.update = yunoUpdate;
	g.memInit = yunoMemInit;
	g.memRestore = yunoMemRestore;
	g.drawTextZen = yunoDrawText;
	g.drawTextHan = yunoDrawText;

	memset(stashedMesName, 0, sizeof(stashedMesName));
	savedCgName.clear();
	savedDataName.clear();
	memset(savedAnimRunning, 0, sizeof(savedAnimRunning));
	reflectorInitialized = false;
	reflectorT = 0;
	reflectorFrame = 0;

	classicsSetOps(g);
	g.stmtOp[0x03] = vmStmtSetFlagConst16_4bitWrap;
	g.stmtOp[0x05] = vmStmtSetFlagExpr4bitWrap;
	g.stmtOp[0x0f] = vmStmtMescallSaveProcedures;

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = classicsCursor;
	g.sys[3] = yunoAnim;
	g.sys[4] = yunoSavedata;
	g.sys[5] = classicsAudio;
	g.sys[7] = sysFile;
	g.sys[8] = yunoLoadImage;
	g.sys[9] = classicsPalette;
	g.sys[10] = yunoGraphics;
	g.sys[11] = sysWait;
	g.sys[12] = sysSetTextColorsIndexed;
	g.sys[13] = sysFarcall;
	g.sys[14] = classicsGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[18] = sysCheckInput;
	g.sys[21] = sysStrlen;
	g.sys[22] = sys22;
	g.sys[23] = yunoSetScreenSurface;

	g.util[1] = classicsGetTextColors;
	g.util[3] = utilNoop;
	g.util[5] = utilBlinkFade;
	g.util[6] = utilScaleH;
	g.util[8] = utilInvertColors;
	g.util[10] = utilFade;
	g.util[11] = utilSavedataStashName;
	g.util[12] = utilPixelate;
	g.util[14] = utilGetTime;
	g.util[15] = utilCheckCursor;
	g.util[16] = utilDelay;
	g.util[17] = utilSaveAnimation;
	g.util[18] = utilRestoreAnimation;
	g.util[19] = utilAnimSaveRunning;
	g.util[20] = utilCopyProgressive;
	g.util[21] = utilFadeProgressive;
	g.util[22] = utilAnimRunning;
	g.util[26] = utilYume;
	g.util[27] = utilWarnUnimplemented;
	g.util[100] = utilWarnUnimplemented;
	g.util[101] = utilWarnUnimplemented;
	g.util[200] = utilCopy;
	g.util[201] = utilBgmPlay;
	g.util[202] = utilBgmIsPlaying;
	g.util[203] = utilSeIsPlaying;
	g.util[210] = utilGetTicks;
	g.util[211] = utilWaitUntil;
	g.util[212] = utilWaitUntil2;
	g.util[213] = utilWarnUnimplemented;
	g.util[214] = utilBgmIsFading;

	g.flags[kFlagReflector] = 0x0002;
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
