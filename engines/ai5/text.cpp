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
#include "common/ptr.h"
#include "common/textconsole.h"
#include "common/ustr.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/surface.h"

#include "ai5/text.h"
#include "ai5/vm.h"

namespace AI5 {

bool g_textAntialias = false;
TextShadowType g_textShadow = kTextShadowNone;
bool g_yunoEng = false;

struct FontEntry {
	int size;
	int yOff;
	Graphics::Font *font;
};

enum {
	kMaxFonts = 256,
	kUIFontSize = 12
};

static FontEntry g_fonts[kMaxFonts];
static int g_nrFonts = 0;
static FontEntry *g_curFont = nullptr;
static bool g_curBold = false;
static Graphics::Font *g_uiFont = nullptr;
static int g_uiAscent = 0;

const char *sjisNextChar(const char *s, uint32 &ch) {
	char buf[3] = { s[0], 0, 0 };
	int len = 1;
	if (sjisIs2Byte(s[0]) && s[1]) {
		buf[1] = s[1];
		len = 2;
	}
	Common::U32String u(buf, Common::kWindows932);
	ch = u.empty() ? '?' : u[0];
	return s + len;
}

enum FontType {
	kFontSmall,
	kFontLarge,
	kFontEng
};

// Fonts: a user-supplied msgothic.ttc, then the fonts shipped in ai5.dat,
// then the CJK fonts shipped with ScummVM.
static Graphics::Font *openFont(FontType type, int size, bool mono) {
	Graphics::TTFRenderMode mode = mono ? Graphics::kTTFRenderModeMonochrome : Graphics::kTTFRenderModeLight;

	Common::File *f = new Common::File();
	if (f->open("msgothic.ttc")) {
		Graphics::Font *font = Graphics::loadTTFFont(f, DisposeAfterUse::YES, size, Graphics::kTTFSizeModeCharacter, 0, 0, mode);
		if (font)
			return font;
	} else {
		delete f;
	}

	static const char *const shipped[] = { "DotGothic16-Regular.ttf", "Kosugi-Regular.ttf", "NotoSansJP-Thin.ttf" };
	Graphics::Font *font = Graphics::loadTTFFontFromArchive(shipped[type], size, Graphics::kTTFSizeModeCharacter, 0, 0, mode);
	if (!font)
		font = Graphics::loadTTFFontFromArchive("NotoSansJP-Regular.otf", size, Graphics::kTTFSizeModeCharacter, 0, 0, mode);
	if (!font)
		error("AI5: no Japanese font found");
	return font;
}

static FontEntry *fontLookup(int size) {
	for (int i = 0; i < g_nrFonts; i++) {
		if (g_fonts[i].size == size)
			return &g_fonts[i];
	}
	return nullptr;
}

static FontEntry *fontInsert(int size, Graphics::Font *font) {
	int ascent = font->getFontAscent();
	Common::Rect a = font->getBoundingBox('A');
	int maxY = ascent - a.top;

	// Rough y-offset: align the baseline to the point size and center on the
	// height of 'A'.
	int yOff = ascent - size;
	yOff += (size - (maxY - 2)) / 2;
	yOff -= g_game->bpp == 8 ? 1 : 2;

	if (g_nrFonts >= kMaxFonts)
		error("Font table is full");
	g_fonts[g_nrFonts].size = size;
	g_fonts[g_nrFonts].yOff = yOff;
	g_fonts[g_nrFonts].font = font;
	return &g_fonts[g_nrFonts++];
}

void gfxTextInit() {
	gfxTextSetSize(memGetSysvar16(kSysVarFontHeight), memGetSysvar16(kSysVarFontWeight));
}

void gfxTextFini() {
	for (int i = 0; i < g_nrFonts; i++)
		delete g_fonts[i].font;
	g_nrFonts = 0;
	g_curFont = nullptr;
	delete g_uiFont;
	g_uiFont = nullptr;
}

void gfxTextSetColors(uint32 bg, uint32 fg) {
	g_gfx.text.bg = bg;
	g_gfx.text.fg = fg;
	if (g_game->bpp == 16) {
		g_gfx.text.bgColor = gfxDecodeBgr555(bg);
		g_gfx.text.fgColor = gfxDecodeBgr555(fg);
	} else if (g_game->bpp == 24) {
		g_gfx.text.bgColor = gfxDecodeBgr(bg);
		g_gfx.text.fgColor = gfxDecodeBgr(fg);
	}
}

void gfxTextGetColors(uint32 &bg, uint32 &fg) {
	bg = g_gfx.text.bg;
	fg = g_gfx.text.fg;
}

void gfxTextFill(int x, int y, int w, int h, uint i) {
	gfxFill(x, y, w, h, i, g_gfx.text.bg);
}

void gfxTextSwapColors(int x, int y, int w, int h, uint i) {
	gfxSwapColors(x, y, w, h, i, g_gfx.text.bg, g_gfx.text.fg);
}

// Render a glyph as a 0/1 mask. Bold is emulated by drawing twice.
static void renderGlyph(uint32 ch, Graphics::Surface &out) {
	Graphics::Font *font = g_curFont->font;
	int w = font->getCharWidth(ch) + (g_curBold ? 1 : 0);
	int h = font->getFontHeight();
	out.create(MAX(w, 1), MAX(h, 1), Graphics::PixelFormat::createFormatCLUT8());
	out.fillRect(Common::Rect(out.w, out.h), 0);
	font->drawChar(&out, ch, 0, 0, 1);
	if (g_curBold)
		font->drawChar(&out, ch, 1, 0, 1);
}

static void glyphBlitIndexed(const Graphics::Surface &glyph, int dstX, int dstY, GfxSurface *s) {
	int glyphX = 0, glyphY = 0;
	int glyphW = glyph.w, glyphH = glyph.h;
	if (dstX < 0) {
		glyphW += dstX;
		glyphX -= dstX;
		dstX = 0;
	}
	if (dstY < 0) {
		glyphH += dstY;
		glyphY -= dstY;
		dstY = 0;
	}
	if (dstX + glyphW > s->w)
		glyphW = s->w - dstX;
	if (dstY + glyphH > s->h)
		glyphH = s->h - dstY;
	if (glyphW <= 0 || glyphH <= 0)
		return;

	// prevent text from overflowing at the bottom
	glyphH = MIN(g_curFont->yOff + g_curFont->size, glyphH);

	for (int row = 0; row < glyphH; row++) {
		const byte *src = (const byte *)glyph.getBasePtr(glyphX, glyphY + row);
		byte *dst = gfxPixel(s, dstX, dstY + row);
		for (int col = 0; col < glyphW; col++, dst++, src++) {
			if (!*src)
				continue;
			*dst = g_gfx.text.fg;
			// shadows can spill past the clipped area, as in the original;
			// keep them inside the surface
			int x = dstX + col, y = dstY + row;
			if (g_textShadow == kTextShadowA) {
				if (x + 2 < s->w) {
					dst[1] = g_gfx.text.bg;
					dst[2] = g_gfx.text.bg;
				}
				if (x + 1 < s->w && y + 1 < s->h)
					*(dst + s->pitch + 1) = g_gfx.text.bg;
			} else if (g_textShadow == kTextShadowB) {
				if (x + 2 < s->w && y + 2 < s->h)
					*(dst + s->pitch * 2 + 2) = g_gfx.text.bg;
			}
		}
	}
}

static void putDirect(GfxSurface *s, int x, int y, const GfxColor &c) {
	if (x < 0 || y < 0 || x >= s->w || y >= s->h)
		return;
	byte *p = gfxPixel(s, x, y);
	p[0] = c.r;
	p[1] = c.g;
	p[2] = c.b;
}

// draw a 1-pixel outline in the background color, then the glyph
static void glyphBlitDirect(const Graphics::Surface &glyph, int dstX, int dstY, GfxSurface *s) {
	for (int row = 0; row < glyph.h; row++) {
		const byte *src = (const byte *)glyph.getBasePtr(0, row);
		for (int col = 0; col < glyph.w; col++) {
			if (!src[col])
				continue;
			for (int dy = -1; dy <= 1; dy++) {
				for (int dx = -1; dx <= 1; dx++)
					putDirect(s, dstX + col + dx, dstY + row + dy, g_gfx.text.bgColor);
			}
		}
	}
	for (int row = 0; row < glyph.h; row++) {
		const byte *src = (const byte *)glyph.getBasePtr(0, row);
		for (int col = 0; col < glyph.w; col++) {
			if (src[col])
				putDirect(s, dstX + col, dstY + row, g_gfx.text.fgColor);
		}
	}
}

static uint drawGlyph(GfxSurface *dst, int x, int y, uint32 ch, GfxRect &damage) {
	Graphics::Surface glyph;
	renderGlyph(ch, glyph);
	y -= g_curFont->yOff;
	uint w = glyph.w;
	if (g_game->bpp == 8) {
		glyphBlitIndexed(glyph, x, y, dst);
		damage.x = x;
		damage.y = y;
		damage.w = glyph.w;
		damage.h = glyph.h;
	} else {
		glyphBlitDirect(glyph, x, y, dst);
		damage.x = x - 1;
		damage.y = y - 1;
		damage.w = glyph.w + 2;
		damage.h = glyph.h + 2;
	}
	glyph.free();
	return w;
}

uint gfxTextDrawGlyphTo(GfxSurface *dst, int x, int y, uint32 ch) {
	if (!g_curFont)
		return 0;
	GfxRect damage;
	return drawGlyph(dst, x, y, ch, damage);
}

uint gfxTextDrawGlyph(int x, int y, uint i, uint32 ch) {
	if (!g_curFont)
		return 0;
	GfxRect damage;
	uint r = drawGlyph(gfxGetSurface(i), x, y, ch, damage);
	gfxDirty(i, damage.x, damage.y, damage.w, damage.h);
	return r;
}

static bool uiFontInit() {
	if (g_uiFont)
		return true;
	g_uiFont = Graphics::loadTTFFontFromArchive("wine_tahoma.ttf", kUIFontSize, Graphics::kTTFSizeModeCharacter, 0, 0, Graphics::kTTFRenderModeMonochrome);
	if (!g_uiFont)
		g_uiFont = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", kUIFontSize, Graphics::kTTFSizeModeCharacter, 0, 0, Graphics::kTTFRenderModeMonochrome);
	if (!g_uiFont) {
		warning("AI5: no UI font found");
		return false;
	}
	// ASCII ascent based on the height of 'A'
	g_uiAscent = g_uiFont->getFontAscent() - g_uiFont->getBoundingBox('A').top;
	return true;
}

void uiDrawText(GfxSurface *s, int x, int y, const char *text, GfxColor color) {
	if (!uiFontInit())
		return;
	Common::U32String str(text, Common::kUtf8);
	int w = g_uiFont->getStringWidth(str);
	int h = g_uiFont->getFontHeight();
	Graphics::Surface tmp;
	tmp.create(MAX(w, 1), MAX(h, 1), Graphics::PixelFormat::createFormatCLUT8());
	tmp.fillRect(Common::Rect(tmp.w, tmp.h), 0);
	g_uiFont->drawString(&tmp, str, 0, 0, tmp.w, 1);

	int dy = y - (g_uiFont->getFontAscent() - g_uiAscent) - g_uiAscent / 2;
	for (int row = 0; row < tmp.h; row++) {
		const byte *src = (const byte *)tmp.getBasePtr(0, row);
		for (int col = 0; col < tmp.w; col++) {
			int px = x + col, py = dy + row;
			if (!src[col] || px < 0 || py < 0 || px >= s->w || py >= s->h)
				continue;
			byte *p = gfxPixel(s, px, py);
			if (s->bytesPerPixel == 4) {
				p[0] = color.r;
				p[1] = color.g;
				p[2] = color.b;
				p[3] = 255;
			} else if (s->bytesPerPixel == 3) {
				p[0] = color.r;
				p[1] = color.g;
				p[2] = color.b;
			}
		}
	}
	tmp.free();
}

int uiMeasureText(const char *text) {
	if (!uiFontInit())
		return 0;
	return g_uiFont->getStringWidth(Common::U32String(text, Common::kUtf8));
}

void gfxTextSetSize(int size, int weight) {
	FontEntry *font = fontLookup(size);
	if (!font) {
		FontType type = g_yunoEng ? kFontEng : (size <= 18 ? kFontSmall : kFontLarge);
		font = fontInsert(size, openFont(type, size, !g_textAntialias));
	}
	g_curBold = weight != 0;
	g_curFont = font;
	g_gfx.text.size = size;
}

void gfxTextSetWeight(int weight) {
	g_curBold = weight != 0;
}

uint gfxTextSizeChar(uint32 ch) {
	return g_curFont ? g_curFont->font->getCharWidth(ch) : 0;
}

} // End of namespace AI5
