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

#include "common/formats/epl.h"

#include "common/endian.h"

// TODO: EPL trees are only read from RMD effect blocks. Not read anywhere yet:
// - P3 FES battle effect containers (BATTLE/EFFECT/*.BED in BTL.CVM, magic "BED400": a table
//   of 24-byte entries pointing at embedded EPL blocks; most entry fields are not understood),
// - standalone .EPL files (FIELD/BTL_EFFECT/BTL_EFF.PAC, ICON/, EVENT/SYS/, FACILITY/EFFECT/),
// - EPL entries of .PM1 event containers, whose 4-word "EPLHead" records are not understood.

namespace Common {

namespace {

const uint32 kRootSize = 0x90;
const uint32 kNodeSize = 0xC0;
const uint32 kRecordHeaderSize = 0x2C;
const uint32 kMaxRecordVersion = 0xD2;
const uint32 kKindCount = 0x21;

float readFloat(const byte *p) {
	const uint32 bits = READ_LE_UINT32(p);
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

void readVec4(const byte *p, float out[4]) {
	for (int i = 0; i < 4; ++i)
		out[i] = readFloat(p + i * 4);
}

void readState(const byte *p, EPL::State &state) {
	readVec4(p + 0x00, state.localOffset);
	readVec4(p + 0x20, state.scaleVector);
	readVec4(p + 0x40, state.offset);
	readVec4(p + 0x50, state.orientation);
	state.scale = readFloat(p + 0x60);
	state.color = READ_LE_UINT32(p + 0x64);
	state.flags = READ_LE_UINT32(p + 0x68);
	state.multiplier = readFloat(p + 0x74);
}

// A record sub-block is at record + offset (offset 0 = none). A non-zero "alloc" word marks a
// runtime-patched record whose offset is a pointer; file data never has one.
bool readBlock(const byte *data, uint32 size, uint32 record, uint32 offset, uint32 length,
		uint32 alloc, Common::Array<byte> &out) {
	out.clear();
	if (alloc != 0)
		return false;
	if (offset == 0)
		return length == 0;
	const uint64 start = (uint64)record + offset;
	if (start + length > size)
		return false;
	out.resize(length);
	if (length)
		memcpy(out.data(), data + start, length);
	return true;
}

} // End of anonymous namespace

EPL::EPL(const byte *data, uint32 size) {
	_ok = parse(data, size);
	if (!_ok)
		_nodes.clear();
}

bool EPL::parse(const byte *data, uint32 size) {
	if (!data || size < kRootSize)
		return false;
	readState(data, _root);
	const int32 count = (int32)READ_LE_UINT32(data + 0x80);
	const uint32 array = READ_LE_UINT32(data + 0x88);
	// A file image has 0 here; non-zero means an already linked (runtime) tree.
	if (READ_LE_UINT32(data + 0x8C) != 0 || count < 0 || (uint64)array + (uint64)count * kNodeSize > size)
		return false;

	Common::Array<int32> shareFrom(count, -1);
	for (int32 i = 0; i < count; ++i) {
		const byte *n = data + array + i * kNodeSize;
		Node node;
		readState(n, node.state);
		node.startFrame = (int32)READ_LE_UINT32(n + 0x80);
		node.flags = READ_LE_UINT32(n + 0x84);
		const int32 id = (int32)READ_LE_UINT32(n + 0x90);
		const int32 sourceIndex = (int32)READ_LE_UINT32(n + 0x94);
		const uint32 sourceFlags = READ_LE_UINT32(n + 0x98);
		uint32 nameLength = 0;
		while (nameLength < 16 && n[0x9C + nameLength])
			++nameLength;
		node.name = Common::String((const char *)n + 0x9C, nameLength);

		if (sourceFlags & 1) {
			// Clone: id is the index of an earlier node.
			if (id < 0 || id >= i)
				return false;
			node.cloneOf = id;
			node.record = _nodes[id].record;
		} else {
			// id is the byte offset of this node's primitive record.
			if (id < 0 || (uint64)id + kRecordHeaderSize > size)
				return false;
			const uint32 r = (uint32)id;
			Record &record = node.record;
			record.version = READ_LE_UINT32(data + r);
			record.kind = READ_LE_UINT16(data + r + 0x04);
			record.sub = READ_LE_UINT16(data + r + 0x0C);
			record.resourceKind = READ_LE_UINT16(data + r + 0x1C);
			if (record.version > kMaxRecordVersion || record.kind >= kKindCount)
				return false;
			if (!readBlock(data, size, r, READ_LE_UINT32(data + r + 0x10), READ_LE_UINT32(data + r + 0x14),
					READ_LE_UINT32(data + r + 0x18), record.parameters))
				return false;
			if (sourceFlags & 2) {
				// Shares another node's secondary resource; its own fields must be empty.
				if (READ_LE_UINT32(data + r + 0x20) != 0 || READ_LE_UINT32(data + r + 0x24) != 0 ||
						sourceIndex < 0 || sourceIndex >= count || sourceIndex == i)
					return false;
				shareFrom[i] = sourceIndex;
			} else if (!readBlock(data, size, r, READ_LE_UINT32(data + r + 0x20), READ_LE_UINT32(data + r + 0x24),
					READ_LE_UINT32(data + r + 0x28), record.resource)) {
				return false;
			}
		}
		_nodes.push_back(node);
	}
	// Shared secondaries may point at any node, so resolve them once all are read, following
	// chains of shares (bounded, so a cycle fails instead of looping).
	for (int32 i = 0; i < count; ++i) {
		int32 source = shareFrom[i];
		for (int32 hops = 0; source >= 0 && shareFrom[source] >= 0; ++hops) {
			if (hops >= count)
				return false;
			source = shareFrom[source];
		}
		if (source >= 0)
			_nodes[i].record.resource = _nodes[source].record.resource;
	}
	// Clones copied their source's record before shares were resolved.
	for (int32 i = 0; i < count; ++i)
		if (_nodes[i].cloneOf >= 0)
			_nodes[i].record = _nodes[_nodes[i].cloneOf].record;
	return true;
}

} // End of namespace Common
