#ifndef KOIHIME_DOKI_EXWLCSARCHIVE_H
#define KOIHIME_DOKI_EXWLCSARCHIVE_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/file.h"
#include "common/hash-str.h"
#include "common/memstream.h"
#include "common/bufferedstream.h"
#include "common/debug.h"

namespace KoihimeDoki {

class Archive;



struct LCSENTRY {
  unsigned long offset;
  unsigned long length;
  char          filename[64];
  unsigned long unknown;
};


struct LCSHEADER {
  unsigned long offset;
  unsigned long length;
  Common::String   filename;
  unsigned long unknown;
};

typedef Common::HashMap<Common::String, Common::ScopedPtr<LCSHEADER>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> PakHeadersMap;

class EXWLCSArchive : public Common::Archive {
	PakHeadersMap _headers;
	Common::String _exwlcsFilename;

public:
	EXWLCSArchive(const Common::String &name);
	~EXWLCSArchive() override;

	// Archive implementation
	bool hasFile(const Common::String &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::String &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::String &name) const override;
private:
  unsigned long entry_count;

unsigned long unobfuscate();

void unobfuscate(LCSENTRY& entry, unsigned long key);

void unobfuscate(unsigned char* buff, unsigned long len) const;

};

/**
 * This factory method creates an Archive instance corresponding to the content
 * of the EXWLCS compressed file with the given name.
 *
 * May return 0 in case of a failure.
 */
EXWLCSArchive *makeEXWLCSArchive(const Common::String &name);

} // End of namespace KoihimeDoki

#endif
