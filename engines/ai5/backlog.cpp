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

#include "ai5/backlog.h"
#include "ai5/vm.h"

namespace AI5 {

struct BacklogEntry {
	uint ptr;
	bool present;
	bool hasVoice;
};

// Circular buffer where head == tail is a valid non-empty state; g_backlogEmpty
// marks the actual empty state.
static BacklogEntry g_backlog[kMemBacklogNrEntries];
static uint g_backlogHead = 0;
static uint g_backlogTail = 0;
static bool g_backlogEmpty = true;

static byte *backlogData(uint no) {
	return g_mem->backlog + no * kMemBacklogDataSize;
}

static uint translateIndex(uint no) {
	return (g_backlogTail + no) % kMemBacklogNrEntries;
}

void backlogClear() {
	memset(g_backlog, 0, sizeof(g_backlog));
	memset(g_mem->backlog, 0, sizeof(g_mem->backlog));
	g_backlogHead = 0;
	g_backlogTail = 0;
	g_backlogEmpty = true;
}

void backlogPrepareOld() {
	if (!vmFlagIsOn(kFlagLogEnable) || vmFlagIsOn(kFlagLogText) || vmFlagIsOn(kFlagLog))
		return;

	if (g_backlog[g_backlogHead].present) {
		g_backlogHead = (g_backlogHead + 1) % kMemBacklogNrEntries;
		if (g_backlogHead == g_backlogTail)
			g_backlogTail = (g_backlogTail + 1) % kMemBacklogNrEntries;
	}

	g_backlog[g_backlogHead].ptr = 0;
	g_backlog[g_backlogHead].present = true;
	g_backlog[g_backlogHead].hasVoice = false;
	g_backlogEmpty = false;
	vmFlagOn(kFlagLogText);
}

void backlogPrepare() {
	if (!vmFlagIsOn(kFlagLogEnable) || vmFlagIsOn(kFlagLogText))
		return;

	if (g_backlogHead == g_backlogTail && !g_backlogEmpty)
		g_backlogTail = (g_backlogTail + 1) % kMemBacklogNrEntries;

	g_backlog[g_backlogHead].ptr = 0;
	g_backlog[g_backlogHead].present = false;
	g_backlog[g_backlogHead].hasVoice = false;
	vmFlagOn(kFlagLogText);
}

void backlogCommitOld() {
	if (!vmFlagIsOn(kFlagLogEnable))
		return;
	vmFlagOff(kFlagLogText);
	vmFlagOff(kFlagLog);
}

void backlogCommit() {
	if (!vmFlagIsOn(kFlagLogEnable) || !vmFlagIsOn(kFlagLogText))
		return;

	g_backlog[g_backlogHead].present = true;
	g_backlogHead = (g_backlogHead + 1) % kMemBacklogNrEntries;
	g_backlogEmpty = false;
	vmFlagOff(kFlagLogText);
}

uint backlogCount() {
	if (g_backlogEmpty || !g_backlog[g_backlogTail].present)
		return 0;
	uint count = 0;
	uint i = g_backlogTail;
	do {
		count++;
		i = (i + 1) % kMemBacklogNrEntries;
	} while (i != g_backlogTail && g_backlog[i].present);
	return count;
}

uint32 backlogGetPointer(uint no) {
	if (no >= kMemBacklogNrEntries) {
		warning("Invalid backlog index: %u", no);
		return 0;
	}
	no = translateIndex(no);
	if (!g_backlog[no].present)
		return 0;
	return backlogData(no) - memRaw();
}

bool backlogHasVoice(uint no) {
	if (no >= kMemBacklogNrEntries) {
		warning("Invalid backlog index: %u", no);
		return false;
	}
	no = translateIndex(no);
	if (!g_backlog[no].present)
		return false;
	return g_backlog[no].hasVoice;
}

void backlogSetHasVoice() {
	g_backlog[g_backlogHead].hasVoice = true;
}

void backlogPushByte(byte b) {
	BacklogEntry *e = &g_backlog[g_backlogHead];
	if (e->ptr + 1 >= kMemBacklogDataSize)
		error("Backlog buffer overflow");

	byte *data = backlogData(g_backlogHead);
	data[e->ptr++] = b;
	data[e->ptr] = 0;
}

} // End of namespace AI5
