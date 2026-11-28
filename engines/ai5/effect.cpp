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
#include "ai5/config.h"
#include "ai5/effect.h"
#include "ai5/gfx.h"
#include "ai5/vm.h"

namespace AI5 {

static void transitionUpdate(VMTimer &timer, uint dstI, uint ms) {
	// FIXME: use gfxDirty with damage coordinates
	gfxWholeSurfaceDirty(dstI);
	vmPeek();
	vmTimerTick(timer, ms * g_config.transitionSpeed);
}

#define X 0xff
#define O 0x00

#define FADE_PATTERN_SIZE 4
#define FADE_SIZE (sizeof(fade_pattern_vert)/FADE_PATTERN_SIZE)

#define F1A X,X,X,O
#define F1B X,X,X,X
#define F1C X,O,X,X
#define F1D X,X,X,X
#define FADE_1 F1A,F1B,F1C,F1D

#define F2A X,X,X,O
#define F2B X,X,X,X
#define F2C X,O,X,O
#define F2D X,X,X,X
#define FADE_2 F2A,F2B,F2C,F2D

#define F3A X,O,X,O
#define F3B X,X,X,X
#define F3C X,O,X,O
#define F3D X,X,X,X
#define FADE_3 F3A,F3B,F3C,F3D

#define F4A X,O,X,O
#define F4B X,X,X,X
#define F4C X,O,X,O
#define F4D O,X,X,X
#define FADE_4 F4A,F4B,F4C,F4D

#define F5A X,O,X,O
#define F5B X,X,X,X
#define F5C X,O,X,O
#define F5D O,X,O,X
#define FADE_5 F5A,F5B,F5C,F5D

#define F6A X,O,X,O
#define F6B O,X,O,X
#define F6C X,O,X,O
#define F6D O,X,O,X
#define FADE_6 F6A,F6B,F6C,F6D

#define F7A X,O,X,O
#define F7B O,X,O,X
#define F7C X,O,X,O
#define F7D O,O,O,X
#define FADE_7 F7A,F7B,F7C,F7D

#define F8A X,O,X,O
#define F8B O,X,O,O
#define F8C X,O,X,O
#define F8D O,O,O,O
#define FADE_8 F8A,F8B,F8C,F8D

#define F9A X,O,X,O
#define F9B O,O,O,O
#define F9C X,O,X,O
#define F9D O,O,O,O
#define FADE_9 F9A,F9B,F9C,F9D

#define F10A X,O,O,O
#define F10B O,O,O,O
#define F10C X,O,X,O
#define F10D O,O,O,O
#define FADE_10 F10A,F10B,F10C,F10D

#define F11A X,O,O,O
#define F11B O,O,O,O
#define F11C O,O,X,O
#define F11D O,O,O,O
#define FADE_11 F11A,F11B,F11C,F11D

#define F12A X,O,O,O
#define F12B O,O,O,O
#define F12C O,O,O,O
#define F12D O,O,O,O
#define FADE_12 F12A,F12B,F12C,F12D

#define R4(x) x, x, x, x
#define R8(x) x, x, x, x, x, x, x, x

static const byte fade_pattern_hori[] = {
	R4(F1A),R4(F2A),R8(F3A),R4(F4A),R4(F5A),R8(F6A),R4(F7A),R4(F8A),R4(F9A),R4(F10A),R4(F11A),R4(F12A),
	R4(F1B),R4(F2B),R8(F3B),R4(F4B),R4(F5B),R8(F6B),R4(F7B),R4(F8B),R4(F9B),R4(F10B),R4(F11B),R4(F12B),
	R4(F1C),R4(F2C),R8(F3C),R4(F4C),R4(F5C),R8(F6C),R4(F7C),R4(F8C),R4(F9C),R4(F10C),R4(F11C),R4(F12C),
	R4(F1D),R4(F2D),R8(F3D),R4(F4D),R4(F5D),R8(F6D),R4(F7D),R4(F8D),R4(F9D),R4(F10D),R4(F11D),R4(F12D),
};

static const byte fade_pattern_vert[] = {
	FADE_1,  FADE_1,  FADE_1,  FADE_1,
	FADE_2,  FADE_2,  FADE_2,  FADE_2,
	FADE_3,  FADE_3,  FADE_3,  FADE_3,
	FADE_3,  FADE_3,  FADE_3,  FADE_3,
	FADE_4,  FADE_4,  FADE_4,  FADE_4,
	FADE_5,  FADE_5,  FADE_5,  FADE_5,
	FADE_6,  FADE_6,  FADE_6,  FADE_6,
	FADE_6,  FADE_6,  FADE_6,  FADE_6,
	FADE_7,  FADE_7,  FADE_7,  FADE_7,
	FADE_8,  FADE_8,  FADE_8,  FADE_8,
	FADE_9,  FADE_9,  FADE_9,  FADE_9,
	FADE_10, FADE_10, FADE_10, FADE_10,
	FADE_11, FADE_11, FADE_11, FADE_11,
	FADE_12, FADE_12, FADE_12, FADE_12,
};


#undef X
#undef O

static void requireIndexed(const char *func) {
	if (g_game->bpp != 8)
		error("Invalid bpp for %s", func);
}

void gfxFadeDown(int x, int y, int w, int h, uint dstI, int srcI) {
	requireIndexed("gfxFadeDown");
	GfxSurface *s = gfxGetSurface(dstI);
	GfxSurface *srcS = srcI < 0 ? nullptr : gfxGetSurface(srcI);
	GfxRect r = { x, y, w, h };
	if (!gfxFillClip(s, &r)) {
		warning("Invalid fade");
		return;
	}

	const int fadeSize = FADE_SIZE;
	VMTimer timer = vmTimerCreate();
	byte *base = gfxPixel(s, r.x, r.y);
	for (int i = 0; i < fadeSize + r.h + FADE_PATTERN_SIZE * 2; i += FADE_PATTERN_SIZE * 2) {
		int row = 0;
		int fadeStart = fadeSize - i;

		// solid fill above the fade pattern
		for (; fadeStart < 0 && row < r.h; fadeStart++, row++) {
			byte *dst = base + row * s->pitch;
			if (srcS)
				memcpy(dst, gfxPixel(srcS, r.x, r.y + row), r.w);
			else
				memset(dst, 0, r.w);
		}
		fadeStart = MAX(fadeStart, 0);

		// fill with the pattern
		const byte *fade = fade_pattern_vert + fadeStart * FADE_PATTERN_SIZE;
		for (int fadeRow = 0; row < r.h && fadeRow < fadeSize - fadeStart; fadeRow++, row++) {
			byte *dst = base + row * s->pitch;
			const byte *src = fade + fadeRow * FADE_PATTERN_SIZE;
			for (int col = 0; col < r.w; col++, dst++) {
				if (src[col % FADE_PATTERN_SIZE])
					*dst = srcS ? *gfxPixel(srcS, r.x + col, r.y + row) : 0;
			}
		}

		transitionUpdate(timer, dstI, 10);
	}
}

void gfxFadeRight(int x, int y, int w, int h, uint dstI, int srcI) {
	requireIndexed("gfxFadeRight");
	GfxSurface *s = gfxGetSurface(dstI);
	GfxSurface *srcS = srcI < 0 ? nullptr : gfxGetSurface(srcI);
	GfxRect r = { x, y, w, h };
	if (!gfxFillClip(s, &r)) {
		warning("Invalid fade");
		return;
	}

	const int fadeSize = FADE_SIZE;
	VMTimer timer = vmTimerCreate();
	byte *base = gfxPixel(s, r.x, r.y);
	for (int i = 0; i < fadeSize + r.w + FADE_PATTERN_SIZE * 2; i += FADE_PATTERN_SIZE * 2) {
		for (int row = 0; row < r.h; row++) {
			int col = 0;
			byte *dst = base + row * s->pitch;
			int fadeStart = fadeSize - i;

			// solid fill left of the fade pattern
			if (fadeStart < 0) {
				int nrCols = MIN(-fadeStart, r.w);
				if (srcS)
					memcpy(dst, gfxPixel(srcS, r.x, r.y + row), nrCols);
				else
					memset(dst, 0, nrCols);
				dst += nrCols;
				col += nrCols;
				fadeStart = 0;
			}

			// fill with the pattern
			const byte *fade = fade_pattern_hori + (row % FADE_PATTERN_SIZE) * fadeSize + fadeStart;
			for (int fadeCol = 0; col < r.w && fadeCol < fadeSize - fadeStart; fadeCol++, col++, dst++, fade++) {
				if (*fade)
					*dst = srcS ? *gfxPixel(srcS, r.x + col, r.y + row) : 0;
			}
		}

		transitionUpdate(timer, dstI, 10);
	}
}

void gfxPixelate(int x, int y, int w, int h, uint dstI, uint mag) {
	requireIndexed("gfxPixelate");
	GfxSurface *s = gfxGetSurface(dstI);
	GfxRect r = { x, y, w, h };
	if (!gfxFillClip(s, &r)) {
		warning("Invalid pixelate");
		return;
	}

	int bandSize = MIN(w, 2 << mag);
	if (bandSize < 2) {
		warning("Invalid magnitude");
		return;
	}

	byte *base = gfxPixel(s, r.x, r.y);
	for (int row = 0; row < r.h; row++) {
		byte *p = base + row * s->pitch;
		for (int col = 0; col < r.w; col += bandSize, p += bandSize) {
			// FIXME: this sampling differs from the original implementation
			byte c = p[MIN(bandSize / 2, (r.w - 1) - col)];
			memset(p, c, MIN(bandSize, r.w - col));
		}
	}
	gfxWholeSurfaceDirty(dstI);
}

static void fadeRow(byte *base, uint row, uint w, uint h, uint pitch) {
	if (row >= h)
		return;
	memset(base + row * pitch, 0, w);
}

// Fill with color 7, then fill progressively from the top and bottom with color 0.
void gfxBlinkFade(int x, int y, int w, int h, uint dstI) {
	requireIndexed("gfxBlinkFade");
	GfxSurface *s = gfxGetSurface(dstI);
	GfxRect r = { x, y, w, h };
	if (!gfxFillClip(s, &r)) {
		warning("Invalid blink_fade");
		return;
	}

	VMTimer timer = vmTimerCreate();
	byte *base = gfxPixel(s, r.x, r.y);
	for (int row = 0; row < r.h; row++)
		memset(base + row * s->pitch, 7, r.w);
	transitionUpdate(timer, dstI, 4);

	uint logicalH = ((uint)r.h + 3u) & ~3u;
	for (uint row = 0; row < logicalH / 2; row += 4) {
		uint rowBot = logicalH - row - 4;
		for (uint i = 0; i < 4; i++) {
			if (row + i < (uint)r.h)
				memset(base + (row + i) * s->pitch, 0, r.w);
			if (rowBot + i < (uint)r.h)
				memset(base + (rowBot + i) * s->pitch, 0, r.w);
		}
		transitionUpdate(timer, dstI, 4);
	}
}

void gfxFadeProgressive(int x, int y, int w, int h, uint dstI) {
	requireIndexed("gfxFadeProgressive");
	GfxSurface *s = gfxGetSurface(dstI);
	GfxRect r = { x, y, w, h };
	if (!gfxFillClip(s, &r)) {
		warning("Invalid fade_progressive");
		return;
	}

	VMTimer timer = vmTimerCreate();
	uint logicalH = ((uint)r.h + 3u) & ~3u;
	byte *base = gfxPixel(s, r.x, r.y);
	for (uint row = 0; row <= logicalH; row += 4) {
		fadeRow(base, row, r.w, r.h, s->pitch);
		fadeRow(base, logicalH - row + 2, r.w, r.h, s->pitch);
		transitionUpdate(timer, dstI, 4);
	}
	for (uint row = 0; row <= logicalH; row += 4) {
		fadeRow(base, row + 1, r.w, r.h, s->pitch);
		fadeRow(base, logicalH - row + 3, r.w, r.h, s->pitch);
		transitionUpdate(timer, dstI, 4);
	}
}

static void copyRow(const byte *srcBase, byte *dstBase, uint row, uint w, uint h, uint srcPitch,
		uint dstPitch, uint bytesPP) {
	if (row >= h)
		return;
	memcpy(dstBase + row * dstPitch, srcBase + row * srcPitch, w * bytesPP);
}

void gfxCopyProgressive(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI) {
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!gfxCopyClip(src, &srcR, dst, &dstP)) {
		warning("Invalid copy");
		return;
	}

	VMTimer timer = vmTimerCreate();
	uint bytesPP = src->bytesPerPixel;
	uint logicalH = ((uint)srcR.h + 3u) & ~3u;
	const byte *srcBase = gfxPixel(src, srcR.x, srcR.y);
	byte *dstBase = gfxPixel(dst, dstP.x, dstP.y);
	for (uint row = 0; row <= logicalH; row += 4) {
		copyRow(srcBase, dstBase, row, srcR.w, srcR.h, src->pitch, dst->pitch, bytesPP);
		copyRow(srcBase, dstBase, logicalH - row + 2, srcR.w, srcR.h, src->pitch, dst->pitch, bytesPP);
		transitionUpdate(timer, dstI, 4);
	}
	for (uint row = 0; row <= logicalH; row += 4) {
		copyRow(srcBase, dstBase, row + 1, srcR.w, srcR.h, src->pitch, dst->pitch, bytesPP);
		copyRow(srcBase, dstBase, logicalH - row + 3, srcR.w, srcR.h, src->pitch, dst->pitch, bytesPP);
		transitionUpdate(timer, dstI, 4);
	}
}

// order in which the pixels of each 4x4 block are copied
static const GfxPoint kCrossfadeOffsets[16] = {
	{ 0, 0 }, { 1, 2 }, { 2, 1 }, { 3, 3 },
	{ 0, 3 }, { 1, 0 }, { 2, 3 }, { 3, 0 },
	{ 0, 1 }, { 1, 3 }, { 2, 0 }, { 3, 2 },
	{ 0, 2 }, { 1, 1 }, { 2, 2 }, { 3, 1 },
};

enum CrossfadeMask {
	kMaskNone,
	kMaskIndexed,
	kMaskDirect
};

// copy one pixel of every (step x step) block at the given offset
static void copyGridPixels(GfxSurface *src, const GfxRect &srcR, GfxSurface *dst, const GfxPoint &dstP,
		int offX, int offY, int step, CrossfadeMask mask, uint32 maskIndex, const GfxColor &maskColor) {
	const int bpp = src->bytesPerPixel;
	for (int chunkY = 0; chunkY < srcR.h; chunkY += step) {
		int row = chunkY + offY;
		if (row >= srcR.h)
			break;
		for (int chunkX = 0; chunkX < srcR.w; chunkX += step) {
			int col = chunkX + offX;
			if (col >= srcR.w)
				break;
			const byte *s = gfxPixel(src, srcR.x + col, srcR.y + row);
			byte *d = gfxPixel(dst, dstP.x + col, dstP.y + row);
			if (mask == kMaskIndexed && *s == maskIndex)
				continue;
			if (mask == kMaskDirect && s[0] == maskColor.r && s[1] == maskColor.g && s[2] == maskColor.b)
				continue;
			memcpy(d, s, bpp);
		}
	}
}

void gfxPixelCrossfade(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI,
		uint frameT, bool (*update)(float t, void *data), void *data) {
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!gfxCopyClip(src, &srcR, dst, &dstP)) {
		warning("Invalid copy");
		return;
	}

	VMTimer timer = vmTimerCreate();
	const GfxColor none = { 0, 0, 0, 0 };
	for (uint i = 0; i < ARRAYSIZE(kCrossfadeOffsets); i++) {
		copyGridPixels(src, srcR, dst, dstP, kCrossfadeOffsets[i].x, kCrossfadeOffsets[i].y, 4, kMaskNone, 0, none);
		if (update && !update((float)i / ARRAYSIZE(kCrossfadeOffsets), data)) {
			gfxCopy(srcX, srcY, w, h, srcI, dstX, dstY, dstI);
			break;
		}
		transitionUpdate(timer, dstI, frameT);
	}
	if (update)
		update(1.f, data);
}

void gfxPixelCrossfadeMaskedIndexed(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY,
		uint dstI, byte maskColor) {
	requireIndexed("gfxPixelCrossfadeMaskedIndexed");
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!gfxCopyClip(src, &srcR, dst, &dstP)) {
		warning("Invalid copy");
		return;
	}

	VMTimer timer = vmTimerCreate();
	const GfxColor none = { 0, 0, 0, 0 };
	for (uint i = 0; i < ARRAYSIZE(kCrossfadeOffsets); i++) {
		copyGridPixels(src, srcR, dst, dstP, kCrossfadeOffsets[i].x, kCrossfadeOffsets[i].y, 4, kMaskIndexed, maskColor, none);
		transitionUpdate(timer, dstI, 30);
	}
}

void gfxPixelCrossfadeMaskedIndexed8x8(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY,
		uint dstI, byte maskColor, uint frameMs) {
	requireIndexed("gfxPixelCrossfadeMaskedIndexed8x8");
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!gfxCopyClip(src, &srcR, dst, &dstP)) {
		warning("Invalid copy");
		return;
	}

	// the 4x4 pattern, applied to each quarter of 8x8 blocks
	static const GfxPoint quarters[4] = { { 0, 0 }, { 4, 4 }, { 4, 0 }, { 0, 4 } };
	VMTimer timer = vmTimerCreate();
	const GfxColor none = { 0, 0, 0, 0 };
	for (uint i = 0; i < ARRAYSIZE(kCrossfadeOffsets); i++) {
		for (int q = 0; q < 4; q++) {
			copyGridPixels(src, srcR, dst, dstP, kCrossfadeOffsets[i].x + quarters[q].x,
					kCrossfadeOffsets[i].y + quarters[q].y, 8, kMaskIndexed, maskColor, none);
			transitionUpdate(timer, dstI, frameMs);
		}
	}
}

void gfxPixelCrossfadeMasked(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI,
		uint32 maskColor) {
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!gfxCopyClip(src, &srcR, dst, &dstP)) {
		warning("Invalid copy");
		return;
	}

	GfxColor mask = gfxDecodeDirect(maskColor);
	VMTimer timer = vmTimerCreate();
	for (uint i = 0; i < ARRAYSIZE(kCrossfadeOffsets); i++) {
		copyGridPixels(src, srcR, dst, dstP, kCrossfadeOffsets[i].x, kCrossfadeOffsets[i].y, 4, kMaskDirect, 0, mask);
		transitionUpdate(timer, dstI, 30);
	}
}

void gfxScaleH(uint i, int mag) {
	if (i >= kGfxNrSurfaces || !g_gfx.surface[i].s) {
		warning("Invalid surface index: %u", i);
		i = 0;
	}

	GfxSurfaceState *s = &g_gfx.surface[i];
	if (mag == 0) {
		s->src.y = 0;
		s->src.h = s->s->h;
		s->dst.y = 0;
		s->scaled = s->src.x != s->dst.x || s->src.y != s->dst.y || s->src.w != s->dst.w || s->src.h != s->dst.h;
	} else if (mag < 0) {
		s->src.y = 0;
		s->src.h = s->s->h + mag;
		s->dst.y = mag;
		s->scaled = true;
	} else {
		s->src.y = 0;
		s->src.h = s->s->h - mag;
		s->dst.y = mag;
		s->scaled = true;
	}

	gfxWholeSurfaceDirty(i);
	gfxUpdate();
}

void gfxZoom(int srcX, int srcY, int w, int h, uint srcI, uint dstI, uint ms) {
	uint steps = MAX<uint>(1, (uint)roundf((float)ms / 32.f));

	VMTimer timer = vmTimerCreate();
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxSurface *src = gfxGetSurface(srcI);
	float stepX = (float)srcX / steps;
	float stepY = (float)srcY / steps;
	float stepW = (640 - w) / (float)steps;
	float stepH = (480 - h) / (float)steps;
	for (uint i = 1; i < steps && !g_engine->shouldQuit(); i++) {
		GfxRect srcR = { 0, 0, 640, 480 };
		GfxRect dstR = { (int)(srcX - stepX * i), (int)(srcY - stepY * i), (int)(w + stepW * i), (int)(h + stepH * i) };
		gfxBlitScaled(src, srcR, dst, dstR);
		transitionUpdate(timer, dstI, 32);
	}
	GfxRect all = { 0, 0, src->w, src->h };
	gfxBlitScaled(src, all, dst, all);
	gfxWholeSurfaceDirty(dstI);
}

} // End of namespace AI5
