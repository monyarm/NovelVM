#ifndef BIBLEBLACK_PAKARCHIVE_H
#define BIBLEBLACK_PAKARCHIVE_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/file.h"
#include "common/hash-str.h"
#include "common/memstream.h"
#include "common/bufferedstream.h"
#include "common/debug.h"

namespace BibleBlack {

class Archive;


struct PakHeader {
Common::String name;
uint32 position;
uint32 size;
};

typedef Common::HashMap<Common::String, Common::ScopedPtr<PakHeader>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> PakHeadersMap;

class PAKArchive : public Common::Archive {
	PakHeadersMap _headers;
	Common::String _pakFilename;

public:
	PAKArchive(const Common::String &name);
	~PAKArchive() override;

	// Archive implementation
	bool hasFile(const Common::String &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::String &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::String &name) const override;
};

/**
 * This factory method creates an Archive instance corresponding to the content
 * of the PAK compressed file with the given name.
 *
 * May return 0 in case of a failure.
 */
PAKArchive *makePAKArchive(const Common::String &name);

} // End of namespace BibleBlack

#endif
