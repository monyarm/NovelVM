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
#include "ai5/audio.h"
#include "ai5/backlog.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/memory.h"
#include "ai5/sys.h"
#include "ai5/vm.h"

#include "ai5/games/shuusaku.h"

namespace AI5 {

namespace {

enum {
	kMenuButtonH = 34,
	kMenuEntryH = 52,
	kMenuBgY = 204,
	kMenuMaxH = 276
};

inline int menuEntryOff(int i) { return kMenuButtonH + kMenuEntryH * i; }
const int kMenuPgDownOff = menuEntryOff(4);

const int kMenuEntryBase = 520;
const int kMenuButtonBase = 936;
inline int menuEntrySrc(int i) { return kMenuEntryBase + kMenuEntryH * i; }
inline int menuButtonSrc(int i) { return kMenuButtonBase + kMenuButtonH * i; }

const int kMenuFrameGrayY = kMenuButtonH * 1;
const int kMenuFrameGreenY = kMenuButtonH * 2;
const int kMenuFrameBlueY = kMenuButtonH * 3;
const int kMenuFrameRedY = kMenuButtonH * 4;
const int kMenuFramePinkY = kMenuButtonH * 5;

enum MenuButton {
	kMenuNone = -1,
	kMenuPageUp = 0,
	kMenuEntry0 = 1,
	kMenuEntry3 = 4,
	kMenuPageDown = 5
};

struct MenuData {
	ShuusakuMenuEntry *entries;
	int nrEntries;
	int x;
	int y;
	int chunkW;
	int page;
	Common::Rect buttons[6];
};

void drawMenuText(uint32_t bodyAddr, int chunkW, int dstY) {
	memSetSysvar16(kSysVarTextStartX, 1);
	memSetSysvar16(kSysVarTextStartY, dstY + 8);
	memSetSysvar16(kSysVarTextEndX, chunkW * 2 + 1);
	memSetSysvar16(kSysVarTextEndY, dstY + 8 + 32);
	memSetSysvar16(kSysVarTextCursorX, 1);
	memSetSysvar16(kSysVarTextCursorY, dstY + 8);
	g_vm->ip.ptr = bodyAddr;
	g_game->vm.exec();
}

void initMenuEntry(int frameY, int dstY, int w, uint8_t color) {
	const int h = kMenuButtonH - 8;
	const int selY = dstY + kMenuEntryH * 4;
	// copy frame (unselected)
	gfxCopyMasked(0, frameY, w, h, 5, 0, dstY, 5, kMaskColor);
	gfxCopyMasked(0, frameY + 8, w, h, 5, 0, dstY + h, 5, kMaskColor);
	// copy frame + fill (selected)
	gfxCopy(0, dstY, w, kMenuEntryH, 5, 0, selY, 5);
	gfxFill(8, selY + 8, w - 16, kMenuEntryH - 16, 5, color);
}

void drawScrollButton(MenuData *menu, int srcY, int dstY, const char *ch) {
	const int w = (menu->chunkW + 1) * 16;
	const int selY = dstY + kMenuButtonH * 2;
	// copy bg
	gfxCopy(0, srcY, w, kMenuButtonH, 5, 0, dstY, 5);
	// copy frame
	gfxCopyMasked(0, kMenuFrameGrayY, w, kMenuButtonH, 5, 0, dstY, 5, kMaskColor);
	// copy frame+bg
	gfxCopy(0, dstY, w, kMenuButtonH, 5, 0, selY, 5);
	// fill inside of button
	gfxFill(8, selY + 8, w - 16, kMenuButtonH - 16, 5, 24);
	// XXX: chunk_w is mult by 16, cursor_x is mult by 8 -> chunk_w = midpoint
	memSetSysvar16(kSysVarTextCursorX, menu->chunkW);
	memSetSysvar16(kSysVarTextCursorY, dstY + 8);
	shuusakuDrawText(ch);
	memSetSysvar16(kSysVarTextCursorX, menu->chunkW);
	memSetSysvar16(kSysVarTextCursorY, dstY + kMenuButtonH * 2 + 8);
	shuusakuDrawText(ch);
}

void drawMenu(MenuData *menu) {
	memSetSysvar16(kSysVarDstSurface, 5);
	const int w = (menu->chunkW + 1) * 16;

	if (menu->page == 0) {
		// no up arrow: copy bg
		gfxCopy(0, kMenuBgY, w, kMenuButtonH, 5, 0, 936, 5);
		gfxCopy(0, kMenuBgY, w, kMenuButtonH, 5, menu->x, menu->y, 0);
	} else {
		// up arrow
		drawScrollButton(menu, kMenuBgY, 936, "\x81\xa3");
		gfxCopy(0, 936, w, kMenuButtonH, 5, menu->x, menu->y, 0);
	}

	int pageEntries = menu->nrEntries - menu->page * 4;
	for (int i = 0; i < 4; i++) {
		const int yOff = i * 52;
		const int bgY = kMenuBgY + kMenuButtonH + yOff;
		const int dstY = kMenuEntryBase + yOff;

		if (i >= pageEntries) {
			// no entry: copy bg to screen
			gfxCopy(0, bgY, w, kMenuEntryH, 5, menu->x, menu->y + kMenuButtonH + yOff, 0);
			continue;
		}

		// copy bg
		gfxCopy(0, bgY, w, 52, 5, 0, dstY, 5);
		// copy frame
		uint8_t frameType = memGetVar4Packed(2010 + (menu->entries[menu->page * 4 + i].index - 1));
		if (frameType == 0) {
			initMenuEntry(kMenuFrameGrayY, dstY, w, 24);
		} else if (frameType == 1) {
			initMenuEntry(kMenuFrameGreenY, dstY, w, 15);
		} else if (frameType < 4) {
			initMenuEntry(kMenuFrameBlueY, dstY, w, 16);
		} else if (frameType < 8) {
			initMenuEntry(kMenuFrameRedY, dstY, w, 17);
		} else if (frameType == 8) {
			initMenuEntry(kMenuFramePinkY, dstY, w, 18);
		} else {
			warning("Unexpected menu frame type: %d", frameType);
		}
		// call menu entry body
		uint32_t bodyAddr = menu->entries[menu->page * 4 + i].bodyAddr;
		drawMenuText(bodyAddr, menu->chunkW, dstY);
		drawMenuText(bodyAddr, menu->chunkW, dstY + kMenuEntryH * 4);
		// copy entry to screen
		gfxCopy(0, dstY, w, kMenuEntryH, 5, menu->x, menu->y + kMenuButtonH + yOff, 0);
	}
	if (menu->page * 4 + 4 >= menu->nrEntries) {
		// no down arrow: copy bg
		const int yOff = kMenuMaxH - kMenuButtonH;
		gfxCopy(0, kMenuBgY + yOff, w, kMenuButtonH, 5, 0, 970, 5);
		gfxCopy(0, kMenuBgY + yOff, w, kMenuButtonH, 5, menu->x, menu->y + yOff, 0);
	} else {
		// down arrow
		const int yOff = kMenuMaxH - kMenuButtonH;
		drawScrollButton(menu, kMenuBgY + kMenuButtonH + kMenuEntryH * 4, 970, "\x81\xa5");
		gfxCopy(0, 970, w, kMenuButtonH, 5, menu->x, menu->y + yOff, 0);
	}

	memSetSysvar16(kSysVarDstSurface, 0);
}

void initMenuFrame(int frameX, int dstY, int chunkW) {
	gfxCopy(frameX, 0, 8, kMenuButtonH, 5, 0, dstY, 5);
	for (int chunk = 0; chunk < chunkW; chunk++) {
		gfxCopy(frameX + 8, 0, 16, kMenuButtonH, 5, 8 + chunk * 16, dstY, 5);
	}
	gfxCopy(frameX + 24, 0, 8, kMenuButtonH, 5, 8 + chunkW * 16, dstY, 5);
}

void menuDrawSelection(MenuData *menu, MenuButton prev, MenuButton cur) {
	const int w = (menu->chunkW + 1) * 16;
	switch (prev) {
	case kMenuNone:
		break;
	case kMenuPageUp:
		gfxCopy(0, menuButtonSrc(0), w, kMenuButtonH, 5, menu->x, menu->y, 0);
		break;
	case kMenuPageDown:
		gfxCopy(0, menuButtonSrc(1), w, kMenuButtonH, 5, menu->x, menu->y + kMenuPgDownOff, 0);
		break;
	default:
		gfxCopy(0, menuEntrySrc(prev - 1), w, kMenuEntryH, 5,
				menu->x, menu->y + menuEntryOff(prev - 1), 0);
		break;
	}

	switch (cur) {
	case kMenuNone:
		break;
	case kMenuPageUp:
		gfxCopy(0, menuButtonSrc(2), w, kMenuButtonH, 5, menu->x, menu->y, 0);
		break;
	case kMenuPageDown:
		gfxCopy(0, menuButtonSrc(3), w, kMenuButtonH, 5, menu->x, menu->y + kMenuPgDownOff, 0);
		break;
	default:
		gfxCopy(0, menuEntrySrc(cur + 3), w, kMenuEntryH, 5,
				menu->x, menu->y + menuEntryOff(cur - 1), 0);
		break;
	}
}

MenuButton menuGetSelected(MenuData *menu) {
	uint x, y;
	cursorGetPos(x, y);
	for (int i = 0; i < 6; i++) {
		if (menu->buttons[i].contains((int16)x, (int16)y))
			return (MenuButton)i;
	}
	return kMenuNone;
}

void menuSetButtonHotspots(MenuData *menu) {
	if (menu->page > 0) {
		menu->buttons[0] = Common::Rect(menu->x + 8, menu->y + 8,
				menu->x + 8 + menu->chunkW * 16, menu->y + 8 + kMenuButtonH - 16);
	} else {
		menu->buttons[0] = Common::Rect();
	}
	if (menu->page * 4 + 4 < menu->nrEntries) {
		menu->buttons[5] = Common::Rect(menu->x + 8, menu->y + kMenuPgDownOff + 8,
				menu->x + 8 + menu->chunkW * 16, menu->y + kMenuPgDownOff + 8 + kMenuButtonH - 16);
	} else {
		menu->buttons[5] = Common::Rect();
	}

	int pageEntries = menu->nrEntries - menu->page * 4;
	for (int i = 0; i < 4; i++) {
		if (i < pageEntries) {
			menu->buttons[i + 1] = Common::Rect(menu->x + 8, menu->y + menuEntryOff(i) + 8,
					menu->x + 8 + menu->chunkW * 16, menu->y + menuEntryOff(i) + 8 + kMenuEntryH - 16);
		} else {
			menu->buttons[i + 1] = Common::Rect();
		}
	}
}

//
// Roulette menu
//

void drawRouletteBorder() {
	// corners
	gfxCopy(320, 0, 16, 16, 5, 56, 119, 0);
	gfxCopyMasked(336, 0, 16, 16, 5, 568, 119, 0, kMaskColor);
	gfxCopyMasked(320, 16, 16, 16, 5, 56, 343, 0, kMaskColor);
	gfxCopy(336, 16, 16, 16, 5, 568, 343, 0);

	// top/bottom
	for (int i = 0; i < 31; i++) {
		gfxCopy(352, 0, 16, 16, 5, 72 + i * 16, 119, 0);
		gfxCopy(352, 0, 16, 16, 5, 72 + i * 16, 343, 0);
	}

	// left/right
	for (int i = 0; i < 13; i++) {
		if (i == 6) {
			// arrow
			gfxCopy(384, 0, 16, 16, 5, 56, 135 + i * 16, 0);
			gfxCopy(384, 16, 16, 16, 5, 568, 135 + i * 16, 0);
		} else {
			// typical
			gfxCopy(368, 0, 16, 16, 5, 56, 135 + i * 16, 0);
			gfxCopy(368, 0, 16, 16, 5, 568, 135 + i * 16, 0);
		}
	}
}

void initRouletteMenuEntry(int frameY, int dstY, int w) {
	const int h = kMenuButtonH - 8;
	gfxCopy(0, frameY, w, h, 5, 0, dstY, 5);
	gfxCopy(0, frameY + 8, w, h, 5, 0, dstY + h, 5);
}

void drawRoulette(int centerY, int menuH) {
	// draw background
	gfxCopy(16, kMenuBgY + 16, 496, 208, 5, 72, 135, 0);
	int srcTop = centerY - 104;
	if (srcTop < 0) {
		// wrap back
		int h = -srcTop;
		gfxCopyMasked(0, kMenuEntryBase + (menuH - h), 496, h, 5, 72, 135, 0, kMaskColor);
		gfxCopyMasked(0, kMenuEntryBase, 496, 208 - h, 5, 72, 135 + h, 0, kMaskColor);
	} else if (srcTop + 208 > menuH) {
		// wrap forward
		int h = menuH - srcTop;
		gfxCopyMasked(0, kMenuEntryBase + srcTop, 496, h, 5, 72, 135, 0, kMaskColor);
		gfxCopyMasked(0, kMenuEntryBase, 496, 208 - h, 5, 72, 135 + h, 0, kMaskColor);
	} else {
		gfxCopyMasked(0, kMenuEntryBase + srcTop, 496, 208, 5, 72, 135, 0, kMaskColor);
	}
}

// XXX: syuusaku.exe's version of this is more generic. But since it's only
//      used in one specific circumstance, we simplify things by hardcoding.
// XXX: The game code includes a condition to run the roulette when placing
//      a camera, but the flag that controls this doesn't appear to be set
//      at any point.
int menuexecRoulette(ShuusakuMenuEntry *entries, int nrEntries) {
	audioSePlay("me55a.wav", 0);

	const uint16_t textCursorX = memGetSysvar16(kSysVarTextCursorX);
	const uint16_t textCursorY = memGetSysvar16(kSysVarTextCursorY);
	const uint16_t textStartX = memGetSysvar16(kSysVarTextStartX);
	const uint16_t textStartY = memGetSysvar16(kSysVarTextStartY);
	const uint16_t textEndX = memGetSysvar16(kSysVarTextEndX);
	const uint16_t textEndY = memGetSysvar16(kSysVarTextEndY);

	// copy area underneath menu
	gfxCopy(56, 119, 528, 240, 0, 0, kMenuBgY, 5);

	// draw frames
	initMenuFrame(32 * 0, kMenuFrameGrayY, 30);
	initMenuFrame(32 * 1, kMenuFrameGreenY, 30);
	initMenuFrame(32 * 2, kMenuFrameBlueY, 30);
	initMenuFrame(32 * 3, kMenuFrameRedY, 30);
	initMenuFrame(32 * 4, kMenuFramePinkY, 30);

	memSetSysvar16(kSysVarDstSurface, 5);

	// draw entries
	for (int i = 0; i < nrEntries; i++) {
		const int yOff = i * 52;
		const int dstY = kMenuEntryBase + yOff;

		uint8_t frameType = memGetVar4Packed(2010 + (entries[i].index - 1));
		if (frameType == 0) {
			initRouletteMenuEntry(kMenuFrameGrayY, dstY, 496);
		} else if (frameType == 1) {
			initRouletteMenuEntry(kMenuFrameGreenY, dstY, 496);
		} else if (frameType < 4) {
			initRouletteMenuEntry(kMenuFrameBlueY, dstY, 496);
		} else if (frameType < 8) {
			initRouletteMenuEntry(kMenuFrameRedY, dstY, 496);
		} else if (frameType == 8) {
			initRouletteMenuEntry(kMenuFramePinkY, dstY, 496);
		} else {
			warning("Unexpected menu frame type: %d", frameType);
		}
		// call menu entry body
		drawMenuText(entries[i].bodyAddr, 30, dstY);
	}

	drawRouletteBorder();

	int ticks = g_ai5->getRandom().getRandomNumber(15) + 7;
	uint32 centerY = kMenuEntryH / 2;
	uint32 menuH = nrEntries * kMenuEntryH;
	for (int i = 0; i < ticks; i++) {
		for (int frame = 0; frame < 4; frame++) {
			drawRoulette(centerY, menuH);
			vmPeek();
			vmDelay(16);
			centerY = (centerY + 13) % menuH;
		}
	}
	drawRoulette(centerY, menuH);

	audioSePlay("me55b.wav", 0);

	while (!inputDown(kInputActivate) && !g_engine->shouldQuit()) {
		vmPeek();
		vmDelay(16);
	}
	while (inputDown(kInputActivate) && !g_engine->shouldQuit()) {
		vmPeek();
		vmDelay(16);
	}

	gfxCopy(0, kMenuBgY, 528, 240, 5, 56, 119, 0);

	memSetSysvar16(kSysVarDstSurface, 0);
	memSetSysvar16(kSysVarTextCursorX, textCursorX);
	memSetSysvar16(kSysVarTextCursorY, textCursorY);
	memSetSysvar16(kSysVarTextStartX, textStartX);
	memSetSysvar16(kSysVarTextStartY, textStartY);
	memSetSysvar16(kSysVarTextEndX, textEndX);
	memSetSysvar16(kSysVarTextEndY, textEndY);
	memSetVar16(200, memGetVar16(200) + 3);
	return entries[ticks % nrEntries].index;
}

} // namespace

int shuusakuMenuexec(ShuusakuMenuEntry *entries, int nr, int mode) {
	if (mode == 1)
		return menuexecRoulette(entries, nr);

	const uint16_t textCursorX = memGetSysvar16(kSysVarTextCursorX);
	const uint16_t textCursorY = memGetSysvar16(kSysVarTextCursorY);
	const uint16_t textStartX = memGetSysvar16(kSysVarTextStartX);
	const uint16_t textStartY = memGetSysvar16(kSysVarTextStartY);
	const uint16_t textEndX = memGetSysvar16(kSysVarTextEndX);
	const uint16_t textEndY = memGetSysvar16(kSysVarTextEndY);

	MenuData menu;
	menu.entries = entries;
	menu.nrEntries = nr;
	menu.x = memGetVar16(110) * 8;
	menu.y = memGetVar16(111);
	menu.chunkW = memGetVar16(112);
	menu.page = MIN<uint32>(memGetVar16(115), (nr - 1) / 4);
	int visibleEntries = MIN(nr, 4);

	// convert center-y to top-y
	menu.y -= kMenuButtonH + (visibleEntries * kMenuEntryH) / 2;
	memSetVar16(113, menu.y);

	// copy area underneath menu
	gfxCopy(menu.x, menu.y, (menu.chunkW + 1) * 16, kMenuMaxH, 0, 0, kMenuBgY, 5);

	// draw frames
	initMenuFrame(32 * 0, kMenuFrameGrayY, menu.chunkW);
	initMenuFrame(32 * 1, kMenuFrameGreenY, menu.chunkW);
	initMenuFrame(32 * 2, kMenuFrameBlueY, menu.chunkW);
	initMenuFrame(32 * 3, kMenuFrameRedY, menu.chunkW);
	initMenuFrame(32 * 4, kMenuFramePinkY, menu.chunkW);

	drawMenu(&menu);
	menuSetButtonHotspots(&menu);
	backlogCommit();

	int ret = 0;
	MenuButton prevSelection = kMenuNone;
	bool done = false;
	while (!done && !g_engine->shouldQuit()) {
		// get selected entry
		MenuButton selection = menuGetSelected(&menu);
		if (selection != prevSelection) {
			menuDrawSelection(&menu, prevSelection, selection);
			prevSelection = selection;
		}

		// handle click
		if (inputDown(kInputActivate)) {
			inputWaitUntilUp(kInputActivate);
			switch (selection) {
			case kMenuNone:
				break;
			case kMenuPageUp:
				menu.page -= 1;
				drawMenu(&menu);
				menuSetButtonHotspots(&menu);
				break;
			case kMenuPageDown:
				menu.page += 1;
				drawMenu(&menu);
				menuSetButtonHotspots(&menu);
				break;
			default:
				ret = menu.entries[menu.page * 4 + selection - 1].index;
				done = true;
				break;
			}
		} else if (mode == 2 && inputDown(kInputCancel)) {
			inputWaitUntilUp(kInputCancel);
			ret = 0;
			done = true;
		}
		vmPeek();
	}

	gfxCopy(0, kMenuBgY, (menu.chunkW + 1) * 16, kMenuMaxH, 5, menu.x, menu.y, 0);
	memSetVar16(115, menu.page);

	memSetSysvar16(kSysVarTextCursorX, textCursorX);
	memSetSysvar16(kSysVarTextCursorY, textCursorY);
	memSetSysvar16(kSysVarTextStartX, textStartX);
	memSetSysvar16(kSysVarTextStartY, textStartY);
	memSetSysvar16(kSysVarTextEndX, textEndX);
	memSetSysvar16(kSysVarTextEndY, textEndY);
	return ret;
}

} // namespace AI5
