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
 * Based on VileVN, Copyright (C) ViLE Team (GPL-3.0).
 *
 */

#ifndef IKURA_FORMATS_ARCHIVE_CABINET_H
#define IKURA_FORMATS_ARCHIVE_CABINET_H

#include "common/archive.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace Ikura::Format::Archive {

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

} // End of namespace Ikura::Format::Archive

#endif
