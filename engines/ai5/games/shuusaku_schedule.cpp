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

#include "common/events.h"
#include "common/str.h"
#include "common/system.h"

#include "ai5/ai5.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/memory.h"
#include "ai5/vm.h"

#include "ai5/games/shuusaku.h"
#include "ai5/games/shuusaku_data.h"

namespace AI5 {

bool g_shuusakuRunningCamEvent = false;

namespace {

const int kScheduleWindowW = 800;
const int kScheduleWindowH = 376;
const int kColW = 80;
const int kRowH = 32;
const int kScheduleOverlay = 0;

// In-screen placement: the original 800x376 window is scaled to fit
// the 640-wide view.
const int kScheduleViewW = 640;
const int kScheduleViewH = kScheduleWindowH * kScheduleViewW / kScheduleWindowW; // 300
const int kScheduleViewY = (480 - kScheduleViewH) / 2;

// Raw system variables (see shuusaku.cpp).
const uint kSysvarFadeMs = 13;
const uint kSysvarWinDay = 60;
const uint kSysvarWinTime = 61;

struct {
	bool open;
	uint startT;
	int currentT;
	int planT;
	VMTimer flashTimer;
	bool flashOn;
	byte parts[kScheduleWindowW * kScheduleWindowH];
	GfxColor partsPal[256];
	byte display[kScheduleWindowW * kScheduleWindowH * 3];
	byte saved[2 * kColW * kScheduleWindowH * 3];
} schedule;

SchedAwayEvent *awayEvents[kNrLoc] = {0};
const SchedCamEvent *clickedCamEvent = nullptr;

/*
 * Get the position of the cell background on the parts CG for a given flags value.
 */
GfxPoint getBgPos(uint flags) {
	if (flags == kSchedFlagUnknown)
		return { 160, kRowH * 7 };
	if (flags & kSchedFlagPink)
		return { 160, kRowH * 8 };
	if (flags & kSchedFlagEvent) {
		if (flags & kSchedFlagEmpty)
			return { 160, kRowH * 5 };
		return { 160, kRowH * 3 };
	}
	if (flags & kSchedFlagOccupied) {
		if (flags & kSchedFlagEmpty)
			return { 160, kRowH * 2 };
		return { 160, 0 };
	}
	if (flags & kSchedFlagEmpty)
		return { 160, kRowH * 1 };
	warning("Invalid flags: %u", flags);
	return { 400, 312 };
}

/*
 * Get the position of the header on the parts CG for a given day/time.
 */
GfxPoint getHeaderPos(uint absT) {
	uint hour = absT / 4;
	uint hourI = absT % 4;
	if (hourI == 1) {
		// 15
		return { 240, 0 };
	} else if (hourI == 2) {
		// 30
		return { 320, 0 };
	} else if (hourI == 3) {
		// 45
		return { 400, 0 };
	} else if (hour < 7) {
		// saturday
		return { 240, (int)((hour + 1) * 24) };
	} else if (hour < 19) {
		// sunday before noon
		hour -= 7;
		return { 320, (int)((hour + 1) * 24) };
	} else if (hour < 31) {
		// sunday after noon
		hour -= 19;
		return { 400, (int)((hour + 1) * 24) };
	} else if (hour < 36) {
		// monday
		hour -= 31;
		return { 240, 192 + (int)(hour * 24) };
	}
	warning("Invalid absolute time: %u", absT);
	return { 160, 288 };
}

/*
 * Get the position of a character head on the parts CG for a given character ID.
 */
GfxPoint getHeadPos(uint ch) {
	return { 160 + (int)ch * 32, 344 };
}

/*
 * Get the flag number for a cell on the schedule, given by day/time and location.
 * (This is the flag which contains the `enum sched_flag` value.)
 */
uint scheduleFlagNo(int absT, uint location) {
	if (absT < 0)
		error("Invalid absolute time: %d", absT);

	// XXX: Akaya flags are out of order
	if (location == kLocAyaka)
		return 3000 + absT;
	if (location < kLocToilet)
		location++;
	return 3000 + location * 150 + absT;
}

void getCameraInfo(uint location, uint *camType, uint *camPlaced, uint *camElapsed) {
	if (location == kLocAyaka) {
		*camType = 0;
		return;
	}
	if (location > kLocAyaka)
		location--;

	uint t = memGetVar4Packed(120 + location);
	*camType = t;
	if (t == kCamTypeNone)
		return;
	if (t > 2) {
		*camType = kCamTypeNone;
		return;
	}

	// Note: invalid day/time values yield -1, which wraps to a large
	// unsigned value, disabling the range checks below (as in the original).
	*camPlaced = (uint)shuusakuAbsoluteTime(memGetVar16(120 + location * 3),
			memGetVar16(121 + location * 3));
	*camElapsed = memGetVar16(213 + location * 2 + ((t == kCamTypeDigi) ? 1 : 0));
}

void getPinkInfo(uint location, bool *pink, uint *startT) {
	uint stateFlag = 120 + location;
	uint startVar = 162 + location * 2;
	if (location == kLocEri || location > kLocAyaka) {
		*pink = false;
		return;
	}
	if (location == kLocAyaka) {
		stateFlag = 129;
		startVar = 160;
	}

	if (memGetVar4Packed(stateFlag) != 9) {
		*pink = false;
		return;
	}

	*pink = true;
	*startT = (uint)shuusakuAbsoluteTime(memGetVar16(startVar), memGetVar16(startVar + 1));
}

/*
 * Wrapper to copy from the parts CG to the display surface.
 */
void blitParts(int srcX, int srcY, int w, int h, int dstX, int dstY) {
	for (int row = 0; row < h; row++) {
		const byte *src = schedule.parts + (srcY + row) * kScheduleWindowW + srcX;
		byte *dst = schedule.display + ((dstY + row) * kScheduleWindowW + dstX) * 3;
		for (int col = 0; col < w; col++, src++, dst += 3) {
			if (*src == kMaskColor)
				continue;
			GfxColor c = schedule.partsPal[*src];
			dst[0] = c.r;
			dst[1] = c.g;
			dst[2] = c.b;
		}
	}
}

/*
 * Copy from the saved-strips surface to the display surface.
 */
void blitSaved(int srcX, int srcY, int w, int h, int dstX, int dstY) {
	for (int row = 0; row < h; row++) {
		const byte *src = schedule.saved + (srcY + row) * 2 * kColW + srcX * 3;
		byte *dst = schedule.display + ((dstY + row) * kScheduleWindowW + dstX) * 3;
		memcpy(dst, src, w * 3);
	}
}

/*
 * Copy from the display surface to the saved-strips surface.
 */
void saveDisplay(int srcX, int srcY, int w, int h, int dstX, int dstY) {
	for (int row = 0; row < h; row++) {
		const byte *src = schedule.display + ((srcY + row) * kScheduleWindowW + srcX) * 3;
		byte *dst = schedule.saved + (dstY + row) * 2 * kColW + dstX * 3;
		memcpy(dst, src, w * 3);
	}
}

void scheduleWindowUpdate() {
	// Scale the composed window onto the overlay.
	GfxSurface *overlay = gfxGetOverlay(kScheduleOverlay);
	memset(overlay->pixels, 0, overlay->h * overlay->pitch);
	for (int row = 0; row < kScheduleViewH; row++) {
		int srcY = row * kScheduleWindowH / kScheduleViewH;
		const byte *src = schedule.display + srcY * kScheduleWindowW * 3;
		byte *dst = overlay->pixels + (kScheduleViewY + row) * overlay->pitch;
		for (int col = 0; col < kScheduleViewW; col++) {
			int srcX = col * kScheduleWindowW / kScheduleViewW;
			const byte *s = src + srcX * 3;
			byte *d = dst + col * 4;
			d[0] = s[0];
			d[1] = s[1];
			d[2] = s[2];
			d[3] = 255;
		}
	}
	gfxScreenDirty();
}

void scheduleWindowDraw() {
	if (!schedule.open)
		return;
	uint day = memGetSysvar16(kSysvarWinDay);
	uint relT = memGetSysvar16(kSysvarWinTime);
	int absT = shuusakuAbsoluteTime(day, relT);
	if (absT < 0) {
		warning("Invalid day/time: %u/%u", day, relT);
		absT = 0;
	}

	for (int i = 0; i < 8; i++) {
		GfxPoint headerPos = getHeaderPos(schedule.startT + i);
		blitParts(headerPos.x, headerPos.y, 80, 24, 160 + i * kColW, 0);
	}

	for (int loc = 0; loc < kNrLoc; loc++) {
		// draw cell background
		uint flagNo = scheduleFlagNo(schedule.startT, loc);
		for (int i = 0; i < 8; i++) {
			byte flags = memGetVar4Packed(flagNo + i);
			if (flags < 8) {
				GfxPoint pos = getBgPos(flags);
				blitParts(pos.x, pos.y, 80, 32, 160 + i * kColW, 24 + loc * kRowH);
			}
		}

		// draw faces
		for (int i = 0; i < 8; i++) {
			if (!awayEvents[loc])
				continue;
			const SchedAwayEvent *ev = &awayEvents[loc][(schedule.startT + i) * 4];
			int dstX = 160 + i * kColW + 48;
			for (int j = 0; j < 4; j++, ev++) {
				if (!ev->flagNo)
					break;
				if (memGetVar4Packed(ev->flagNo) > 1) {
					GfxPoint pos = getHeadPos(ev->character);
					blitParts(pos.x, pos.y, 32, 32, dstX, 24 + loc * kRowH);
					dstX -= 16;
				}
			}
		}

		// draw cameras
		uint camType, camPlaced, camElapsed;
		getCameraInfo(loc, &camType, &camPlaced, &camElapsed);
		for (int i = 0; i < 8; i++) {
			uint cellT = schedule.startT + i;
			if (camType != kCamTypeNone && camPlaced <= cellT
					&& camPlaced + camElapsed >= cellT) {
				blitParts(160 + (camType - 1) * 80, 312, 80, 32,
						160 + i * kColW, 24 + loc * kRowH);
				uint camEndT = camPlaced + (camType == kCamTypeDigi ? 4 : 8);
				if (cellT >= camEndT) {
					blitParts(320, 312, 80, 32, 160 + i * kColW, 24 + loc * kRowH);
				}
			}
		}

		bool pink;
		uint pinkStartT;
		getPinkInfo(loc, &pink, &pinkStartT);
		for (int i = 0; i < 8; i++) {
			uint cellT = schedule.startT + i;
			if (pink && cellT >= pinkStartT) {
				blitParts(160, 256, 80, 32, 160 + i * kColW, 24 + loc * kRowH);
			}
		}
	}

	// save area behind yellow flashing box (current time)
	int i = schedule.currentT - (int)schedule.startT;
	if (i >= 0 && i < 8) {
		saveDisplay(160 + i * 80, 0, 80, kScheduleWindowH, 0, 0);
	}

	// save area behind red flashing box (plan time)
	i = schedule.planT - (int)schedule.startT;
	if (i >= 0 && i < 8) {
		saveDisplay(160 + i * 80, 0, 80, kScheduleWindowH, 80, 0);
	}
}

void updateTime() {
	schedule.currentT = shuusakuAbsoluteTime(memGetSysvar16(kSysvarWinDay),
			memGetSysvar16(kSysvarWinTime));
}

void scheduleClose() {
	gfxOverlayDisable(kScheduleOverlay);
	audioSyssePlay("se03.wav", 0);
	schedule.open = false;
}

void drawCurrentTimeBox() {
	if (!schedule.open)
		return;
	int i = schedule.currentT - (int)schedule.startT;
	if (i < 0 || i >= 8)
		return;
	blitParts(480, 0, 80, kScheduleWindowH, 160 + i * 80, 0);
	scheduleWindowUpdate();
}

void drawPlanTimeBox() {
	if (!schedule.open)
		return;
	int i = schedule.planT - (int)schedule.startT;
	if (i < 0 || i >= 8)
		return;
	blitParts(560, 0, 80, kScheduleWindowH, 160 + i * 80, 0);
	scheduleWindowUpdate();
}

void clearCurrentTimeBox() {
	if (!schedule.open)
		return;
	int i = schedule.currentT - (int)schedule.startT;
	if (i < 0 || i >= 8)
		return;
	blitSaved(0, 0, 80, kScheduleWindowH, 160 + i * 80, 0);
	scheduleWindowUpdate();
}

void clearPlanTimeBox() {
	if (!schedule.open)
		return;
	int i = schedule.planT - (int)schedule.startT;
	if (i < 0 || i >= 8)
		return;
	blitSaved(80, 0, 80, kScheduleWindowH, 160 + i * 80, 0);
	scheduleWindowUpdate();
}

void loadCamImage(const char *name, int i, GfxRect *metrics) {
	Common::SharedPtr<CG> cg = assetCgLoad(name);
	if (!cg) {
		warning("Failed to load CG \"%s\"", name);
		return;
	}

	gfxDrawCG(i, *cg);
	if (!cg->palette.empty())
		memcpy(g_mem->palette + 10 * 4, cg->palette.data() + 10 * 4, 236 * 4);
	if (metrics) {
		metrics->x = cg->x;
		metrics->y = cg->y;
		metrics->w = cg->w;
		metrics->h = cg->h;
	}
}

void camEventWait() {
	while (true) {
		if (inputDown(kInputActivate)) {
			do {
				vmDelay(16);
				handleEvents();
				gfxUpdate();
			} while (inputDown(kInputActivate));
			break;
		}
		if (inputDown(kInputCtrl))
			break;
		vmDelay(16);
		handleEvents();
		shuusakuScheduleTick();
		gfxUpdate();
	}
}

bool haveCamEvent(const SchedCamEvent *ev) {
	for (int i = 0; i < 3 && ev->entries[i].flagNo; i++) {
		if (memGetVar4Packed(ev->entries[i].flagNo))
			return true;
	}
	return false;
}

void runCamEvent(const SchedCamEvent *ev) {
	if (g_shuusakuRunningCamEvent)
		return;
	if (!haveCamEvent(ev))
		return;

	g_shuusakuRunningCamEvent = true;
	g_game->flags[kFlagAnimEnable] = 0;
	int savedScreenY = g_gfx.surface[0].src.y;

	// save surface 0 pixels
	GfxSurface *screen = gfxGetSurface(0);
	byte *saved = new byte[640 * 480];
	for (int row = 0; row < 480; row++) {
		memcpy(saved + row * 640, screen->pixels + row * screen->pitch, 640);
	}
	// save palettes
	byte memPalette[256 * 4];
	memcpy(memPalette, g_mem->palette, sizeof(memPalette));
	GfxColor gfxPalette[256];
	memcpy(gfxPalette, g_gfx.palette, sizeof(gfxPalette));

	for (int i = 0; i < 3 && ev->entries[i].flagNo; i++) {
		const SchedCamEventEntry *e = &ev->entries[i];
		byte flag = memGetVar4Packed(e->flagNo);
		if (!flag)
			continue;

		shuusakuCrossfadeTo(0, 0, 0);
		g_gfx.surface[0].src.y = 0;

		char name[16];
		if (flag & 2) {
			// video
			loadCamImage("ev11.gpx", 1, nullptr);
			gfxCopy(0, 0, 640, 480, 1, 0, 0, 0);
			shuusakuCrossfade(g_mem->palette, false);

			Common::sprintf_s(name, sizeof(name), "%s.mdd", e->name);
			shuusakuPlayMovie(name);
			shuusakuAfterMovieCrossfade();
			// fill with proper black before updating palette
			gfxFill(0, 0, 640, 72, 0, 12);
			gfxFill(0, 312, 640, 168, 0, 12);
			gfxFill(0, 72, 56, 240, 0, 12);
			gfxFill(376, 72, 264, 240, 0, 12);

			Common::sprintf_s(name, sizeof(name), "%s.gpx", e->name);
			loadCamImage(name, 1, nullptr);
			shuusakuUpdatePalette(g_mem->palette);
			shuusakuZoom(56, 72, 320, 240, 1);
		} else {
			// photo
			Common::sprintf_s(name, sizeof(name), "%s.gpx", e->name);
			loadCamImage(name, 1, nullptr);
			gfxCopy(0, 0, 640, 480, 1, 0, 0, 0);
			shuusakuCrossfade(g_mem->palette, false);
		}

		camEventWait();

		if (e->zoomName) {
			GfxRect metrics = { 0, 0, 0, 0 };
			Common::sprintf_s(name, sizeof(name), "%s.gpx", e->zoomName);
			loadCamImage(name, 1, &metrics);
			shuusakuCamEventZoom(metrics.x, metrics.y, metrics.w, metrics.h);
			gfxCopy(metrics.x, metrics.y, metrics.w, metrics.h, 1, metrics.x, metrics.y, 0);
			camEventWait();
		}
	}

	shuusakuCrossfadeTo(0, 0, 0);
	g_gfx.surface[0].src.y = savedScreenY;

	// restore palette
	memcpy(g_mem->palette, memPalette, sizeof(memPalette));

	for (int row = 0; row < 480; row++) {
		memcpy(screen->pixels + row * screen->pitch, saved + row * 640, 640);
	}
	delete[] saved;

	// crossfade to original palette/pixels
	gfxPaletteCrossfadeColors(gfxPalette, 0, 256, memGetSysvar16(kSysvarFadeMs) * 16);

	g_gfx.surface[0].src.y = savedScreenY;
	g_game->flags[kFlagAnimEnable] = kFlagAlwaysOn;
	g_shuusakuRunningCamEvent = false;
}

const SchedCamEvent *getCamEvent(uint loc, uint t) {
	for (const SchedCamEvent *ev = kCamEvents[loc]; ev && ev->t; ev++) {
		if (ev->t == t)
			return ev;
	}
	return nullptr;
}

void scheduleMouseDown(int x, int y) {
	// convert view coordinates to original window coordinates
	int wx = x * kScheduleWindowW / kScheduleViewW;
	int wy = (y - kScheduleViewY) * kScheduleWindowH / kScheduleViewH;
	if (wx < 160 || wx >= 160 + 8 * 80 || wy < 24 || wy >= 24 + 32 * 11)
		return;
	wx -= 160;
	wy -= 24;
	uint t = schedule.startT + wx / 80;
	uint loc = wy / 32;
	clickedCamEvent = getCamEvent(loc, t);
}

void drawFlash(bool on) {
	if (on) {
		clearCurrentTimeBox();
		if (schedule.planT >= 0 && schedule.planT != schedule.currentT)
			clearPlanTimeBox();
	} else {
		drawPlanTimeBox();
		drawCurrentTimeBox();
	}
}

void scrollLeft() {
	if (schedule.startT >= 2) {
		schedule.startT -= 2;
		scheduleWindowDraw();
		drawFlash(!schedule.flashOn);
		scheduleWindowUpdate();
	}
}

void scrollRight() {
	if (schedule.startT <= 134) {
		schedule.startT += 2;
		scheduleWindowDraw();
		drawFlash(!schedule.flashOn);
		scheduleWindowUpdate();
	}
}

// Initialize the away-event index, placing each event at the first free
// slot of its interval (at most 4 events share an interval).
void initAwayEvents() {
	for (int loc = 0; loc < kNrLoc; loc++) {
		const SchedAwayEvent *defs = kAwayEvents[loc];
		if (!defs)
			continue;
		SchedAwayEvent *events = new SchedAwayEvent[kNrIntervals * 4];
		memset(events, 0, kNrIntervals * 4 * sizeof(SchedAwayEvent));
		for (const SchedAwayEvent *def = defs; def->flagNo; def++) {
			int j = 0;
			while (j < 4 && events[def->t * 4 + j].flagNo)
				j++;
			if (j == 4) {
				warning("Too many away events at t=%u", def->t);
				continue;
			}
			events[def->t * 4 + j] = *def;
		}
		awayEvents[loc] = events;
	}
}

} // namespace

int shuusakuAbsoluteTime(int day, int t) {
	uint hour = (t / 100);
	if (hour > 23)
		return -1;
	if (day == kDaySat) {
		if (hour < 17)
			return -1;
		hour -= 17;
	} else if (day == kDaySun) {
		hour += 7;
	} else if (day == kDayMon) {
		if (hour > 4)
			return -1;
		hour += 31;
	} else {
		return -1;
	}

	return hour * 4 + (t % 100) / 15;
}

void shuusakuScheduleSetFlag(int location, int day, int t, int flag) {
	uint absT = (uint)shuusakuAbsoluteTime(day, t);
	if (flag == kSchedFlagEvent)
		flag |= kSchedFlagOccupied;

	// XXX: location 0 is Ayaka here (otherwise in normal schedule order)
	uint flagNo = 3000 + location * 150 + absT;
	memSetVar4Packed(flagNo, memGetVar4Packed(flagNo) | flag);
}

bool shuusakuSubwindowValid() {
	uint day = memGetSysvar16(kSysvarWinDay);
	uint t = memGetSysvar16(kSysvarWinTime);
	if (day == 0xffff || t == 0xffff)
		return false;
	if (day == kDayMon && t == 500)
		return false;
	return true;
}

void shuusakuScheduleWindowToggle() {
	if (schedule.open) {
		scheduleClose();
	} else {
		if (!shuusakuSubwindowValid())
			return;
		schedule.open = true;
		updateTime();
		if (schedule.currentT >= 0) {
			// round down to nearest multiple of 2
			schedule.startT = MIN<uint>(136, (uint)schedule.currentT) & ~1u;
		}
		scheduleWindowDraw();
		gfxOverlayEnable(kScheduleOverlay);
		scheduleWindowUpdate();
		audioSyssePlay("se02.wav", 0);
	}
}

void shuusakuScheduleUpdate() {
	if (!schedule.open)
		return;
	if (!shuusakuSubwindowValid()) {
		scheduleClose();
		return;
	}
	updateTime();
	scheduleWindowDraw();
	scheduleWindowUpdate();
}

bool shuusakuScheduleWindowEvent(const Common::Event &e) {
	if (!schedule.open)
		return false;

	switch (e.type) {
	case Common::EVENT_KEYDOWN:
		switch (e.kbd.keycode) {
		case Common::KEYCODE_LEFT:
			scrollLeft();
			return true;
		case Common::KEYCODE_RIGHT:
			scrollRight();
			return true;
		default:
			break;
		}
		break;
	case Common::EVENT_LBUTTONDOWN: {
		int x = e.mouse.x, y = e.mouse.y;
		bool inside = x >= 0 && x < kScheduleViewW
			&& y >= kScheduleViewY && y < kScheduleViewY + kScheduleViewH;
		if (!inside)
			break;
		scheduleMouseDown(x, y);
		return true;
	}
	case Common::EVENT_WHEELDOWN:
		scrollRight();
		return true;
	case Common::EVENT_WHEELUP:
		scrollLeft();
		return true;
	default:
		break;
	}
	return false;
}

void shuusakuScheduleTick() {
	if (clickedCamEvent) {
		const SchedCamEvent *ev = clickedCamEvent;
		clickedCamEvent = nullptr;
		runCamEvent(ev);
	}

	if (!vmTimerTickAsync(schedule.flashTimer, 1000))
		return;

	drawFlash(schedule.flashOn);
	schedule.flashOn = !schedule.flashOn;
}

void shuusakuScheduleSetPlanTime(int day, int t) {
	schedule.planT = shuusakuAbsoluteTime(day, t);
	scheduleWindowDraw();
	scheduleWindowUpdate();

	// XXX: hack to immediately update flashing box
	schedule.flashTimer = vmTimerCreate() - 1001;
	schedule.flashOn = false;
	shuusakuScheduleTick();
}

void shuusakuScheduleClearPlan() {
	clearPlanTimeBox();
	schedule.planT = -1;
}

void shuusakuScheduleWindowInit() {
	// load UI parts
	Common::SharedPtr<CG> cg = assetCgLoad("dialy.gpx");
	if (!cg) {
		warning("Failed to load cg \"dialy.gpx\"");
		return;
	}
	assert(cg->w <= kScheduleWindowW && cg->h <= kScheduleWindowH);
	for (int row = 0; row < cg->h; row++) {
		memcpy(schedule.parts + row * kScheduleWindowW,
		       cg->pixels.data() + row * cg->w, cg->w);
	}

	if (!cg->palette.empty()) {
		const byte *c = cg->palette.data();
		for (int i = 0; i < 256; i++, c += 4) {
			schedule.partsPal[i].r = c[2];
			schedule.partsPal[i].g = c[1];
			schedule.partsPal[i].b = c[0];
			schedule.partsPal[i].a = 255;
		}
	}

	// draw static part of display (room names)
	blitParts(0, 0, 160, kScheduleWindowH, 0, 0);

	initAwayEvents();
	schedule.currentT = -1;
	schedule.planT = -1;
}

} // namespace AI5
