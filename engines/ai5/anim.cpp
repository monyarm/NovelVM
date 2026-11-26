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

#include "common/ptr.h"
#include "common/textconsole.h"

#include "ai5/anim.h"
#include "ai5/buffer.h"

namespace AI5 {

AnimParser::AnimParser(GameId game) : _game(game) {
	switch (game) {
	case GID_ISAKU:
	case GID_DOUKYUUSEI:
	case GID_AISHIMAI:
	case GID_BEYOND:
		_drawCallSize = 17;
		_type = kAnimA;
		break;
	case GID_DOUKYUUSEI2:
	case GID_KAKYUUSEI:
	case GID_SHUUSAKU:
		_drawCallSize = 17;
		_type = kAnimA8;
		break;
	case GID_SHANGRLIA:
		_drawCallSize = 10;
		_type = kAnimS4;
		break;
	default:
		_drawCallSize = 33;
		_type = kAnimS4;
		break;
	}
}

static void parseColor(Buffer &in, AnimColor &out) {
	byte b1 = in.readU8();
	byte b2 = in.readU8();
	out.r = (b2 & 0xf0) | (b2 >> 4);
	out.g = (b2 & 0x0f) | (b2 << 4);
	out.b = (b1 & 0x0f) | (b1 << 4);
}

static AnimDrawOpcode parseS4Opcode(byte op) {
	switch (op & 0xf0) {
	case 0x10: return kAnimDrawCopy;
	case 0x20: return kAnimDrawCopyMasked;
	case 0x30: return kAnimDrawSwap;
	case 0x40: return kAnimDrawSetColor;
	case 0x50: return kAnimDrawCompose;
	case 0x60: return kAnimDrawFill;
	case 0x80: return kAnimDrawSetPalette;
	default:
		return kAnimDrawInvalid;
	}
}

static bool parseS4DrawCall(Buffer &in, AnimDrawCall &out, uint srcI, uint callSize) {
	uint32 start = in.index();
	byte op = in.readU8();
	switch ((out.op = parseS4Opcode(op))) {
	case kAnimDrawFill:
		out.dst.i = ((op >> 1) & 1) ? srcI : 0;
		out.dst.x = in.readU8() * 8;
		out.dst.y = in.readU16();
		out.dim.w = (in.readU8() + 1) * 8 - out.dst.x;
		out.dim.h = (in.readU16() + 1) - out.dst.y;
		break;
	case kAnimDrawCopy:
	case kAnimDrawCopyMasked:
	case kAnimDrawSwap:
		out.src.i = ((op >> 1) & 1) ? srcI : 0;
		out.dst.i = (op & 1) ? srcI : 0;
		out.src.x = in.readU8() * 8;
		out.src.y = in.readU16();
		out.dim.w = (in.readU8() + 1) * 8 - out.src.x;
		out.dim.h = (in.readU16() + 1) - out.src.y;
		out.dst.x = in.readU8() * 8;
		out.dst.y = in.readU16();
		break;
	case kAnimDrawCompose:
		out.bg.i = (op & 1) ? srcI : 0;
		out.fg.i = ((op >> 1) & 1) ? srcI : 0;
		out.dst.i = ((op >> 2) & 1) ? srcI : 0;
		out.fg.x = in.readU8() * 8;
		out.fg.y = in.readU16();
		out.dim.w = (in.readU8() + 1) * 8 - out.fg.x;
		out.dim.h = (in.readU16() + 1) - out.fg.y;
		out.bg.x = in.readU8() * 8;
		out.bg.y = in.readU16();
		out.dst.x = in.readU8() * 8;
		out.dst.y = in.readU16();
		break;
	case kAnimDrawSetColor:
		// the index is also the blue value
		out.colorIndex = in.peekU8();
		parseColor(in, out.color);
		break;
	case kAnimDrawSetPalette:
		for (int i = 0; i < 16; i++)
			parseColor(in, out.colors[i]);
		break;
	default:
		warning("AnimParser: invalid draw call opcode: %02x", op);
		return false;
	}
	in.seek(start + callSize);
	return true;
}

static AnimDrawOpcode parseA8Opcode(byte op) {
	switch (op & 0xf0) {
	case 0x10: return kAnimDrawCopy;
	case 0x20: return kAnimDrawCopyMasked;
	case 0x30: return kAnimDrawSwap;
	// 0x40 is COMPOSE in Shuusaku, unused in Kakyuusei
	case 0x40: return kAnimDrawCompose;
	// 0x50 is COPY_MASKED (redundant) in Kakyuusei, unused in Shuusaku
	case 0x50: return kAnimDrawCopyMasked2;
	case 0x60: return kAnimDrawFill;
	default:
		return kAnimDrawInvalid;
	}
}

static void parseCopyArgs(Buffer &in, AnimDrawCall &out) {
	out.src.x = in.readU16();
	out.src.y = in.readU16();
	out.dim.w = in.readU16();
	out.dim.h = in.readU16();
	out.dst.x = in.readU16();
	out.dst.y = in.readU16();
}

static void parseComposeArgs(Buffer &in, AnimDrawCall &out) {
	out.fg.x = in.readU16();
	out.fg.y = in.readU16();
	out.dim.w = in.readU16();
	out.dim.h = in.readU16();
	out.bg.x = in.readU16();
	out.bg.y = in.readU16();
	out.dst.x = out.bg.x;
	out.dst.y = out.bg.y;
}

static bool parseA8DrawCall(Buffer &in, AnimDrawCall &out, uint srcI, uint callSize) {
	uint32 start = in.index();
	byte op = in.readU8();
	switch ((out.op = parseA8Opcode(op))) {
	case kAnimDrawCopy:
	case kAnimDrawCopyMasked:
	case kAnimDrawSwap:
		out.dst.i = (op & 1) ? srcI : 0;
		out.src.i = ((op >> 1) & 1) ? srcI : 0;
		parseCopyArgs(in, out);
		break;
	case kAnimDrawFill:
		out.dst.i = ((op >> 1) & 1) ? srcI : 0;
		out.dst.x = in.readU16();
		out.dst.y = in.readU16();
		out.dim.w = in.readU16();
		out.dim.h = in.readU16();
		break;
	case kAnimDrawCompose:
		out.dst.i = (op & 1) ? srcI : 0;
		out.fg.i = ((op >> 1) & 1) ? srcI : 0;
		out.bg.i = 3; // hardcoded in Shuusaku
		parseComposeArgs(in, out);
		break;
	case kAnimDrawCopyMasked2:
		out.dst.i = (op & 1) ? srcI : 0;
		out.fg.i = ((op >> 1) & 1) ? srcI : 0;
		out.bg.i = ((op >> 2) & 1) ? srcI : 0;
		parseComposeArgs(in, out);
		break;
	default:
		warning("AnimParser: invalid draw call opcode: %02x", op);
		return false;
	}
	in.seek(start + callSize);
	return true;
}

static AnimDrawOpcode parseAOpcode(byte op, GameId game) {
	switch (op & 0xf0) {
	case 0x10:
		return kAnimDrawCopy;
	case 0x20:
		return kAnimDrawCopyMasked;
	case 0x30:
		return game == GID_BEYOND ? kAnimDrawComposeWithOffset : kAnimDrawSwap;
	case 0x40:
		return kAnimDrawCompose;
	default:
		break;
	}
	// BE-YOND
	switch (op) {
	case 0x60: return kAnimDraw0x60CopyMasked;
	case 0x61: return kAnimDraw0x61Compose;
	case 0x62: return kAnimDraw0x62;
	case 0x63: return kAnimDraw0x63CopyMaskedWithXOffset;
	case 0x64: return kAnimDraw0x64ComposeMasked;
	case 0x65: return kAnimDraw0x65Compose;
	case 0x66: return kAnimDraw0x66;
	default:
		return kAnimDrawInvalid;
	}
}

static bool parseADrawCall(Buffer &in, AnimDrawCall &out, uint srcI, uint callSize, GameId game) {
	uint32 start = in.index();
	byte op = in.readU8();
	switch ((out.op = parseAOpcode(op, game))) {
	case kAnimDrawCopy:
	case kAnimDrawCopyMasked:
	case kAnimDrawSwap:
	case kAnimDraw0x62:
	case kAnimDraw0x66:
		out.src.i = srcI;
		out.dst.i = 0;
		parseCopyArgs(in, out);
		break;
	case kAnimDrawCompose:
	case kAnimDrawComposeWithOffset:
		out.bg.i = 2;
		out.fg.i = srcI;
		out.dst.i = 0;
		parseComposeArgs(in, out);
		break;
	case kAnimDraw0x60CopyMasked:
		out.src.i = 4;
		out.dst.i = 11;
		parseCopyArgs(in, out);
		out.dst.y -= 8;
		break;
	case kAnimDraw0x61Compose:
		out.bg.i = 10;
		out.fg.i = 10;
		out.dst.i = 4;
		parseComposeArgs(in, out);
		out.bg.x = 200;
		out.bg.y = 320;
		break;
	case kAnimDraw0x63CopyMaskedWithXOffset:
		out.src.i = 4;
		out.dst.i = 11;
		parseCopyArgs(in, out);
		out.dst.y += 20;
		break;
	case kAnimDraw0x64ComposeMasked:
		out.bg.i = 8;
		out.fg.i = 1;
		out.dst.i = 0;
		parseComposeArgs(in, out);
		break;
	case kAnimDraw0x65Compose:
		out.bg.i = 2;
		out.fg.i = 3;
		out.dst.i = 0;
		parseComposeArgs(in, out);
		break;
	default:
		warning("AnimParser: invalid draw call opcode: %02x", op);
		return false;
	}
	in.seek(start + callSize);
	return true;
}

bool AnimParser::parseDrawCall(const byte *data, AnimDrawCall &out, uint srcI) const {
	Buffer b(data, _drawCallSize);
	out = AnimDrawCall();
	switch (_type) {
	case kAnimS4: return parseS4DrawCall(b, out, srcI, _drawCallSize);
	case kAnimA8: return parseA8DrawCall(b, out, srcI, _drawCallSize);
	case kAnimA: return parseADrawCall(b, out, srcI, _drawCallSize, _game);
	default:
		break;
	}
	error("AnimParser: invalid anim type");
}

static void parseInstruction(Buffer &in, const Anim &anim, AnimInstruction &out, bool wide) {
	// the 16-bit format truncates the opcode to 8 bits
	byte op = wide ? (byte)in.readU16() : in.readU8();
	out.arg = 0;
	switch (op) {
	case kAnimOpNoop:
	case kAnimOpCheckStop:
	case kAnimOpReset:
	case kAnimOpHalt:
	case kAnimOpLoopEnd:
	case kAnimOpLoop2End:
		out.op = op;
		break;
	case kAnimOpStall:
	case kAnimOpLoopStart:
	case kAnimOpLoop2Start:
		out.op = op;
		out.arg = wide ? in.readU16() : in.readU8();
		break;
	case kAnimOpLoadPalette:
		out.op = op;
		out.arg = in.readU16();
		break;
	default:
		if (op < 20 || op - 20u >= anim.drawCalls.size()) {
			warning("AnimParser: invalid draw call index %d at %x", (int)op - 20, in.index() - 1);
			out.op = kAnimOpNoop;
			break;
		}
		out.op = kAnimOpDraw;
		out.arg = op - 20;
		break;
	}
}

Anim *AnimParser::parse(const byte *data, uint32 size) const {
	Buffer in(data, size);
	Common::ScopedPtr<Anim> anim(new Anim());

	if (_type == kAnimS4) {
		uint16 streamPtr[kAnimMaxStreams];
		uint32 streamEnd[kAnimMaxStreams];
		byte nrStreams = in.readU8();
		if (nrStreams > kAnimMaxStreams) {
			warning("AnimParser: too many streams in animation file");
			return nullptr;
		}
		if (!nrStreams) {
			warning("AnimParser: no streams in animation file");
			return nullptr;
		}
		for (uint i = 0; i < nrStreams; i++)
			streamPtr[i] = in.readU16();

		uint streamStart = streamPtr[0];
		for (uint i = 1; i < nrStreams; i++)
			streamStart = MIN<uint>(streamStart, streamPtr[i]);

		while (!in.end() && in.index() < streamStart) {
			AnimDrawCall call = AnimDrawCall();
			if (!parseS4DrawCall(in, call, 1, _drawCallSize))
				return nullptr;
			anim->drawCalls.push_back(call);
		}

		for (uint i = 0; i < nrStreams; i++) {
			in.seek(streamPtr[i]);
			// Many S4 files contain stream pointers which point into
			// previous streams (apparently miscounted LOOP2_START
			// instructions). Such streams are treated as empty.
			bool overlap = false;
			for (uint j = 0; j < i; j++) {
				if (streamPtr[j] < streamPtr[i] && streamEnd[j] > streamPtr[i]) {
					warning("AnimParser: streams %u and %u overlap (possibly broken file)", i, j);
					overlap = true;
					break;
				}
			}
			while (!overlap && !in.end() && in.peekU8() != 0xff) {
				AnimInstruction instr;
				parseInstruction(in, *anim, instr, false);
				anim->streams[i].push_back(instr);
			}
			streamEnd[i] = in.index();
		}
	} else if (_type == kAnimA8) {
		uint32 streamPtr[10];
		byte nrDrawCalls = in.readU8();
		for (int i = 0; i < 10; i++)
			streamPtr[i] = in.readU16();

		uint streamStart = streamPtr[0];
		for (int i = 0; i < 10; i++)
			streamStart = MIN<uint>(streamStart, streamPtr[i]);

		while (!in.end() && in.index() < streamStart) {
			if (_game == GID_SHUUSAKU || _game == GID_DOUKYUUSEI2) {
				// Palettes are counted as draw calls but have a different
				// length. In practice they always start with 0xff or 0x00.
				byte c = in.peekU8();
				if (c == 0 || c == 0xff) {
					AnimPalette pal;
					pal.addr = in.index();
					for (int i = 0; i < 256; i++) {
						pal.colors[i].r = in.readU8();
						pal.colors[i].g = in.readU8();
						pal.colors[i].b = in.readU8();
					}
					anim->palettes.push_back(pal);
					nrDrawCalls--;
					continue;
				}
			}
			AnimDrawCall call = AnimDrawCall();
			if (!parseA8DrawCall(in, call, 1, _drawCallSize))
				return nullptr;
			anim->drawCalls.push_back(call);
		}
		if (anim->drawCalls.size() != nrDrawCalls)
			warning("AnimParser: declared draw call count doesn't match number of parsed calls");

		for (int i = 0; i < 10; i++) {
			in.seek(streamPtr[i]);
			while (!in.end() && in.peekU8() != 0xff) {
				AnimInstruction instr;
				parseInstruction(in, *anim, instr, false);
				anim->streams[i].push_back(instr);
			}
		}
	} else {
		uint32 streamPtr[kAnimMaxStreams];
		uint16 nrDrawCalls = in.readU16();
		for (uint i = 0; i < kAnimMaxStreams; i++)
			streamPtr[i] = in.readU32();

		uint streamStart = streamPtr[0];
		for (uint i = 0; i < kAnimMaxStreams; i++)
			streamStart = MIN<uint>(streamStart, streamPtr[i]);

		while (!in.end() && in.index() < streamStart) {
			AnimDrawCall call = AnimDrawCall();
			if (!parseADrawCall(in, call, 1, _drawCallSize, _game))
				return nullptr;
			anim->drawCalls.push_back(call);
		}
		if (anim->drawCalls.size() != nrDrawCalls)
			warning("AnimParser: declared draw call count doesn't match number of parsed calls");

		for (uint i = 0; i < kAnimMaxStreams; i++) {
			in.seek(streamPtr[i]);
			while (!in.end() && in.peekU16() != 0xffff) {
				AnimInstruction instr;
				parseInstruction(in, *anim, instr, true);
				anim->streams[i].push_back(instr);
			}
		}
	}
	return anim.release();
}

} // End of namespace AI5
