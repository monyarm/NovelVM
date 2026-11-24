#ifndef FORMAT_AFS_H
#define FORMAT_AFS_H

#include "common/archive.h"
#include "common/hash-str.h"
#include "common/str.h"

namespace Common {

struct AFSEntry {
	Common::String name;
	uint32 offset = 0;
	uint32 size = 0;
};

/**
 * Archive reader for CRI/Sega's AFS format (predates CPK; still used
 * alongside it by later CRI-middleware titles -- real evidence this
 * session: Persona 3 Portable's voice archives, `data/sound/voice/`,
 * found nested inside `umd0.cpk`/`umd1.cpk`).
 *
 * Real structure: magic "AFS\0", a `uint32` entry count, then that many
 * (offset, size) `uint32` pairs (raw, uncompressed data -- no CRILAYLA).
 * A second table follows at the next real 2048-byte (CD/UMD sector)
 * boundary at or after the last entry's end: one 48-byte record per file
 * (a 32-byte null-padded filename, then 16 trailing bytes of unconfirmed
 * meaning -- not required for extraction, see docs/middleware-plan.md).
 * Verified against two real Persona 3 Portable voice archives (533 and
 * 1021 entries): the computed metadata offset landed exactly on real
 * filenames both times (`0000_401_001_0000.adx`, `0000_402_005_0000.adx`).
 *
 * If the metadata table is missing, truncated, or doesn't fit the real
 * file size, members are exposed as synthetic "N.dat" names instead of
 * failing outright.
 */
class AFS : public Common::Archive {
public:
	AFS(const Common::String &filename);
	~AFS() override;

	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;

private:
	typedef Common::HashMap<Common::String, AFSEntry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> AFSEntryMap;

	Common::String _filename;
	AFSEntryMap _entries;

	bool load();
};

AFS *makeAFSArchive(const Common::String &name);

} // namespace Common

#endif
