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

#include "common/system.h"
#include "common/textconsole.h"

#include "ai5/ai5.h"
#include "ai5/animplay.h"
#include "ai5/asset.h"
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/effect.h"
#include "ai5/game.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/menu.h"
#include "ai5/sys.h"
#include "ai5/text.h"
#include "ai5/vm.h"

namespace AI5 {

// Helper for System.SaveData functions.
const char *sysSaveNameFmt(const char *fmt, uint saveNo) {
	static char saveName[10];
	snprintf(saveName, sizeof(saveName), fmt, saveNo);
	return saveName;
}

const char *sysSaveNameNo(uint saveNo) {
	// The 2003 Kawarazaki re-release uses four-digit slot names; its
	// save directory carries FLAG0000..FLAG0023 and FLAG0100.
	if (g_game && g_game->saveNameFourDigits) {
		if (saveNo > 9999)
			error("Invalid save number: %u", saveNo);
		return sysSaveNameFmt("FLAG%04u", saveNo);
	}
	if (saveNo > 99)
		error("Invalid save number: %u", saveNo);
	return sysSaveNameFmt("FLAG%02u", saveNo);
}

const char *sysSaveName(ParamList &params) {
	return sysSaveNameNo(vmExprParam(params, 1));
}

void utilWarnUnimplemented(ParamList &params) {
	ai5FallbackLog("Util.function[%u] not implemented", params.params[0].val);
	warning("Util.function[%u] not implemented", params.params[0].val);
}

void utilNoop(ParamList &params) {
}

void sysSetFontSize(ParamList &params) {
	gfxTextSetSize(memGetSysvar16(kSysVarFontHeight), memGetSysvar16(kSysVarFontWeight));
}

static byte *writeDigit(byte *buf, int n, bool halfwidth) {
	if (halfwidth) {
		buf[0] = '0' + n;
		return buf + 1;
	}
	buf[0] = 0x82;
	buf[1] = 0x4f + n;
	return buf + 2;
}

enum {
	kMaxDigits = 10
};

const char *sysNumberToStringDigits(uint32 n, uint displayDigits, bool halfwidth) {
	displayDigits = MIN<uint>(displayDigits, kMaxDigits);
	byte nrDigits = 0;
	byte digits[kMaxDigits];
	for (int i = kMaxDigits - 1; n && i >= 0; i--, n /= 10, nrDigits++)
		digits[i] = n % 10;
	for (int i = kMaxDigits - (nrDigits + 1); i >= 0; i--)
		digits[i] = 0;

	if (!displayDigits)
		displayDigits = nrDigits;

	static byte buf[kMaxDigits * 2 + 1];
	if (!displayDigits) {
		byte *end = writeDigit(buf, 0, halfwidth);
		*end = 0;
	} else {
		byte *digitStart = digits + (kMaxDigits - displayDigits);
		byte *p = buf;
		for (uint i = 0; i < displayDigits; i++)
			p = writeDigit(p, digitStart[i], halfwidth);
		*p = 0;
	}
	return (const char *)buf;
}

const char *sysNumberToString(uint32 n) {
	uint16 flags = memGetSysvar16(kSysVarDisplayNumberFlags);
	return sysNumberToStringDigits(n, flags & 0xff, flags & 0x100);
}

void sysDisplayNumber(ParamList &params) {
	uint16 flags = memGetSysvar16(kSysVarDisplayNumberFlags);
	const char *str = sysNumberToString(vmExprParam(params, 0));

	if (flags & 0x400) {
		memSetVar32(18, strlen(str));
		return;
	}

	if (g_game->drawTextZen)
		g_game->drawTextZen(str);
	else
		vmDrawText(str, 1);
}

void sysCursorSavePos(ParamList &params) {
	uint x, y;
	cursorGetPos(x, y);
	memSetSysvar16(kSysVarCursorX, x);
	memSetSysvar16(kSysVarCursorY, y);
}

void sysFile(ParamList &params) {
	// TODO: older games have no File.write, and take no command parameter
	switch (vmExprParam(params, 0)) {
	case 0:
		vmLoadDataFile(vmStringParam(params, 1), vmExprParam(params, 2));
		break;
	default:
		error("System.File.function[%u] not implemented", params.params[0].val);
	}
}

void sysLoadFile(ParamList &params) {
	vmLoadDataFile(vmStringParam(params, 0), vmExprParam(params, 1));
}

void sysLoadImageAt(const char *name, uint i, uint xMult) {
	Common::Array<byte> data;
	if (!assetCgLoadRaw(name, data)) {
		warning("Failed to load CG \"%s\"", name);
		return;
	}

	// copy CG data into file_data
	uint32 off = memGetSysvar32(kSysVarCgOffset);
	if (off + data.size() > kMemFileDataSize)
		error("CG data would exceed buffer size");
	vmLoadFile(data.data(), data.size(), off);

	Common::SharedPtr<CG> cg = assetCgDecode(
			g_assetCgName.empty() ? name : g_assetCgName.c_str(), data);
	if (!cg) {
		warning("Failed to decode CG \"%s\"", name);
		return;
	}

	if (!vmFlagIsOn(kFlagPaletteOnly)) {
		memSetSysvar16(kSysVarCgX, cg->x / xMult);
		memSetSysvar16(kSysVarCgY, cg->y);
		memSetSysvar16(kSysVarCgW, cg->w / xMult);
		memSetSysvar16(kSysVarCgH, cg->h);
		gfxDrawCG(i, *cg);
	}

	if (cg->isIndexed() && vmFlagIsOn(kFlagLoadPalette))
		memcpy(g_mem->palette, cg->palette.data(), 256 * 4);
}

void sysLoadImage(ParamList &params) {
	animHaltAll();
	sysLoadImageAt(vmStringParam(params, 0), memGetSysvar16(kSysVarDstSurface), 1);
}

struct CopyArgs {
	int srcX, srcY, w, h;
	uint srcI;
	int dstX, dstY;
	uint dstI;
};

// (src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
static CopyArgs readCopyArgs(ParamList &params) {
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

void sysGraphicsCopy(ParamList &params) {
	CopyArgs a = readCopyArgs(params);
	gfxCopy(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI);
}

void sysGraphicsCopyMasked(ParamList &params) {
	CopyArgs a = readCopyArgs(params);
	gfxCopyMasked(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI, memGetSysvar16(kSysVarMaskColor));
}

void sysGraphicsCopyMasked24(ParamList &params) {
	CopyArgs a = readCopyArgs(params);
	gfxCopyMasked(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI, memGetSysvar32(kSysVarMaskColor24));
}

void sysGraphicsFillBg(ParamList &params) {
	// System.Graphics.fill_bg(x, y, br_x, br_y)
	int x = vmExprParam(params, 1);
	int y = vmExprParam(params, 2);
	int w = (int)vmExprParam(params, 3) - x + 1;
	int h = (int)vmExprParam(params, 4) - y + 1;
	gfxTextFill(x, y, w, h, memGetSysvar16(kSysVarDstSurface));
}

void sysGraphicsCopySwap(ParamList &params) {
	CopyArgs a = readCopyArgs(params);
	gfxCopySwap(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI);
}

void sysGraphicsSwapBgFg(ParamList &params) {
	// System.Graphics.swap_bg_fg(x, y, br_x, br_y)
	int x = vmExprParam(params, 1);
	int y = vmExprParam(params, 2);
	int w = (int)vmExprParam(params, 3) - x + 1;
	int h = (int)vmExprParam(params, 4) - y + 1;
	gfxTextSwapColors(x, y, w, h, memGetSysvar16(kSysVarDstSurface));
}

void sysGraphicsCompose(ParamList &params) {
	int fgX = vmExprParam(params, 1);
	int fgY = vmExprParam(params, 2);
	int w = (int)vmExprParam(params, 3) - fgX + 1;
	int h = (int)vmExprParam(params, 4) - fgY + 1;
	uint fgI = vmExprParam(params, 5);
	int bgX = vmExprParam(params, 6);
	int bgY = vmExprParam(params, 7);
	uint bgI = vmExprParam(params, 8);
	int dstX = vmExprParam(params, 9);
	int dstY = vmExprParam(params, 10);
	uint dstI = vmExprParam(params, 11);
	gfxCompose(fgX, fgY, w, h, fgI, bgX, bgY, bgI, dstX, dstY, dstI, memGetSysvar16(kSysVarMaskColor));
}

void sysGraphicsBlend(ParamList &params) {
	CopyArgs a = readCopyArgs(params);
	uint alpha = vmExprParam(params, 9);
	if (alpha < 1)
		return;
	if (alpha > 15)
		gfxCopy(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI);
	else
		gfxBlend(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI, alpha * 16 - 8);
}

void sysGraphicsBlendMasked(ParamList &params) {
	CopyArgs a = readCopyArgs(params);
	byte *mask = memRaw() + vmExprParam(params, 9) + 4;
	if (!memPtrValid(mask, a.w * a.h))
		error("Invalid mask pointer");
	gfxBlendMasked(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI, mask);
}

void sysGraphicsInvertColors(ParamList &params) {
	int x = vmExprParam(params, 1);
	int y = vmExprParam(params, 2);
	int w = (int)vmExprParam(params, 3) - x + 1;
	int h = (int)vmExprParam(params, 4) - y + 1;
	gfxInvertColors(x, y, w, h, memGetSysvar16(kSysVarDstSurface));
}

void sysGraphicsCopyProgressive(ParamList &params) {
	CopyArgs a = readCopyArgs(params);
	gfxCopyProgressive(a.srcX, a.srcY, a.w, a.h, a.srcI, a.dstX, a.dstY, a.dstI);
}

void sysWait(ParamList &params) {
	if (params.nrParams == 0 || vmExprParam(params, 0) == 0) {
		while (!g_engine->shouldQuit()) {
			if (inputDown(kInputCtrl)) {
				vmPeek();
				vmDelay(g_config.msgSkipDelay);
				return;
			}
			if (inputDown(kInputActivate)) {
				inputWaitUntilUp(kInputActivate);
				return;
			}
			vmPeek();
			vmDelay(16);
		}
	} else {
		VMTimer timer = vmTimerCreate();
		VMTimer target = timer + (params.params[0].val / 4) * 15;
		while (timer < target && !inputDown(kInputShift) && !g_engine->shouldQuit()) {
			vmPeek();
			vmTimerTick(timer, MIN<uint32>(target - timer, 8));
		}
	}
}

void sysSetTextColorsIndexed(ParamList &params) {
	uint32 colors = vmExprParam(params, 0);
	gfxTextSetColors((colors >> 4) & 0xf, colors & 0xf);
}

void sysSetTextColorsIndexedWithSysvar(ParamList &params) {
	byte param = vmExprParam(params, 0);
	byte bg = (param & 0xf0) >> 4;
	byte fg = param & 0x0f;
	memSetSysvar16(kSysVarBgColor, ((uint16)bg << 8) | fg);
	gfxTextSetColors(bg, fg);
}

void sysSetTextColorsDirect(ParamList &params) {
	gfxTextSetColors(vmExprParam(params, 0), vmExprParam(params, 1));
}

void sysFarcall(ParamList &params) {
	uint32 addr = vmExprParam(params, 0);
	if (addr >= sizeof(Memory))
		error("Tried to farcall to invalid address");

	VMPointer savedIp = g_vm->ip;
	g_vm->ip.ptr = 0;
	g_vm->ip.code = memRaw() + addr;
	vmExec();
	g_vm->ip = savedIp;
}

// An array lookup by cursor position. Entries are
//     { u16 id; u16 x_left, y_top, x_right, y_bot; }
// terminated by id 0xFFFF. The matching id (or 0xFFFF) goes to var16[18].
void sysGetCursorSegmentAt(uint x, uint y, uint32 off) {
	if (x >= (uint)gfxViewWidth() || y >= (uint)gfxViewHeight()) {
		warning("Invalid argument to System.get_cursor_segment: (%u,%u)", x, y);
		return;
	}

	const byte *a = g_mem->fileData + off;
	while (a < g_mem->fileData + kMemFileDataSize - 10) {
		uint16 id = READ_LE_UINT16(a);
		if (id == 0xffff) {
			memSetVar16(18, 0xffff);
			return;
		}
		uint16 xLeft = READ_LE_UINT16(a + 2);
		uint16 yTop = READ_LE_UINT16(a + 4);
		uint16 xRight = READ_LE_UINT16(a + 6);
		uint16 yBot = READ_LE_UINT16(a + 8);
		if (x >= xLeft && x <= xRight && y >= yTop && y <= yBot) {
			memSetVar16(18, id);
			return;
		}
		a += 10;
	}
	warning("Read past end of buffer in System.get_cursor_segment");
	memSetVar16(18, 0);
}

void sysGetCursorSegment(ParamList &params) {
	sysGetCursorSegmentAt(vmExprParam(params, 0), vmExprParam(params, 1), memGetSysvar32(kSysVarA6Offset));
}

void sysMenuGetNo(ParamList &params) {
	menuGetNo(vmExprParam(params, 0));
}

void sysCheckInput(ParamList &params) {
	uint input = vmExprParam(params, 0);
	bool value = vmExprParam(params, 1);
	if (input >= kInputNrInputs) {
		warning("Invalid input number: %u", input);
		memSetVar32(18, false);
		return;
	}
	memSetVar32(18, value == inputDown((InputEventType)input));
}

void sysStrlen(ParamList &params) {
	uint32 ptr = vmExprParam(params, 0);
	if (ptr >= sizeof(Memory))
		error("Invalid pointer: %u", ptr);
	const char *str = (const char *)memRaw() + ptr;
	uint32 n = 0;
	while (ptr + n < sizeof(Memory) && str[n])
		n++;
	memSetVar32(18, n);
}

void sysGetTime(ParamList &params) {
	TimeDate t;
	g_system->getTimeAndDate(t);
	memSetVar16(0, t.tm_year + 1900);
	memSetVar16(1, t.tm_mon + 1);
	memSetVar16(2, t.tm_wday);
	memSetVar16(3, t.tm_mday);
	memSetVar16(4, t.tm_hour);
	memSetVar16(5, t.tm_min);
	memSetVar16(6, MIN(t.tm_sec, 59));
}

} // End of namespace AI5
