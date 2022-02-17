#ifndef KOIHIME_DOKI_EXWLCSARCHIVE_H
#define KOIHIME_DOKI_EXWLCSARCHIVE_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/file.h"
#include "common/path.h"
#include "common/hash-str.h"
#include "common/memstream.h"
#include "common/bufferedstream.h"
#include "common/debug.h"

namespace KoihimeDoki::Format::Archive {

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

typedef Common::HashMap<Common::String, Common::ScopedPtr<LCSHEADER>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> EXWLCSHeadersMap;

class EXWLCSArchive : public Common::Archive {
	EXWLCSHeadersMap _headers;
	Common::String _exwlcsFilename;

public:
	EXWLCSArchive(const Common::String &name);
	~EXWLCSArchive() override;

	// Archive implementation
	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;
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
EXWLCSArchive *EXWLCSFactory(const Common::String &name);

} // namespace KoihimeDoki::Format::Archive

#endif
