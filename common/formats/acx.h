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

#ifndef FORMAT_ACX_H
#define FORMAT_ACX_H

#include "common/archive.h"
#include "common/array.h"
#include "common/str.h"

namespace Common {

/**
 * Archive reader for CRI's "legacy" ACX format: a small cue table wrapping
 * several embedded raw ADX streams (real-data-verified: confirmed used in
 * vanilla Persona 3/4 on PS2). No TOC/CPK/CVM involved -- just an 8-byte
 * header (zero uint32 + a uint32 stream count), followed by one
 * (offset:uint32BE, size:uint32BE) entry per stream, followed by the
 * streams themselves concatenated back to back.
 *
 * Real-data-verified against Persona 4's BTL.CVM (MODEL_BTL/ENEMY/ .ACX files):
 * every entry's offset+size equals the next entry's offset, and the last
 * entry's offset+size equals the file size, across every sample checked.
 *
 * ACX has no real per-stream names, so members are exposed as "N.adx"
 * where N is the stream's index in file order.
 */
class ACX : public Common::Archive {
public:
	ACX(const Common::String &filename);
	~ACX() override;

	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;

private:
	struct Entry {
		uint32 offset;
		uint32 size;
	};

	Common::String _filename;
	Common::Array<Entry> _entries;

	bool load();
};

/**
 * This factory method creates an Archive instance corresponding to the
 * content of the ACX file with the given name.
 *
 * May return 0 in case of a failure.
 */
ACX *makeACXArchive(const Common::String &name);

} // namespace Common

#endif
