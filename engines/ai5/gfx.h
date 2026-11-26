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

#ifndef AI5_GFX_H
#define AI5_GFX_H

#include "common/ptr.h"
#include "common/scummsys.h"

namespace AI5 {

struct CG;

enum {
	kGfxNrSurfaces = 15,
	kGfxNrOverlays = 4
};

struct GfxRect {
	int x, y, w, h;
};

struct GfxPoint {
	int x, y;
};

struct GfxColor {
	byte r, g, b, a;
};

/**
 * A drawing surface with the same layout as the original's SDL surfaces:
 * 8-bit indexed, 24-bit RGB (bytes in R, G, B order) or 32-bit RGBA.
 */
struct GfxSurface {
	byte *pixels;
	int w, h;
	int pitch;
	int bytesPerPixel;
};

struct GfxSurfaceState {
	GfxSurface *s;
	GfxRect src;     // source rectangle when scaled
	GfxRect dst;     // destination rectangle when scaled
	bool scaled;     // if true, src and dst differ
	bool dirty;
	GfxRect damaged;
};

struct GfxOverlay {
	GfxSurface *s;
	bool enabled;
};

struct Gfx {
	GfxSurfaceState surface[kGfxNrSurfaces];
	GfxOverlay overlay[kGfxNrOverlays];
	// index of the currently displayed surface
	uint screen;
	// composed screen image (RGB24)
	GfxSurface *display;
	GfxColor palette[256];
	// palette of the displayed surface
	GfxColor screenPalette[256];
	struct {
		uint32 bg;
		uint32 fg;
		GfxColor bgColor;
		GfxColor fgColor;
		uint size;
	} text;
	bool hidden;
};

extern Gfx g_gfx;

int gfxViewWidth();
int gfxViewHeight();

void gfxInit();
void gfxFini();
void gfxUpdate();
void gfxDirty(uint surface, int x, int y, int w, int h);
void gfxScreenDirty();
void gfxWholeSurfaceDirty(uint surface);
bool gfxIsDirty(uint surface);
void gfxClean(uint surface);
void gfxOverlayEnable(int n);
void gfxOverlayDisable(int n);
uint gfxCurrentSurface();
void gfxSetScreenSurface(uint i);
GfxSurface *gfxGetSurface(uint i);
GfxSurface *gfxGetOverlay(int n);
/** Push the composed display to the backend, optionally blended towards a color. */
void gfxPresent(const GfxColor *mask, byte alpha);

// display operations
void gfxDisplayHide(uint32 color);
void gfxDisplayUnhide();
void gfxDisplayFreeze();
void gfxDisplayUnfreeze();
void gfxDisplayFadeOut(uint32 vmColor, uint ms, bool (*cb)() = nullptr);
void gfxDisplayFadeIn(uint ms, bool (*cb)() = nullptr);

// palette operations
void gfxUpdatePaletteNoDirty(int start, int n);
void gfxUpdatePalette(int start, int n);
void gfxPaletteSetNoUpdate(const byte *data, uint start, uint n);
void gfxPaletteSet(const byte *data, uint start, uint n);
void gfxPaletteSetColor(byte c, byte r, byte g, byte b);
void gfxPaletteCopy(byte *dst, uint start, uint n);
void gfxCrossfadeColors(byte *pal, byte *colorsIn, uint nrColorsIn, uint ms,
		bool (*cb)(float, void *), void *data);
void gfxPaletteCrossfadeColors(GfxColor *newPal, uint start, uint n, uint ms);
void gfxPaletteCrossfade(const byte *data, uint start, uint n, uint ms);
void gfxPaletteCrossfadeTo(byte r, byte g, byte b, uint start, uint n, uint ms);

// clipping
bool gfxCopyClip(GfxSurface *src, GfxRect *srcR, GfxSurface *dst, GfxPoint *dstP);
bool gfxFillClip(GfxSurface *s, GfxRect *r);

GfxColor gfxDecodeBgr555(uint16 c);
GfxColor gfxDecodeBgr(uint32 c);
GfxColor gfxDecodeDirect(uint32 color);

// draw operations
void gfxCopy(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI);
void gfxCopyMasked(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI, uint32 maskColor);
void gfxIndexedCopyMasked(int srcX, int srcY, int w, int h, GfxSurface *src, int dstX, int dstY,
		GfxSurface *dst, byte maskColor);
void gfxIndexedCopyMaskedDstGt(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY,
		uint dstI, byte maskColor, byte threshold);
void gfxCopySwap(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI);
void gfxCompose(int fgX, int fgY, int w, int h, uint fgI, int bgX, int bgY, uint bgI,
		int dstX, int dstY, uint dstI, uint16 maskColor);
void gfxBlend(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI, byte alpha);
void gfxBlendMasked(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI, byte *mask);
void gfxBlendWithMaskColorTo(int aX, int aY, int w, int h, uint aI, int bX, int bY, uint bI,
		int dstX, int dstY, uint dstI, int maskW, int maskH, byte *mask);
void gfxInvertColors(int x, int y, int w, int h, uint i);
void gfxFill(int x, int y, int w, int h, uint i, uint32 c);
void gfxSwapColors(int x, int y, int w, int h, uint i, uint32 c1, uint32 c2);
void gfxBlendFill(int x, int y, int w, int h, uint i, uint32 c, byte rate);
void gfxDrawCG(uint i, const CG &cg);

inline byte *gfxPixel(GfxSurface *s, int x, int y) {
	return s->pixels + y * s->pitch + x * s->bytesPerPixel;
}

} // End of namespace AI5

#endif // AI5_GFX_H
