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

#ifndef AI5_POPUPMENU_H
#define AI5_POPUPMENU_H

#include "common/array.h"
#include "common/str.h"

namespace AI5 {

/**
 * A right-click menu. ai5-sdl2 shows it in separate windows; here it is
 * drawn on an overlay inside the game screen.
 */
class PopupMenu {
public:
	typedef void (*ClickFunc)();
	typedef void (*RadioFunc)(int index);

	~PopupMenu();

	int appendEntry(int iconNo, const char *label, const char *hotkey, ClickFunc onClick);
	int appendRadioGroup(const char *const *labels, int nrLabels, int dflt, RadioFunc onClick);
	int appendSeparator();
	/** Adds a submenu; the menu takes ownership. */
	int appendSubmenu(int iconNo, const char *label, PopupMenu *submenu);
	void setActive(int entryId, bool active);

	/** Show the menu at (x, y) and run it until it closes. */
	void run(int x, int y);

	enum EntryType {
		kEntryLabel,
		kEntryRadio,
		kEntrySubmenu,
		kEntrySeparator
	};

	struct Entry {
		int id;
		EntryType type;
		bool active;
		int iconNo;
		Common::String text;
		Common::String hotkey;
		ClickFunc onClick;
		RadioFunc onRadio;
		int radioIndex;
		bool radioOn;
		PopupMenu *submenu;
		int y;
	};

	Common::Array<Entry> _entries;

private:
	Entry &newEntry(EntryType type);
	int _nextId = 0;
};

} // End of namespace AI5

#endif // AI5_POPUPMENU_H
