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

#ifndef AI5_ANIM_H
#define AI5_ANIM_H

#include "common/array.h"

#include "ai5/detection.h"

namespace AI5 {

enum AnimType {
	kAnimS4,
	kAnimA8, // 8-bit .A
	kAnimA   // 16-bit .A (typical)
};

enum AnimOpcode {
	kAnimOpDraw = -1,
	kAnimOpNoop = 0,          // stall 1 cycle
	kAnimOpCheckStop = 1,     // check if VM requested stop
	kAnimOpStall = 2,         // stall N cycles
	kAnimOpReset = 3,         // reset IP to 0 (loop from beginning)
	kAnimOpHalt = 4,          // stop
	kAnimOpLoopStart = 5,     // loop N times
	kAnimOpLoopEnd = 6,       // go to start of loop (or continue if loop finished)
	kAnimOpLoop2Start = 7,    // nested loop start
	kAnimOpLoop2End = 8,      // nested loop end
	kAnimOpLoadPalette = 0x96 // load palette
};

enum AnimDrawOpcode {
	kAnimDrawInvalid = -1,
	kAnimDrawCopy,
	kAnimDrawCopyMasked,
	kAnimDrawCopyMasked2,
	kAnimDrawSwap,
	kAnimDrawSetColor,
	kAnimDrawCompose,
	kAnimDrawComposeWithOffset,
	kAnimDrawFill,
	kAnimDrawSetPalette,
	// BE-YOND
	kAnimDraw0x60CopyMasked,
	kAnimDraw0x61Compose,
	kAnimDraw0x62,
	kAnimDraw0x63CopyMaskedWithXOffset,
	kAnimDraw0x64ComposeMasked,
	kAnimDraw0x65Compose,
	kAnimDraw0x66
};

struct AnimTarget {
	byte i;
	int x, y;
};

struct AnimSize {
	int w, h;
};

struct AnimColor {
	byte r, g, b;
};

struct AnimDrawCall {
	AnimDrawOpcode op;
	// fill: dst, dim
	// copy: src, dst, dim
	// compose: fg, bg, dst, dim
	AnimTarget src, dst, fg, bg;
	AnimSize dim;
	// set color
	byte colorIndex;
	AnimColor color;
	// set palette
	AnimColor colors[16];
};

struct AnimPalette {
	uint16 addr;
	AnimColor colors[256];
};

struct AnimInstruction {
	int op; // AnimOpcode, or kAnimOpDraw
	uint16 arg;
};

enum {
	kAnimMaxStreams = 100
};

struct Anim {
	Common::Array<AnimInstruction> streams[kAnimMaxStreams];
	Common::Array<AnimDrawCall> drawCalls;
	Common::Array<AnimPalette> palettes;
};

class AnimParser {
public:
	explicit AnimParser(GameId game);

	AnimType getType() const { return _type; }
	uint getDrawCallSize() const { return _drawCallSize; }

	Anim *parse(const byte *data, uint32 size) const;
	bool parseDrawCall(const byte *data, AnimDrawCall &out, uint srcI) const;

private:
	GameId _game;
	AnimType _type;
	uint _drawCallSize;
};

} // End of namespace AI5

#endif // AI5_ANIM_H
