#ifndef SMT_CVM_H
#define SMT_CVM_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/file.h"
#include "common/hash-str.h"
#include "common/memstream.h"
#include "common/bufferedstream.h"
#include "common/debug.h"

#include "engines/smt/formats/archive/cvm/isodirectory.h"

namespace SMT
{

class Archive;
class IsoDirectoryRecord;

const int32 CVM_HEADER_SIZE = 0x1800;
const int32 ISO_RESERVED_SIZE = 0x8000;
const int32 ISO_BLOCKSIZE = 0x800;
const int32 ISO_ROOTDIRECTORY_OFFSET = 0x9C;
const int32 ID_PRIM_VOLUME_DESC = 0x01;

struct CVMHeader
{
	Common::String path;
	uint32 position;
	uint32 size;
};

typedef Common::HashMap<Common::String, Common::ScopedPtr<CVMHeader>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> CVMHeadersMap;

class CVMArchive : public Common::Archive
{
	CVMHeadersMap _headers;
	Common::String _cvmFilename;

public:
	CVMArchive(const Common::String &name);
	~CVMArchive() override;

	IsoDirectoryRecord* RootDirectory()
	{
		return mRootDirectory;
	}

	// Archive implementation
	bool hasFile(const Common::String &name) const override;
	int32 listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::String &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::String &name) const override;

private:
	IsoDirectoryRecord* mRootDirectory;
};

/**
 * This factory method creates an Archive instance corresponding to the content
 * of the CVM compressed file with the given name.
 *
 * May return 0 in case of a failure.
 */
CVMArchive *makeCVMArchive(const Common::String &name);

} // namespace SMT

#endif
