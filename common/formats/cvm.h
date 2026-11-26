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

#ifndef FORMAT_CVMARCHIVE_H
#define FORMAT_CVMARCHIVE_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/bufferedstream.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/path.h"
#include "common/hash-str.h"
#include "common/memstream.h"

namespace Common {

class Archive;

struct CVMEntry {
	uint32 offset;
	uint32 size;
	Common::String name;
	Common::String cvmFile;
	Common::Array<CVMEntry> parent;
	Common::Array<CVMEntry> subEntries;
};

enum RecordFlags : byte {
	FileRecord = 1 << 0,
	DirectoryRecord = 1 << 1
};

const int CVM_HEADER_SIZE = 0x1800;
const int ISO_RESERVED_SIZE = 0x8000;
const int ISO_BLOCKSIZE = 0x800;
const int ISO_ROOTDIRECTORY_OFFSET = 0x9C;
const int ID_PRIM_VOLUME_DESC = 0x01;

typedef Common::HashMap<Common::String, Common::ScopedPtr<CVMEntry>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> CVMEntrysMap;

class CVM : public Common::Archive {
	Common::String _cvmFilename;

public:
	CVM(const Common::String &name);
	void ReadFile(Common::SeekableReadStream &reader);
	CVMEntrysMap _entries;
	~CVM() override;

	CVMEntry ReadISORecord(Common::SeekableReadStream &reader, bool isRoot = false, Common::String parentName = "");

	// Archive implementation
	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;
};

/**
 * This factory method creates an Archive instance corresponding to the content
 * of the CVM compressed file with the given name.
 *
 * May return 0 in case of a failure.
 */
CVM *makeCVMArchive(const Common::String &name);

} // namespace Common

#endif
