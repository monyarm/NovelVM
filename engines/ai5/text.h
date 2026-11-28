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

#ifndef AI5_TEXT_H
#define AI5_TEXT_H

#include "common/scummsys.h"

#include "ai5/gfx.h"

namespace AI5 {

enum TextShadowType {
	kTextShadowNone = 0,
	kTextShadowA = 1,
	kTextShadowB = 2
};

// default: false
extern bool g_textAntialias;
extern TextShadowType g_textShadow;
extern bool g_yunoEng;

void gfxTextInit();
void gfxTextFini();
void gfxTextSetColors(uint32 bg, uint32 fg);
void gfxTextGetColors(uint32 &bg, uint32 &fg);
void gfxTextSetSize(int size, int weight);
void gfxTextSetWeight(int weight);
void gfxTextFill(int x, int y, int w, int h, uint i);
void gfxTextSwapColors(int x, int y, int w, int h, uint i);
uint gfxTextDrawGlyph(int x, int y, uint i, uint32 ch);
uint gfxTextDrawGlyphTo(GfxSurface *dst, int x, int y, uint32 ch);
uint gfxTextSizeChar(uint32 ch);

/** Draw UTF-8 text with the small UI font. */
void uiDrawText(GfxSurface *s, int x, int y, const char *text, GfxColor color);
int uiMeasureText(const char *text);

inline bool sjisIs2Byte(char c) {
	byte b = c;
	return (b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc);
}

/** Decode one Shift-JIS character to Unicode; returns the next character. */
const char *sjisNextChar(const char *s, uint32 &ch);

} // End of namespace AI5

#endif // AI5_TEXT_H
