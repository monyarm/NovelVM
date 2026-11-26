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

#ifndef AI5_EFFECT_H
#define AI5_EFFECT_H

#include "common/scummsys.h"

namespace AI5 {

void gfxBlinkFade(int x, int y, int w, int h, uint dstI);
void gfxFadeDown(int x, int y, int w, int h, uint dstI, int srcI);
void gfxFadeRight(int x, int y, int w, int h, uint dstI, int srcI);
void gfxPixelate(int x, int y, int w, int h, uint dstI, uint mag);
void gfxFadeProgressive(int x, int y, int w, int h, uint dstI);
void gfxCopyProgressive(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI);
void gfxPixelCrossfade(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI,
		uint frameT, bool (*update)(float t, void *data), void *data);
void gfxPixelCrossfadeMasked(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY, uint dstI,
		uint32 maskColor);
void gfxPixelCrossfadeMaskedIndexed(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY,
		uint dstI, byte maskColor);
void gfxPixelCrossfadeMaskedIndexed8x8(int srcX, int srcY, int w, int h, uint srcI, int dstX, int dstY,
		uint dstI, byte maskColor, uint frameMs);
void gfxScaleH(uint i, int mag);
void gfxZoom(int srcX, int srcY, int w, int h, uint srcI, uint dstI, uint ms);

} // End of namespace AI5

#endif // AI5_EFFECT_H
