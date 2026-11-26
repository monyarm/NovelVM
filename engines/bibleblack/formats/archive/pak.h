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

#ifndef BIBLEBLACK_PAK_H
#define BIBLEBLACK_PAK_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/file.h"
#include "common/path.h"
#include "common/hash-str.h"
#include "common/memstream.h"
#include "common/bufferedstream.h"
#include "common/debug.h"

namespace BibleBlack::Format::Archive {

class Archive;


struct PAKHeader {
Common::String name;
uint32 position;
uint32 size;
};

typedef Common::HashMap<Common::String, Common::ScopedPtr<PAKHeader>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> PAKHeadersMap;

class PAK : public Common::Archive {
	PAKHeadersMap _headers;
	Common::String _pakFilename;

public:
	PAK(const Common::String &name);
	~PAK() override;

	// Archive implementation
	bool hasFile(const Common::Path &name) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &name) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &name) const override;
};

/**
 * This factory method creates an Archive instance corresponding to the content
 * of the PAK compressed file with the given name.
 *
 * May return 0 in case of a failure.
 */
PAK *PAKFactory(const Common::String &name);

} // namespace BibleBlack::Format::Archive

#endif
