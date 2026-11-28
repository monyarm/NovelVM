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

#include "common/rect.h"

#include "ai5/ai5.h"
#include "ai5/asset.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/memory.h"
#include "ai5/vm.h"

#include "ai5/games/shuusaku.h"
#include "ai5/games/shuusaku_data.h"

namespace AI5 {

namespace {

enum {
	kScreen = 0,     // view.gpx on 1, viewpart.gpx on 2, viewmono.gpx on 3, kanryou.gpx on 6
	kCsEnabled = 1,
	kCsHover = 2,
	kCsDisabled = 3
};

struct CharSelect {
	Common::Rect pos;
	bool enabled;
	uint32 enabledFlag;
	uint32 completeFlag;
};

CharSelect characters[kNrChar] = {
	{ Common::Rect(11,  43,  165, 252),  false, 120,  10 }, // NAGISA
	{ Common::Rect(166, 43,  320, 252),  false, 121,  11 }, // KAORI
	{ Common::Rect(321, 43,  475, 252),  false, 122,  12 }, // SHIHO
	{ Common::Rect(476, 43,  630, 252),  false, 123,  13 }, // CHIAKI
	{ Common::Rect(11,  253, 165, 462),  false, 123,  14 }, // ASAMI (flag 123 as in the original)
	{ Common::Rect(166, 253, 320, 462),  false, 125,  15 }, // MOEKO
	{ Common::Rect(321, 253, 475, 462),  false, 2793, 16 }, // ERI
	{ Common::Rect(476, 253, 630, 462),  false, 129,  17 }  // AYAKA
};

void blitHover(CharSelect *ch) {
	gfxCopy(ch->pos.left, ch->pos.top, ch->pos.width(), ch->pos.height(),
			kCsHover, ch->pos.left, ch->pos.top, kScreen);
}

void blitUnhover(CharSelect *ch) {
	gfxCopy(ch->pos.left, ch->pos.top, ch->pos.width(), ch->pos.height(),
			kCsEnabled, ch->pos.left, ch->pos.top, kScreen);
}

int charHovered = -1;

int charSelectHandleInput() {
	if (inputDown(kInputCancel))
		return 0;

	uint mx, my;
	cursorGetPos(mx, my);

	bool hovering = false;
	for (int i = 0; i < kNrChar; i++) {
		if (!characters[i].enabled)
			continue;
		if (characters[i].pos.contains((int16)mx, (int16)my)) {
			hovering = true;
			if (charHovered != i) {
				blitHover(&characters[i]);
				if (charHovered >= 0)
					blitUnhover(&characters[charHovered]);
				charHovered = i;
			}
			if (inputDown(kInputActivate)) {
				inputWaitUntilUp(kInputActivate);
				return i + 1;
			}
		}
	}

	if (!hovering && charHovered >= 0) {
		blitUnhover(&characters[charHovered]);
		charHovered = -1;
	}

	return -1;
}

enum {
	kThumb = 1,
	kThumbW = 120,
	kThumbH = 90
};

inline int thumbSrcX(int i) { return (i % 5) * 120; }
inline int thumbSrcY(int i) { return 90 + (i / 5) * 90; }
inline int thumbDstX(int i) { return 12 + (i % 5) * 124; }
inline int thumbDstY(int i) { return 44 + (i / 5) * 94; }

Common::Rect sceneThumb(int i) {
	return Common::Rect(thumbDstX(i), thumbDstY(i),
			thumbDstX(i) + kThumbW, thumbDstY(i) + kThumbH);
}

void blitCrosshair(int i) {
	gfxCopyMasked(0, 0, kThumbW, kThumbH, kThumb, thumbDstX(i), thumbDstY(i), kScreen, kMaskColor);
}

void blitQuestionMark(int i) {
	gfxCopy(120, 0, kThumbW, kThumbH, kThumb, thumbDstX(i), thumbDstY(i), kScreen);
}

void blitThumbnail(int i) {
	gfxCopy(thumbSrcX(i), thumbSrcY(i), kThumbW, kThumbH, kThumb,
			thumbDstX(i), thumbDstY(i), kScreen);
}

int sceneHovered = -1;

int sceneSelectHandleInput(const Scene *scenes, bool sceneEnabled[20]) {
	if (inputDown(kInputCancel))
		return 0;

	uint mx, my;
	cursorGetPos(mx, my);

	bool hovering = false;
	for (int i = 0; i < 20 && scenes[i].flagNo; i++) {
		if (!sceneEnabled[i])
			continue;
		Common::Rect r = sceneThumb(i);
		if (r.contains((int16)mx, (int16)my)) {
			hovering = true;
			if (sceneHovered != i) {
				blitCrosshair(i);
				if (sceneHovered >= 0)
					blitThumbnail(sceneHovered);
				sceneHovered = i;
			}
			if (inputDown(kInputActivate)) {
				inputWaitUntilUp(kInputActivate);
				return i + 1;
			}
		}
	}

	if (!hovering && sceneHovered >= 0) {
		blitThumbnail(sceneHovered);
		sceneHovered = -1;
	}

	return -1;
}

void _loadImage(const char *name, int i) {
	Common::SharedPtr<CG> cg = assetCgLoad(name);
	if (!cg) {
		warning("Failed to load CG \"%s\"", name);
		return;
	}

	gfxDrawCG(i, *cg);
	if (!cg->palette.empty())
		memcpy(g_mem->palette + 10 * 4, cg->palette.data() + 10 * 4, 236 * 4);
}

} // namespace

int shuusakuSceneViewerCharSelect() {
	charHovered = -1;

	for (int i = 0; i < kNrChar; i++) {
		CharSelect *ch = &characters[i];
		ch->enabled = memGetVar4Packed(ch->enabledFlag) == 9;
		if (!ch->enabled) {
			gfxCopy(ch->pos.left, ch->pos.top, ch->pos.width(), ch->pos.height(),
					kCsDisabled, ch->pos.left, ch->pos.top, kScreen);
		}
		if (memGetVar4Packed(10 + i)) {
			int x = (i == kCharEri) ? 151 : 0;
			gfxCopyMasked(x, 0, 151, 206, 6, ch->pos.left + 1, ch->pos.top + 1, kScreen, kMaskColor);
			gfxCopyMasked(x, 0, 151, 206, 6, ch->pos.left + 1, ch->pos.top + 1, kCsEnabled, kMaskColor);
			gfxCopyMasked(x, 0, 151, 206, 6, ch->pos.left + 1, ch->pos.top + 1, kCsHover, kMaskColor);
		}
	}

	shuusakuCrossfade(g_mem->palette, false);

	int clicked = -1;
	while (!g_engine->shouldQuit()) {
		vmPeek();
		clicked = charSelectHandleInput();
		if (clicked >= 0)
			break;
		vmDelay(16);
	}
	if (g_engine->shouldQuit())
		clicked = 0;

	return clicked;
}

int shuusakuSceneViewerSceneSelect(int ch, bool needBg) {
	sceneHovered = -1;

	// load background CG if needed
	if (needBg) {
		byte pal[256 * 4] = {0};
		gfxPaletteSet(pal + 10 * 4, 10, 236);
		_loadImage("choice.gpx", 0);
	}

	// draw unlocked scenes
	const Scene *list = kSceneLists[ch];
	bool sceneEnabled[20] = {false};
	for (int i = 0; list[i].flagNo; i++) {
		if (memGetVar4Packed(list[i].flagNo)) {
			blitThumbnail(i);
			sceneEnabled[i] = true;
		} else {
			blitQuestionMark(i);
		}
	}

	if (needBg)
		shuusakuCrossfade(g_mem->palette, true);

	// save screen to surface 8
	gfxCopy(0, 0, 640, 480, 0, 0, 0, 8);

	int clicked = -1;
	while (!g_engine->shouldQuit()) {
		vmPeek();
		clicked = sceneSelectHandleInput(list, sceneEnabled);
		if (clicked >= 0)
			break;
		vmDelay(16);
	}
	if (g_engine->shouldQuit())
		clicked = 0;

	if (clicked <= 0) {
		byte pal[256 * 4];
		memset(pal, 0, 256 * 4);
		shuusakuCrossfade(pal, true);
		return 0;
	}

	_loadImage(list[clicked - 1].cgName, 2);
	gfxPaletteSet(g_mem->palette + 42 * 4, 42, 204);

	Common::Rect r = sceneThumb(clicked - 1);
	shuusakuZoom(r.left, r.top, r.width(), r.height(), 2);
	return list[clicked - 1].sceneId;
}

} // namespace AI5
