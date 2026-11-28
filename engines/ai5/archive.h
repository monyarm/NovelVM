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
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

#ifndef AI5_ARCHIVE_H
#define AI5_ARCHIVE_H

#include "common/archive.h"
#include "common/array.h"
#include "common/file.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/ptr.h"

#include "ai5/detection.h"

namespace AI5 {

enum ArcFlags {
	kArcCompressed = 1 << 0, // members are LZSS compressed
	kArcBitwise    = 1 << 1  // LZSS is the bitwise AIWIN variant
};

/**
 * AI5WIN resource archive (.ARC, .DAT, .AWD, .AWF).
 */
class ArcArchive : public Common::Archive {
public:
	enum Type {
		kTypeArc,
		kTypeDat,
		kTypeAwd,
		kTypeAwf
	};

	enum AwdType {
		kAwdPcm = 1,
		kAwdMp3 = 85
	};

	struct Entry {
		Common::String name;
		uint32 offset;
		uint32 size;
		uint16 awdType;
		uint32 loopStart;
		uint32 loopEnd;
	};

	static ArcArchive *open(const Common::Path &path, GameId game, uint flags);

	Type getType() const { return _type; }
	const Entry *findEntry(const Common::Path &path) const;
	const Common::Array<Entry> &getEntries() const { return _entries; }

	bool hasFile(const Common::Path &path) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &path) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override;

private:
	struct Meta {
		uint32 arcSize;
		uint32 nrFiles;
		uint indexOff;
		uint entrySize;
		uint nameLength;
		uint32 offsetKey;
		uint32 sizeKey;
		byte nameKey;
		uint offsetOff;
		uint sizeOff;
		uint nameOff;
		uint awdTypeOff;
		uint loopStartOff;
		uint loopEndOff;
	};

	ArcArchive() : _type(kTypeArc), _flags(0), _meta() {}

	bool readArcMeta(GameId game);
	bool readDatMeta();
	bool readAwdMeta();
	bool readAwfMeta();
	bool readSizeAndCount();
	bool readIndex(GameId game);
	bool readTypicalEntry(byte *buf, Entry &e);
	bool readKakyuuseiIndex();
	void createIndex();

	mutable Common::File _file;
	Type _type;
	uint _flags;
	Meta _meta;
	Common::Array<Entry> _entries;
	Common::HashMap<Common::String, uint, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _index;
};

} // End of namespace AI5

#endif // AI5_ARCHIVE_H
