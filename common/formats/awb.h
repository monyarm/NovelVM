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

#ifndef FORMAT_AWB_H
#define FORMAT_AWB_H

#include "common/archive.h"
#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Common {

/**
 * Archive reader for CRI Middleware's AWB (AFS2) format: a little-endian
 * wave bank container holding multiple audio subfiles (HCA, ADX, VAG, etc.).
 * Each subfile is identified by a numeric ID, not a filename.
 *
 * Real structure (little-endian): magic "AFS2", version byte, offset-size
 * byte (2 or 4), waveid alignment (u16 LE), entry count (s32 LE), offset
 * alignment (u16 LE), HCA subkey (u16 LE), then an ID table (N x u16 LE)
 * and an offset table ((N+1) x offset_size). Offsets are absolute from
 * file start, aligned to offset_alignment.
 *
 * The subkey at offset 0x0E is combined with a game-specific seed key to
 * derive HCA decryption keys when needed. Stored here for callers to use.
 *
 * Codec of each subfile is detected by peeking the first bytes (HCA:
 * "HCA\0" & 0x7F7F7F7F, ADX: 0x8000 big-endian, VAG: "VAGp", WAV: "RIFF").
 *
 * Members are exposed as "id_NNNN.ext" where NNNN is the entry's numeric ID
 * and ext is guessed from the codec.
 */
class AWB : public Common::Archive {
public:
	AWB(const Common::String &filename);
	// For embedded/streamed AWB data (e.g. inside an ACB).
	AWB(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeStream);
	~AWB() override;

	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;

	// Look up a subfile by its numeric waveform ID (the value ACB cues
	// reference) instead of its synthetic "id_NNNN.ext" member name.
	Common::SeekableReadStream *createReadStreamForId(uint16 id) const;

	uint16 getSubkey() const { return _subkey; }

private:
	struct Entry {
		Common::String name;
		uint16 id = 0;
		uint32 offset = 0;
		uint32 size = 0;
	};

	Common::String _filename;
	Common::DisposablePtr<Common::SeekableReadStream> _embeddedStream;
	Common::Array<Entry> _entries;
	uint16 _subkey = 0;

	bool loadFromStream(Common::SeekableReadStream &f);
	int findEntryByName(const Common::String &name) const;
	int findEntryById(uint16 id) const;
	Common::SeekableReadStream *createReadStreamForEntry(const Entry &entry) const;
};

AWB *makeAWBArchive(const Common::String &name);

} // namespace Common

#endif
