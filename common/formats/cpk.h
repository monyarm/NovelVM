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
 */

#ifndef FORMAT_CPKARCHIVE_H
#define FORMAT_CPKARCHIVE_H

#include "common/archive.h"
#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace Common {

class File;

struct CPKEntry {
	Common::String name;
	uint64 offset = 0;      // absolute offset of the stored bytes in the .cpk file
	uint32 size = 0;        // stored size
	uint32 extractSize = 0; // size after decompression; differs from size for CRILAYLA members
	uint32 id = 0;          // ITOC numeric ID; 0 for TOC-named entries
	bool hasId = false;     // true only for ITOC entries
};

/**
 * Read-only reader for CRI Middleware CPK archives.
 *
 * The file is a set of chunks, each a 16-byte header followed by a CRI @UTF
 * table. The "CPK " header table at offset 0 points at a TOC (named members,
 * offsets relative to the TOC chunk) and/or an ITOC (numeric-ID members packed
 * back to back from ContentOffset in ascending-ID order). Members whose stored
 * and extracted sizes differ are CRILAYLA-compressed and are decompressed on
 * read. ETOC and GTOC tables are ignored.
 *
 * Malformed input never aborts: unreadable tables and out-of-range entries are
 * dropped with a warning.
 */
class CPK : public Common::Archive {
public:
	CPK(const Common::String &filename);
	~CPK() override;

	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;

	/** Look up an ITOC member by numeric ID. TOC members are not reachable here. */
	bool getEntryById(uint32 id, CPKEntry &entry) const;

private:
	typedef Common::HashMap<Common::String, uint32, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> NameMap;
	typedef Common::HashMap<uint32, uint32> IdMap;

	Common::String _filename;
	uint64 _fileSize;
	Common::Array<CPKEntry> _entries;  // all accepted entries, TOC first, then ITOC by ID
	Common::Array<uint32> _named;      // indices into _entries that own a name, in insertion order
	NameMap _byName;
	IdMap _byId;

	void load();
	void loadToc(Common::File &f, uint64 tocOffset);
	void loadItoc(Common::File &f, uint64 itocOffset, uint64 contentOffset, uint64 align);
	bool addEntry(const CPKEntry &entry, bool isItoc);
};

CPK *makeCPKArchive(const Common::String &name);

} // namespace Common

#endif
