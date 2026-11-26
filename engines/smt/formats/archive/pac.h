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

#ifndef SMT_PAC_H
#define SMT_PAC_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/bufferedstream.h"
#include "common/debug.h"
#include "common/file-format.h"
#include "common/file.h"
#include "common/path.h"
#include "common/hash-str.h"
#include "common/memstream.h"

namespace SMT::Format::Archive {

class Archive;

struct Entry {
	Common::String name;
	uint32 position;
	uint32 size;
};

typedef Common::HashMap<Common::String, Common::ScopedPtr<Entry>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> EntriesMap;

class PAC : public Common::Archive, public Common::FileFormat {
	EntriesMap entries;
	Common::String _pakFilename;
	Common::Array<byte> _streamData;
	Common::ScopedPtr<Common::SeekableReadStream> _stream;

public:
	PAC();
	PAC(const Common::String &name);
	PAC(Common::SeekableReadStream &stream);
	PAC(Common::SeekableReadStream *stream);
	~PAC() override;

	void readFile(Common::SeekableReadStream &stream);
	// Archive implementation
	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;
	bool getMemberRange(const Common::Path &name, uint32 &offset, uint32 &size) const;

	//File Format implementation

	const Common::String FileFormats[14] = {".bin", ".f00", ".f01", ".p00", ".p01", ".fpc", ".pak", ".pac", ".pack", ".se", ".arc", ".abin", ".se", ".pse"};

	const Common::Array<Common::String> getFileExtensions() override;

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
PAC *PACFactory(const Common::String &name);

} // namespace SMT::Format::Archive

#endif
