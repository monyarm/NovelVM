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

} // End of namespace AI5
