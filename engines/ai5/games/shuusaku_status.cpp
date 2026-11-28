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

#include "common/system.h"

#include "ai5/ai5.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/gfx.h"
#include "ai5/memory.h"

#include "ai5/games/shuusaku.h"

namespace AI5 {

namespace {

const int kStatusWindowW = 640;
const int kStatusWindowH = 64;
const int kStatusPartsH = 320;
const int kStatusOverlay = 1;

// Raw system variables (see shuusaku.cpp).
const uint kSysvarWinDay = 60;
const uint kSysvarWinTime = 61;

struct {
	bool open;
	byte parts[kStatusWindowW * kStatusPartsH];
	GfxColor partsPal[256];
} status;

/*
 * Wrapper to copy from the parts CG to the status overlay.
 */
void blitParts(int srcX, int srcY, int w, int h, int dstX, int dstY) {
	GfxSurface *overlay = gfxGetOverlay(kStatusOverlay);
	for (int row = 0; row < h; row++) {
		const byte *src = status.parts + (srcY + row) * kStatusWindowW + srcX;
		byte *dst = overlay->pixels + (dstY + row) * overlay->pitch + dstX * 4;
		for (int col = 0; col < w; col++, src++, dst += 4) {
			GfxColor c = status.partsPal[*src];
			dst[0] = c.r;
			dst[1] = c.g;
			dst[2] = c.b;
			dst[3] = 255;
		}
	}
}

void blitNumber(uint n, int i) {
	n = MIN<uint>(n, 15);
	uint row = n / 5;
	uint col = n % 5;
	blitParts(392 + col * 40, 64 + row * 16, 40, 16, 348 + i * 60, 40);
}

void blitMeal(uint value, int i) {
	value = MIN<uint>(value, 4);
	// image
	blitParts(value * 56, 72, 56, 32, 148 + i * 60, 8);
	// label
	blitParts(i * 56, 144, 56, 16, 148 + i * 60, 40);
}

void blitBathTemp(uint value, int i) {
	value = MIN<uint>(value, 4);
	// image
	blitParts(value * 56, 112, 56, 32, 12 + i * 60, 8);
	// text
	blitParts(i * 112, 144, 56, 16, 12 + i * 60, 40);
}

void statusWindowDraw() {
	if (!status.open)
		return;

	// background
	blitParts(0, 0, kStatusWindowW, kStatusWindowH, 0, 0);

	// item counts
	blitNumber(memGetVar16(101), 0);
	blitNumber(memGetVar16(100), 1);
	blitNumber(memGetVar16(102), 2);
	blitNumber(memGetVar16(104), 3);
	blitNumber(memGetVar16(103), 4);

	int absT = shuusakuAbsoluteTime(memGetSysvar16(kSysvarWinDay), memGetSysvar16(kSysvarWinTime));
	if (absT >= shuusakuAbsoluteTime(kDaySat, 1900)) {
		// Saturday dinner
		blitMeal(memGetVar4Packed(170), 0);
	}
	if (absT >= shuusakuAbsoluteTime(kDaySat, 2100)) {
		// Saturday bath
		blitBathTemp(memGetVar4Packed(245), 0);
	}
	if (absT >= shuusakuAbsoluteTime(kDaySun, 900)) {
		// Sunday breakfast
		blitMeal(memGetVar4Packed(276), 1);
	}
	if (absT >= shuusakuAbsoluteTime(kDaySun, 1900)) {
		// Sunday dinner
		blitMeal(memGetVar4Packed(311), 2);
	}
	if (absT >= shuusakuAbsoluteTime(kDaySun, 2100)) {
		// Sunday bath
		blitBathTemp(memGetVar4Packed(326), 1);
	}
}

void statusClose() {
	gfxOverlayDisable(kStatusOverlay);
	audioSyssePlay("se03.wav", 0);
	status.open = false;
}

} // namespace

void shuusakuStatusWindowToggle() {
	if (status.open) {
		statusClose();
	} else {
		if (!shuusakuSubwindowValid())
			return;
		status.open = true;
		gfxOverlayEnable(kStatusOverlay);
		statusWindowDraw();
		gfxScreenDirty();
		audioSyssePlay("se02.wav", 0);
	}
}

void shuusakuStatusUpdate() {
	if (!status.open)
		return;
	if (!shuusakuSubwindowValid()) {
		statusClose();
		return;
	}
	statusWindowDraw();
	gfxScreenDirty();
}

bool shuusakuStatusWindowEvent(const Common::Event &e) {
	if (!status.open)
		return false;
	// The original uses a separate window with a close button; in-screen
	// the window is toggled with F1 or the context menu instead.
	return false;
}

void shuusakuStatusInit() {
	// load UI parts
	Common::SharedPtr<CG> cg = assetCgLoad("propitem.gpx");
	if (!cg) {
		warning("Failed to load cg \"propitem.gpx\"");
		return;
	}
	assert(cg->w <= kStatusWindowW && cg->h <= kStatusPartsH);
	for (uint row = 0; row < (uint)cg->h; row++) {
		memcpy(status.parts + row * kStatusWindowW,
		       cg->pixels.data() + row * cg->w, cg->w);
	}

	if (!cg->palette.empty()) {
		const byte *c = cg->palette.data();
		for (int i = 0; i < 256; i++, c += 4) {
			status.partsPal[i].r = c[2];
			status.partsPal[i].g = c[1];
			status.partsPal[i].b = c[0];
			status.partsPal[i].a = 255;
		}
	}
}

} // namespace AI5
