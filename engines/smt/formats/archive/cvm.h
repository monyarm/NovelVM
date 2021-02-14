#ifndef SMT_CVMARCHIVE_H
#define SMT_CVMARCHIVE_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/bufferedstream.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/hash-str.h"
#include "common/memstream.h"

namespace SMT {

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

class CVMArchive : public Common::Archive {
	Common::String _cvmFilename;

public:
	CVMArchive(const Common::String &name);
	void ReadFile(Common::SeekableReadStream &reader);
	CVMEntrysMap _entries;
	~CVMArchive() override;

	CVMEntry ReadISORecord(Common::SeekableReadStream &reader, bool isRoot = false, Common::String parentName = "");

	// Archive implementation
	bool hasFile(const Common::String &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::String &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::String &name) const override;
};

/**
 * This factory method creates an Archive instance corresponding to the content
 * of the CVM compressed file with the given name.
 *
 * May return 0 in case of a failure.
 */
CVMArchive *makeCVMArchive(const Common::String &name);

} // End of namespace SMT

#endif
