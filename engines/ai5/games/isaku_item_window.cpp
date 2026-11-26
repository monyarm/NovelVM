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

#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/games/isaku.h"

namespace AI5 {

// The original shows the item window in a separate window. NovelVM always
// uses the original's in-screen overlay mode.

enum {
	kItemWindowW = 320,
	kItemWindowH = 32,
	kOverlayX = 160,
	kOverlayY = 224
};

static struct {
	bool enabled;
	bool opened;
	bool created;
	bool lmbDown;
	bool rmbDown;
	int selected;
} g_itemWindow;

void isakuItemWindowCreate() {
	GfxSurface *s = gfxGetOverlay(1);
	for (int y = 0; y < s->h; y++) {
		byte *p = gfxPixel(s, 0, y);
		for (int x = 0; x < s->w; x++, p += 4) {
			p[0] = p[1] = p[2] = 0;
			p[3] = 200;
		}
	}
	g_itemWindow.created = true;
	g_itemWindow.enabled = true;
	g_itemWindow.selected = -1;
}

void isakuItemWindowUpdate() {
	if (!g_itemWindow.opened)
		return;
	GfxSurface *src = g_gfx.surface[7].s;
	GfxSurface *dst = gfxGetOverlay(1);
	GfxRect srcR = { 0, 0, kItemWindowW, kItemWindowH };
	GfxPoint dstP = { kOverlayX, kOverlayY };
	if (gfxCopyClip(src, &srcR, dst, &dstP)) {
		for (int row = 0; row < srcR.h; row++) {
			const byte *s = gfxPixel(src, srcR.x, srcR.y + row);
			byte *d = gfxPixel(dst, dstP.x, dstP.y + row);
			for (int col = 0; col < srcR.w; col++, s += 3, d += 4) {
				d[0] = s[0];
				d[1] = s[1];
				d[2] = s[2];
				d[3] = 255;
			}
		}
	}
	gfxScreenDirty();
}

void isakuItemWindowToggle() {
	if (!g_itemWindow.enabled)
		return;
	if (g_itemWindow.opened) {
		gfxOverlayDisable(1);
		isakuBuiltinSePlay("wincls.wav");
		g_itemWindow.opened = false;
	} else {
		gfxOverlayEnable(1);
		isakuBuiltinSePlay("winopn.wav");
		g_itemWindow.opened = true;
		isakuItemWindowUpdate();
	}
}

static void selectItem(int i) {
	cursorSetPos(kOverlayX + 16 + i * 32, kOverlayY + 16);
	g_itemWindow.selected = i;
}

void isakuItemWindowTick() {
	if (!g_itemWindow.opened)
		return;

	if (inputDown(kInputSpace)) {
		inputWaitUntilUpAlways(kInputSpace);
		isakuItemWindowToggle();
		return;
	}

	if (inputDown(kInputLeft)) {
		inputWaitUntilUpAlways(kInputLeft);
		if (g_itemWindow.selected < 0)
			selectItem(0);
		else if (g_itemWindow.selected == 0)
			selectItem(9);
		else
			selectItem(g_itemWindow.selected - 1);
	} else if (inputDown(kInputRight)) {
		inputWaitUntilUpAlways(kInputRight);
		if (g_itemWindow.selected < 0 || g_itemWindow.selected >= 9)
			selectItem(0);
		else
			selectItem(g_itemWindow.selected + 1);
	}
}

bool isakuItemWindowEvent(const Common::Event &e) {
	switch (e.type) {
	case Common::EVENT_LBUTTONDOWN:
	case Common::EVENT_LBUTTONUP:
		g_itemWindow.lmbDown = e.type == Common::EVENT_LBUTTONDOWN;
		return true;
	case Common::EVENT_RBUTTONDOWN:
	case Common::EVENT_RBUTTONUP:
		g_itemWindow.rmbDown = e.type == Common::EVENT_RBUTTONDOWN;
		return true;
	default:
		return false;
	}
}

bool isakuItemWindowIsEnabled() {
	return g_itemWindow.enabled;
}

bool isakuItemWindowIsOpen() {
	return g_itemWindow.enabled && g_itemWindow.opened;
}

void isakuItemWindowGetPos(int &x, int &y, int &w, int &h) {
	if (!g_itemWindow.enabled) {
		x = 0;
		y = 0;
	} else {
		x = kOverlayX;
		y = kOverlayY;
	}
	w = x + kItemWindowW - 1;
	h = y + kItemWindowH - 1;
}

void isakuItemWindowGetCursorPos(int &x, int &y) {
	if (!g_itemWindow.enabled) {
		x = kItemWindowW;
		y = kItemWindowH;
	} else {
		uint ux, uy;
		cursorGetPos(ux, uy);
		x = ux < (uint)kOverlayX ? (int)kItemWindowW : (int)ux;
		y = uy < (uint)kOverlayY ? (int)kItemWindowH : (int)uy;
		x -= kOverlayX;
		y -= kOverlayY;
	}

	if (x >= 0 && x < kItemWindowW && y >= 0 && y < kItemWindowH)
		g_itemWindow.selected = x / 32;
}

void isakuItemWindowEnable() {
	if (!g_itemWindow.created)
		return;
	g_itemWindow.enabled = true;
}

void isakuItemWindowDisable() {
	g_itemWindow.enabled = false;
	if (g_itemWindow.opened) {
		gfxOverlayDisable(1);
		g_itemWindow.opened = false;
	}
}

void isakuItemWindowGetMouseState(bool &lmbDown, bool &rmbDown) {
	if (!g_itemWindow.enabled) {
		lmbDown = false;
		rmbDown = false;
		return;
	}
	lmbDown = g_itemWindow.lmbDown || inputDown(kInputActivate);
	rmbDown = g_itemWindow.rmbDown;
}

} // End of namespace AI5
