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

#include "common/algorithm.h"
#include "common/events.h"
#include "common/error.h"
#include "common/rect.h"
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
#include "ai5/popupmenu.h"
#include "ai5/savedata.h"
#include "ai5/sys.h"
#include "ai5/text.h"
#include "ai5/vm.h"

#include "ai5/games/shuusaku.h"

namespace AI5 {

namespace {
enum {
	kMesNameSize = 32,
	kVar32Off    = kMesNameSize,
	kVar16Off    = kVar32Off + 26 * 4,
	kVar4Off     = kVar16Off + 500 * 2,
	kVar4Size    = 2500,
	kHeapOff     = kVar4Off + kVar4Size,
	kMem16Size   = kHeapOff + 100,
	kSysvar16Off = kMem16Size,   // system variables are not saved
	kAnimOffset  = 0xa0000,
	kAnimSize    = 0x10000,
	kZoomSteps    = 8,
	kMovieX       = 56,
	kMovieY       = 72
};
static_assert(kMem16Size == 3736, "Shuusaku mem16 size");

// Raw system variables (no MesSysVar16 name).
const uint kSysvarFadeMs     = 13;   // crossfade duration (x16 ms)
const uint kSysvarSkipFlag   = 19;   // set when skipping is disallowed
const uint kSysvarKettei     = 50;   // KETTEI option from the ini
const uint kSysvarWinDay     = 60;   // sub-window day (0xffff = invalid)
const uint kSysvarWinTime    = 61;   // sub-window time
const uint kSysvarPalFrozen  = 69;   // freeze low palette colors

byte extraPalette[0x400] = {0};

char bgmFile[33] = {0};
char seFile[33] = {0};
int bgmVol = 0;

// schedule plan strip state
struct Plan {
	int time;
	int day;
	int i;
	bool flashEnabled;
	bool flashShown;
	VMTimer flashTimer;
} plan;

bool messageWindowShown = false;

bool crossfadeTick(float, void *) {
	if (memGetSysvar16(kSysvarSkipFlag))
		return true;
	return !inputDown(kInputShift);
}

void fillPalette(byte *pal, int r, int g, int b) {
	for (uint i = 0; i < 256 * 4; i += 4) {
		pal[i + 0] = b;
		pal[i + 1] = g;
		pal[i + 2] = r;
		pal[i + 3] = 1;
	}
}

int screenY() {
	return g_gfx.surface[g_gfx.screen].src.y;
}

void setScreenY(int y) {
	g_gfx.surface[g_gfx.screen].src.y = y;
	gfxScreenDirty();
}

void shuusakuAnimLoadPalette(byte *src) {
	byte pal[256 * 4];
	for (uint i = 0; i < 236; i++) {
		pal[(10 + i) * 4 + 0] = src[i * 3 + 0];
		pal[(10 + i) * 4 + 1] = src[i * 3 + 1];
		pal[(10 + i) * 4 + 2] = src[i * 3 + 2];
	}
	shuusakuUpdatePalette(pal);
}

void loadImage(const char *name, int i, int xOff, int yOff) {
	Common::SharedPtr<CG> cg = assetCgLoad(name);
	if (!cg) {
		warning("Failed to load CG \"%s\"", name);
		return;
	}
	cg->x += xOff;
	cg->y += yOff;
	gfxDrawCG(i, *cg);
	cg->x -= xOff;
	cg->y -= yOff;
	if (!cg->palette.empty() && vmFlagIsOn(kFlagLoadPalette)) {
		memcpy(g_mem->palette, cg->palette.data(), MIN<uint>(cg->palette.size(), sizeof(g_mem->palette)));
	} else if (!cg->palette.empty()) {
		memcpy(extraPalette, cg->palette.data(), MIN<uint>(cg->palette.size(), sizeof(extraPalette)));
	}
	memSetSysvar16(kSysVarCgX, cg->x / 8);
	memSetSysvar16(kSysVarCgY, cg->y);
	memSetSysvar16(kSysVarCgW, cg->w / 8);
	memSetSysvar16(kSysVarCgH, cg->h);
}

void _loadImage(const char *name, int i) {
	Common::SharedPtr<CG> cg = assetCgLoad(name);
	if (!cg) {
		warning("Failed to load CG \"%s\"", name);
		return;
	}
	gfxDrawCG(i, *cg);
	if (!cg->palette.empty())
		gfxPaletteSet(cg->palette.data(), 0, 236);
}

void checkTime(int day, int t) {
	if (day != kDaySun && day != kDaySat && day != kDayMon)
		error("Invalid day: %d", day);
	int hour = t / 100;
	int minute = t % 100;
	if (hour > 23)
		error("Invalid time: %d", t);
	if (minute > 45 || minute % 15)
		error("Invalid time: %d", t);
}

void drawDatetime(int day, int t, int dstX, int dstY, int dstI) {
	// The date/time strip is composed from parts of surface 4.
	if (day == kDaySun) {
		gfxCopy(240, 256, 88, 48, 4, dstX, dstY, dstI);
	} else if (day == kDayMon) {
		gfxCopy(328, 240, 88, 48, 4, dstX, dstY, dstI);
	} else if (day == kDaySat) {
		gfxCopy(240, 240, 88, 48, 4, dstX, dstY, dstI);
	} else {
		error("Invalid day: %d", day);
	}

	int hour = t / 100;
	int minute = t % 100;
	if (hour < 12) {
		gfxCopy(192, 240, 24, 16, 4, dstX + 16, dstY + 16, dstI);
		gfxCopy(216, 256, 24, 16, 4, dstX + 32, dstY + 16, dstI);
	} else {
		gfxCopy(216, 240, 24, 16, 4, dstX + 16, dstY + 16, dstI);
		gfxCopy(192, 256, 24, 16, 4, dstX + 32, dstY + 16, dstI);
		if (hour > 12)
			hour -= 12;
	}

	gfxCopy(hour < 10 ? 172 : 164, 240, 4, 32, 4, dstX + 24, dstY + 16, dstI);
	gfxCopy((hour % 10) * 16, 240, 16, 32, 4, dstX + 28, dstY + 16, dstI);
	gfxCopy(176, 240, 8, 32, 4, dstX + 44, dstY + 16, dstI);
	gfxCopy((minute / 10) * 16, 240, 16, 32, 4, dstX + 52, dstY + 16, dstI);
	gfxCopy((minute % 10) * 16, 240, 16, 32, 4, dstX + 68, dstY + 16, dstI);
	gfxCopy(188, 240, 4, 32, 4, dstX + 84, dstY + 16, dstI);
}

void planTick() {
	if (!plan.flashEnabled || plan.i == 0)
		return;
	if (!vmTimerTickAsync(plan.flashTimer, 1000))
		return;
	if (plan.flashShown) {
		// restore the current date/time strip
		gfxCopy(0, 0, plan.i * 88, 48, 3, 0, 0, 0);
		plan.flashShown = false;
	} else {
		// show the planned date/time strip
		gfxCopy(0, 320, plan.i * 88, 48, 4, 0, 0, 0);
		plan.flashShown = true;
	}
}

void planFini() {
	gfxCopy(0, 272, 352, 48, 4, 0, 0, 0);
	gfxCopy(0, 416, 88, 48, 3, 0, 0, 0);
	plan.flashEnabled = false;
	shuusakuScheduleClearPlan();
}

void planInit() {
	plan.day = memGetVar32(4);
	plan.time = memGetVar32(19);
	plan.i = 0;
	plan.flashEnabled = true;
	checkTime(plan.day, plan.time);
	// save the current date/time strip
	gfxCopy(0, 0, 88, 48, 0, 0, 416, 3);
	// blank the plan strip on surface 4
	gfxCopy(0, 0, 352, 48, 3, 0, 272, 4);
	shuusakuScheduleSetPlanTime(plan.day, plan.time);
}

void planDrawDatetime() {
	if (!plan.flashEnabled)
		return;
	drawDatetime(plan.day, plan.time, plan.i * 88, 320, 4);
	plan.i++;
	if (plan.i > 1)
		gfxCopy(0, 320, plan.i * 88, 48, 4, 0, 0, 0);
	shuusakuScheduleSetPlanTime(plan.day, plan.time);
	plan.time += 15;
	if (plan.time % 100 == 60)
		plan.time += 40;
	// force the flash timer to expire immediately
	plan.flashTimer = vmTimerCreate() - 1001;
	plan.flashShown = false;
	planTick();
}

void planBack() {
	if (plan.i == 0)
		return;
	plan.i--;
	if (plan.time == 0) {
		if (plan.day == kDaySun)
			plan.day = kDaySat;
		else if (plan.day == kDayMon)
			plan.day = kDaySun;
		else
			error("Invalid plan day");
		plan.time = 2345;
	} else if (plan.time % 100 == 0) {
		plan.time -= 55;
	} else {
		plan.time -= 15;
	}
	// erase the erased date/time strip
	gfxCopy(plan.i * 88, 272, 88, 48, 4, plan.i * 88, 0, 0);
	shuusakuScheduleSetPlanTime(plan.day, plan.time);
}

void showHideMessageWindow(int show) {
	static const int kMsgWindowY = 371;
	if (show && !messageWindowShown) {
		int y = screenY() + kMsgWindowY;
		animPause(1);
		// save the background behind the window
		gfxCopy(0, y, 640, 74, 0, 0, 80, 4);
		// save the window frame
		gfxCopy(0, 80, 640, 74, 4, 0, 160, 4);
		// draw the window frame masked
		gfxCopyMasked(0, 0, 640, 74, 4, 0, 80, 4, kMaskColor);
		gfxCopy(0, 80, 640, 74, 4, 0, y, 0);
		messageWindowShown = true;
	} else if (!show && messageWindowShown) {
		int y = screenY() + kMsgWindowY;
		gfxCopy(0, 160, 640, 74, 4, 0, y, 0);
		animUnpause(1);
		messageWindowShown = false;
	}
}

// The engine's zoom transition grows the source rectangle in eight steps
// (32 ms each), matching the original's fixed eight 20 ms steps closely
// enough. The step deltas are kept for the scene viewer's zoom-out.
int zoomXStep, zoomYStep, zoomWStep, zoomHStep;

} // namespace

void shuusakuZoom(int x, int y, int w, int h, int srcI) {
	zoomXStep = x / kZoomSteps;
	zoomYStep = y / kZoomSteps;
	zoomWStep = (640 - w) / kZoomSteps;
	zoomHStep = (480 - h) / kZoomSteps;
	gfxZoom(x, y, w, h, srcI, 0, 256);
}

static void sceneViewerZoomOut() {
	// Copy of the zoomed scene; the background it zooms out to is
	// surface 8.
	byte *scene = new byte[640 * 480];
	memcpy(scene, gfxGetSurface(0)->pixels, 640 * 480);
	GfxSurface src = { scene, 640, 480, 640, 1 };
	GfxSurface *dst = gfxGetSurface(0);
	GfxRect full = { 0, 0, 640, 480 };

	GfxRect r = full;
	for (int i = 0; i < kZoomSteps - 1 && !g_engine->shouldQuit(); i++) {
		r.x += zoomXStep;
		r.y += zoomYStep;
		r.w -= zoomWStep;
		r.h -= zoomHStep;
		// restore the background around the shrinking scene
		gfxCopy(0, 0, 640, 480, 8, 0, 0, 0);
		gfxBlitScaled(&src, full, dst, r);
		gfxScreenDirty();
		vmPeek();
		vmDelay(20);
	}
	delete[] scene;

	gfxCopy(0, 0, 640, 480, 8, 0, 0, 0);
}

void shuusakuCamEventZoom(int cgX, int cgY, int cgW, int cgH) {
	// Draw an expanding white rectangle outline over the photo.
	const int kCamZoomOverlay = 2;
	GfxSurface *overlay = gfxGetOverlay(kCamZoomOverlay);
	gfxOverlayEnable(kCamZoomOverlay);

	int stepW = cgW / 16;
	int stepH = cgH / 16;
	GfxRect r = { cgX + cgW / 2, cgY + cgH / 2, 1, 1 };
	for (int i = 0; i < 16 && !g_engine->shouldQuit(); i++) {
		memset(overlay->pixels, 0, overlay->h * overlay->pitch);
		for (int x = r.x; x < r.x + r.w; x++) {
			for (int k = 0; k < 2; k++) {
				int y = r.y + k * (r.h - 1);
				if (x >= 0 && x < (int)overlay->w && y >= 0 && y < (int)overlay->h) {
					byte *p = overlay->pixels + y * overlay->pitch + x * 4;
					p[0] = p[1] = p[2] = p[3] = 0xff;
				}
			}
		}
		for (int y = r.y; y < r.y + r.h; y++) {
			for (int k = 0; k < 2; k++) {
				int x = r.x + k * (r.w - 1);
				if (x >= 0 && x < (int)overlay->w && y >= 0 && y < (int)overlay->h) {
					byte *p = overlay->pixels + y * overlay->pitch + x * 4;
					p[0] = p[1] = p[2] = p[3] = 0xff;
				}
			}
		}
		gfxScreenDirty();
		r.x -= stepW / 2;
		r.y -= stepH / 2;
		r.w += stepW;
		r.h += stepH;
		vmPeek();
		vmDelay(20);
	}

	gfxOverlayDisable(kCamZoomOverlay);
	memset(overlay->pixels, 0, overlay->h * overlay->pitch);
	gfxScreenDirty();
}

void shuusakuAfterMovieCrossfade() {
	// Fade the high colors to black.
	byte pal[256 * 4];
	memset(pal + 182 * 4, 0, 64 * 4);
	byte colors[64];
	for (int i = 0; i < 64; i++)
		colors[i] = 182 + i;
	gfxCrossfadeColors(pal, colors, 64, 64, nullptr, nullptr);
}

namespace {

//
// Movie (.mdd) decoder
//

struct Movie {
	uint32 nrFrames;
	int w, h;
	const byte *frame[100];
};

const int prevLineOffsets[16] = { -20, -16, -12, -8, -6, -4, -2, -1, 0, 1, 2, 4, 6, 8, 12, 16 };
const int sameLineOffsets[8] = { -1, -2, -4, -6, -8, -12, -16, -20 };

byte *decodeOffset(byte *dst, int stride, byte b) {
	int xOff, yOff;
	if (b & 0x70) {
		xOff = prevLineOffsets[b & 0xf];
		yOff = -((b >> 4) & 7);
	} else {
		xOff = sameLineOffsets[b & 7];
		yOff = 0;
	}
	return dst + stride * yOff + xOff;
}

void movieDrawFrame(Movie *mov, const byte *src) {
	GfxSurface *surf = gfxGetSurface(0);
	byte *dstBase = surf->pixels + kMovieY * surf->pitch + kMovieX;
	for (int row = 0; row < mov->h; row++) {
		byte *dst = dstBase + row * surf->pitch;
		for (int col = 0; col < mov->w;) {
			byte b = *src++;
			if (b & 0x80) {
				const byte *copySrc = decodeOffset(dst, surf->pitch, b);
				int len = *src++ + 2;
				memcpy(dst, copySrc, len);
				dst += len;
				col += len;
			} else {
				memcpy(dst, src, b);
				dst += b;
				src += b;
				col += b;
			}
		}
	}
	gfxDirty(0, kMovieX, kMovieY, mov->w, mov->h);
}

} // namespace

void shuusakuPlayMovie(const char *name) {
	Common::SeekableReadStream *stream = assetLoad(kAssetMovie, name);
	if (!stream) {
		warning("Failed to open movie \"%s\"", name);
		return;
	}
	Common::Array<byte> data(stream->size());
	stream->read(data.data(), data.size());
	delete stream;

	if (data.size() < 12 + 708 + 1) {
		warning("Invalid movie \"%s\"", name);
		return;
	}

	Movie mov;
	mov.nrFrames = READ_LE_UINT32(data.data());
	mov.w = READ_LE_UINT16(data.data() + 4);
	mov.h = READ_LE_UINT16(data.data() + 6);
	if (mov.nrFrames > 100 || kMovieX + mov.w > 640 || kMovieY + mov.h > 480) {
		warning("Invalid movie \"%s\"", name);
		return;
	}
	for (uint32 i = 0; i < mov.nrFrames; i++)
		mov.frame[i] = data.data() + 8 + mov.nrFrames * 4 + 708
			+ READ_LE_UINT32(data.data() + 8 + i * 4);

	// The palette is a run of 3-byte RGB entries used for colors 31..171
	// (stored at colors 41..181 of the game palette).
	const byte *pal = data.data() + 8 + mov.nrFrames * 4;
	for (int i = 31; i < 172; i++) {
		const byte *c = pal + i * 3;
		g_mem->palette[(10 + i) * 4 + 0] = c[2];
		g_mem->palette[(10 + i) * 4 + 1] = c[1];
		g_mem->palette[(10 + i) * 4 + 2] = c[0];
	}
	shuusakuUpdatePalette(g_mem->palette);

	VMTimer timer = vmTimerCreate();
	for (uint32 i = 0; i < mov.nrFrames && !g_engine->shouldQuit(); i++) {
		movieDrawFrame(&mov, mov.frame[i]);
		vmPeek();
		vmTimerTick(timer, 80);
	}
}

void shuusakuDrawText(const char *_text) {
	// The message-window backlog is fed by the VM reader; drawing only
	// renders the glyphs.
	int surface = memGetSysvar16(kSysVarDstSurface);
	int startX = memGetSysvar16(kSysVarTextStartX);
	int endX = startX + memGetSysvar16(kSysVarTextEndX);
	int lineSpace = memGetSysvar16(kSysVarLineSpace);
	int x = memGetSysvar16(kSysVarTextCursorX);
	int y = memGetSysvar16(kSysVarTextCursorY);

	const byte *text = (const byte *)_text;
	uint32 ch;
	while (*text) {
		if (text[0] == 0x81 && text[1] == 0x93) {
			// newline
			x = startX;
			y += lineSpace;
			text += 2;
			continue;
		}
		bool zenkaku = sjisIs2Byte(text[0]);
		int charSpace = zenkaku ? 2 : 1;
		if (x + charSpace > endX) {
			// wrap
			x = startX;
			y += lineSpace;
		}
		text = (const byte *)sjisNextChar((const char *)text, ch);
		gfxTextDrawGlyph(x * 8, y, surface, ch);
		x += charSpace;
	}

	memSetSysvar16(kSysVarTextCursorX, x);
	memSetSysvar16(kSysVarTextCursorY, y);
}

namespace {

void clearMessageWindow() {
	gfxCopy(0, 80, 640, 74, 4, 0, screenY() + 371, 0);
	memSetSysvar16(kSysVarTextCursorX, memGetSysvar16(kSysVarTextStartX));
	memSetSysvar16(kSysVarTextCursorY, memGetSysvar16(kSysVarTextStartY));
}

void toggleMessageWindow() {
	gfxCopySwap(0, 160, 640, 74, 4, 0, screenY() + 371, 0);
}

//
// Save data
//

void shuusakuSaveFlags(const char *name) {
	// Merge the flag region into the save file: keep the maximum of
	// each nibble outside the "cam event" range and OR inside it.
	// Note that savedataRead/Write address both the file and the
	// buffer with the same offset.
	byte save[kMem16Size] = {0};
	savedataRead(name, save, kVar4Off, kVar4Size + 100);
	byte *var4 = save + kVar4Off;
	for (int i = 0; i < kVar4Size; i++) {
		byte memFlags = memGetVar4(i);
		if (i < 1350 || i >= 1400) {
			if ((var4[i] & 0xf0) < (memFlags & 0xf0))
				var4[i] = (var4[i] & 0x0f) | (memFlags & 0xf0);
			if ((var4[i] & 0x0f) < (memFlags & 0x0f))
				var4[i] = (var4[i] & 0xf0) | (memFlags & 0x0f);
		} else {
			var4[i] |= memFlags;
		}
	}
	memcpy(save + kHeapOff, memRaw() + kHeapOff, 100);
	savedataWrite(name, save, kVar4Off, kVar4Size + 100);
}

//
// Statements
//

void stmtTxt() {
	if (vmFlagIsOn(kFlagLogEnable) && vmFlagIsOn(kFlagLogText)) {
		backlogPrepare();
		vmFlagOn(kFlagLog);
		backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtZenkaku]);
	}
	char str[kVMTxtBufSize];
	vmReadTextAiw(str, sizeof(str), g_game->mesTextTermNul ? 0x00 : 0xff);
	shuusakuDrawText(str);
	if (vmFlagIsOn(kFlagLogEnable) && vmFlagIsOn(kFlagLogText))
		vmFlagOff(kFlagLog);
}

// The Load and Save statements name their file after the slot number.
// The 2003 Kawarazaki re-release pads it to four digits (FLAG0100),
// the other games write the bare number (FLAG100).
static const char *saveSlotName(ParamList &params) {
	const uint slot = vmExprParam(params, 1);
	if (g_game->saveNameFourDigits)
		return sysSaveNameNo(slot);
	return sysSaveNameFmt("FLAG%u", slot);
}

void stmtLoad() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	const char *name = saveSlotName(params);

	switch (vmExprParam(params, 0)) {
	case 0:
		savedataRead(name, memRaw(), 0, kMem16Size);
		vmMesjmpAiw(memMesName());
		break;
	case 1:
		savedataLoad(name, 0);
		break;
	case 2:
		savedataRead(name, memRaw(), kVar4Off, kSysvar16Off - kVar4Off);
		break;
	case 3:
		savedataRead(name, memRaw(), kVar32Off, kVar16Off - kVar32Off);
		break;
	case 4:
		savedataRead(name, memRaw(), kVar16Off + vmExprParam(params, 2) * 2, 2);
		break;
	default:
		error("Load.function[%u] not implemented", params.params[0].val);
	}
}

void stmtSave() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	const char *name = saveSlotName(params);

	uint16 var = memGetVar16(116);
	memSetVar16(116, 0);
	switch (vmExprParam(params, 0)) {
	case 0:
		savedataWrite(name, memRaw(), 0, kMem16Size);
		break;
	case 1:
		savedataSave(name, 0);
		break;
	case 3:
		shuusakuSaveFlags(name);
		break;
	case 4:
		savedataWrite(name, memRaw(), kVar16Off + vmExprParam(params, 1) * 2, 2);
		break;
	default:
		error("Save.function[%u] not implemented", params.params[0].val);
	}
	memSetVar16(116, var);
}

void stmtMenuexec() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);

	shuusakuStatusUpdate();
	int no = vmExprParam(params, 0);
	if ((uint)no >= kAiwMaxMenus)
		error("Invalid menu index: %u", no);
	int mode = vmExprParam(params, 1);

	uint32 savedIp = g_vm->ip.ptr;
	ShuusakuMenuEntry entries[100];
	int nr = 0;
	for (int i = 0; i < g_aiwMenuNrEntries[no]; i++) {
		uint32 condAddr = g_aiwMenuEntries[no][i].condAddr;
		if (condAddr) {
			g_vm->ip.ptr = condAddr;
			if (!g_game->vm.eval())
				continue;
		}
		entries[nr].bodyAddr = g_aiwMenuEntries[no][i].bodyAddr;
		entries[nr].index = i + 1;
		nr++;
	}

	g_game->flags[kFlagAnimEnable] = 0;
	int selected = shuusakuMenuexec(entries, nr, mode);
	g_game->flags[kFlagAnimEnable] = kFlagAlwaysOn;

	memSetVar32(21, selected);
	g_vm->ip.ptr = savedIp;
}

void stmtDisplayNumber() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	shuusakuDrawText(sysNumberToStringDigits(vmExprParam(params, 0), 0, false));
}

void stmtSetTextColor() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	int c = vmExprParam(params, 0);
	memSetSysvar16(kSysVarBgColor, c);
	gfxTextSetColors((c >> 8) + 10, (c & 0xff) + 10);
}

void stmtWait() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	backlogCommit();

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
		uint32 target = timer + params.params[0].val * 10;
		while (timer < target && !inputDown(kInputShift) && !g_engine->shouldQuit()) {
			vmPeek();
			vmTimerTick(timer, MIN<uint32>(target - timer, 8));
		}
	}
}

void stmtTextClear() {
	int x = memGetSysvar16(kSysVarTextStartX);
	int y = memGetSysvar16(kSysVarTextStartY);
	int endX = memGetSysvar16(kSysVarTextEndX);
	int endY = memGetSysvar16(kSysVarTextEndY);
	int w = (endX - x) + 1;
	int h = (endY - y) + 1;
	int dst = memGetSysvar16(kSysVarDstSurface);
	int c = (memGetSysvar16(kSysVarBgColor) >> 8) + 10;
	gfxFill(x * 8, y, w * 8, h, dst, c);
	memSetSysvar16(kSysVarTextCursorX, x);
	memSetSysvar16(kSysVarTextCursorY, y);
}

void stmtCommitMessage() {
	backlogCommit();
	bool hidden = false;
	while (!g_engine->shouldQuit()) {
		if (!hidden) {
			if (inputDown(kInputActivate)) {
				inputWaitUntilUp(kInputActivate);
				clearMessageWindow();
				break;
			}
			if (inputDown(kInputCtrl)) {
				vmDelay(16);
				clearMessageWindow();
				break;
			}
		}
		if (inputDown(kInputCancel)) {
			inputWaitUntilUp(kInputCancel);
			toggleMessageWindow();
			if (!hidden) {
				animUnpause(1);
				hidden = true;
			} else {
				animPause(1);
				hidden = false;
			}
		}
		vmPeek();
		vmDelay(16);
	}
	audioVoiceStop(0);
}

void stmtLoadImage() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	int x = 0, y = 0;
	if (params.nrParams > 1) {
		x = vmExprParam(params, 1) * 8;
		y = vmExprParam(params, 2);
	}
	loadImage(vmStringParam(params, 0), memGetSysvar16(kSysVarDstSurface), x, y);
}

void stmtSurfaceCopy() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	int srcX = vmExprParam(params, 0) * 8;
	int srcY = vmExprParam(params, 1);
	int srcW = (vmExprParam(params, 2) * 8 - srcX) + 8;
	int srcH = (vmExprParam(params, 3) - srcY) + 1;
	int srcI = vmExprParam(params, 4);
	int dstX = vmExprParam(params, 5) * 8;
	int dstY = vmExprParam(params, 6);
	int dstI = vmExprParam(params, 7);
	gfxCopy(srcX, srcY, srcW, srcH, srcI, dstX, dstY, dstI);
}

void stmtSurfaceCopyMasked() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	int srcX = vmExprParam(params, 0) * 8;
	int srcY = vmExprParam(params, 1);
	int srcW = (vmExprParam(params, 2) * 8 - srcX) + 8;
	int srcH = (vmExprParam(params, 3) - srcY) + 1;
	int srcI = vmExprParam(params, 4);
	int dstX = vmExprParam(params, 5) * 8;
	int dstY = vmExprParam(params, 6);
	int dstI = vmExprParam(params, 7);
	gfxCopyMasked(srcX, srcY, srcW, srcH, srcI, dstX, dstY, dstI, kMaskColor);
}

void stmtSurfaceFill() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	int x = vmExprParam(params, 0) * 8;
	int y = vmExprParam(params, 1);
	int w = (vmExprParam(params, 2) * 8 - x) + 8;
	int h = (vmExprParam(params, 3) - y) + 1;
	int dst = memGetSysvar16(kSysVarDstSurface);
	int c = (memGetSysvar16(kSysVarBgColor) >> 8) + 10;
	gfxFill(x, y, w, h, dst, c);
}

void stmtSetColor() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	int i = vmExprParam(params, 0);
	if (i > 15 && i < 32)
		return;
	g_mem->palette[(10 + i) * 4 + 0] = vmExprParam(params, 3);
	g_mem->palette[(10 + i) * 4 + 1] = vmExprParam(params, 2);
	g_mem->palette[(10 + i) * 4 + 2] = vmExprParam(params, 1);
	g_mem->palette[(10 + i) * 4 + 3] = 1;
}

void stmtShowHide() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	if (params.nrParams > 0) {
		byte pal[256 * 4];
		fillPalette(pal, vmExprParam(params, 0), vmExprParam(params, 1), vmExprParam(params, 2));
		shuusakuUpdatePalette(pal);
	} else {
		shuusakuUpdatePalette(g_mem->palette);
	}
}

void stmtCrossfade() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	if (params.nrParams > 0) {
		shuusakuCrossfadeTo(vmExprParam(params, 0), vmExprParam(params, 1), vmExprParam(params, 2));
	} else {
		shuusakuCrossfade(g_mem->palette, false);
	}
}

void stmtCrossfade2() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	if (params.nrParams > 0) {
		byte pal[256 * 4];
		fillPalette(pal, vmExprParam(params, 0), vmExprParam(params, 1), vmExprParam(params, 2));
		shuusakuCrossfade2(pal);
	} else {
		shuusakuCrossfade2(g_mem->palette);
	}
}

//
// Anim / audio / movie statements
//

void loadAnim(const char *str, int no) {
	vmLoadDataFile(str, kAnimOffset + (no ? kAnimSize : 0));
	for (int i = 0; i < 10; i++)
		animHalt(no ? i + 10 : i);
}

void animStartSync(int no) {
	animStart(no);
	while (animStreamRunning(no) && !g_engine->shouldQuit())
		vmPeek();
}

void stmtAnim() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);

	if (params.nrParams == 0)
		error("Anim.function[]");
	if (params.params[0].type == kMesParamString) {
		loadAnim(params.params[0].str, params.nrParams > 1 ? vmExprParam(params, 1) : 0);
		return;
	}
	switch (vmExprParam(params, 0)) {
	case 0:
		animInitStreamFrom(1, vmExprParam(params, 2), kAnimOffset);
		break;
	case 1:
		animStart(1);
		break;
	case 2:
		animPauseSync(1);
		break;
	case 3:
		animStartSync(1);
		break;
	case 4:
		animHalt(1);
		break;
	case 6:
		animUnpauseRange(0, 10);
		break;
	case 7:
		animPauseRangeSync(0, 10);
		break;
	case 8:
		animUnpause(1);
		break;
	case 10:
	case 26: {
		// TODO: Anim.function 10 and 26 are unimplemented. They mirror
		//       each other the way the 0-4 calls mirror 16-20, i.e. one
		//       per animation stream, but libai5's shuusaku table
		//       leaves both unset, so their behaviour is unknown;
		//       accept and skip them for now.
		static bool warned = false;
		if (!warned) {
			warned = true;
			warning("Anim.function[%u] is not implemented",
					params.params[0].val);
		}
		break;
	}
	case 16:
		animInitStreamFrom(11, vmExprParam(params, 2), kAnimOffset + kAnimSize);
		break;
	case 17:
		animStart(11);
		break;
	case 18:
		animPauseSync(11);
		break;
	case 19:
		animStartSync(11);
		break;
	case 20:
		animHalt(11);
		break;
	default:
		error("Anim.function[%u]", params.params[0].val);
	}
}

void stmtLoadAudio() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	Common::strlcpy(bgmFile, vmStringParam(params, 0), sizeof(bgmFile));
}

void stmtLoadEffect() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	Common::strlcpy(seFile, vmStringParam(params, 0), sizeof(seFile));
}

void stmtLoadVoice() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	audioVoicePlay(vmStringParam(params, 0), 0);
}

void stmtAudio() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);

	switch (vmExprParam(params, 0)) {
	case 0:
		audioBgmPlay(bgmFile, false);
		break;
	case 1:
		audioStop(kAudioChBgm);
		break;
	case 3: {
		int t = 1500 * (vmExprParam(params, 1) + 1);
		audioFade(kAudioChBgm, kAudioVolumeMin, t, true, false);
		break;
	}
	case 5: {
		int vol = vmExprParam(params, 1) * 50 + kAudioVolumeMin;
		audioSetVolume(kAudioChBgm, bgmVol + vol);
		break;
	}
	case 16:
		audioSePlay(seFile, 0);
		break;
	case 17:
		audioSeStop(0);
		break;
	case 19: {
		int t = 1500 * (vmExprParam(params, 1) + 1);
		audioSeFade(kAudioVolumeMin, t, true, false, 0);
		break;
	}
	case 20: {
		int t = 1500 * (vmExprParam(params, 1) + 1);
		audioSeFade(kAudioVolumeMin, t, true, true, 0);
		break;
	}
	case 22:
		while (audioIsPlaying(kAudioChSe0) && !g_engine->shouldQuit()) {
			vmPeek();
			vmDelay(16);
		}
		break;
	default:
		error("Audio.function[%u]", params.params[0].val);
	}
}

void stmtPlayMovie() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	shuusakuPlayMovie(vmStringParam(params, 0));
}

//
// Utils
//

void utilPixelCrossfade(ParamList &params) {
	int src = vmExprParam(params, 1);
	int x = vmExprParam(params, 2);
	int y = vmExprParam(params, 3);
	int w = (vmExprParam(params, 4) - x) + 1;
	int h = (vmExprParam(params, 5) - y) + 1;
	gfxPixelCrossfade(x * 8, y, w * 8, h, src, x * 8, y, 0, 20, crossfadeTick, nullptr);
}

void utilPlan(ParamList &params) {
	switch (vmExprParam(params, 1)) {
	case 0:
		planFini();
		break;
	case 1:
		planInit();
		break;
	case 2:
		planDrawDatetime();
		break;
	case 3:
		planBack();
		break;
	default:
		error("Util.plan[%u]", params.params[1].val);
	}
}

void utilShowHideMessageWindow(ParamList &params) {
	showHideMessageWindow(vmExprParam(params, 1));
}

void utilUpdateSchedule(ParamList &params) {
	int loc = vmExprParam(params, 1);
	if (loc != 0xff && loc > 10)
		error("Invalid location: %d", loc);
	if (loc != 0xff) {
		int day = memGetVar32(4);
		int t = memGetVar32(19);
		int flag = vmExprParam(params, 2);
		shuusakuScheduleSetFlag(loc, day, t, flag);
	}
	shuusakuScheduleUpdate();
}

void utilLoadExtraPalette(ParamList &) {
	memcpy(g_mem->palette + 42 * 4, extraPalette + 42 * 4, 96 * 4);
}

void utilPhotoSlide(ParamList &params) {
	int dir = vmExprParam(params, 1);
	if (dir) {
		// slide out
		for (int x = 0; x > -408; x -= 68) {
			gfxCopy(0, 56, 408, 312, 1, 0, 56, 0);
			gfxCopyMasked(0, 0, 408, 312, 2, x, 56, 0, kMaskColor);
			vmPeek();
			vmDelay(10);
		}
	} else {
		// slide in
		for (int x = -340; x <= 0; x += 68) {
			gfxCopy(0, 56, 408, 312, 1, 0, 56, 0);
			gfxCopyMasked(0, 0, 408, 312, 2, x, 56, 0, kMaskColor);
			vmPeek();
			vmDelay(10);
		}
	}
}

void utilStatusDirty(ParamList &) {
	shuusakuStatusUpdate();
}

struct PixelCrossfadeData {
	GfxColor oldPal[236];
	GfxColor newPal[236];
};

byte u8InterpLocal(byte from, byte to, float rate) {
	return from + (byte)((to - from) * rate);
}

bool pixelAndPaletteCrossfadeTick(float t, void *data) {
	PixelCrossfadeData *d = (PixelCrossfadeData *)data;
	if (memGetSysvar16(kSysvarSkipFlag) && inputDown(kInputShift)) {
		// snap to the target palette
		for (int i = 0; i < 236; i++) {
			if (i > 15 && i < 32)
				continue;
			g_gfx.palette[10 + i] = d->newPal[i];
		}
		gfxUpdatePalette(10, 16);
		gfxUpdatePalette(42, 204);
		return false;
	}
	for (int i = 0; i < 236; i++) {
		if (i > 15 && i < 32)
			continue;
		g_gfx.palette[10 + i].r = u8InterpLocal(d->oldPal[i].r, d->newPal[i].r, t);
		g_gfx.palette[10 + i].g = u8InterpLocal(d->oldPal[i].g, d->newPal[i].g, t);
		g_gfx.palette[10 + i].b = u8InterpLocal(d->oldPal[i].b, d->newPal[i].b, t);
	}
	return true;
}

void utilPixelAndPaletteCrossfade(ParamList &params) {
	int src = vmExprParam(params, 1);
	int x = vmExprParam(params, 2);
	int y = vmExprParam(params, 3);
	int w = (vmExprParam(params, 4) - x) + 1;
	int h = (vmExprParam(params, 5) - y) + 1;

	PixelCrossfadeData data;
	for (int i = 0; i < 236; i++) {
		data.oldPal[i] = g_gfx.palette[10 + i];
		const byte *c = g_mem->palette + (10 + i) * 4;
		data.newPal[i].r = c[2];
		data.newPal[i].g = c[1];
		data.newPal[i].b = c[0];
		data.newPal[i].a = 255;
	}
	gfxPixelCrossfade(x * 8, y, w * 8, h, src, x * 8, y, 0, 20,
			pixelAndPaletteCrossfadeTick, &data);
}

void utilDrawDatetime(ParamList &params) {
	int t = vmExprParam(params, 1);
	int day = vmExprParam(params, 2);
	int dst = vmExprParam(params, 3);
	gfxCopy(0, 0, 88, 48, 0, 0, 368, 4);
	drawDatetime(day, t, 0, 0, dst);
}

void utilZoomMovie(ParamList &) {
	shuusakuZoom(kMovieX, kMovieY, 320, 240, 1);
}

void utilSceneViewerZoomOut(ParamList &) {
	sceneViewerZoomOut();
}

void utilClearHighColors(ParamList &) {
	shuusakuAfterMovieCrossfade();
}

void utilEndingPixelCrossfadeSlow(ParamList &) {
	gfxPixelCrossfade(0, 0, 640, 480, 1, 0, 0, 0, 600, crossfadeTick, nullptr);
}

void utilEndingPixelCrossfade(ParamList &) {
	gfxPixelCrossfade(0, 0, 640, 480, 1, 0, 480, 0, 20, crossfadeTick, nullptr);
}

void utilSceneViewerCharSelect(ParamList &) {
	memSetVar32(22, shuusakuSceneViewerCharSelect());
}

void utilSceneViewerSceneSelect(ParamList &params) {
	bool needBg = vmExprParam(params, 1);
	memSetVar32(22, shuusakuSceneViewerSceneSelect(memGetVar32(22) - 1, needBg));
}

void utilNameInput(ParamList &) {
	shuusakuNameInputScreen(memRaw() + kHeapOff, memRaw() + kHeapOff + 16);
}

void readName(const byte *src, char *dst) {
	// 16 bytes, 0xff terminated
	int i;
	for (i = 0; i < 16 && src[i] != 0xff; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

void utilDrawMyouji(ParamList &) {
	char name[17];
	readName(memRaw() + kHeapOff, name);
	shuusakuDrawText(name);
}

void utilDrawNamae(ParamList &) {
	char name[17];
	readName(memRaw() + kHeapOff + 16, name);
	shuusakuDrawText(name);
}

void drawCreditText(const char *text, int y, int fg, int size) {
	gfxTextSetSize(size, 1);
	gfxTextSetColors(g_gfx.text.bg, fg);
	g_textShadow = kTextShadowNone;
	int x = 9;
	const byte *p = (const byte *)text;
	uint32 ch;
	while (*p) {
		bool zenkaku = sjisIs2Byte(p[0]);
		int charSpace = size / (zenkaku ? 1 : 2);
		p = (const byte *)sjisNextChar((const char *)p, ch);
		gfxTextDrawGlyph(x, y, 0, ch);
		x += charSpace;
	}
	gfxTextSetSize(16, 1);
	g_textShadow = kTextShadowB;
}

void utilCredits(ParamList &params) {
	// Staff-roll lines (Shift-JIS).
	static const char header[] =
		"\x8A\xE9\x89\xE6\x81\x45\x83\x56\x83\x69\x83\x8A\x83\x49"
		"\x81\x45\x83\x51\x81\x5B\x83\x80\x83\x66\x83\x55\x83\x43\x83\x93";
	static const char eri[] = "\x8D\x82\x95\x94\x20\x8A\x47\x97\xA2";

	switch (vmExprParam(params, 1)) {
	case 0:
		drawCreditText(header, 188, 14, 24);
		break;
	case 2: {
		char myouji[17], namae[17];
		readName(memRaw() + kHeapOff, myouji);
		readName(memRaw() + kHeapOff + 16, namae);
		char name[34];
		Common::strlcpy(name, myouji, sizeof(name));
		Common::strlcat(name, " ", sizeof(name));
		Common::strlcat(name, namae, sizeof(name));
		drawCreditText(name, 252, 22, 32);
		break;
	}
	default:
		drawCreditText(eri, 292, 18, 32);
		break;
	}
}

void utilScrollDown(ParamList &params) {
	int target = vmExprParam(params, 1);
	for (int y = screenY() + 4; y <= target; y += 4) {
		setScreenY(y);
		vmPeek();
		vmDelay(20);
	}
}

void utilPixelDrop(ParamList &) {
	// Per-pixel falling-rain effect from surface 1 to surface 0.
	struct PixelDrop {
		int16 y;
		int16 velocity;
	};
	PixelDrop *drops = new PixelDrop[640 * 480];

	uint32 seed = 1;
	for (int row = 0; row < 480; row++) {
		for (int col = 0; col < 640; col++) {
			PixelDrop *p = &drops[row * 640 + col];
			seed = seed * 5723 + 1;
			int v = seed % 43;
			if (v < 17)
				v = 0;
			p->y = row;
			p->velocity = v;
		}
	}

	GfxSurface *dst = gfxGetSurface(0);
	GfxSurface *fg = gfxGetSurface(1);
	bool freezeLow = memGetSysvar16(kSysvarPalFrozen);

	int mod = 11;
	for (int frame = 0; frame < 16; frame++) {
		mod += 2;
		gfxCopy(0, 0, 640, 480, 3, 0, 0, 0);
		for (int row = 0; row < 480; row++) {
			for (int col = 0; col < 640; col++) {
				PixelDrop *p = &drops[row * 640 + col];
				if (p->y >= 480)
					continue;
				byte *fgP = fg->pixels + row * fg->pitch + col;
				if (*fgP == kMaskColor)
					continue;
				byte *dstP = dst->pixels + p->y * dst->pitch + col;
				*dstP = *fgP;
				seed = seed * 5723 + 1;
				int v = seed % mod;
				if (v < 7)
					v = 0;
				p->y += p->velocity;
				p->velocity = v;
			}
		}
		for (int i = 0; i < 236; i++) {
			if (freezeLow && i < 16)
				continue;
			if (i > 15 && i < 32)
				continue;
			GfxColor c = g_gfx.palette[10 + i];
			c.r = MIN<byte>(c.r + 16, 255);
			c.g = MIN<byte>(c.g + 16, 255);
			c.b = MIN<byte>(c.b + 16, 255);
			g_gfx.palette[10 + i] = c;
		}
		gfxUpdatePalette(0, 246);
		vmPeek();
		vmDelay(30);
	}
	delete[] drops;
}

void utilAnimWait(ParamList &params) {
	int which = vmExprParam(params, 1);
	int slot = vmExprParam(params, 2) + (which ? 10 : 0);
	while (!g_engine->shouldQuit()) {
		if (inputDown(kInputActivate) || inputDown(kInputCancel)) {
			memSetVar32(18, 1);
			return;
		}
		if (animGetState(slot) == kAnimStateHalted) {
			memSetVar32(18, 0);
			return;
		}
		if (inputDown(kInputCtrl)) {
			memSetVar32(18, 1);
			return;
		}
		vmPeek();
	}
}

void utilSetYOffset(ParamList &params) {
	setScreenY(vmExprParam(params, 1));
}

void utilQuake(ParamList &params) {
	int nr = vmExprParam(params, 1);
	int curY = screenY();
	int quakeY = curY - nr;
	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < nr; i++, quakeY++) {
		setScreenY(quakeY);
		gfxUpdate();
		vmPeek();
		vmTimerTick(timer, 20);
		setScreenY(curY);
		gfxUpdate();
		vmPeek();
		vmTimerTick(timer, 20);
	}
}

void utilEndingCrossfade(ParamList &) {
	shuusakuCrossfade2(g_mem->palette);
}

void utilCamEventZoom(ParamList &params) {
	shuusakuCamEventZoom(vmExprParam(params, 1) * 8, vmExprParam(params, 2),
			vmExprParam(params, 3) * 8, vmExprParam(params, 4));
}

} // namespace

void shuusakuUpdatePalette(const byte *pal) {
	if (!memGetSysvar16(kSysvarPalFrozen))
		gfxPaletteSet(pal + 10 * 4, 10, 16);
	gfxPaletteSet(pal + 42 * 4, 42, 204);
}

void shuusakuCrossfade(const byte *pal, bool allow1632) {
	byte colors[256];
	int nr = 0;
	bool freezeLow = memGetSysvar16(kSysvarPalFrozen);
	if (!freezeLow) {
		for (int i = 10; i < 26; i++)
			colors[nr++] = i;
	}
	if (allow1632) {
		for (int i = 26; i < 42; i++)
			colors[nr++] = i;
	}
	for (int i = 42; i < 246; i++)
		colors[nr++] = i;

	int ms = memGetSysvar16(kSysvarFadeMs) * 16;
	gfxCrossfadeColors(const_cast<byte *>(pal), colors, nr, ms, crossfadeTick, nullptr);
}

void shuusakuCrossfadeTo(int r, int g, int b) {
	byte pal[256 * 4];
	fillPalette(pal, r, g, b);
	shuusakuCrossfade(pal, false);
}

void shuusakuCrossfade2(const byte *pal) {
	GfxColor newPal[236];
	for (int i = 0; i < 236; i++) {
		const byte *c = pal + (10 + i) * 4;
		newPal[i].b = c[0];
		newPal[i].g = c[1];
		newPal[i].r = c[2];
		newPal[i].a = 255;
	}

	int ms = memGetSysvar16(kSysvarFadeMs);
	bool freezeLow = memGetSysvar16(kSysvarPalFrozen);
	for (int iteration = 0; iteration < 256; iteration++) {
		for (int i = 0; i < 236; i++) {
			if (freezeLow && i < 16)
				continue;
			if (i > 15 && i < 22)
				continue;
			GfxColor c = g_gfx.palette[10 + i];
			if (c.r < newPal[i].r)
				c.r++;
			else if (c.r > newPal[i].r)
				c.r--;
			if (c.g < newPal[i].g)
				c.g++;
			else if (c.g > newPal[i].g)
				c.g--;
			if (c.b < newPal[i].b)
				c.b++;
			else if (c.b > newPal[i].b)
				c.b--;
			g_gfx.palette[10 + i] = c;
		}
		gfxUpdatePalette(0, 246);
		vmPeek();
		vmDelay(ms);
		if (!crossfadeTick(0, nullptr)) {
			for (int i = 0; i < 236; i++) {
				if (freezeLow && i < 16)
					continue;
				if (i > 15 && i < 22)
					continue;
				g_gfx.palette[10 + i] = newPal[i];
			}
			gfxUpdatePalette(0, 246);
			break;
		}
	}
}

namespace {

//
// Context menu
//

void scheduleMenuClicked() {
	shuusakuScheduleWindowToggle();
}

void statusMenuClicked() {
	shuusakuStatusWindowToggle();
}

void quitGameClicked() {
	g_engine->quitGame();
}

void openContextMenu() {
	PopupMenu m;
	m.appendEntry(1, "Schedule", "Space", scheduleMenuClicked);
	m.appendEntry(2, "Items & Status", "F1", statusMenuClicked);
	m.appendSeparator();
	m.appendEntry(-1, "Quit Game", "Alt+F4", quitGameClicked);
	m.appendSeparator();
	m.appendEntry(-1, "Cancel", nullptr, nullptr);

	uint x, y;
	cursorGetPos(x, y);
	m.run(x, y);
}

} // namespace

bool shuusakuHandleEvent(const Common::Event &e) {
	if (shuusakuScheduleWindowEvent(e))
		return true;
	if (shuusakuStatusWindowEvent(e))
		return true;

	switch (e.type) {
	case Common::EVENT_KEYDOWN:
		if (e.kbd.keycode == Common::KEYCODE_SPACE) {
			shuusakuScheduleWindowToggle();
			return true;
		}
		if (e.kbd.keycode == Common::KEYCODE_F1) {
			shuusakuStatusWindowToggle();
			return true;
		}
		break;
	case Common::EVENT_MBUTTONDOWN:
		openContextMenu();
		return true;
	default:
		break;
	}
	return false;
}

namespace {

void shuusakuMemInit() {
	g_memPtr.mesName = memRaw();
	g_memPtr.var4 = memRaw() + kVar4Off;
	g_memPtr.systemVar16Ptr = memRaw(); // dummy pointer (kept for parity)
	g_memPtr.var16 = memRaw() + kVar16Off;
	g_memPtr.systemVar16 = memRaw() + kSysvar16Off;
	g_memPtr.var32 = memRaw() + kVar32Off;
	g_memPtr.systemVar32 = memRaw(); // dummy pointer (kept for parity)

	memSetSysvar16(kSysVarTextEndX, 79);
	memSetSysvar16(kSysVarTextEndY, 479);
	memSetSysvar16(kSysVarBgColor, 0xff);
	memSetSysvar16(kSysvarFadeMs, 32);
	memSetSysvar16(kSysVarLineSpace, 18);
	memSetSysvar16(kSysvarWinDay, 0xffff);
	memSetSysvar16(kSysvarWinTime, 0xffff);
}

void unprefixedError() {
	error("Unprefixed text");
}

void shuusakuMemRestore() {
	// The original leaves this empty.
}

int initVolume(int *value, AudioChannel ch) {
	int val = *value;
	if (val < 0 || val > 191)
		val = 191;
	int vol = -6000 + (val * 6000 + 95) / 191;
	audioSetVolume(ch, vol);
	return vol;
}

} // namespace

void shuusakuInit() {
	shuusakuScheduleWindowInit();
	shuusakuStatusInit();
	bgmVol = initVolume(&g_config.volume.music, kAudioChBgm);
	initVolume(&g_config.volume.se, kAudioChSe0);
	initVolume(&g_config.volume.voice, kAudioChVoice0);
	memSetSysvar16(kSysvarKettei, g_config.kettei ? 1 : 0);
	g_assetEffectIsBgm = false;
	g_textShadow = kTextShadowB;
	gfxTextSetSize(16, 1);
	g_animFrameT = 20;
	g_animLoadPalette = shuusakuAnimLoadPalette;
	_loadImage("selwaku.gpx", 5);
	_loadImage("bll.gpx", 0);
}

void shuusakuUpdate() {
	if (g_shuusakuRunningCamEvent)
		return;
	planTick();
	shuusakuScheduleTick();
}

namespace {

const struct { uint16 w, h; } surfaceSizes[] = {
	{ 640, 1200 },
	{ 640, 560 },
	{ 640, 480 },
	{ 640, 480 },
	{ 640, 480 },
	{ 640, 1200 },
	{ 640, 2040 },
	{ 640, 1480 },
	{ 640, 480 }
};

} // namespace

void initGameShuusaku(Game &g) {
	g.id = GID_SHUUSAKU;
	g.view.w = 640;
	g.view.h = 480;
	for (uint i = 0; i < ARRAYSIZE(surfaceSizes); i++) {
		g.surfaceSizes[i].w = surfaceSizes[i].w;
		g.surfaceSizes[i].h = surfaceSizes[i].h;
	}
	g.bpp = 8;
	g.var4Size = kVar4Size;
	g.mem16Size = kMem16Size;

	g.init = shuusakuInit;
	g.update = shuusakuUpdate;
	g.handleEvent = shuusakuHandleEvent;
	g.memInit = shuusakuMemInit;
	g.memRestore = shuusakuMemRestore;
	g.unprefixedZen = unprefixedError;
	g.unprefixedHan = unprefixedError;
	g.drawTextZen = nullptr; // draw via stmtTxt
	g.drawTextHan = nullptr;

	g.vm.exec = vmExecAiw;
	g.vm.eval = vmEvalAiw;
	g.vm.readParams = vmReadParamsAiw;
	g.vm.endCode = 0xff;
	vmSetDefaultOps(g);

	// AIW expression opcodes.
	g.exprOp[0xe1] = vmExprMinusUnsigned;
	g.exprOp[0xe5] = vmExprRandWithImmRange;
	g.exprOp[0xf3] = vmExprCflagPacked;
	g.exprOp[0xf4] = vmExprEflagPacked;
	g.exprOp[0xf6] = vmExprVar16Const16;
	g.exprOp[0xf7] = vmExprVar16Expr;
	g.exprOp[0xf8] = vmExprSysvar16Const16;
	g.exprOp[0xf9] = vmExprSysvar16Expr;

	// Statement opcodes.
	g.stmtOp[0x00] = stmtTxt;
	g.stmtOp[0x01] = vmStmtJmp;
	g.stmtOp[0x02] = vmStmtUtil;
	g.stmtOp[0x03] = vmStmtMesjmpAiw;
	g.stmtOp[0x04] = vmStmtMescallAiw;
	g.stmtOp[0x05] = vmStmtSetFlagConst16Aiw;
	g.stmtOp[0x06] = vmStmtSetFlagExprAiw;
	g.stmtOp[0x07] = vmStmtSetVar32Const8Aiw;
	g.stmtOp[0x0a] = vmStmtSetVar16Const16Aiw;
	g.stmtOp[0x0b] = vmStmtSetVar16ExprAiw;
	g.stmtOp[0x0c] = vmStmtSetSysvar16Const16Aiw;
	g.stmtOp[0x0d] = vmStmtSetSysvar16ExprAiw;
	g.stmtOp[0x0e] = stmtLoad;
	g.stmtOp[0x0f] = stmtSave;
	g.stmtOp[0x10] = vmStmtJz;
	g.stmtOp[0x11] = vmStmtDefproc;
	g.stmtOp[0x12] = vmStmtCall;
	g.stmtOp[0x13] = vmStmtDefmenuAiw;
	g.stmtOp[0x14] = stmtMenuexec;
	g.stmtOp[0x15] = stmtDisplayNumber;
	g.stmtOp[0x16] = stmtSetTextColor;
	g.stmtOp[0x20] = stmtWait;
	g.stmtOp[0x21] = stmtTextClear;
	g.stmtOp[0x22] = stmtCommitMessage;
	g.stmtOp[0x23] = stmtLoadImage;
	g.stmtOp[0x24] = stmtSurfaceCopy;
	g.stmtOp[0x25] = stmtSurfaceCopyMasked;
	g.stmtOp[0x27] = stmtSurfaceFill;
	g.stmtOp[0x29] = stmtSetColor;
	g.stmtOp[0x2a] = stmtShowHide;
	g.stmtOp[0x2b] = stmtCrossfade;
	g.stmtOp[0x2c] = stmtCrossfade2;
	g.stmtOp[0x2e] = stmtAnim;
	g.stmtOp[0x2f] = stmtLoadAudio;
	g.stmtOp[0x30] = stmtLoadEffect;
	g.stmtOp[0x31] = stmtLoadVoice;
	g.stmtOp[0x32] = stmtAudio;
	g.stmtOp[0x33] = stmtPlayMovie;

	// Util functions.
	g.util[0] = utilPixelCrossfade;
	g.util[1] = utilPlan;
	g.util[2] = utilShowHideMessageWindow;
	g.util[3] = utilUpdateSchedule;
	g.util[5] = utilLoadExtraPalette;
	g.util[6] = utilPhotoSlide;
	g.util[7] = utilStatusDirty;
	g.util[8] = utilPixelAndPaletteCrossfade;
	g.util[9] = utilDrawDatetime;
	g.util[10] = utilZoomMovie;
	g.util[11] = utilCamEventZoom;
	g.util[12] = utilClearHighColors;
	g.util[13] = utilEndingPixelCrossfadeSlow;
	g.util[14] = utilSceneViewerCharSelect;
	g.util[15] = utilSceneViewerSceneSelect;
	g.util[17] = utilSceneViewerZoomOut;
	g.util[18] = utilNameInput;
	g.util[19] = utilDrawMyouji;
	g.util[20] = utilDrawNamae;
	g.util[21] = utilCredits;
	g.util[22] = utilScrollDown;
	g.util[23] = utilPixelDrop;
	g.util[24] = utilAnimWait;
	g.util[25] = utilSetYOffset;
	g.util[26] = utilQuake;
	g.util[27] = utilEndingCrossfade;
	g.util[29] = utilEndingPixelCrossfade;
	g.util[30] = utilNoop; // set_config_enabled

	// No syscall functions.
	for (int i = 0; i < 256; i++)
		g.sys[i] = nullptr;

	g.flags[kFlagAnimEnable] = kFlagAlwaysOn;
	g.flags[kFlagVoiceEnable] = kFlagAlwaysOn;
	g.flags[kFlagAudioEnable] = kFlagAlwaysOn;
	g.flags[kFlagWaitKeyup] = kFlagAlwaysOn;
	g.flags[kFlagLoadPalette] = 0x0001;
}

} // namespace AI5
