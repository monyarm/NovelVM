/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage
 * (GPL-2.0-or-later).
 */

#include "common/file.h"

#include "ai5/ai5.h"
#include "ai5/config.h"
#include "ai5/game.h"
#include "ai5/memory.h"
#include "ai5/sys.h"
#include "ai5/vm.h"

#include "ai5/games/fallback.h"

namespace AI5 {

namespace {
// Memory layout of the most common AI5WIN configuration (the one
// shared by Doukyuusei and friends).
enum {
	kMesNameSize = 128,
	kVar4Size    = 4096,
	kVar4Off     = kMesNameSize,
	kSv16PtrOff  = kVar4Off + kVar4Size,
	kVar16Off    = kSv16PtrOff + 4,
	kSysvar16Off = kVar16Off + 26 * 2,
	kVar32Off    = kSysvar16Off + 26 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	kHeapOff     = kSysvar32Off + 211 * 4,
	kMem16Size   = 8192
};
static_assert(kHeapOff == 0x14a0, "unexpected AI5WIN heap offset");

void fallbackMemRestore() {
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

void fallbackMemInit() {
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
	fallbackMemRestore();
}

void fallbackInit() {
	ai5FallbackLog("generic AI5WIN fallback mode (unknown release)");
}

void fallbackWarnSys(ParamList &params) {
	ai5FallbackLog("System.function[%u] not implemented (fallback)",
			params.params[0].val);
}

void fallbackWarnUtil(ParamList &params) {
	ai5FallbackLog("Util.function[%u] not implemented (fallback)",
			params.params[0].val);
}

bool iniExists(const char *name) {
	Common::File f;
	return f.open(Common::Path(name));
}

const struct { uint16 w, h; } classicSizes[] = {
	{ 640, 480 },
	{ 640, 480 },
	{ 640, 480 },
	{ 640, 480 },
	{ 992, 832 },
	{ 640, 480 },
	{ 640, 1280 },
	{ 640, 480 },
	{ 1280, 1280 }
};

} // namespace

void initGameAi5Fallback(Game &g) {
	g.id = GID_AI5_FALLBACK;
	g.view.w = 640;
	g.view.h = 480;
	for (uint i = 0; i < ARRAYSIZE(classicSizes); i++) {
		g.surfaceSizes[i].w = classicSizes[i].w;
		g.surfaceSizes[i].h = classicSizes[i].h;
	}
	g.bpp = 16;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;

	g.init = fallbackInit;
	g.memInit = fallbackMemInit;
	g.memRestore = fallbackMemRestore;

	// AIWIN-era releases (detected by their ini file) use the AIW VM
	// with the Shuusaku memory layout; classic AI5WIN releases use the
	// original VM with the Doukyuusei layout.
	bool aiwin = iniExists("syuusaku.ini") || iniExists("aiwin.ini");
	if (aiwin) {
		ai5FallbackLog("AIWIN ini found: using the AIW VM");
		g.vm.exec = vmExecAiw;
		g.vm.eval = vmEvalAiw;
		g.vm.readParams = vmReadParamsAiw;
		g.vm.endCode = 0xff;
	} else {
		g.vm.exec = vmExec;
		g.vm.eval = vmEval;
		g.vm.readParams = vmReadParams;
		g.vm.endCode = 0x00;
	}
	vmSetDefaultOps(g);

	if (aiwin) {
		g.exprOp[0xe1] = vmExprMinusUnsigned;
		g.exprOp[0xe5] = vmExprRandWithImmRange;
		g.exprOp[0xf3] = vmExprCflagPacked;
		g.exprOp[0xf4] = vmExprEflagPacked;
		g.exprOp[0xf6] = vmExprVar16Const16;
		g.exprOp[0xf7] = vmExprVar16Expr;
		g.exprOp[0xf8] = vmExprSysvar16Const16;
		g.exprOp[0xf9] = vmExprSysvar16Expr;
	} else {
		g.exprOp[0xe5] = vmExprRandWithImmRange;
		g.stmtOp[0x03] = vmStmtSetFlagConst16_4bitSaturate;
		g.stmtOp[0x05] = vmStmtSetFlagExpr4bitSaturate;
	}

	// Wire the engine-generic system functions, then route every empty
	// slot to a warn-and-continue handler so the game can keep going
	// while we log what it actually calls.
	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[7] = sysLoadFile;
	g.sys[8] = sysLoadImage;
	g.sys[12] = sysSetTextColorsDirect;
	g.sys[13] = sysFarcall;
	g.sys[14] = sysGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[16] = sysGetTime;
	g.sys[18] = sysCheckInput;
	g.sys[255] = utilNoop;
	for (int i = 0; i < 256; i++) {
		if (!g.sys[i])
			g.sys[i] = fallbackWarnSys;
	}
	for (int i = 0; i < 601; i++) {
		if (!g.util[i])
			g.util[i] = fallbackWarnUtil;
	}

	g.flags[kFlagWaitKeyup] = kFlagAlwaysOn;
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
}

} // namespace AI5
