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
#include "ai5/animplay.h"
#include "ai5/gfx.h"
#include "ai5/vm.h"

namespace AI5 {

struct AnimStream {
	byte state;
	// S4/A file in memory
	byte *fileData;
	// the stream's bytecode
	byte *bytecode;
	uint ip;
	// number of cycles to stall
	uint stallCount;
	struct { uint start, count; } loop;
	struct { uint start, count; } loop2;
	// offset into animation CG
	struct { uint x, y; } off;
	// index of stream in animation file
	uint stream;
	bool initialized;
};

static AnimStream g_streams[kAnimMaxStreams];

uint g_animFrameT = 16;
void (*g_animLoadPalette)(byte *) = nullptr;
AnimParser *g_animParser = nullptr;

static void checkSlot(uint i) {
	if (i >= kAnimMaxStreams)
		error("Invalid animation slot index: %u", i);
}

static uint32 getMaskColor() {
	if (g_game->bpp == 24)
		return memGetSysvar32(kSysVarMaskColor24);
	if (g_game->id == GID_SHUUSAKU)
		return 10;
	return memGetSysvar16(kSysVarMaskColor);
}

static bool wideBytecode() {
	return g_animParser->getType() == kAnimA;
}

static void initStream(uint slot, uint stream, uint32 off) {
	AnimStream *anim = &g_streams[slot];
	memset(anim, 0, sizeof(AnimStream));
	anim->fileData = g_mem->fileData + off;
	if (!wideBytecode())
		anim->bytecode = anim->fileData + READ_LE_UINT16(anim->fileData + 1 + stream * 2);
	else
		anim->bytecode = anim->fileData + READ_LE_UINT32(anim->fileData + 2 + stream * 4);
	anim->stream = stream;
	anim->initialized = true;
}

void animInitStream(uint slot, uint stream) {
	checkSlot(slot);
	if (stream >= kAnimMaxStreams)
		error("Invalid animation stream index: %u", stream);
	initStream(slot, stream, memGetSysvar32(kSysVarDataOffset));
}

void animInitStreamFrom(uint slot, uint stream, uint32 off) {
	checkSlot(slot);
	if (stream >= kAnimMaxStreams)
		error("Invalid animation stream index: %u", stream);
	initStream(slot, stream, off);
}

AnimState animGetState(uint slot) {
	checkSlot(slot);
	return (AnimState)g_streams[slot].state;
}

void animStart(uint slot) {
	checkSlot(slot);
	if (g_streams[slot].initialized) {
		g_streams[slot].state = kAnimStateRunning;
		g_streams[slot].ip = 0;
	}
}

void animStop(uint slot) {
	checkSlot(slot);
	g_streams[slot].state = kAnimStateHaltNext;
}

void animPause(uint slot) {
	checkSlot(slot);
	if (g_streams[slot].initialized && g_streams[slot].state == kAnimStateRunning)
		g_streams[slot].state = kAnimStatePauseNext;
}

void animPauseSync(uint slot) {
	checkSlot(slot);
	// same condition as the original, which returns early for initialized streams
	if (g_streams[slot].initialized || g_streams[slot].state != kAnimStateRunning)
		return;

	g_streams[slot].state = kAnimStatePauseNext;
	do {
		vmPeek();
	} while (g_streams[slot].state != kAnimStatePaused && !g_engine->shouldQuit());
}

void animUnpause(uint slot) {
	checkSlot(slot);
	if (g_streams[slot].initialized && g_streams[slot].state == kAnimStatePaused)
		g_streams[slot].state = kAnimStateRunning;
}

void animHalt(uint slot) {
	checkSlot(slot);
	g_streams[slot].state = kAnimStateHalted;
	g_streams[slot].initialized = false;
}

void animWait(uint slot) {
	checkSlot(slot);
	g_streams[slot].state = kAnimStateWaiting;
	do {
		vmPeek();
	} while (g_streams[slot].state != kAnimStateHalted && !g_engine->shouldQuit());
}

void animStopAll() {
	for (int i = 0; i < kAnimMaxStreams; i++) {
		if (g_streams[i].state != kAnimStateHalted)
			g_streams[i].state = kAnimStateHaltNext;
	}
}

void animHaltAll() {
	for (int i = 0; i < kAnimMaxStreams; i++) {
		g_streams[i].state = kAnimStateHalted;
		g_streams[i].initialized = false;
	}
}

void animResetAll() {
	for (int i = 0; i < kAnimMaxStreams; i++) {
		if (g_streams[i].state == kAnimStateHalted)
			continue;
		initStream(i, g_streams[i].stream, memGetSysvar32(kSysVarDataOffset));
	}
}

static bool streamExecute(AnimStream *anim);

void animRestartAll() {
	for (int i = 0; i < kAnimMaxStreams; i++) {
		if (g_streams[i].state == kAnimStateHalted)
			continue;
		AnimStream *anim = &g_streams[i];
		uint off = memGetSysvar32(kSysVarDataOffset);
		initStream(i, anim->stream, off);
		streamExecute(anim);
		initStream(i, anim->stream, off);
		anim->state = kAnimStateRunning;
		anim->ip = 0;
	}
}

void animWaitAll() {
	while (animRunning() && !g_engine->shouldQuit())
		vmPeek();
}

static bool rangeRunning(uint start, uint end) {
	for (uint i = start; i < end; i++) {
		if (g_streams[i].state != kAnimStateHalted && g_streams[i].state != kAnimStatePaused)
			return true;
	}
	return false;
}

void animPauseRangeSync(uint start, uint end) {
	for (uint i = start; i < end; i++) {
		if (g_streams[i].state == kAnimStateRunning)
			g_streams[i].state = kAnimStatePauseNext;
	}
	// wait for all animations to halt or pause
	do {
		vmPeek();
	} while (rangeRunning(start, end) && !g_engine->shouldQuit());
}

void animPauseAllSync() {
	animPauseRangeSync(0, kAnimMaxStreams);
}

void animUnpauseRange(uint start, uint end) {
	for (uint i = start; i < end; i++) {
		if (g_streams[i].state == kAnimStatePaused)
			g_streams[i].state = kAnimStateRunning;
	}
}

void animUnpauseAll() {
	animUnpauseRange(0, kAnimMaxStreams);
}

void animSetOffset(uint slot, uint x, uint y) {
	checkSlot(slot);
	g_streams[slot].off.x = x;
	g_streams[slot].off.y = y;
}

static uint16 readValue(AnimStream *anim) {
	if (!wideBytecode())
		return anim->bytecode[anim->ip++];
	uint16 code = READ_LE_UINT16(anim->bytecode + anim->ip);
	anim->ip += 2;
	return code;
}

static bool streamDraw(AnimStream *anim, byte i) {
	if (i < 20) {
		warning("Invalid draw call index: %u", i);
		return false;
	}

	const uint size = g_animParser->getDrawCallSize();
	uint off;
	switch (g_animParser->getType()) {
	case kAnimS4:
		off = 1 + anim->fileData[0] * 2 + (i - 20) * size;
		break;
	case kAnimA8:
		off = 1 + 10 * 2 + (i - 20) * size;
		break;
	default:
		off = 2 + 100 * 4 + (i - 20) * size;
		break;
	}

	AnimDrawCall call;
	uint srcI = 1;
	if (g_game->id == GID_DOUKYUUSEI)
		srcI = 9;
	else if (g_game->id == GID_SHUUSAKU)
		srcI = (anim - g_streams) < 10 ? 6 : 7;
	if (!g_animParser->parseDrawCall(anim->fileData + off, call, srcI)) {
		warning("Failed to parse draw call %u", i);
		return false;
	}

	const int offX = anim->off.x, offY = anim->off.y;
	switch (call.op) {
	case kAnimDrawFill:
		gfxFill(call.dst.x + offX, call.dst.y + offY, call.dim.w, call.dim.h, call.dst.i, 8);
		break;
	case kAnimDrawCopy:
		gfxCopy(call.src.x, call.src.y, call.dim.w, call.dim.h, call.src.i,
				call.dst.x + offX, call.dst.y + offY, call.dst.i);
		break;
	case kAnimDrawCopyMasked:
		gfxCopyMasked(call.src.x, call.src.y, call.dim.w, call.dim.h, call.src.i,
				call.dst.x + offX, call.dst.y + offY, call.dst.i, getMaskColor());
		break;
	case kAnimDrawCopyMasked2:
		gfxCopyMasked(call.fg.x, call.fg.y, call.dim.w, call.dim.h, call.fg.i,
				call.dst.x + offX, call.dst.y + offY, call.dst.i, getMaskColor());
		break;
	case kAnimDrawSwap:
		gfxCopySwap(call.src.x, call.src.y, call.dim.w, call.dim.h, call.src.i,
				call.dst.x + offX, call.dst.y + offY, call.dst.i);
		break;
	case kAnimDrawCompose:
		gfxCompose(call.fg.x, call.fg.y, call.dim.w, call.dim.h, call.fg.i,
				call.bg.x, call.bg.y, call.bg.i,
				call.dst.x + offX, call.dst.y + offY, call.dst.i, getMaskColor());
		break;
	default:
		// SET_COLOR, SET_PALETTE, and the BE-YOND operations are not implemented
		break;
	}
	if (g_game->afterAnimDraw)
		g_game->afterAnimDraw(&call);
	return true;
}

static bool streamExecute(AnimStream *anim) {
	if (anim->stallCount) {
		anim->stallCount--;
		return false;
	}
	uint16 op = readValue(anim);
	switch (op) {
	case kAnimOpNoop:
		break;
	case kAnimOpCheckStop:
		if (anim->state == kAnimStateHaltNext)
			anim->state = kAnimStateHalted;
		else if (anim->state == kAnimStatePauseNext)
			anim->state = kAnimStatePaused;
		break;
	case kAnimOpStall:
		anim->stallCount = readValue(anim);
		break;
	case kAnimOpReset:
		anim->ip = 0;
		break;
	case kAnimOpHalt:
		anim->state = kAnimStateHalted;
		break;
	case kAnimOpLoopStart:
		anim->loop.count = readValue(anim);
		anim->loop.start = anim->ip;
		break;
	case kAnimOpLoopEnd:
		if (anim->loop.count && --anim->loop.count)
			anim->ip = anim->loop.start;
		break;
	case kAnimOpLoop2Start:
		anim->loop2.count = readValue(anim);
		anim->loop2.start = anim->ip;
		break;
	case kAnimOpLoop2End:
		if (anim->loop2.count && --anim->loop2.count)
			anim->ip = anim->loop2.start;
		break;
	case 0xff:
	case 0xffff:
		anim->state = kAnimStateHalted;
		break;
	default:
		// LOAD_PALETTE is also a valid draw call index, so it is only an
		// instruction if g_animLoadPalette is set
		if (op == kAnimOpLoadPalette && g_animLoadPalette) {
			uint16 addr = READ_LE_UINT16(anim->bytecode + anim->ip);
			g_animLoadPalette(anim->fileData + addr);
			anim->ip += 2;
		} else {
			return streamDraw(anim, op);
		}
		break;
	}
	return false;
}

void animExecute() {
	if (!vmFlagIsOn(kFlagAnimEnable))
		return;

	static uint32 prevFrameT = 0;
	uint32 t = vmGetTicks();
	if (t - prevFrameT < g_animFrameT)
		return;

	prevFrameT = t;
	for (int i = 0; i < kAnimMaxStreams; i++) {
		AnimStream *anim = &g_streams[i];
		if (anim->state == kAnimStateHalted || anim->state == kAnimStatePaused)
			continue;
		streamExecute(anim);
	}
}

bool animStreamRunning(uint stream) {
	assert(stream < kAnimMaxStreams);
	return g_streams[stream].state != kAnimStateHalted;
}

bool animRunning() {
	for (int i = 0; i < kAnimMaxStreams; i++) {
		if (g_streams[i].state != kAnimStateHalted)
			return true;
	}
	return false;
}

void animExecCopyCall(uint stream) {
	if (g_animParser->getType() != kAnimA)
		error("Wrong animation type for animExecCopyCall");
	if (stream >= kAnimMaxStreams)
		error("Invalid animation stream index: %u", stream);

	byte *data = g_mem->fileData + memGetSysvar32(kSysVarDataOffset);
	byte *bytecode = data + READ_LE_UINT32(data + 2 + stream * 4);
	uint16 no = READ_LE_UINT16(bytecode);
	if (no < 20 || (no - 20) >= READ_LE_UINT16(data)) {
		warning("Invalid draw call index: %d", (int)no - 20);
		return;
	}

	byte *call = data + 2 + 100 * 4 + (no - 20) * g_animParser->getDrawCallSize();
	gfxCopyMasked(READ_LE_UINT16(call + 2), READ_LE_UINT16(call + 4), READ_LE_UINT16(call + 6),
			READ_LE_UINT16(call + 8), 1, READ_LE_UINT16(call + 10), READ_LE_UINT16(call + 12),
			memGetSysvar16(kSysVarDstSurface), getMaskColor());
}

void animDecomposeDrawCall(const AnimDrawCall &call, int &dstX, int &dstY, int &w, int &h) {
	switch (call.op) {
	case kAnimDrawCopy:
	case kAnimDrawCopyMasked:
	case kAnimDrawSwap:
	case kAnimDrawCompose:
	case kAnimDrawCopyMasked2:
		dstX = call.dst.x;
		dstY = call.dst.y;
		w = call.dim.w;
		h = call.dim.h;
		break;
	default:
		error("Unexpected animation draw operation: %d", (int)call.op);
	}
}

} // End of namespace AI5
