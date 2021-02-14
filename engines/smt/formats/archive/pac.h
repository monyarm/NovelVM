#ifndef SMT_PACARCHIVE_H
#define SMT_PACARCHIVE_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/bufferedstream.h"
#include "common/debug.h"
#include "common/file-format.h"
#include "common/file.h"
#include "common/hash-str.h"
#include "common/memstream.h"

namespace SMT {

class Archive;

struct Entry {
	Common::String name;
	uint32 position;
	uint32 size;
};

typedef Common::HashMap<Common::String, Common::ScopedPtr<Entry>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> EntriesMap;

class PACArchive : public Common::Archive, public Common::FileFormat {
	EntriesMap entries;
	Common::String _pakFilename;

public:
	PACArchive();
	PACArchive(const Common::String &name);
	PACArchive(Common::SeekableReadStream &stream);
	~PACArchive() override;

	void readFile(Common::SeekableReadStream &stream);
	// Archive implementation
	bool hasFile(const Common::String &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::String &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::String &name) const override;

	//File Format implementation

	const Common::String FileFormats[14] = {".bin", ".f00", ".f01", ".p00", ".p01", ".fpc", ".pak", ".pac", ".pack", ".se", ".arc", ".abin", ".se", ".pse"};

	const Common::Array<Common::String> getFileExtensions();

	static bool isValid(Common::SeekableReadStream &stream);

	enum FormatVersion {
		Unknown = 0,

		/// <summary>
		/// 252 bytes filename, 4 bytes filesize
		/// </summary>
		Version1 = 1,

		/// <summary>
		/// Entry count header, 32 bytes filename, 4 bytes filesize
		/// </summary>
		Version2 = 2,

		Version2BE = 3,

		/// <summary>
		/// Entry count header, 24 bytes filename, 4 bytes filesize
		/// </summary>
		Version3 = 4,

		Version3BE = 5,

		Autodetect = 6
	};

	FormatVersion Version;

private:
	static FormatVersion DetectVersion(Common::SeekableReadStream &stream);

	static bool IsValidFormatVersion1(Common::SeekableReadStream &stream);
	static bool IsValidFormatVersion2And3(Common::SeekableReadStream &stream, int entrySize, bool &isBigEndian);

	bool IsBigEndian() { return Version == FormatVersion::Version2BE || Version == FormatVersion::Version3BE; };
	bool IsLittleEndian() { return !IsBigEndian(); };
	void ReadEntries(Common::SeekableReadStream &stream);
};

/**
 * This factory method creates an Archive instance corresponding to the content
 * of the PAC compressed file with the given name.
 *
 * May return 0 in case of a failure.
 */
PACArchive *makePACArchive(const Common::String &name);

} // End of namespace SMT

#endif
