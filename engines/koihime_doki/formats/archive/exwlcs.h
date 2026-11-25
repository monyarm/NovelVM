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
 */

#ifndef KOIHIME_DOKI_FORMATS_ARCHIVE_EXWLCS_H
#define KOIHIME_DOKI_FORMATS_ARCHIVE_EXWLCS_H

#include "common/archive.h"
#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace KoihimeDoki {
namespace Format {
namespace Archive {

/**
 * Read-only archive for a `<base>`/`<base>.lst` EXWLCS pair.
 *
 * The index holds each member's payload offset, length and raw name. Member
 * types are guessed by sniffing the stored payload, an extension is appended
 * to the stored name, and non-image payloads are XOR-decoded when read.
 * See doc/formats/exwlcs.md for the format description.
 */
class EXWLCSArchive : public Common::Archive {
public:
	EXWLCSArchive(const Common::String &archiveBaseName);
	~EXWLCSArchive() override;

	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;

private:
	struct Entry {
		Common::String name;
		uint32 offset;
		uint32 size;
		bool encoded;
	};

	typedef Common::HashMap<Common::String, uint, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> EntryMap;

	void readIndex();

	const Common::String _baseName;
	Common::Array<Entry> _entries;
	EntryMap _entryMap;
};

/**
 * Create an archive for the given base name.
 *
 * Always returns a valid archive: a missing or unreadable body or index yields
 * an empty archive whose queries all fail, so a caller that does not check for
 * null cannot crash.
 */
EXWLCSArchive *EXWLCSFactory(const Common::String &name);

} // namespace Archive
} // namespace Format
} // namespace KoihimeDoki

#endif
