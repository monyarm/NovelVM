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

#include "common/formats/afs.h"

#include "common/file.h"
#include "common/memstream.h"
#include "common/textconsole.h"

namespace Common {

namespace {

const uint32 kSectorSize = 2048;

uint32 alignUp(uint32 v, uint32 align) {
	return ((v + align - 1) / align) * align;
}

} // namespace

AFS::AFS(const Common::String &filename) : _filename(filename) {
	load();
}

AFS::~AFS() {
}

bool AFS::load() {
	Common::File f;
	if (!f.open(Common::Path(_filename))) {
		warning("AFS::AFS(): Could not open %s", _filename.c_str());
		return false;
	}

	char magic[4];
	if (f.read(magic, 4) != 4 || memcmp(magic, "AFS\0", 4) != 0) {
		warning("AFS::AFS(): '%s' is not an AFS archive", _filename.c_str());
		return false;
	}

	uint32 count = f.readUint32LE();
	if (f.err() || f.eos() || count == 0 || count > 100000) {
		warning("AFS::AFS(): implausible entry count in %s", _filename.c_str());
		return false;
	}

	Common::Array<AFSEntry> entries(count);
	const int64 fileSize = f.size();
	uint32 lastEnd = 0;
	for (uint32 i = 0; i < count; i++) {
		entries[i].offset = f.readUint32LE();
		entries[i].size = f.readUint32LE();
		if (f.err() || f.eos()) {
			warning("AFS::AFS(): truncated TOC in %s", _filename.c_str());
			return false;
		}
		// TOC values are untrusted: an entry running past the file (or wrapping 32 bits)
		// would corrupt the metadata bound below and the member reads.
		if (fileSize >= 0 && (uint64)entries[i].offset + entries[i].size > (uint64)fileSize) {
			warning("AFS::AFS(): entry %u of %s lies outside the file, ignoring its data", i, _filename.c_str());
			entries[i].offset = 0;
			entries[i].size = 0;
		}
		uint32 end = entries[i].offset + entries[i].size;
		if (end > lastEnd)
			lastEnd = end;
	}

	// The metadata (filename) table is located by the (offset, size) pair right after the TOC;
	// archives without one put it at the next sector boundary after the last entry's data.
	// (CRI's loader itself never reads names; it addresses files by index.)
	uint32 metaStart = alignUp(lastEnd, kSectorSize);
	const uint32 metaPointer = f.readUint32LE(), metaSize = f.readUint32LE();
	if (!f.err() && !f.eos() && metaPointer != 0 && metaSize >= count * 48)
		metaStart = metaPointer;
	bool haveMeta = f.size() >= 0 && (int64)metaStart + (int64)count * 48 <= f.size();

	for (uint32 i = 0; i < count; i++) {
		Common::String name;
		if (haveMeta) {
			f.seek(metaStart + i * 48);
			char rawName[33];
			f.read(rawName, 32);
			rawName[32] = '\0';
			name = Common::String(rawName);
		}
		if (name.empty())
			name = Common::String::format("%u.dat", i);

		entries[i].name = name;
		_entries[name] = entries[i];
	}

	return true;
}

bool AFS::hasFile(const Common::Path &name) const {
	return _entries.contains(name.toString());
}

int AFS::listMembers(Common::ArchiveMemberList &list) const {
	int matches = 0;
	for (AFSEntryMap::const_iterator it = _entries.begin(); it != _entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(Common::Path(it->_value.name), *this)));
		matches++;
	}
	return matches;
}

const Common::ArchiveMemberPtr AFS::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *AFS::createReadStreamForMember(const Common::Path &name) const {
	if (!_entries.contains(name.toString()))
		return nullptr;

	const AFSEntry &entry = _entries[name.toString()];

	Common::File f;
	if (!f.open(Common::Path(_filename)))
		return nullptr;
	f.seek(entry.offset);

	byte *data = new byte[entry.size];
	if (f.read(data, entry.size) != entry.size) {
		delete[] data;
		return nullptr;
	}
	return new Common::MemoryReadStream(data, entry.size, DisposeAfterUse::YES);
}

AFS *makeAFSArchive(const Common::String &name) {
	return new AFS(name);
}

} // namespace Common
