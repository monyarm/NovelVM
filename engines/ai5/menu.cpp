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

#include "ai5/ai5.h"
#include "ai5/input.h"
#include "ai5/menu.h"
#include "ai5/vm.h"

namespace AI5 {

static bool g_menuInitialized = false;
static uint g_menuIndex = 0;

// maps menu numbers (defmenu argument) to address indices
static uint g_menuNoToIndex[kMemMenuEntryMax];

static uint countEntries() {
	for (uint i = 0; i < kMemMenuEntryMax; i++) {
		if (!g_mem->menuEntryAddresses[i])
			return i;
	}
	return kMemMenuEntryMax;
}

void menuDefine(uint menuNo, bool empty) {
	if (!g_menuInitialized) {
		memset(g_mem->menuEntryAddresses, 0, sizeof(g_mem->menuEntryAddresses));
		memset(g_menuNoToIndex, 0xff, sizeof(g_menuNoToIndex));
		g_menuIndex = 0;
		g_menuInitialized = true;
	}

	// an empty entry writes nothing to menu_entry_numbers or
	// menu_entry_addresses
	if (empty)
		return;

	if (g_menuIndex >= kMemMenuEntryMax || menuNo >= kMemMenuEntryMax)
		error("menuDefine: too many menu entries");

	// virtual address of the current IP (farcall'd from bytecode)
	byte *ip = g_vm->ip.code + g_vm->ip.ptr;
	assert(ip >= memRaw() && ip < memEnd());

	// menu numbers are written sequentially, regardless of the current
	// contents
	g_mem->menuEntryNumbers[g_menuIndex++] = menuNo;

	// addresses go to the first free slot
	uint i = countEntries();
	if (i >= kMemMenuEntryMax)
		error("menuDefine: too many menu entries");
	g_mem->menuEntryAddresses[i] = ip - memRaw();

	// remember which menu number belongs to which address index
	// (for menuGetNo)
	g_menuNoToIndex[menuNo] = i;
}

void menuExec() {
	for (int i = 32; i < 40; i++) {
		if (!g_vm->procedures[i].code)
			error("menuExec: procedure %d is undefined", i);
	}

	memSetSysvar16(kSysVarNrMenuEntries, countEntries());

	// initialize menu
	vmCallProcedure(38);
	while (!g_engine->shouldQuit()) {
		if (vmFlagIsOn(kFlagMenuReturn) || vmFlagIsOn(kFlagReturn))
			break;
		// update menu
		vmCallProcedure(39);
		if (inputDown(kInputActivate)) {
			vmCallProcedure(32);
			if (g_game->id != GID_DOUKYUUSEI2 || memGetVar4(2035) == 0)
				inputWaitUntilUp(kInputActivate);
		} else if (inputDown(kInputCancel)) {
			if (g_game->id == GID_DOUKYUUSEI2)
				inputWaitUntilUp(kInputCancel);
			vmCallProcedure(33);
			inputWaitUntilUp(kInputCancel);
		} else if (inputDown(kInputUp)) {
			vmCallProcedure(34);
			inputWaitUntilUp(kInputUp);
		} else if (inputDown(kInputDown)) {
			vmCallProcedure(35);
			inputWaitUntilUp(kInputDown);
		} else if (inputDown(kInputLeft)) {
			vmCallProcedure(36);
			inputWaitUntilUp(kInputLeft);
		} else if (inputDown(kInputRight)) {
			vmCallProcedure(37);
			inputWaitUntilUp(kInputRight);
		} else {
			vmDelay(16);
		}
	}
	g_menuInitialized = false;
}

// Common pattern ("selected" = var16[18]):
//     System.get_menu_no(selected); // menu_no of selected index -> System.var16[22]
//     selected = System.var16[22] + 1;
void menuGetNo(uint index) {
	for (uint no = 0; no < kMemMenuEntryMax; no++) {
		if (g_menuNoToIndex[no] == index) {
			memSetSysvar16(kSysVarMenuNo, no);
			return;
		}
	}
	memSetSysvar16(kSysVarMenuNo, 200);
}

} // End of namespace AI5
