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

#include "common/algorithm.h"
#include "common/array.h"
#include "common/events.h"
#include "common/formats/winexe.h"
#include "common/ptr.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/cursorman.h"
#include "graphics/wincursor.h"

#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"

namespace AI5 {

enum {
	kCursorMaxFrames = 16
};

static Common::Array<Graphics::WinCursorGroup *> g_cursors;
static Graphics::Cursor *g_systemCursor = nullptr;
static uint g_currentCursor = 0;
static bool g_cursorAnimating = false;
static bool g_cursorLoaded = false;
static uint g_cursorNrFrames = 2;
static uint g_cursorFrame = 0;
static uint g_cursorFrameTime[kCursorMaxFrames] = { 500, 500 };
static uint32 g_cursorNextSwap = 0;
static CursorDirection g_cursorDir = kCursorDirNone;

void cursorInit(const char *exeName) {
	g_systemCursor = Graphics::makeDefaultWinCursor();
	CursorMan.replaceCursor(g_systemCursor);

	Common::ScopedPtr<Common::WinResources> exe(Common::WinResources::createFromEXE(Common::Path(exeName)));
	if (!exe) {
		warning("Failed to read cursors from %s", exeName);
		return;
	}

	// cursors are numbered in resource directory order
	Common::Array<Common::WinResourceID> ids = exe->getIDList(Common::kWinGroupCursor);
	Common::sort(ids.begin(), ids.end(), [](const Common::WinResourceID &a, const Common::WinResourceID &b) {
		return a.getID() < b.getID();
	});
	for (uint i = 0; i < ids.size(); i++) {
		Graphics::WinCursorGroup *group = Graphics::WinCursorGroup::createCursorGroup(exe.get(), ids[i]);
		if (!group || group->cursors.empty())
			warning("Failed to load cursor %u", i);
		g_cursors.push_back(group);
	}
}

void cursorFini() {
	for (uint i = 0; i < g_cursors.size(); i++)
		delete g_cursors[i];
	g_cursors.clear();
	delete g_systemCursor;
	g_systemCursor = nullptr;
}

static void cursorSwap() {
	if (!g_cursorAnimating)
		return;
	g_cursorFrame = (g_cursorFrame + 1) % g_cursorNrFrames;
	CursorMan.replaceCursor(g_cursors[g_currentCursor + g_cursorFrame]->cursors[0].cursor);
	g_cursorNextSwap = g_system->getMillis() + g_cursorFrameTime[(g_cursorFrame + 1) % g_cursorNrFrames];
}

void cursorUpdate() {
	if (g_cursorAnimating && g_system->getMillis() >= g_cursorNextSwap)
		cursorSwap();
}

void cursorLoad(uint no, uint nrFrames, const uint *frameTime) {
	assert(nrFrames <= kCursorMaxFrames);
	if (no + nrFrames > g_cursors.size()) {
		warning("Invalid cursor number: %u+%u", no, nrFrames);
		return;
	}
	for (uint i = 0; i < nrFrames; i++) {
		if (!g_cursors[no + i] || g_cursors[no + i]->cursors.empty()) {
			warning("Frame %u not loaded for cursor %u", i, no);
			return;
		}
	}
	g_currentCursor = no;
	if (frameTime) {
		for (uint i = 0; i < nrFrames; i++)
			g_cursorFrameTime[i] = frameTime[i];
	} else {
		g_cursorFrameTime[0] = 500;
		g_cursorFrameTime[1] = 500;
	}
	g_cursorNrFrames = nrFrames;
	g_cursorFrame = nrFrames - 1;
	g_cursorAnimating = true;
	g_cursorLoaded = true;
	cursorSwap();
}

void cursorUnload() {
	g_cursorAnimating = false;
	g_cursorLoaded = false;
	CursorMan.replaceCursor(g_systemCursor);
}

void cursorReload() {
	uint frameTime[kCursorMaxFrames];
	for (uint i = 0; i < g_cursorNrFrames; i++)
		frameTime[i] = g_cursorFrameTime[i];
	cursorLoad(g_currentCursor, g_cursorNrFrames, frameTime);
}

void cursorShow() {
	if (g_cursorLoaded)
		g_cursorAnimating = true;
	CursorMan.showMouse(true);
}

void cursorHide() {
	g_cursorAnimating = false;
	CursorMan.showMouse(false);
}

void cursorSetPos(uint x, uint y) {
	if (g_config.noWarpMouse)
		return;
	g_system->warpMouse(x, y);
}

void cursorGetPos(uint &x, uint &y) {
	Common::Point p = g_system->getEventManager()->getMousePos();
	x = CLIP<int>(p.x, 0, gfxViewWidth() - 1);
	y = CLIP<int>(p.y, 0, gfxViewHeight() - 1);
}

void cursorSetDirection(CursorDirection dir) {
	g_cursorDir = dir;
}

CursorDirection cursorGetDirection() {
	return g_cursorDir;
}

} // End of namespace AI5
