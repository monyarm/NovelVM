#ifndef FORMAT_CPKARCHIVE_H
#define FORMAT_CPKARCHIVE_H

#include "common/archive.h"
#include "common/array.h"
#include "common/hash-str.h"
#include "common/str.h"

namespace Common {

struct CPKEntry {
	Common::String name;
	uint64 offset;
	uint32 size;        // bytes stored on disk (may be CRILAYLA-compressed)
	uint32 extractSize;  // decompressed size; equals size when not compressed
	uint32 id = 0;       // ITOC numeric ID; 0 for TOC-named entries
	bool hasId = false;  // true when `id` is meaningful (ITOC entry)
};

/**
 * Archive reader for CRI Middleware's CPK format: a "CPK "-magic header
 * containing a CRI @UTF table (TocOffset/ItocOffset/etc.), followed by either
 * a "TOC "-magic @UTF table listing directory/file names, sizes and offsets,
 * or an "ITOC"-magic @UTF ID table for archives with no directory tree.
 * Entries may be CRILAYLA-compressed.
 *
 * Real-data-verified against Persona 3 Portable's umd0.cpk/umd1.cpk (TOC
 * path only). The ITOC path is implemented from the documented CRI layout
 * (CriPakTools ReadITOC / vgmstream cpk.c: a CpkItocInfo table whose DataL/
 * DataH columns are CpkItocL/CpkItocH ID/FileSize/ExtractSize tables, with
 * file offsets laid out sequentially from ContentOffset) but has NOT been
 * verified against a real ITOC archive -- none was available in the corpus.
 */
class CPK : public Common::Archive {
public:
	CPK(const Common::String &filename);
	~CPK() override;

	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;

	// Numeric-ID lookup for ITOC entries (matching `CPKEntry::id`).
	bool getEntryById(uint32 id, CPKEntry &entry) const;

private:
	typedef Common::HashMap<Common::String, CPKEntry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> CPKEntryMap;

	Common::String _filename;
	CPKEntryMap _entries;
	Common::HashMap<uint32, CPKEntry> _entriesById;

	bool load();
	bool loadITOC(Common::SeekableReadStream &f, uint64 itocOffset, uint64 contentOffset, uint16 align);
};

/**
 * This factory method creates an Archive instance corresponding to the
 * content of the CPK file with the given name.
 *
 * May return 0 in case of a failure.
 */
CPK *makeCPKArchive(const Common::String &name);

} // namespace Common

#endif
