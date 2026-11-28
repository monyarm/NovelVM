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

#ifndef AI5_ANIMPLAY_H
#define AI5_ANIMPLAY_H

#include "common/scummsys.h"

#include "ai5/anim.h"

namespace AI5 {

enum AnimState {
	kAnimStateHalted,    // stream is halted
	kAnimStateRunning,   // stream is running
	kAnimStateHaltNext,  // halt on next CHECK_STOP instruction
	kAnimStateWaiting,   // waiting until halted
	kAnimStatePauseNext, // pause on next CHECK_STOP instruction
	kAnimStatePaused     // stream is paused
};

extern uint g_animFrameT;
extern void (*g_animLoadPalette)(byte *);
extern AnimParser *g_animParser;

void animExecute();
bool animRunning();
bool animStreamRunning(uint slot);
void animInitStream(uint slot, uint stream);
void animInitStreamFrom(uint slot, uint stream, uint32 off);
AnimState animGetState(uint slot);
void animStart(uint slot);
void animStop(uint slot);
void animPause(uint slot);
void animPauseSync(uint slot);
void animUnpause(uint slot);
void animHalt(uint slot);
void animWait(uint slot);
void animStopAll();
void animHaltAll();
void animResetAll();
void animRestartAll();
void animWaitAll();
void animPauseRangeSync(uint start, uint end);
void animPauseAllSync();
void animUnpauseRange(uint start, uint end);
void animUnpauseAll();
void animSetOffset(uint slot, uint x, uint y);
void animExecCopyCall(uint stream);
void animDecomposeDrawCall(const AnimDrawCall &call, int &dstX, int &dstY, int &w, int &h);

} // End of namespace AI5

#endif // AI5_ANIMPLAY_H
