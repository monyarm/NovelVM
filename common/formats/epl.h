/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
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
 */

#ifndef COMMON_FORMATS_EPL_H
#define COMMON_FORMATS_EPL_H

#include "common/array.h"
#include "common/str.h"

namespace Common {

/**
 * Persona 3 FES / Persona 4 EPL effect tree, as embedded in RMD chunk 0xF0F000E0 (and as a
 * standalone .EPL file). A tree is a root plus a flat list of child nodes; each node owns
 * (or clones) a primitive record of some kind (particles, rings, flash quads, ...) whose
 * parameter block and secondary resource are kept as raw bytes for the effect runtime.
 *
 * Layout: root header 0x90 bytes, then `count` 0xC0-byte nodes, then primitive records
 * (0x2C-byte header + parameter block + optional secondary resource). Offsets are relative
 * to the start of the tree, or for record sub-blocks to the record itself.
 */
class EPL {
public:
	/** Primitive kinds used by P3 FES/P4 RMDs. */
	enum Kind {
		kParticle = 1,
		kScreenBlur = 4,
		kScreenFilter = 5,
		kBoard = 7,
		kFlashQuads = 10,
		kRing = 12,
		kWind = 16,
		kModel = 26,
		kObjectParticle = 28
	};

	/** Secondary resource kinds. */
	enum ResourceKind {
		kResourceNone = 0,
		kResourceTMX = 1,        ///< embedded TMX texture
		kResourceTrack = 2,      ///< animated texture track
		kResourceRWStream = 3,   ///< embedded RenderWare stream (texture dictionary + clump)
		kResourceTextureSlot = 4 ///< u32 index into the 22 global effect textures
	};

	struct Record {
		uint32 version = 0; ///< <= 210
		uint16 kind = 0;
		uint16 sub = 0;     ///< sub-type within the kind (emitter model, ring style, ...)
		uint16 resourceKind = kResourceNone;
		Common::Array<byte> parameters;
		Common::Array<byte> resource; ///< secondary resource bytes (may be shared with another node's)
	};

	/** Transform/colour state shared by the root and the nodes (the first 0x80 bytes of each). */
	struct State {
		float localOffset[4] = {0, 0, 0, 1}; ///< root: offset of the whole effect
		float scaleVector[4] = {1, 1, 1, 1}; ///< node: per-axis scale (state flag 0x100)
		float offset[4] = {0, 5, 0, 1};      ///< node: position relative to the effect origin
		float orientation[4] = {0, 0, 0, 1}; ///< node: rotation quaternion (x, y, z, w)
		float scale = 1;
		uint32 color = 0xFFFFFFFF;           ///< packed RGBA, byte 0 = R
		uint32 flags = 0x80;                 ///< state flags (camera aim, scale inheritance, ...)
		float multiplier = 1;                ///< root: extra factor on the scale given to children
	};

	enum StateFlags {
		kStateBaseHeight = 0x04,     ///< base point y forced to 5.0
		kStateAimOffset = 0x08,      ///< offset swung toward the camera
		kStateAimOffsetFlat = 0x10,  ///< as kStateAimOffset, horizontally only
		kStateFaceCamera = 0x20,     ///< orientation faces the camera (pitch and yaw)
		kStateFaceCameraYaw = 0x40,  ///< orientation x yaw-only camera aim
		kStateScaleOffset = 0x80,    ///< offset is multiplied by the parent scale
		kStateVectorScale = 0x100    ///< scale is scaleVector x scale
	};

	struct Node {
		State state;
		int32 startFrame = 0;  ///< node is idle until the tree frame reaches this
		uint32 flags = 1;      ///< bit 0: scale inherits the tree scale; bit 1: disabled
		int32 cloneOf = -1;    ///< index of the earlier node whose primitive this one clones
		Common::String name;   ///< informative only (e.g. "P-SPIRL")
		Record record;         ///< for clones: a copy of the source node's record
	};

	EPL(const byte *data, uint32 size);

	bool ok() const { return _ok; }
	const State &root() const { return _root; }
	const Common::Array<Node> &nodes() const { return _nodes; }

private:
	bool _ok = false;
	State _root;
	Common::Array<Node> _nodes;

	bool parse(const byte *data, uint32 size);
};

} // End of namespace Common

#endif
