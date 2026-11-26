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

#include "common/events.h"
#include "common/system.h"
#include "graphics/cursorman.h"

#include "ai5/ai5.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/popupmenu.h"
#include "ai5/text.h"
#include "ai5/vm.h"

namespace AI5 {

enum {
	kBorderSize = 3,
	kEntryH = 18,
	kEntryIconSize = 16,
	kEntryIconPad = 2,
	kEntryTextPad = 6,
	kEntryHotkeyPad = 12,
	kEntryPadRight = 14,
	kSeparatorH = 8,
	kSeparatorPad = 2,
	kPopupOverlay = 3,
	kSubmenuDelay = 200
};

static const GfxColor kBgColor = { 212, 208, 200, 255 };
static const GfxColor kFgColor = { 24, 22, 18, 255 };
static const GfxColor kSelBgColor = { 61, 113, 163, 255 };
static const GfxColor kSelFgColor = { 248, 248, 248, 255 };
static const GfxColor kInactiveBgColor = { 248, 248, 248, 255 };
static const GfxColor kInactiveFgColor = { 166, 165, 164, 255 };
static const GfxColor kBorderBrightColor = { 250, 249, 248, 255 };
static const GfxColor kBorderDimColor = { 103, 101, 97, 255 };
static const GfxColor kBorderDarkColor = { 60, 59, 57, 255 };

PopupMenu::~PopupMenu() {
	for (uint i = 0; i < _entries.size(); i++)
		delete _entries[i].submenu;
}

PopupMenu::Entry &PopupMenu::newEntry(EntryType type) {
	Entry e = Entry();
	e.id = _nextId++;
	e.type = type;
	e.active = true;
	e.iconNo = -1;
	_entries.push_back(e);
	return _entries.back();
}

int PopupMenu::appendEntry(int iconNo, const char *label, const char *hotkey, ClickFunc onClick) {
	Entry &e = newEntry(kEntryLabel);
	e.iconNo = iconNo;
	e.text = label;
	if (hotkey)
		e.hotkey = hotkey;
	e.onClick = onClick;
	return e.id;
}

int PopupMenu::appendRadioGroup(const char *const *labels, int nrLabels, int dflt, RadioFunc onClick) {
	int id = _nextId;
	for (int i = 0; i < nrLabels; i++) {
		Entry &e = newEntry(kEntryRadio);
		e.text = labels[i];
		e.onRadio = onClick;
		e.radioIndex = i;
		e.radioOn = i == dflt;
	}
	return id;
}

int PopupMenu::appendSeparator() {
	return newEntry(kEntrySeparator).id;
}

int PopupMenu::appendSubmenu(int iconNo, const char *label, PopupMenu *submenu) {
	Entry &e = newEntry(kEntrySubmenu);
	e.iconNo = iconNo;
	e.text = label;
	e.submenu = submenu;
	return e.id;
}

void PopupMenu::setActive(int entryId, bool active) {
	for (uint i = 0; i < _entries.size(); i++) {
		if (_entries[i].id == entryId) {
			_entries[i].active = active;
			return;
		}
	}
	warning("No menu entry with id %d", entryId);
}

namespace {

struct MenuWindow {
	PopupMenu *menu;
	int selected; // entry index, or -1
	int x, y, w, h;
	int textX, hotkeyX;
};

void fillRect(GfxSurface *s, int x, int y, int w, int h, const GfxColor &c) {
	GfxRect r = { x, y, w, h };
	if (!gfxFillClip(s, &r))
		return;
	for (int row = r.y; row < r.y + r.h; row++) {
		byte *p = gfxPixel(s, r.x, row);
		for (int col = 0; col < r.w; col++, p += 4) {
			p[0] = c.r;
			p[1] = c.g;
			p[2] = c.b;
			p[3] = 255;
		}
	}
}

// 1-bit glyphs, as in ai5-sdl2
const byte kArrow[7][4] = {
	{ 1, 0, 0, 0 }, { 1, 1, 0, 0 }, { 1, 1, 1, 0 }, { 1, 1, 1, 1 },
	{ 1, 1, 1, 0 }, { 1, 1, 0, 0 }, { 1, 0, 0, 0 }
};

const byte kCheck[7][7] = {
	{ 0, 0, 0, 0, 0, 0, 1 }, { 0, 0, 0, 0, 0, 1, 1 }, { 1, 0, 0, 0, 1, 1, 1 },
	{ 1, 1, 0, 1, 1, 1, 0 }, { 1, 1, 1, 1, 1, 0, 0 }, { 0, 1, 1, 1, 0, 0, 0 },
	{ 0, 0, 1, 0, 0, 0, 0 }
};

template<int W>
void drawGlyph(GfxSurface *s, int x, int y, const byte (*glyph)[W], int h, const GfxColor &c) {
	for (int row = 0; row < h; row++) {
		for (int col = 0; col < W; col++) {
			if (glyph[row][col])
				fillRect(s, x + col, y + row, 1, 1, c);
		}
	}
}

void calcSize(MenuWindow &m) {
	int maxLabel = 0, maxHotkey = 0;
	int height = kBorderSize * 2;
	bool haveIcon = false;
	for (uint i = 0; i < m.menu->_entries.size(); i++) {
		PopupMenu::Entry &e = m.menu->_entries[i];
		switch (e.type) {
		case PopupMenu::kEntrySeparator:
			height += kSeparatorH;
			continue;
		case PopupMenu::kEntryLabel:
			if (e.iconNo >= 0)
				haveIcon = true;
			if (!e.hotkey.empty())
				maxHotkey = MAX(maxHotkey, uiMeasureText(e.hotkey.c_str()));
			break;
		case PopupMenu::kEntryRadio:
			haveIcon = true;
			break;
		case PopupMenu::kEntrySubmenu:
			maxHotkey = MAX(maxHotkey, 4); // arrow
			break;
		}
		maxLabel = MAX(maxLabel, uiMeasureText(e.text.c_str()));
		height += kEntryH;
	}
	// entry positions, relative to the menu
	int y = kBorderSize;
	for (uint i = 0; i < m.menu->_entries.size(); i++) {
		PopupMenu::Entry &e = m.menu->_entries[i];
		e.y = y;
		y += e.type == PopupMenu::kEntrySeparator ? kSeparatorH : kEntryH;
	}
	int textPad = kEntryTextPad + (haveIcon ? kEntryIconPad + kEntryIconSize : 0);
	m.w = textPad + maxLabel + (maxHotkey ? maxHotkey + kEntryHotkeyPad : 0) + kEntryPadRight;
	m.h = height;
	m.textX = textPad;
	m.hotkeyX = textPad + maxLabel + kEntryHotkeyPad;
}

void drawEntry(GfxSurface *s, const MenuWindow &m, const PopupMenu::Entry &e, bool selected) {
	if (e.type == PopupMenu::kEntrySeparator)
		return;

	const int x0 = m.x, y0 = m.y + e.y;
	fillRect(s, x0 + kBorderSize, y0, m.w - kBorderSize * 2, kEntryH, selected && e.active ? kSelBgColor : kBgColor);

	if (e.iconNo >= 0) {
		const GfxSurface *icon = iconGet(e.iconNo);
		if (icon) {
			GfxRect srcR = { 0, 0, icon->w, icon->h };
			GfxPoint dstP = { x0 + kBorderSize + kEntryIconPad, y0 + (kEntryH - icon->h) / 2 };
			GfxSurface *src = const_cast<GfxSurface *>(icon);
			if (gfxCopyClip(src, &srcR, s, &dstP)) {
				for (int row = 0; row < srcR.h; row++) {
					const byte *sp = gfxPixel(src, srcR.x, srcR.y + row);
					byte *dp = gfxPixel(s, dstP.x, dstP.y + row);
					for (int col = 0; col < srcR.w; col++, sp += 4, dp += 4) {
						if (sp[3])
							memcpy(dp, sp, 4);
					}
				}
			}
		}
	}

	const GfxColor &fg = !e.active ? kInactiveFgColor : (selected ? kSelFgColor : kFgColor);
	int textY = y0 + kEntryH / 2;
	if (!e.active && !selected) {
		uiDrawText(s, x0 + m.textX + 1, textY + 1, e.text.c_str(), kInactiveBgColor);
		if (e.type == PopupMenu::kEntryLabel && !e.hotkey.empty())
			uiDrawText(s, x0 + m.hotkeyX + 1, textY + 1, e.hotkey.c_str(), kInactiveBgColor);
	}
	uiDrawText(s, x0 + m.textX, textY, e.text.c_str(), fg);
	if (e.type == PopupMenu::kEntryLabel && !e.hotkey.empty())
		uiDrawText(s, x0 + m.hotkeyX, textY, e.hotkey.c_str(), fg);

	if (e.type == PopupMenu::kEntryRadio && e.radioOn)
		drawGlyph<7>(s, x0 + kBorderSize + 3, textY - 4, kCheck, 7, fg);
	if (e.type == PopupMenu::kEntrySubmenu)
		drawGlyph<4>(s, x0 + m.w - kBorderSize - 8, textY - 4, kArrow, 7, fg);
}

void drawFrame(GfxSurface *s, const MenuWindow &m) {
	const int x = m.x, y = m.y, w = m.w, h = m.h;
	fillRect(s, x, y, w, h, kBgColor);
	fillRect(s, x + 1, y + 1, w - 3, 1, kBorderBrightColor);
	fillRect(s, x + 1, y + 2, 1, h - 4, kBorderBrightColor);
	fillRect(s, x + w - 2, y + 1, 1, h - 2, kBorderDimColor);
	fillRect(s, x + 1, y + h - 2, w - 3, 1, kBorderDimColor);
	fillRect(s, x + w - 1, y, 1, h, kBorderDarkColor);
	fillRect(s, x, y + h - 1, w - 1, 1, kBorderDarkColor);

	for (uint i = 0; i < m.menu->_entries.size(); i++) {
		const PopupMenu::Entry &e = m.menu->_entries[i];
		if (e.type == PopupMenu::kEntrySeparator) {
			int sx = x + kBorderSize + kSeparatorPad;
			int sw = w - kBorderSize * 2 - kSeparatorPad * 2;
			fillRect(s, sx, y + e.y + 3, sw, 1, kBorderDimColor);
			fillRect(s, sx, y + e.y + 4, sw, 1, kBorderBrightColor);
		} else {
			drawEntry(s, m, e, (int)i == m.selected);
		}
	}
}

// entry under (x, y), or -1
int hitTest(const MenuWindow &m, int x, int y) {
	if (x < m.x + kBorderSize || x >= m.x + m.w - kBorderSize || y < m.y + kBorderSize || y >= m.y + m.h - kBorderSize)
		return -1;
	for (uint i = 0; i < m.menu->_entries.size(); i++) {
		const PopupMenu::Entry &e = m.menu->_entries[i];
		int h = e.type == PopupMenu::kEntrySeparator ? kSeparatorH : kEntryH;
		if (y >= m.y + e.y && y < m.y + e.y + h)
			return i;
	}
	return -1;
}

bool inside(const MenuWindow &m, int x, int y) {
	return x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h;
}

} // End of anonymous namespace

void PopupMenu::run(int x, int y) {
	GfxSurface *s = gfxGetOverlay(kPopupOverlay);
	const int viewW = gfxViewWidth(), viewH = gfxViewHeight();

	// open menus, root first
	Common::Array<MenuWindow> stack;
	MenuWindow root = { this, -1, x, y, 0, 0, 0, 0 };
	calcSize(root);
	root.x = CLIP(root.x, 0, MAX(0, viewW - root.w));
	root.y = CLIP(root.y, 0, MAX(0, viewH - root.h));
	stack.push_back(root);

	uint32 openAt = 0;
	int openFor = -1;

	// the game cursor may be hidden
	bool cursorVisible = CursorMan.showMouse(true);
	cursorUnload();
	gfxOverlayEnable(kPopupOverlay);

	bool done = false;
	PopupMenu::ClickFunc click = nullptr;
	PopupMenu::RadioFunc radio = nullptr;
	int radioIndex = 0;
	bool redraw = true;

	while (!done && !g_engine->shouldQuit()) {
		Common::Event ev;
		while (g_system->getEventManager()->pollEvent(ev)) {
			switch (ev.type) {
			case Common::EVENT_MOUSEMOVE: {
				// topmost menu under the cursor
				for (int i = stack.size() - 1; i >= 0; i--) {
					if (!inside(stack[i], ev.mouse.x, ev.mouse.y))
						continue;
					int sel = hitTest(stack[i], ev.mouse.x, ev.mouse.y);
					if (sel != stack[i].selected) {
						stack[i].selected = sel;
						// close deeper menus, open a submenu after a delay
						while ((int)stack.size() > i + 1)
							stack.pop_back();
						if (sel >= 0 && stack[i].menu->_entries[sel].type == kEntrySubmenu) {
							openAt = g_system->getMillis() + kSubmenuDelay;
							openFor = i;
						}
						redraw = true;
					}
					break;
				}
				break;
			}
			case Common::EVENT_LBUTTONDOWN:
			case Common::EVENT_RBUTTONDOWN: {
				bool hit = false;
				for (uint i = 0; i < stack.size(); i++)
					hit = hit || inside(stack[i], ev.mouse.x, ev.mouse.y);
				if (!hit)
					done = true;
				break;
			}
			case Common::EVENT_LBUTTONUP: {
				MenuWindow &top = stack.back();
				if (!inside(top, ev.mouse.x, ev.mouse.y) || top.selected < 0)
					break;
				const Entry &e = top.menu->_entries[top.selected];
				if (!e.active)
					break;
				if (e.type == kEntryLabel) {
					click = e.onClick;
					done = true;
				} else if (e.type == kEntryRadio) {
					radio = e.onRadio;
					radioIndex = e.radioIndex;
					done = true;
				}
				break;
			}
			case Common::EVENT_KEYDOWN:
				if (ev.kbd.keycode == Common::KEYCODE_ESCAPE)
					done = true;
				break;
			default:
				break;
			}
		}

		if (openFor >= 0 && g_system->getMillis() >= openAt) {
			const MenuWindow &parent = stack[openFor];
			if ((int)stack.size() == openFor + 1 && parent.selected >= 0) {
				const Entry &e = parent.menu->_entries[parent.selected];
				MenuWindow child = { e.submenu, -1, parent.x + parent.w - kBorderSize, parent.y + e.y - kBorderSize, 0, 0, 0, 0 };
				calcSize(child);
				// open to the left when there is no room on the right
				if (child.x + child.w > viewW)
					child.x = MAX(0, parent.x - child.w + kBorderSize);
				child.y = CLIP(child.y, 0, MAX(0, viewH - child.h));
				stack.push_back(child);
				redraw = true;
			}
			openFor = -1;
		}

		if (redraw) {
			memset(s->pixels, 0, s->pitch * s->h);
			for (uint i = 0; i < stack.size(); i++)
				drawFrame(s, stack[i]);
			gfxScreenDirty();
			redraw = false;
		}
		gfxUpdate();
		g_system->delayMillis(16);
	}

	memset(s->pixels, 0, s->pitch * s->h);
	gfxOverlayDisable(kPopupOverlay);
	cursorReload();
	CursorMan.showMouse(cursorVisible);

	if (click)
		click();
	if (radio)
		radio(radioIndex);
}

} // End of namespace AI5
