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

#include "common/events.h"
#include "common/str.h"
#include "common/system.h"
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
#include "ai5/menu.h"
#include "ai5/savedata.h"
#include "ai5/sys.h"
#include "ai5/text.h"
#include "ai5/vm.h"
#include "ai5/games/aishimai.h"

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
	kVar32Off = kSysvar16Off + 24 * 2,
	kSysvar32Off = kVar32Off + 26 * 4,
	kHeapOff = kSysvar32Off + 62 * 4
};
static_assert(kHeapOff == 0xa48, "unexpected Ai Shimai heap offset");

} // End of anonymous namespace

static void aiShimaiMemRestore() {
	memSetSysvar16Ptr(kSysvar16Off);
	memSetSysvar32(kSysVarMemory, offsetof(Memory, mem16));
	memSetSysvar32(kSysVarFileData, offsetof(Memory, fileData));
	memSetSysvar32(kSysVarMenuEntryAddresses, offsetof(Memory, menuEntryAddresses));
	memSetSysvar32(kSysVarMenuEntryNumbers, offsetof(Memory, menuEntryNumbers));
	memSetSysvar32(kSysVarMapData, 0);

	uint16 flags = memGetSysvar16(kSysVarFlags);
	memSetSysvar16(kSysVarFlags, (flags & 0xffbf) | 0x21);
	memSetSysvar16((uint)0, kHeapOff);
}

static void aiShimaiMemInit() {
	// pointer table for memory access
	g_memPtr.mesName = memRaw();
	g_memPtr.var4 = memRaw() + kVar4Off;
	g_memPtr.systemVar16Ptr = memRaw() + kSv16PtrOff;
	g_memPtr.var16 = memRaw() + kVar16Off;
	g_memPtr.systemVar16 = memRaw() + kSysvar16Off;
	g_memPtr.var32 = memRaw() + kVar32Off;
	g_memPtr.systemVar32 = memRaw() + kSysvar32Off;

	memSetSysvar16(kSysVarFlags, 0x60f);
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
	memSetSysvar32(11, 0);
	aiShimaiMemRestore();
}

// Text Variables:
// ---------------
//
// var4[2001] controls whether "separate"-rendered text is merged in
// System.function[22].function[1]
//   * 1 -> text is merged
//   * !1 -> text is not merged
//
// var4[2002] selects the font.
//   * 0 -> FONT.FNT
//   * 1 -> SELECT1.FNT
//   * 2 -> SELECT2.FNT
//   * 3 -> SELECT3.FNT
//
// (Note that SELECT fonts always use the "merged" rendering mode.)
//
// var4[2017] controls whether the "merged" or "separate" rendering mode is used.
//   * 0 -> use "separate" rendering mode to surface 7
//   * !0 -> use "merged" rendering mode to System.dstSurface
//
// var4[2018] controls whether text is greyscale or redscale
//   * 0 -> greyscale
//   * !0 -> redscale

// XXX: many functions below assume RGB24 surfaces

/*
 * Get character index from table.
 */
static int getCharIndex(uint16 ch, byte *table) {
	uint16 size = READ_LE_UINT16(table + 0);
	for (uint i = 0; i < size; i++) {
		if (READ_LE_UINT16(table + (i + 1) * 2) == ch)
			return i;
	}
	return -1;
}

/*
 * Blend monochrome color data with an RGB24 pixel at a given alpha level.
 */
static void alphaBlendRgbMono(byte *bg, byte fg, byte alpha) {
	uint32 a = (uint32)alpha + 1;
	uint32 invA = 256 - (uint32)alpha;
	bg[0] = (byte)((a * fg + invA * bg[0]) >> 8);
	bg[1] = (byte)((a * fg + invA * bg[1]) >> 8);
	bg[2] = (byte)((a * fg + invA * bg[2]) >> 8);
}

/*
 * Blend a BGR24 pixel with an RGB24 pixel at a given alpha level.
 */
static void alphaBlendRgbBgr(byte *bg, byte *fg, byte alpha) {
	uint32 a = (uint32)alpha + 1;
	uint32 invA = 256 - (uint32)alpha;
	bg[0] = (byte)((a * fg[2] + invA * bg[0]) >> 8);
	bg[1] = (byte)((a * fg[1] + invA * bg[1]) >> 8);
	bg[2] = (byte)((a * fg[0] + invA * bg[2]) >> 8);
}

/*
 * This is the simple rendering mode, in which the mask and greyscale color data are
 * merged and written directly to a surface.
 */
static void renderCharMerged(byte *dstIn, byte *fntIn, byte *mskIn,
		byte *pal, int charW, int charH, int stride) {
	for (int row = 0; row < charH; row++) {
		byte *fnt = fntIn + charW * row;
		byte *msk = mskIn + charW * row;
		byte *dst = dstIn + row * stride;
		for (int col = 0; col < charW; col++, fnt++, msk++, dst += 3) {
			if (*msk == 0)
				continue;
			if (pal) {
				byte alpha = (MIN<int>(*msk, 15) * 16) - 8;
				byte *c = pal + *fnt * 3;
				alphaBlendRgbBgr(dst, c, alpha);
			} else if (*msk > 15) {
				dst[0] = *fnt;
				dst[1] = *fnt;
				dst[2] = *fnt;
			} else {
				byte alpha = (MIN<int>(*msk, 15) * 16) - 8;
				alphaBlendRgbMono(dst, *fnt, alpha);
			}
		}
	}
}

/*
 * "Redscale" rendering mode. This mode is like the "merged" mode, except that only the
 * red channel is blended. The green and blue channels are set to zero whenever the
 * mask is non-zero.
 */
static void renderCharRedscale(byte *dstIn, byte *fntIn, byte *mskIn,
		byte *pal, int charW, int charH, int stride) {
	for (int row = 0; row < charH; row++) {
		byte *fnt = fntIn + charW * row;
		byte *msk = mskIn + charW * row;
		byte *dst = dstIn + row * stride;
		for (int col = 0; col < charW; col++, fnt++, msk++, dst += 3) {
			if (*msk == 0)
				continue;
			if (*msk > 15) {
				dst[0] = *fnt;
			} else {
				byte alpha = (MIN<int>(*msk, 15) * 16) - 8;
				alphaBlendRgbMono(dst, *fnt, alpha);
			}
			dst[1] = 0;
			dst[2] = 0;
		}
	}
}

/*
 * In this rendering mode, the greyscale color data is written at the text cursor, and
 * the mask data is written 256 lines below the cursor. Merging the two is a separate
 * operation.
 */
static void renderCharSeparate(byte *dstIn, byte *fntIn, byte *mskIn,
		byte *pal, int charW, int charH, int stride) {
	for (int row = 0; row < charH; row++) {
		byte *fnt = fntIn + charW * row;
		byte *msk = mskIn + charW * row;
		byte *fntDst = dstIn + row * stride;
		byte *mskDst = dstIn + (row + 256) * stride;
		for (int col = 0; col < charW; col++, fnt++, msk++, fntDst += 3, mskDst += 3) {
			if (*fnt) {
				fntDst[0] = *fnt;
				fntDst[1] = *fnt;
				fntDst[2] = *fnt;
			}
			if (*msk) {
				mskDst[0] = *msk;
				mskDst[1] = *msk;
				mskDst[2] = *msk;
			}
		}
	}
}

struct RenderTextParams {
	int charW, charH;
	uint surface;
	void (*renderChar)(byte*,byte*,byte*,byte*,int,int,int);
	byte *fontTbl;
	byte *fontMsk;
	byte *fontFnt;
	byte *fontPal;
};

/*
 * Render a string according to the given parameters.
 */
static void renderText(const char *txt, RenderTextParams *p) {
	const uint16 startX = memGetSysvar16(kSysVarTextStartX);
	const uint16 endX = memGetSysvar16(kSysVarTextEndX);
	const uint16 charSpace = memGetSysvar16(kSysVarCharSpace);
	const uint16 lineSpace = memGetSysvar16(kSysVarLineSpace);
	uint16 x = memGetSysvar16(kSysVarTextCursorX);
	uint16 y = memGetSysvar16(kSysVarTextCursorY);

	GfxSurface *surf = gfxGetSurface(p->surface);

	bool lastCharIsClose = false;
	for (; *txt; txt += 2) {
		// get index of char in font data
		uint16 charCode = READ_LE_UINT16((const byte *)txt + 0);
		int charI = getCharIndex(charCode, p->fontTbl);
		if (charI < 0) {
			warning("Invalid character: %04x", charCode);
			continue;
		}

		// opener at end of line: drop to next line
		if (x + charSpace > endX && charIsOpener(txt)) {
			y += lineSpace;
			x = startX;
		}

		// closer at start of line: go to last column of previous line
		// (game allows some extra space for this)
		if (x == startX && !lastCharIsClose && charIsCloser(txt)) {
			y -= lineSpace;
			x = endX;
			lastCharIsClose = true;
		} else {
			lastCharIsClose = false;
		}

		byte *charMsk = p->fontMsk + (charI * p->charW * p->charH);
		byte *charFnt = p->fontFnt + (charI * p->charW * p->charH);
		byte *dst = surf->pixels + y * surf->pitch + x * 3;
		p->renderChar(dst, charFnt, charMsk, p->fontPal, p->charW, p->charH, surf->pitch);

		x += charSpace;
		if (x + charSpace > endX) {
			y += lineSpace;
			x = startX;
		}
	}

	memSetSysvar16(kSysVarTextCursorX, x);
	memSetSysvar16(kSysVarTextCursorY, y);

	gfxWholeSurfaceDirty(p->surface);
}

/*
 * Render a string using one of the SELECT fonts.
 */
static void renderTextSelect(const char *txt) {
	int sel = memGetVar4(2002);
	if (sel < 1 || sel > 3) {
		warning("Invalid SELECT font index: %d", sel);
		return;
	}
	RenderTextParams p;
	p.charW = sel == 2 ? 49 : 47;
	p.charH = sel == 2 ? 49 : 47;
	p.surface = memGetSysvar16(kSysVarDstSurface);
	p.renderChar = renderCharMerged;
	p.fontTbl = g_mem->fileData + memGetVar32(3);
	p.fontMsk = g_mem->fileData + memGetVar32(5 + (sel - 1) * 3);
	p.fontFnt = g_mem->fileData + memGetVar32(6 + (sel - 1) * 3);
	p.fontPal = g_mem->fileData + memGetVar32(4 + (sel - 1) * 3);
	renderText(txt, &p);
}

/*
 * Custom TXT function.
 */
static void aiShimaiTxt(const char *txt) {
	if (memGetVar4(2002) != 0) {
		if (memGetVar4(2002) < 4)
			renderTextSelect(txt);
		else {
			vmDrawText(txt, 1);
		}
		return;
	}

	bool renderMerged = memGetVar4(2017) != 0;
	bool renderRedscale = memGetVar4(2018) != 0;
	RenderTextParams p;
	p.charW = 28;
	p.charH = 28;
	p.surface = renderMerged ? memGetSysvar16(kSysVarDstSurface) : 7;
	p.renderChar = renderRedscale ? renderCharRedscale :
				renderMerged ? renderCharMerged :
				renderCharSeparate;
	p.fontTbl = g_mem->fileData + memGetVar32(0);
	p.fontMsk = g_mem->fileData + memGetVar32(1);
	p.fontFnt = g_mem->fileData + memGetVar32(2);
	p.fontPal = nullptr;
	renderText(txt, &p);
}

static void aiShimaiCursor(ParamList &params) {
	static uint cursor1FrameTime[4] = { 200, 200, 200, 500 };
	switch (vmExprParam(params, 0)) {
	case 0: cursorShow(); break;
	case 1: cursorHide(); break;
	case 2: sysCursorSavePos(params); break;
	case 3: cursorSetPos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 4:
		switch (vmExprParam(params, 1)) {
		case 0: cursorUnload(); break;
		case 1: cursorLoad(0, 4, cursor1FrameTime); break;
		case 2: cursorLoad(4, 2, nullptr); break;
		default: warning("Invalid cursor number: %u", vmExprParam(params, 1));
		}
		break;
	case 5:
		cursorSetDirection(kCursorDirNone);
		// fall through
	case 6:
		memSetVar32(18, cursorGetDirection());
		cursorSetDirection(kCursorDirNone);
		break;
	default: warning("System.Cursor.function[%u] not implemented",
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

static void aiShimaiAnim(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: animInitStream(vmAnimParam(params, 1), vmAnimParam(params, 1)); break;
	case 1: animStart(vmAnimParam(params, 1)); break;
	case 2: animStop(vmAnimParam(params, 1)); break;
	case 3: animHalt(vmAnimParam(params, 1)); break;
	case 4: animWait(vmAnimParam(params, 1)); break;
	case 5: animStopAll(); break;
	case 6: animHaltAll(); break;
	case 7: animResetAll(); break;
	//case 8: TODO
	default: warning("System.Anim.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void aiShimaiResumeLoad(const char *saveName) {
	byte buf[kVar4Off + kVar4Size];
	byte *var4 = buf + kVar4Off;
	byte *memVar4 = memRaw() + kVar4Off;
	savedataRead(saveName, memRaw(), 0, kMem16Size);
	savedataRead("FLAG00", buf, kVar4Off, kVar4Size);

	memcpy(memVar4 + 700, var4 + 700, 181);
	memcpy(memVar4 + 1065, var4 + 1065, 735);
	memVar4[2005] = var4[2005];
	memVar4[2009] = var4[2009];

	aiShimaiMemRestore();
	vmLoadMes(memMesName());
	vmFlagOn(kFlagReturn);
}

static void aiShimaiLoadExtraVar32(const char *saveName) {
	// sysvar32[12] -> sysvar32[61]
	savedataRead(saveName, memRaw(), kSysvar32Off + (12 * 4), 50 * 4);
}

static void aiShimaiSaveExtraVar32(const char *saveName) {
	// sysvar32[12] -> sysvar32[61]
	savedataWrite(saveName, memRaw(), kSysvar32Off + (12 * 4), 50 * 4);
}

static void aiShimaiLoadHeap(const char *saveName, int start, int count) {
	if (count <= 0 || start < 0 || start + count > 1464) {
		warning("Invalid heap load: %d+%d", start, count);
		return;
	}
	savedataRead(saveName, memRaw(), kHeapOff + start, count);
}

static void aiShimaiSaveHeap(const char *saveName, int start, int count) {
	if (count <= 0 || start < 0 || start + count > 1464) {
		warning("Invalid heap save: %d+%d", start, count);
		return;
	}
	savedataWrite(saveName, memRaw(), kHeapOff + start, count);
}

static void aiShimaiSavedata(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: aiShimaiResumeLoad(sysSaveName(params)); break;
	case 1: savedataResumeSave(sysSaveName(params)); break;
	case 2: savedataLoadVar4Restore(sysSaveName(params)); break;
	case 3: savedataSaveUnionVar4(sysSaveName(params)); break;
	case 4: aiShimaiLoadExtraVar32(sysSaveName(params)); break;
	case 5: aiShimaiSaveExtraVar32(sysSaveName(params)); break;
	case 6: memset(memRaw() + kVar4Off, 0, kVar4Size); break;
	case 7: aiShimaiLoadHeap(sysSaveName(params), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	case 8: aiShimaiSaveHeap(sysSaveName(params), vmExprParam(params, 2),
				vmExprParam(params, 3)); break;
	default: error("System.SaveData.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void aiShimaiAudio(ParamList &params) {
	static bool haveNext = false;
	static char next[128] = {0};
	switch (vmExprParam(params, 0)) {
	case 0: audioBgmPlay(vmStringParam(params, 1), true); break;
	case 1: audioStop(kAudioChBgm); break;
	case 2: audioFade(kAudioChBgm, kAudioVolumeMin, 3000, true, false); break;
	case 3: audioFade(kAudioChBgm, kAudioVolumeMin, 3000, true, true); break;
	case 4:
		Common::strlcpy(next, vmStringParam(params, 1), sizeof(next));
		haveNext = true;
		break;
	case 5:
		if (haveNext) {
			audioBgmPlay(next, true);
			haveNext = false;
		}
		break;
	case 6: audioSePlay(vmStringParam(params, 1), vmExprParam(params, 2)); break;
	case 7: audioSeStop(vmExprParam(params, 1)); break;
	case 8: audioSeFade(kAudioVolumeMin, 3000, true, false, vmExprParam(params, 1)); break;
	case 9: audioSeFade(kAudioVolumeMin, 3000, true, true, vmExprParam(params, 1)); break;
	default: error("System.Audio.function[%u] not implemented",
				 params.params[0].val);
	}
}

static char preparedVoice[kStringParamSize] = {0};
static bool havePreparedVoice = false;

static void aiShimaiVoice(ParamList &params) {

	if (!vmFlagIsOn(kFlagVoiceEnable))
		return;
	if (params.nrParams > 2 && vmExprParam(params, 2) != 0) {
		switch (vmExprParam(params, 0)) {
		case 0: audioVoicesubPlay(vmStringParam(params, 1)); break;
		case 1: audioVoicesubStop(); break;
		case 5: memSetVar32(18, audioVoicesubIsPlaying()); break;
		default: warning("System.Voice(sub).function[%u] not implemented",
					 params.params[0].val);
		}
		return;
	}
	switch (vmExprParam(params, 0)) {
	case 0: audioVoicePlay(vmStringParam(params, 1), 0); break;
	case 1: audioStop(kAudioChVoice0); break;
	//case 2: audioVoicePlaySync(vmStringParam(params, 1)); break;
	case 3:
		if (vmFlagIsOn(kFlagLog))
			backlogSetHasVoice();
		Common::strlcpy(preparedVoice, vmStringParam(params, 1), sizeof(preparedVoice));
		havePreparedVoice = true;
		break;
	case 4:
		if (vmFlagIsOn(kFlagLog))
			backlogSetHasVoice();
		if (havePreparedVoice) {
			audioVoicePlay(preparedVoice, 0);
			havePreparedVoice = 0;
			memSetVar32(18, 1);
		} else {
			memSetVar32(18, 0);
		}
		break;
	case 5:
		memSetVar32(18, audioIsPlaying(kAudioChVoice0));
		break;
	default: warning("System.Voice.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void aiShimaiDisplay(ParamList &params) {
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

static void aiShimaiGraphics(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: sysGraphicsCopy(params); break;
	case 1: sysGraphicsCopyMasked24(params); break;
	case 2: sysGraphicsFillBg(params); break;
	case 4: sysGraphicsSwapBgFg(params); break;
	case 6: {
		VMTimer timer = vmTimerCreate();
		sysGraphicsBlend(params);
		// XXX: The game calls this function in a loop to implement a
		//      crossfade effect. We throttle it here so that the
		//      effect is visible on modern systems.
		if (!inputDown(kInputCtrl))
			vmTimerTick(timer, g_config.transitionSpeed * 16);
		break;
	}
	case 7: sysGraphicsBlendMasked(params); break;
	default: error("System.Graphics.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void aiShimaiGetTime(ParamList &params) {
	TimeDate t;
	g_system->getTimeAndDate(t);
	memSetVar16(0, t.tm_year + 1900);
	memSetVar16(1, t.tm_mon + 1);
	memSetVar16(2, t.tm_wday);
	memSetVar16(3, t.tm_mday);
	memSetVar16(4, t.tm_hour);
	memSetVar16(5, t.tm_min);
	// upstream uses MAX here, which always yields 59
	memSetVar16(6, MIN(t.tm_sec, 59));
}

static void aiShimaiBacklog(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: backlogClear(); break;
	case 1: backlogPrepare(); break;
	case 2: backlogCommit(); break;
	case 3: memSetVar32(18, backlogCount()); break;
	case 4: memSetVar32(18, backlogGetPointer(vmExprParam(params, 1))); break;
	case 5: memSetVar16(18, backlogHasVoice(vmExprParam(params, 1))); break;
	default: warning("System.Backlog.function[%u] not implemented",
				 params.params[0].val);
	}
}

static void clearOverlayText(GfxSurface *dst) {
	for (int row = 336; row < 336 + 128; row++)
		memset(gfxPixel(dst, 0, row), 0, 640 * 4);
}

static void updateText(ParamList &params) {
	if (memGetVar4(2001) != 1)
		return;

	GfxSurface *src = gfxGetSurface(7);
	GfxSurface *dst = gfxGetOverlay(0);

	// clear overlay
	clearOverlayText(dst);

	// merge color/mask from surface 7 and write to overlay surface
	// color data is at (0,   0) -> (640, 128) on surface 7
	// mask data is at  (0, 256) -> (640, 384) on surface 7
	// destination is   (0, 336) -> (640, 464) on overlay
	for (int row = 0; row < 128; row++) {
		byte *fnt = src->pixels + row * src->pitch;
		byte *msk = src->pixels + (row + 256) * src->pitch;
		byte *p = dst->pixels + (row + 336) * dst->pitch;
		for (int col = 0; col < 640; col++, fnt += 3, msk += 3, p += 4) {
			// XXX: only blue channel matters for mask
			if (msk[2] == 0)
				continue;
			// ???: do all channels get copied here?
			p[0] = fnt[0];
			p[1] = fnt[1];
			p[2] = fnt[2];
			if (msk[2] > 15) {
				p[3] = 255;
			} else {
				p[3] = *msk * 16 - 8;
			}
		}
	}

	// TODO: mark (x,y,w,h) as dirty
	//int x = vmExprParam(params, 2);
	//int y = vmExprParam(params, 3);
	//int w = vmExprParam(params, 4);
	//int h = vmExprParam(params, 5);
	gfxScreenDirty();
}

static void clearText(ParamList &params) {
	clearOverlayText(gfxGetOverlay(0));

	// TODO: mark (x,y,w,h) as dirty
	//int x = vmExprParam(params, 2);
	//int y = vmExprParam(params, 3);
	//int w = vmExprParam(params, 4);
	//int h = vmExprParam(params, 5);
	gfxScreenDirty();

}

static void sys22(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 1:  updateText(params); break;
	case 2:  clearText(params); break;
	default: warning("System.function[22].function[%u] not implemented",
				 params.params[0].val);
	}
}

#define IME_BUF_LEN 1024
static byte imeBuf[IME_BUF_LEN] = {0};
static uint imeCursorPos = 0;
static bool imeCursorInside = false;
static bool imeEnabled = false;
static bool imeCompositionStarted = false;
static bool imeCompositionFinished = false;

#define IME_LOG(...) debug(5, __VA_ARGS__)

static void imeReset() {
	imeEnabled = true;
	imeCompositionStarted = false;
	imeCompositionFinished = false;
}

static void imeEnable() {
	IME_LOG("imeEnable()");
	imeReset();
	memset(imeBuf, 0, sizeof(imeBuf));
}

static void imeDisable() {
	IME_LOG("imeDisable()");
	imeReset();
}

// zenkaku (Shift-JIS) equivalents of ASCII letters and hankaku katakana
static const uint16 hanzenTable[256] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0x8260, 0x8261, 0x8262, 0x8263, 0x8264, 0x8265, 0x8266,
	0x8267, 0x8268, 0x8269, 0x826a, 0x826b, 0x826c, 0x826d, 0x826e,
	0x826f, 0x8270, 0x8271, 0x8272, 0x8273, 0x8274, 0x8275, 0x8276,
	0x8277, 0x8278, 0x8279, 0, 0, 0, 0, 0,
	0, 0x8281, 0x8282, 0x8283, 0x8284, 0x8285, 0x8286, 0x8287,
	0x8288, 0x8289, 0x828a, 0x828b, 0x828c, 0x828d, 0x828e, 0x828f,
	0x8290, 0x8291, 0x8292, 0x8293, 0x8294, 0x8295, 0x8296, 0x8297,
	0x8298, 0x8299, 0x829a, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0x8142, 0x8175, 0x8176, 0x8141, 0x8145, 0x8392, 0x8340,
	0x8342, 0x8344, 0x8346, 0x8348, 0x8383, 0x8385, 0x8387, 0x8362,
	0x815b, 0x8341, 0x8343, 0x8345, 0x8347, 0x8349, 0x834a, 0x834c,
	0x834e, 0x8350, 0x8352, 0x8354, 0x8356, 0x8358, 0x835a, 0x835c,
	0x835e, 0x8360, 0x8363, 0x8365, 0x8367, 0x8369, 0x836a, 0x836b,
	0x836c, 0x836d, 0x836e, 0x8371, 0x8374, 0x8377, 0x837a, 0x837d,
	0x837e, 0x8380, 0x8381, 0x8382, 0x8384, 0x8386, 0x8388, 0x8389,
	0x838a, 0x838b, 0x838c, 0x838d, 0x838f, 0x8393, 0x814a, 0x814b,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
};

/*
 * Convert hankaku characters to zenkaku.
 */
static Common::String imeConvertZenkaku(const Common::String &in) {
	Common::String out;
	for (uint i = 0; i < in.size(); i++) {
		byte c = in[i];
		if (sjisIs2Byte(c) && i + 1 < in.size()) {
			out += c;
			out += in[++i];
		} else if (hanzenTable[c]) {
			out += (char)(hanzenTable[c] >> 8);
			out += (char)(hanzenTable[c] & 0xff);
		} else {
			// full-width '?'
			out += '\x81';
			out += '\x48';
		}
	}
	return out;
}

static void imeSetText(const Common::String &sjis) {
	uint len = MIN<uint>(1023, sjis.size());
	memcpy(imeBuf, sjis.c_str(), len);
	imeBuf[len] = '\0';
}

/*
 * Finalize the composition text.
 */
static void imeCommitText(const Common::String &sjis) {
	imeCompositionStarted = false;
	imeCompositionFinished = true;
	imeCursorPos = 0;
	imeCursorInside = false;
	imeSetText(imeConvertZenkaku(sjis));
	IME_LOG("imeCommitText(\"%s\")", (const char *)imeBuf);
}

static void sysImeGetText(ParamList &params) {
	byte *out = memRaw() + vmExprParam(params, 1);
	uint outLen = vmExprParam(params, 2);
	if (!memPtrValid(out, outLen + 2))
		error("Invalid output buffer for System.IME.getString");
	if (!memchr(out, 0, outLen + 2)) {
		warning("Output buffer is not null-terminated");
		out[outLen] = 0;
		out[outLen + 1] = 0;
	}

	int32 ord = strcmp((char*)out, (char*)imeBuf);
	uint copyLen = MIN<uint>(outLen, strlen((char *)imeBuf));
	memcpy(out, imeBuf, copyLen);
	out[copyLen] = 0;
	out[copyLen + 1] = 0;
	imeBuf[copyLen] = 0;
	imeBuf[copyLen + 1] = 0;

	memSetVar16(18, copyLen);
	memSetVar32(18, ord);

	if (ord)
		IME_LOG("imeGetText(...) -> %s, %u, %d", (char *)out, copyLen, ord);
}

static void sysImeStrcmp(ParamList &params) {
	char *str = memGetCString(vmExprParam(params, 1));
	if (!str)
		error("Invalid string for System.IME.strcmp");
	memSetVar16(18, strcmp(str, (char *)imeBuf));
	//IME_LOG("imeStrcmp(...) -> %u", memGetVar16(18));
}

static void aiShimaiIme(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: imeEnable(); break;
	case 1: imeDisable(); break;
	case 2: memSetVar16(18, imeCompositionStarted); break;
	case 3: sysImeGetText(params); break;
	case 4: memSetVar16(18, imeCursorInside); break; // ???
	case 5: memSetVar16(18, imeCursorPos); break;
	case 6: sysImeStrcmp(params); break;
	case 7: memSetVar16(18, imeCompositionFinished ? 2 : 0); break;
	}
}

static void utilShiftScreen(ParamList &params) {
	int32 x = vmExprParam(params, 1);
	int32 y = vmExprParam(params, 2);

	VMTimer timer = vmTimerCreate();
	// shift the displayed screen without changing surface 0
	GfxSurfaceState *s = &g_gfx.surface[g_gfx.screen];
	s->dst.x = x;
	s->dst.y = y;
	s->scaled = true;
	gfxScreenDirty();
	gfxUpdate();
	s->dst.x = 0;
	s->dst.y = 0;
	s->scaled = false;
	gfxScreenDirty();
	vmTimerTick(timer, 16);
}

static void utilCopyToSurface7(ParamList &params) {
	// XXX: this is dead code...? Only used in MES.ARC:DEFMAIN.MES,
	//      NOT in DATA.ARC:DEFMAIN.MES (which is the real DEFMAIN).
	int x = vmExprParam(params, 1);
	int y = vmExprParam(params, 2);
	int w = vmExprParam(params, 3);
	int h = vmExprParam(params, 4);
	gfxCopy(x, y, w, h, 0, 0, 0, 7);
}

static void utilStrcpy(ParamList &params) {
	byte *dst = memRaw() + vmExprParam(params, 1);
	char *src = memGetCString(vmExprParam(params, 2));
	if (!src)
		error("Invalid source for strcpy");
	if (!memPtrValid(dst, strlen(src) + 1))
		error("Invalid destination for strcpy");
	Common::strlcpy((char *)dst, src, strlen(src) + 1);
}

static void utilStrcpy2(ParamList &params) {
	byte *src = memRaw() + vmExprParam(params, 1);
	byte *dst = memRaw() + vmExprParam(params, 2);
	uint count = vmExprParam(params, 3);
	uint off = vmExprParam(params, 4);

	if (!memPtrValid(src + off + 1, count))
		error("Invalid source for strcpy2");
	if (!memPtrValid(dst, count + 2))
		error("Invalid destination for strcpy2");

	// don't read from 2nd byte of zenkaku character
	if (off > 1 && (off & 1) && mesCharIsZenkaku(src[off - 1]))
		off++;
	src = src + off;

	uint i;
	for (i = 0; i < count && src[i]; i++) {
		dst[i] = src[i];
		if (mesCharIsZenkaku(src[i])) {
			i++;
			dst[i] = src[i];
		}
	}
	i = MIN(i, count);
	dst[i] = 0;
	dst[i+1] = 0;

	memSetVar16(18, i);
	memSetVar32(18, off);
}

static void utilLocationIndex(ParamList &params) {
	static const char *outLoc[5] = {
		"\x8a\x77\x8d\x5a",         // 学校
		"\x96\x6b\x91\xf2\x89\xc6", // 北沢家
		"\x8e\x96\x96\xb1\x8f\x8a", // 事務所
		"\x96\xec\x90\xec\x89\xc6", // 野川家
		nullptr
	};
	static const char *schoolLoc[6] = {
		"\x8a\x77\x8d\x5a\x82\xcc\x8a\x4f",         //"学校の外",
		"\x91\xcc\x88\xe7\x8f\x80\x94\xf5\x8e\xba", //"体育準備室",
		"\x94\xfc\x8f\x70\x8f\x80\x94\xf5\x8e\xba", //"美術準備室",
		"\x8d\x5a\x8e\xc9\x82\xcc\x89\xae\x8f\xe3", //"校舎の屋上",
		"\x8b\xb3\x8e\xba",                         //"教室",
		nullptr
	};
	const char **options = vmExprParam(params, 1) ? schoolLoc : outLoc;
	const char *loc = memGetCString(vmExprParam(params, 2) + 1);
	if (!loc)
		error("Invalid cstring parameter");

	for (int i = 0; options[i]; i++) {
		if (!strcmp(options[i], loc)) {
			memSetVar16(18, i);
			return;
		}
	}
	memSetVar16(18, 255);
}

static void utilLocationZoom(ParamList &params) {
	static const uint outCoords[5][2] = {
		{  60, 8   },
		{ 129, 320 },
		{ 416, 312 },
		{ 368, 40  },
		{   0, 0   },
	};
	static const uint schoolCoords[5][2] = {
		{ 452, 336 },
		{  20, 304 },
		{ 436, 32  },
		{   8, 8   },
		{ 216, 204 },
	};

	// get coordinates of zoom area
	uint x, y;
	uint school = vmExprParam(params, 1);
	uint loc = vmExprParam(params, 2);
	if ((!school && loc > 3) || loc > 4) {
		warning("Invalid location index: %u:%u", school, loc);
		return;
	}
	if (school) {
		x = schoolCoords[loc][0];
		y = schoolCoords[loc][1];
	} else {
		x = outCoords[loc][0];
		y = outCoords[loc][1];
	}

	gfxZoom(x, y, 180, 136, 2, 0, 350);
}

static void utilGetMess(ParamList &params) {
	// TODO: return "CONFIG.MESS" value from ini
	memSetVar32(18, 0);
}

static void utilWriteBacklogHeader(ParamList &params) {
	backlogPushByte(16);
	backlogPushByte(2);
	backlogPushByte(8);
	backlogPushByte(255);
	backlogPushByte(0);
}

static void utilLine(ParamList &params) {
	uint16 cursorY = memGetSysvar16(kSysVarTextCursorY);
	uint16 lineSpace = memGetSysvar16(kSysVarLineSpace);
	uint16 startX = memGetSysvar16(kSysVarTextStartX);
	memSetSysvar16(kSysVarTextCursorY, cursorY + lineSpace);
	memSetSysvar16(kSysVarTextCursorX, startX);
}

static void utilSaveVoice(ParamList &params) {
	warning("Util.saveVoice not implemented");
}

static void utilQuit(ParamList &params) {
	g_engine->quitGame();
}

static void utilGetImode(ParamList &params) {
	// TODO: return "CONFIG.IMODE" value from ini
	memSetVar32(18, 0);
}

static void utilSetPreparedVoice(ParamList &params) {
	havePreparedVoice = !!vmExprParam(params, 1);
}

static void utilCgmodeZoom(ParamList &params) {
	uint x = vmExprParam(params, 1);
	uint y = vmExprParam(params, 2);
	gfxZoom(x, y, 160, 120, 5, 0, 350);
}

static void utilScroll(ParamList &params) {
	int end = vmExprParam(params, 1);
	if (end > 1280) {
		warning("Invalid end argument to Util.scroll: %d", end);
		return;
	}
	end = -end;

	VMTimer timer = vmTimerCreate();
	for (int y = 479; y >= end && !g_engine->shouldQuit(); y--) {
		gfxCopy(0, 0, 400, 1280, 1, 120, y, 0);

		gfxWholeSurfaceDirty(0);
		vmPeek();
		if (inputDown(kInputCancel)) {
			memSetVar32(18, 1);
			return;
		}
		vmTimerTick(timer, 16);
	}
	memSetVar32(18, 0);
}

static void util15(ParamList &params) {
	warning("Util.function[15] not implemented");
}

static void utilGetCut(ParamList &params) {
	// TODO: return "CONFIG.CUT" value from ini
	memSetVar32(18, 1);
}

static void aiShimaiInit() {
	gfxTextSetColors(0, 0xffffff);
	gfxOverlayEnable(0);
}

// ScummVM has no IME composition events, so typed characters are committed
// directly (as full-width characters, like the original's commit path).
static bool aiShimaiHandleEvent(const Common::Event &e) {
	if (!imeEnabled || e.type != Common::EVENT_KEYDOWN)
		return false;
	if (e.kbd.ascii >= 0x20 && e.kbd.ascii < 0x7f) {
		imeCommitText(Common::String((char)e.kbd.ascii));
		return true;
	}
	return false;
}

void initGameAiShimai(Game &g) {
	static const struct { uint16 w, h; } sizes[] = {
		{ 640, 480 },
		{ 640, 1280 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 512 },
		{ 864, 468 },
		{ 720, 680 },
		{ 640, 480 }
	};
	g.id = GID_AISHIMAI;
	for (uint i = 0; i < ARRAYSIZE(sizes); i++) {
		g.surfaceSizes[i].w = sizes[i].w;
		g.surfaceSizes[i].h = sizes[i].h;
	}
	g.bpp = 24;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;
	g.init = aiShimaiInit;
	g.memInit = aiShimaiMemInit;
	g.memRestore = aiShimaiMemRestore;
	g.drawTextZen = aiShimaiTxt;
	g.handleEvent = aiShimaiHandleEvent;

	g.vm.exec = vmExec;
	g.vm.eval = vmEval;
	g.vm.readParams = vmReadParams;
	g.vm.endCode = 0x00;
	vmSetDefaultOps(g);

	g.sys[0] = sysSetFontSize;
	g.sys[1] = sysDisplayNumber;
	g.sys[2] = aiShimaiCursor;
	g.sys[3] = aiShimaiAnim;
	g.sys[4] = aiShimaiSavedata;
	g.sys[5] = aiShimaiAudio;
	g.sys[6] = aiShimaiVoice;
	g.sys[7] = sysFile;
	g.sys[8] = sysLoadImage;
	g.sys[9] = aiShimaiDisplay;
	g.sys[10] = aiShimaiGraphics;
	g.sys[11] = sysWait;
	g.sys[12] = sysSetTextColorsDirect;
	g.sys[13] = sysFarcall;
	g.sys[14] = sysGetCursorSegment;
	g.sys[15] = sysMenuGetNo;
	g.sys[16] = aiShimaiGetTime;
	g.sys[18] = sysCheckInput;
	g.sys[19] = aiShimaiBacklog;
	g.sys[21] = sysStrlen;
	g.sys[22] = sys22;
	g.sys[23] = aiShimaiIme;

	g.util[0] = utilShiftScreen;
	g.util[1] = utilCopyToSurface7;
	g.util[2] = utilStrcpy;
	g.util[3] = utilStrcpy2;
	g.util[4] = utilLocationIndex;
	g.util[5] = utilLocationZoom;
	g.util[6] = utilGetMess;
	g.util[7] = utilWriteBacklogHeader;
	g.util[8] = utilLine;
	g.util[9] = utilSaveVoice;
	g.util[10] = utilQuit;
	g.util[11] = utilGetImode;
	g.util[12] = utilSetPreparedVoice;
	g.util[13] = utilCgmodeZoom;
	g.util[14] = utilScroll;
	g.util[15] = util15;
	g.util[16] = utilGetCut;

	g.flags[kFlagAnimEnable] = 0x0004;
	g.flags[kFlagMenuReturn] = 0x0008;
	g.flags[kFlagReturn] = 0x0010;
	g.flags[kFlagLogEnable] = 0x0020;
	g.flags[kFlagLogText] = 0x0040;
	g.flags[kFlagLog] = 0x0080;
	g.flags[kFlagVoiceEnable] = 0x0100;
	g.flags[kFlagLogSys] = 0x1000;
	g.flags[kFlagWaitKeyup] = kFlagAlwaysOn;
}

} // End of namespace AI5
