#ifndef IKURA_FORMATS_ARCHIVE_CABINET_H
#define IKURA_FORMATS_ARCHIVE_CABINET_H

#include "common/archive.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace Ikura {
namespace Format {
namespace Archive {

// Ikura/GDL resource cabinet (ArchiveIkura in the VileVN reference,
// res/archives/aikura.cpp).
class Cabinet : public Common::Archive {
public:
	// Takes ownership of stream either way. Returns nullptr on any
	// malformed/unrecognized input rather than throwing or asserting.
	static Cabinet *open(Common::SeekableReadStream *stream);

	~Cabinet() override;

	bool hasFile(const Common::Path &path) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &path) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override;

private:
	struct Entry {
		uint32 start;
		uint32 size;
	};

	typedef Common::HashMap<Common::String, Entry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> EntryMap;

	explicit Cabinet(Common::SeekableReadStream *stream);

	bool parseSM2MPX10();
	bool parseHeaderless();
	bool addEntry(const Common::String &name, uint32 start, uint32 size);

	Common::SeekableReadStream *_stream;
	EntryMap _entries;
};

} // End of namespace Archive
} // End of namespace Format
} // End of namespace Ikura

#endif
