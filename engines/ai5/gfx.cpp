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
#include "engines/util.h"
#include "gui/message.h"
#include "graphics/surface.h"

#include "ai5/ai5.h"
#include "ai5/cg.h"
#include "ai5/config.h"
#include "ai5/gfx.h"
#include "ai5/vm.h"

namespace AI5 {

Gfx g_gfx;

static int g_viewW = 640;
static int g_viewH = 400;
static Graphics::Surface g_screenBuf; // backend screen format
static Graphics::PixelFormat g_screenFormat;

int gfxViewWidth() { return g_viewW; }
int gfxViewHeight() { return g_viewH; }

GfxColor gfxDecodeBgr555(uint16 c) {
	GfxColor r = { (byte)((c & 0x7c00) >> 7), (byte)((c & 0x03e0) >> 2), (byte)((c & 0x001f) << 3), 255 };
	return r;
}

GfxColor gfxDecodeBgr(uint32 c) {
	GfxColor r = { (byte)(c & 0xff), (byte)((c >> 8) & 0xff), (byte)((c >> 16) & 0xff), 0 };
	return r;
}

GfxColor gfxDecodeDirect(uint32 color) {
	if (g_game->bpp == 16)
		return gfxDecodeBgr555(color);
	if (g_game->bpp == 24)
		return gfxDecodeBgr(color);
	error("gfxDecodeDirect: invalid bpp");
}

static GfxSurface *createSurface(int w, int h, int bytesPerPixel) {
	GfxSurface *s = new GfxSurface();
	s->w = w;
	s->h = h;
	s->bytesPerPixel = bytesPerPixel;
	s->pitch = w * bytesPerPixel;
	s->pixels = (byte *)calloc(s->pitch, h);
	return s;
}

static void freeSurface(GfxSurface *s) {
	if (!s)
		return;
	free(s->pixels);
	delete s;
}

// union of two rectangles; an empty rectangle is ignored
static void unionRect(const GfxRect &a, const GfxRect &b, GfxRect &out) {
	if (a.w <= 0 || a.h <= 0) {
		out = b;
		return;
	}
	if (b.w <= 0 || b.h <= 0) {
		out = a;
		return;
	}
	int x0 = MIN(a.x, b.x), y0 = MIN(a.y, b.y);
	int x1 = MAX(a.x + a.w, b.x + b.w), y1 = MAX(a.y + a.h, b.y + b.h);
	out.x = x0;
	out.y = y0;
	out.w = x1 - x0;
	out.h = y1 - y0;
}

void gfxDirty(uint surface, int x, int y, int w, int h) {
	g_gfx.surface[surface].dirty = true;
	GfxRect r = { x, y, w, h };
	unionRect(g_gfx.surface[surface].damaged, r, g_gfx.surface[surface].damaged);
}

bool gfxIsDirty(uint surface) {
	return g_gfx.surface[surface].dirty;
}

void gfxClean(uint surface) {
	g_gfx.surface[surface].dirty = false;
	g_gfx.surface[surface].damaged = GfxRect();
}

void gfxScreenDirty() {
	g_gfx.surface[g_gfx.screen].dirty = true;
	g_gfx.surface[g_gfx.screen].damaged = g_gfx.surface[g_gfx.screen].src;
}

void gfxWholeSurfaceDirty(uint surface) {
	GfxSurfaceState *s = &g_gfx.surface[surface];
	gfxDirty(surface, s->src.x, s->src.y, s->src.w, s->src.h);
}

void gfxOverlayEnable(int n) {
	g_gfx.overlay[n].enabled = true;
	gfxScreenDirty();
}

void gfxOverlayDisable(int n) {
	g_gfx.overlay[n].enabled = false;
	gfxScreenDirty();
}

GfxSurface *gfxGetSurface(uint i) {
	if (i >= kGfxNrSurfaces || !g_gfx.surface[i].s) {
		warning("Invalid surface index: %u", i);
		i = 0;
	}
	return g_gfx.surface[i].s;
}

static GfxSurface *gfxScreen() {
	return g_gfx.surface[g_gfx.screen].s;
}

uint gfxCurrentSurface() {
	return g_gfx.screen;
}

GfxSurface *gfxGetOverlay(int n) {
	if (!g_gfx.overlay[n].s)
		g_gfx.overlay[n].s = createSurface(g_viewW, g_viewH, 4);
	return g_gfx.overlay[n].s;
}

void gfxInit() {
	// Shuusaku's view size differs from surface 0
	if (g_game->view.w && g_game->view.h) {
		g_viewW = g_game->view.w;
		g_viewH = g_game->view.h;
	} else {
		g_viewW = g_game->surfaceSizes[0].w;
		g_viewH = g_game->surfaceSizes[0].h;
	}

	initGraphics(g_viewW, g_viewH, nullptr);
	Common::List<Graphics::PixelFormat> formats = g_system->getSupportedFormats();
	for (Common::List<Graphics::PixelFormat>::iterator it = formats.begin(); it != formats.end(); ++it) {
		if (it->bytesPerPixel == 2 || it->bytesPerPixel == 4) {
			initGraphics(g_viewW, g_viewH, &*it);
			break;
		}
	}
	g_screenFormat = g_system->getScreenFormat();
	if (g_screenFormat.bytesPerPixel == 1)
		error("AI5: the backend does not support a high color screen");
	g_screenBuf.create(g_viewW, g_viewH, g_screenFormat);

	const int bpp = g_game->bpp == 8 ? 1 : 3;
	for (int i = 0; i < kGfxNrSurfaces; i++) {
		int w = g_game->surfaceSizes[i].w;
		int h = g_game->surfaceSizes[i].h;
		if (!w || !h)
			break;
		GfxSurfaceState &s = g_gfx.surface[i];
		s.s = createSurface(w, h, bpp);
		s.src.x = s.src.y = s.dst.x = s.dst.y = 0;
		s.src.w = s.dst.w = w;
		s.src.h = s.dst.h = h;
		s.scaled = false;
		s.dirty = false;
		s.damaged = GfxRect();
	}
	g_gfx.surface[g_gfx.screen].src.w = g_viewW;
	g_gfx.surface[g_gfx.screen].src.h = g_viewH;
	gfxScreenDirty();

	g_gfx.display = createSurface(g_viewW, g_viewH, 3);
}

void gfxFini() {
	for (int i = 0; i < kGfxNrSurfaces; i++) {
		freeSurface(g_gfx.surface[i].s);
		g_gfx.surface[i].s = nullptr;
	}
	for (int i = 0; i < kGfxNrOverlays; i++) {
		freeSurface(g_gfx.overlay[i].s);
		g_gfx.overlay[i].s = nullptr;
	}
	freeSurface(g_gfx.display);
	g_gfx.display = nullptr;
	g_screenBuf.free();
}

void gfxPresent(const GfxColor *mask, byte alpha) {
	const uint a = alpha;
	for (int y = 0; y < g_viewH; y++) {
		const byte *src = gfxPixel(g_gfx.display, 0, y);
		for (int x = 0; x < g_viewW; x++, src += 3) {
			uint r = src[0], g = src[1], b = src[2];
			if (mask) {
				r = (r * (255 - a) + mask->r * a) / 255;
				g = (g * (255 - a) + mask->g * a) / 255;
				b = (b * (255 - a) + mask->b * a) / 255;
			}
			uint32 c = g_screenFormat.RGBToColor(r, g, b);
			if (g_screenFormat.bytesPerPixel == 2)
				*(uint16 *)g_screenBuf.getBasePtr(x, y) = c;
			else
				*(uint32 *)g_screenBuf.getBasePtr(x, y) = c;
		}
	}
	g_system->copyRectToScreen(g_screenBuf.getPixels(), g_screenBuf.pitch, 0, 0, g_viewW, g_viewH);
	g_system->updateScreen();
}

static void presentRect(const GfxRect &r) {
	for (int y = r.y; y < r.y + r.h; y++) {
		const byte *src = gfxPixel(g_gfx.display, r.x, y);
		for (int x = r.x; x < r.x + r.w; x++, src += 3) {
			uint32 c = g_screenFormat.RGBToColor(src[0], src[1], src[2]);
			if (g_screenFormat.bytesPerPixel == 2)
				*(uint16 *)g_screenBuf.getBasePtr(x, y) = c;
			else
				*(uint32 *)g_screenBuf.getBasePtr(x, y) = c;
		}
	}
	g_system->copyRectToScreen(g_screenBuf.getBasePtr(r.x, r.y), g_screenBuf.pitch, r.x, r.y, r.w, r.h);
}

// convert part of the screen surface to the RGB24 display
static void blitToDisplay(GfxSurface *src, GfxRect srcR, GfxPoint dstP) {
	if (!gfxCopyClip(src, &srcR, g_gfx.display, &dstP))
		return;
	for (int row = 0; row < srcR.h; row++) {
		const byte *s = gfxPixel(src, srcR.x, srcR.y + row);
		byte *d = gfxPixel(g_gfx.display, dstP.x, dstP.y + row);
		if (src->bytesPerPixel == 1) {
			for (int col = 0; col < srcR.w; col++, d += 3) {
				const GfxColor &c = g_gfx.screenPalette[s[col]];
				d[0] = c.r;
				d[1] = c.g;
				d[2] = c.b;
			}
		} else {
			memcpy(d, s, srcR.w * 3);
		}
	}
}

// blend an RGBA overlay onto the display
static void blendOverlay(GfxSurface *src, GfxRect r) {
	GfxPoint p = { r.x, r.y };
	if (!gfxCopyClip(src, &r, g_gfx.display, &p))
		return;
	for (int row = 0; row < r.h; row++) {
		const byte *s = gfxPixel(src, r.x, r.y + row);
		byte *d = gfxPixel(g_gfx.display, p.x, p.y + row);
		for (int col = 0; col < r.w; col++, s += 4, d += 3) {
			uint a = s[3];
			d[0] = (s[0] * a + d[0] * (255 - a)) / 255;
			d[1] = (s[1] * a + d[1] * (255 - a)) / 255;
			d[2] = (s[2] * a + d[2] * (255 - a)) / 255;
		}
	}
}

void gfxPresentFrame(const GfxSurface *frame) {
	GfxSurface *src = const_cast<GfxSurface *>(frame);
	GfxRect r = { 0, 0, frame->w, frame->h };
	GfxPoint p = { 0, 0 };
	if (!gfxCopyClip(src, &r, g_gfx.display, &p))
		return;
	for (int row = 0; row < r.h; row++)
		memcpy(gfxPixel(g_gfx.display, 0, row), gfxPixel(src, 0, row), r.w * 3);
	presentRect(r);
	g_system->updateScreen();
	// the next update redraws the screen surface
	gfxScreenDirty();
}

void gfxWarningMessage(const char *message) {
	ai5FallbackLog("%s", message);
	warning("%s", message);
	GUI::MessageDialog dialog(message);
	dialog.runModal();
}

void gfxUpdate() {
	GfxSurfaceState *screen = &g_gfx.surface[g_gfx.screen];
	// The backend draws the mouse cursor when the screen is presented, so a
	// moved pointer has to be presented even when nothing else is dirty.
	static int lastMouseX = -1, lastMouseY = -1;
	Common::Point mouse = g_system->getEventManager()->getMousePos();
	const bool mouseMoved = mouse.x != lastMouseX || mouse.y != lastMouseY;
	lastMouseX = mouse.x;
	lastMouseY = mouse.y;
	if (g_gfx.hidden || !screen->dirty) {
		if (mouseMoved)
			g_system->updateScreen();
		return;
	}

	if (screen->scaled) {
		// redraw the whole view with the source rectangle scaled to the
		// destination rectangle
		memset(g_gfx.display->pixels, 0, g_gfx.display->pitch * g_gfx.display->h);
		GfxSurface *view = g_gfx.display;
		GfxSurface conv = { nullptr, screen->s->w, screen->s->h, screen->s->w * 3, 3 };
		Common::Array<byte> buf;
		if (screen->s->bytesPerPixel == 1) {
			// expand indexed pixels through the palette first
			buf.resize(conv.pitch * conv.h);
			conv.pixels = buf.data();
			for (int y = 0; y < conv.h; y++) {
				const byte *sp = gfxPixel(screen->s, 0, y);
				byte *dp = gfxPixel(&conv, 0, y);
				for (int x = 0; x < conv.w; x++, dp += 3) {
					const GfxColor &c = g_gfx.screenPalette[sp[x]];
					dp[0] = c.r;
					dp[1] = c.g;
					dp[2] = c.b;
				}
			}
		} else {
			conv = *screen->s;
		}
		gfxBlitScaled(&conv, screen->src, view, screen->dst);
		GfxRect all = { 0, 0, g_viewW, g_viewH };
		for (int i = 0; i < kGfxNrOverlays; i++) {
			if (g_gfx.overlay[i].s && g_gfx.overlay[i].enabled)
				blendOverlay(g_gfx.overlay[i].s, all);
		}
		presentRect(all);
		g_system->updateScreen();
		gfxClean(g_gfx.screen);
		return;
	}

	GfxRect dstR = screen->damaged;
	// only Shuusaku uses this
	dstR.y -= screen->src.y;
	GfxPoint dstP = { dstR.x, dstR.y };
	blitToDisplay(screen->s, screen->damaged, dstP);
	for (int i = 0; i < kGfxNrOverlays; i++) {
		if (g_gfx.overlay[i].s && g_gfx.overlay[i].enabled)
			blendOverlay(g_gfx.overlay[i].s, screen->damaged);
	}

	GfxSurface view = { nullptr, g_viewW, g_viewH, 0, 0 };
	if (gfxFillClip(&view, &dstR))
		presentRect(dstR);
	g_system->updateScreen();
	gfxClean(g_gfx.screen);
}

void gfxDisplayFreeze() {
	g_gfx.hidden = true;
}

void gfxDisplayUnfreeze() {
	g_gfx.hidden = false;
	gfxScreenDirty();
}

static double fadeFrameTime() {
	return 16 * g_config.transitionSpeed;
}

static GfxColor vmColorToRgb(uint32 vmColor) {
	if (g_game->bpp == 8)
		return g_gfx.palette[vmColor & 0xff];
	return gfxDecodeDirect(vmColor);
}

void gfxDisplayFadeOut(uint32 vmColor, uint ms, bool (*cb)()) {
	if (g_gfx.hidden)
		return;
	g_gfx.hidden = true;

	int step = MAX(1, (int)round(256.0 / ((ms * g_config.transitionSpeed) / fadeFrameTime())));
	GfxColor c = vmColorToRgb(vmColor);

	VMTimer timer = vmTimerCreate();
	for (int i = 0; i < 256; i += step) {
		gfxPresent(&c, i);
		vmPeek();
		vmTimerTick(timer, fadeFrameTime());
		if (cb && !cb())
			break;
	}

	// leave the display filled with the color, which fade-in starts from
	for (int y = 0; y < g_viewH; y++) {
		byte *d = gfxPixel(g_gfx.display, 0, y);
		for (int x = 0; x < g_viewW; x++, d += 3) {
			d[0] = c.r;
			d[1] = c.g;
			d[2] = c.b;
		}
	}
	gfxPresent(nullptr, 0);
}

void gfxDisplayFadeIn(uint ms, bool (*cb)()) {
	int step = MAX(1, (int)round(256.0 / ((ms * g_config.transitionSpeed) / fadeFrameTime())));

	// fade from the current display contents to the screen surface
	GfxSurface *from = createSurface(g_viewW, g_viewH, 3);
	memcpy(from->pixels, g_gfx.display->pixels, from->pitch * from->h);
	GfxRect all = { 0, 0, g_viewW, g_viewH };
	GfxPoint origin = { 0, 0 };
	blitToDisplay(gfxScreen(), all, origin);

	VMTimer timer = vmTimerCreate();
	for (int i = 255; i >= 0 && !g_engine->shouldQuit(); i -= step) {
		for (int y = 0; y < g_viewH; y++) {
			const byte *f = gfxPixel(from, 0, y);
			const byte *s = gfxPixel(g_gfx.display, 0, y);
			for (int x = 0; x < g_viewW; x++, f += 3, s += 3) {
				uint r = (s[0] * (255 - i) + f[0] * i) / 255;
				uint g = (s[1] * (255 - i) + f[1] * i) / 255;
				uint b = (s[2] * (255 - i) + f[2] * i) / 255;
				uint32 c = g_screenFormat.RGBToColor(r, g, b);
				if (g_screenFormat.bytesPerPixel == 2)
					*(uint16 *)g_screenBuf.getBasePtr(x, y) = c;
				else
					*(uint32 *)g_screenBuf.getBasePtr(x, y) = c;
			}
		}
		g_system->copyRectToScreen(g_screenBuf.getPixels(), g_screenBuf.pitch, 0, 0, g_viewW, g_viewH);
		g_system->updateScreen();

		vmPeek();
		vmTimerTick(timer, fadeFrameTime());
		if (cb && !cb())
			break;
	}
	freeSurface(from);

	gfxPresent(nullptr, 0);
	g_gfx.hidden = false;
	gfxScreenDirty();
}

void gfxDisplayHide(uint32 color) {
	// fill the screen with a color and prevent updates
	GfxColor c = { 0, 0, 0, 255 };
	if (g_game->bpp != 8)
		c = gfxDecodeDirect(color);
	gfxPresent(&c, 255);
	g_gfx.hidden = true;
}

void gfxDisplayUnhide() {
	g_gfx.hidden = false;
	gfxScreenDirty();
}

static void readPalette(GfxColor *dst, const byte *src, uint n) {
	for (uint i = 0; i < n; i++) {
		dst[i].b = src[i * 4 + 0];
		dst[i].g = src[i * 4 + 1];
		dst[i].r = src[i * 4 + 2];
		dst[i].a = 255;
	}
}

void gfxPaletteCopy(byte *dst, uint start, uint n) {
	for (uint i = start; i < n; i++, dst += 4) {
		dst[0] = g_gfx.palette[i].b;
		dst[1] = g_gfx.palette[i].g;
		dst[2] = g_gfx.palette[i].r;
		dst[3] = 255;
	}
}

void gfxUpdatePaletteNoDirty(int start, int n) {
	if (g_game->bpp != 8)
		return;
	memcpy(g_gfx.screenPalette + start, g_gfx.palette + start, n * sizeof(GfxColor));
}

void gfxUpdatePalette(int start, int n) {
	gfxUpdatePaletteNoDirty(start, n);
	gfxScreenDirty();
}

void gfxPaletteSetNoUpdate(const byte *data, uint start, uint n) {
	assert(start + n <= 256);
	readPalette(g_gfx.palette + start, data, n);
}

void gfxPaletteSet(const byte *data, uint start, uint n) {
	gfxPaletteSetNoUpdate(data, start, n);
	gfxUpdatePalette(start, n);
}

void gfxPaletteSetColor(byte c, byte r, byte g, byte b) {
	g_gfx.palette[c].r = r;
	g_gfx.palette[c].g = g;
	g_gfx.palette[c].b = b;
	gfxUpdatePalette(0, 256);
}

static byte u8Interp(byte a, byte b, float rate) {
	int d = b - a;
	return a + d * rate;
}

// crossfade the given color indices
static void paletteCrossfadeIndices(GfxColor *newPal, GfxColor *old, byte *colors, uint nrColors,
		uint maxColor, uint ms, bool (*cb)(float, void *), void *data) {
	ms *= g_config.transitionSpeed;

	uint startT = vmGetTicks();
	uint t = 0;
	while (t < ms && !g_engine->shouldQuit()) {
		float rate = (float)t / (float)ms;
		for (uint i = 0; i < nrColors; i++) {
			byte c = colors[i];
			g_gfx.palette[c].r = u8Interp(old[c].r, newPal[c].r, rate);
			g_gfx.palette[c].g = u8Interp(old[c].g, newPal[c].g, rate);
			g_gfx.palette[c].b = u8Interp(old[c].b, newPal[c].b, rate);
		}
		gfxUpdatePalette(0, maxColor + 1);

		vmPeek();
		vmDelay(fadeFrameTime());
		t = vmGetTicks() - startT;

		if (cb && !cb(rate, data))
			break;
	}

	for (uint i = 0; i < nrColors; i++) {
		byte c = colors[i];
		g_gfx.palette[c].r = newPal[c].r;
		g_gfx.palette[c].g = newPal[c].g;
		g_gfx.palette[c].b = newPal[c].b;
	}
	gfxUpdatePalette(0, maxColor + 1);
	gfxUpdate();
}

void gfxCrossfadeColors(byte *pal, byte *colorsIn, uint nrColorsIn, uint ms,
		bool (*cb)(float, void *), void *data) {
	GfxColor newPal[256];
	readPalette(newPal, pal, 256);

	GfxColor old[256];
	memcpy(old, g_gfx.screenPalette, sizeof(old));
	memcpy(g_gfx.palette, old, sizeof(old));

	byte colors[256];
	uint nrColors = 0;
	uint maxColor = 0;
	for (uint i = 0; i < nrColorsIn; i++) {
		byte c = colorsIn[i];
		if (old[c].r != newPal[c].r || old[c].g != newPal[c].g || old[c].b != newPal[c].b) {
			colors[nrColors++] = c;
			maxColor = MAX<uint>(maxColor, c);
		}
	}
	if (nrColors == 0)
		return;

	paletteCrossfadeIndices(newPal, old, colors, nrColors, maxColor, ms, cb, data);
}

void gfxPaletteCrossfadeColors(GfxColor *newPal, uint start, uint n, uint ms) {
	assert(start + n <= 256);
	GfxColor old[256];
	memcpy(old, g_gfx.screenPalette, sizeof(old));
	memcpy(g_gfx.palette, old, sizeof(old));

	// palette indices that need to be interpolated
	byte colors[256];
	uint nrColors = 0;
	uint maxColor = 0;
	for (uint i = start; i < start + n; i++) {
		if (old[i].r != newPal[i].r || old[i].g != newPal[i].g || old[i].b != newPal[i].b) {
			colors[nrColors++] = i;
			maxColor = MAX(maxColor, i);
		}
	}
	if (nrColors == 0)
		return;

	paletteCrossfadeIndices(newPal, old, colors, nrColors, maxColor, ms, nullptr, nullptr);
}

void gfxPaletteCrossfade(const byte *data, uint start, uint n, uint ms) {
	GfxColor newPal[256];
	readPalette(newPal, data, 256);
	gfxPaletteCrossfadeColors(newPal, start, n, ms);
}

void gfxPaletteCrossfadeTo(byte r, byte g, byte b, uint start, uint n, uint ms) {
	GfxColor newPal[256];
	for (int i = 0; i < 256; i++) {
		newPal[i].r = r;
		newPal[i].g = g;
		newPal[i].b = b;
		newPal[i].a = 255;
	}
	gfxPaletteCrossfadeColors(newPal, start, n, ms);
}

void gfxSetScreenSurface(uint i) {
	if (i >= kGfxNrSurfaces || !g_gfx.surface[i].s)
		error("Invalid surface number: %u", i);
	g_gfx.screen = i;
	gfxUpdatePalette(0, 256);
}

// AI5WIN.EXE doesn't clip: rectangles exceeding the destination surface
// write to wrong addresses.
bool gfxCopyClip(GfxSurface *src, GfxRect *srcR, GfxSurface *dst, GfxPoint *dstP) {
	if (srcR->x < 0) {
		srcR->w += srcR->x;
		dstP->x -= srcR->x;
		srcR->x = 0;
	}
	if (srcR->y < 0) {
		srcR->h += srcR->y;
		dstP->y -= srcR->y;
		srcR->y = 0;
	}
	if (dstP->x < 0) {
		srcR->w += dstP->x;
		srcR->x -= dstP->x;
		dstP->x = 0;
	}
	if (dstP->y < 0) {
		srcR->h += dstP->y;
		srcR->y -= dstP->y;
		dstP->y = 0;
	}
	if (srcR->x + srcR->w > src->w)
		srcR->w = src->w - srcR->x;
	if (srcR->y + srcR->h > src->h)
		srcR->h = src->h - srcR->y;
	if (dstP->x + srcR->w > dst->w)
		srcR->w = dst->w - dstP->x;
	if (dstP->y + srcR->h > dst->h)
		srcR->h = dst->h - dstP->y;
	return srcR->w > 0 && srcR->h > 0;
}

bool gfxFillClip(GfxSurface *s, GfxRect *r) {
	if (r->x < 0) {
		r->w += r->x;
		r->x = 0;
	}
	if (r->y < 0) {
		r->h += r->y;
		r->y = 0;
	}
	if (r->x + r->w > s->w)
		r->w = s->w - r->x;
	if (r->y + r->h > s->h)
		r->h = s->h - r->y;
	return r->w > 0 && r->h > 0;
}

static bool copyBegin(GfxSurface *src, GfxRect *srcR, GfxSurface *dst, GfxPoint *dstP) {
	if (!gfxCopyClip(src, srcR, dst, dstP)) {
		warning("Invalid copy");
		return false;
	}
	return true;
}

static bool fillBegin(GfxSurface *dst, GfxRect *r) {
	if (!gfxFillClip(dst, r)) {
		warning("Invalid fill");
		return false;
	}
	return true;
}

// plain copy with clipping; rows are moved top to bottom
static void rawCopy(int srcX, int srcY, int w, int h, GfxSurface *src, int dstX, int dstY, GfxSurface *dst) {
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!gfxCopyClip(src, &srcR, dst, &dstP))
		return;
	for (int row = 0; row < srcR.h; row++)
		memmove(gfxPixel(dst, dstP.x, dstP.y + row), gfxPixel(src, srcR.x, srcR.y + row), srcR.w * src->bytesPerPixel);
}

void gfxCopy(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI) {
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	if (g_game->bpp == 8) {
		GfxRect srcR = { srcX, srcY, w, h };
		GfxPoint dstP = { dstX, dstY };
		if (copyBegin(src, &srcR, dst, &dstP))
			rawCopy(srcR.x, srcR.y, srcR.w, srcR.h, src, dstP.x, dstP.y, dst);
	} else {
		rawCopy(srcX, srcY, w, h, src, dstX, dstY, dst);
	}
	gfxDirty(dstI, dstX, dstY, w, h);
}

void gfxIndexedCopyMasked(int srcX, int srcY, int w, int h, GfxSurface *src, int dstX, int dstY,
		GfxSurface *dst, byte maskColor) {
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!copyBegin(src, &srcR, dst, &dstP))
		return;
	for (int row = 0; row < srcR.h; row++) {
		const byte *s = gfxPixel(src, srcR.x, srcR.y + row);
		byte *d = gfxPixel(dst, dstP.x, dstP.y + row);
		for (int col = 0; col < srcR.w; col++) {
			if (s[col] != maskColor)
				d[col] = s[col];
		}
	}
}

// copy_masked, but only if the dst pixel color is greater than threshold
void gfxIndexedCopyMaskedDstGt(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY,
		uint dstI, byte maskColor, byte threshold) {
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!copyBegin(src, &srcR, dst, &dstP))
		return;
	for (int row = 0; row < srcR.h; row++) {
		const byte *s = gfxPixel(src, srcR.x, srcR.y + row);
		byte *d = gfxPixel(dst, dstP.x, dstP.y + row);
		for (int col = 0; col < srcR.w; col++) {
			if (s[col] != maskColor && d[col] > threshold)
				d[col] = s[col];
		}
	}
}

// color-keyed RGB24 copy
static void directCopyMasked(int srcX, int srcY, int w, int h, GfxSurface *src, int dstX, int dstY,
		GfxSurface *dst, uint32 maskColor) {
	GfxColor mask = gfxDecodeDirect(maskColor);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (!gfxCopyClip(src, &srcR, dst, &dstP))
		return;
	for (int row = 0; row < srcR.h; row++) {
		const byte *s = gfxPixel(src, srcR.x, srcR.y + row);
		byte *d = gfxPixel(dst, dstP.x, dstP.y + row);
		for (int col = 0; col < srcR.w; col++, s += 3, d += 3) {
			if (s[0] != mask.r || s[1] != mask.g || s[2] != mask.b)
				memcpy(d, s, 3);
		}
	}
}

void gfxCopyMasked(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI, uint32 maskColor) {
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	if (g_game->bpp == 8)
		gfxIndexedCopyMasked(srcX, srcY, w, h, src, dstX, dstY, dst, maskColor);
	else
		directCopyMasked(srcX, srcY, w, h, src, dstX, dstY, dst, maskColor);
	gfxDirty(dstI, dstX, dstY, w, h);
}

void gfxCopySwap(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI) {
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (copyBegin(src, &srcR, dst, &dstP)) {
		const int bpp = src->bytesPerPixel;
		for (int row = 0; row < srcR.h; row++) {
			byte *s = gfxPixel(src, srcR.x, srcR.y + row);
			byte *d = gfxPixel(dst, dstP.x, dstP.y + row);
			for (int i = 0; i < srcR.w * bpp; i++)
				SWAP(s[i], d[i]);
		}
	}
	gfxDirty(srcI, srcX, srcY, w, h);
	gfxDirty(dstI, dstX, dstY, w, h);
}

void gfxCompose(int fgX, int fgY, int w, int h, uint fgI, int bgX, int bgY, uint bgI,
		int dstX, int dstY, uint dstI, uint16 maskColor) {
	GfxSurface *fg = gfxGetSurface(fgI);
	GfxSurface *bg = gfxGetSurface(bgI);
	GfxSurface *dst = gfxGetSurface(dstI);
	if (g_game->bpp == 8) {
		GfxRect srcR = { bgX, bgY, w, h };
		GfxPoint dstP = { dstX, dstY };
		if (copyBegin(bg, &srcR, dst, &dstP))
			rawCopy(srcR.x, srcR.y, srcR.w, srcR.h, bg, dstP.x, dstP.y, dst);
		gfxIndexedCopyMasked(fgX, fgY, w, h, fg, dstX, dstY, dst, maskColor);
	} else {
		rawCopy(bgX, bgY, w, h, bg, dstX, dstY, dst);
		directCopyMasked(fgX, fgY, w, h, fg, dstX, dstY, dst, maskColor);
	}
	gfxDirty(dstI, dstX, dstY, w, h);
}

static void alphaBlendTo(const byte *bg, const byte *fg, byte *dst, byte alpha) {
	uint32 a = (uint32)alpha + 1;
	uint32 invA = 256 - (uint32)alpha;
	dst[0] = (byte)((a * fg[0] + invA * bg[0]) >> 8);
	dst[1] = (byte)((a * fg[1] + invA * bg[1]) >> 8);
	dst[2] = (byte)((a * fg[2] + invA * bg[2]) >> 8);
}

void gfxBlend(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI, byte alpha) {
	if (g_game->bpp == 8)
		error("Invalid bpp for gfxBlend");

	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	if (gfxCopyClip(src, &srcR, dst, &dstP)) {
		for (int row = 0; row < srcR.h; row++) {
			const byte *s = gfxPixel(src, srcR.x, srcR.y + row);
			byte *d = gfxPixel(dst, dstP.x, dstP.y + row);
			for (int col = 0; col < srcR.w; col++, s += 3, d += 3) {
				d[0] = (s[0] * alpha + d[0] * (255 - alpha)) / 255;
				d[1] = (s[1] * alpha + d[1] * (255 - alpha)) / 255;
				d[2] = (s[2] * alpha + d[2] * (255 - alpha)) / 255;
			}
		}
	}
	gfxDirty(dstI, dstX, dstY, w, h);
}

void gfxBlendMasked(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI, byte *mask) {
	if (g_game->bpp == 8)
		error("Invalid bpp for gfxBlendMasked");
	GfxSurface *src = gfxGetSurface(srcI);
	GfxSurface *dst = gfxGetSurface(dstI);
	GfxRect srcR = { srcX, srcY, w, h };
	GfxPoint dstP = { dstX, dstY };
	// FIXME: mask pointer is not adjusted for clipping (same as the original)
	if (!copyBegin(src, &srcR, dst, &dstP))
		return;

	for (int row = 0; row < srcR.h; row++) {
		byte *s = gfxPixel(src, srcR.x, srcR.y + row);
		byte *d = gfxPixel(dst, dstP.x, dstP.y + row);
		for (int col = 0; col < srcR.w; col++, s += 3, d += 3, mask++) {
			if (*mask == 0) {
				// nothing
			} else if (*mask > 15) {
				memcpy(d, s, 3);
			} else {
				alphaBlendTo(d, s, d, *mask * 16 - 8);
			}
		}
	}
	gfxDirty(dstI, dstX, dstY, w, h);
}

void gfxBlendWithMaskColorTo(int aX, int aY, int w, int h, uint aI, int bX, int bY, uint bI,
		int dstX, int dstY, uint dstI, int maskW, int maskH, byte *mask) {
	GfxSurface *a = gfxGetSurface(aI);
	GfxSurface *b = gfxGetSurface(bI);
	GfxSurface *dst = gfxGetSurface(dstI);
	if (aX < 0 || aY < 0 || aX + w > a->w || aY + h > a->h)
		return;
	if (bX < 0 || bY < 0 || bX + w > b->w || bY + h > b->h)
		return;
	if (dstX < 0 || dstY < 0 || dstX + w > dst->w || dstY + h > dst->h)
		return;

	for (int row = 0; row < h; row++) {
		const byte *srcPx = gfxPixel(a, aX, aY + row);
		const byte *newPx = gfxPixel(b, bX, bY + row);
		byte *dstPx = gfxPixel(dst, dstX, dstY + row);
		// same as the original: the mask row does not advance
		const byte *maskPx = mask + aY * maskW + aX;
		for (int col = 0; col < w; col++, srcPx += 3, newPx += 3, dstPx += 3, maskPx++) {
			if (*maskPx == 0)
				memcpy(dstPx, srcPx, 3);
			else if (*maskPx > 7)
				memcpy(dstPx, newPx, 3);
			else
				// the original passes (dst, src, new), writing into b
				alphaBlendTo(srcPx, newPx, dstPx, *maskPx * 32 - 16);
		}
	}
}

void gfxInvertColors(int x, int y, int w, int h, uint i) {
	if (g_game->bpp != 8)
		error("Invalid bpp for gfxInvertColors");
	GfxSurface *s = gfxGetSurface(i);
	GfxRect r = { x, y, w, h };
	if (!fillBegin(s, &r))
		return;
	for (int row = 0; row < r.h; row++) {
		byte *p = gfxPixel(s, r.x, r.y + row);
		for (int col = 0; col < r.w; col++)
			p[col] ^= 0xf;
	}
	gfxDirty(i, x, y, w, h);
}

void gfxFill(int x, int y, int w, int h, uint i, uint32 c) {
	GfxSurface *dst = gfxGetSurface(i);
	GfxRect r = { x, y, w, h };
	if (gfxFillClip(dst, &r)) {
		if (g_game->bpp == 8) {
			for (int row = 0; row < r.h; row++)
				memset(gfxPixel(dst, r.x, r.y + row), c, r.w);
		} else {
			GfxColor color = gfxDecodeDirect(c);
			for (int row = 0; row < r.h; row++) {
				byte *p = gfxPixel(dst, r.x, r.y + row);
				for (int col = 0; col < r.w; col++, p += 3) {
					p[0] = color.r;
					p[1] = color.g;
					p[2] = color.b;
				}
			}
		}
	}
	gfxDirty(i, x, y, w, h);
}

void gfxSwapColors(int x, int y, int w, int h, uint i, uint32 c1, uint32 c2) {
	GfxSurface *s = gfxGetSurface(i);
	GfxRect r = { x, y, w, h };
	if (fillBegin(s, &r)) {
		if (g_game->bpp == 8) {
			for (int row = 0; row < r.h; row++) {
				byte *p = gfxPixel(s, r.x, r.y + row);
				for (int col = 0; col < r.w; col++) {
					if (p[col] == (byte)c1)
						p[col] = c2;
					else if (p[col] == (byte)c2)
						p[col] = c1;
				}
			}
		} else {
			GfxColor color1 = gfxDecodeDirect(c1);
			GfxColor color2 = gfxDecodeDirect(c2);
			for (int row = 0; row < r.h; row++) {
				byte *p = gfxPixel(s, r.x, r.y + row);
				for (int col = 0; col < r.w; col++, p += 3) {
					// same as the original: pixels that are not color 2 become color 2
					const GfxColor &c = (p[0] == color2.r && p[1] == color2.g && p[2] == color2.b) ? color1 : color2;
					p[0] = c.r;
					p[1] = c.g;
					p[2] = c.b;
				}
			}
		}
	}
	gfxDirty(i, x, y, w, h);
}

void gfxBlendFill(int x, int y, int w, int h, uint i, uint32 c, byte rate) {
	if (g_game->bpp == 8)
		error("Invalid bpp for gfxBlendFill");
	GfxSurface *s = gfxGetSurface(i);
	GfxRect r = { x, y, w, h };
	if (!fillBegin(s, &r))
		return;

	uint32 a = (uint32)rate + 1;
	uint32 invA = 256 - (uint32)rate;
	GfxColor color = gfxDecodeDirect(c);
	// the original multiplies into 8-bit fields, which truncates
	byte cr = color.r * a, cg = color.g * a, cb = color.b * a;
	for (int row = 0; row < r.h; row++) {
		byte *p = gfxPixel(s, r.x, r.y + row);
		for (int col = 0; col < r.w; col++, p += 3) {
			p[0] = (byte)((cr + invA * p[0]) >> 8);
			p[1] = (byte)((cg + invA * p[1]) >> 8);
			p[2] = (byte)((cb + invA * p[2]) >> 8);
		}
	}
	gfxDirty(i, x, y, w, h);
}

void gfxBlitScaled(GfxSurface *src, const GfxRect &srcR, GfxSurface *dst, const GfxRect &dstR) {
	if (srcR.w <= 0 || srcR.h <= 0 || dstR.w <= 0 || dstR.h <= 0)
		return;
	assert(src->bytesPerPixel == dst->bytesPerPixel);
	const int bpp = src->bytesPerPixel;
	for (int y = MAX(dstR.y, 0); y < MIN(dstR.y + dstR.h, dst->h); y++) {
		int sy = srcR.y + (y - dstR.y) * srcR.h / dstR.h;
		if (sy < 0 || sy >= src->h)
			continue;
		for (int x = MAX(dstR.x, 0); x < MIN(dstR.x + dstR.w, dst->w); x++) {
			int sx = srcR.x + (x - dstR.x) * srcR.w / dstR.w;
			if (sx < 0 || sx >= src->w)
				continue;
			memcpy(gfxPixel(dst, x, y), gfxPixel(src, sx, sy), bpp);
		}
	}
}

void gfxDrawCG(uint i, const CG &cg) {
	GfxSurface *s = gfxGetSurface(i);
	if (g_game->bpp == 8) {
		if (!cg.isIndexed())
			error("Tried to draw direct-color CG to indexed surface");
		assert(cg.x + cg.w <= (uint)s->w && cg.y + cg.h <= (uint)s->h);
		for (uint row = 0; row < cg.h; row++)
			memcpy(gfxPixel(s, cg.x, cg.y + row), cg.pixels.data() + row * cg.w, cg.w);
	} else {
		if (cg.isIndexed())
			error("Tried to draw indexed CG to direct-color surface");
		// RGBA blit: alpha blended onto the surface
		GfxSurface src = { const_cast<byte *>(&cg.pixels[0]), (int)cg.w, (int)cg.h, (int)cg.w * 4, 4 };
		GfxRect srcR = { 0, 0, (int)cg.w, (int)cg.h };
		GfxPoint dstP = { (int)cg.x, (int)cg.y };
		if (gfxCopyClip(&src, &srcR, s, &dstP)) {
			for (int row = 0; row < srcR.h; row++) {
				const byte *sp = gfxPixel(&src, srcR.x, srcR.y + row);
				byte *dp = gfxPixel(s, dstP.x, dstP.y + row);
				for (int col = 0; col < srcR.w; col++, sp += 4, dp += 3) {
					uint a = sp[3];
					if (a == 255) {
						memcpy(dp, sp, 3);
					} else {
						dp[0] = (sp[0] * a + dp[0] * (255 - a)) / 255;
						dp[1] = (sp[1] * a + dp[1] * (255 - a)) / 255;
						dp[2] = (sp[2] * a + dp[2] * (255 - a)) / 255;
					}
				}
			}
		}
	}
	gfxDirty(i, cg.x, cg.y, cg.w, cg.h);
}

} // End of namespace AI5
