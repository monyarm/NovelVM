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

#include "common/formats/acx.h"

#include "common/debug.h"
#include "common/file.h"
#include "common/memstream.h"

namespace Common {

ACX::ACX(const Common::String &filename) : _filename(filename) {
	load();
}

ACX::~ACX() {
}

bool ACX::load() {
	Common::File f;
	if (!f.open(Common::Path(_filename))) {
		warning("ACX::ACX(): Could not open %s", _filename.c_str());
		return false;
	}

	f.readUint32BE(); // always 0 in every real sample checked
	uint32 count = f.readUint32BE();
	if (f.err() || f.eos() || count == 0 || count > 0xffff) {
		warning("ACX::ACX(): implausible stream count in %s", _filename.c_str());
		return false;
	}

	_entries.resize(count);
	for (uint32 i = 0; i < count; i++) {
		_entries[i].offset = f.readUint32BE();
		_entries[i].size = f.readUint32BE();
	}
	if (f.err() || f.eos()) {
		warning("ACX::ACX(): truncated table in %s", _filename.c_str());
		_entries.clear();
		return false;
	}

	return true;
}

static bool parseIndex(const Common::String &name, uint32 &index) {
	Common::String stem = name;
	int dot = stem.findLastOf('.');
	if (dot >= 0)
		stem = stem.substr(0, dot);
	if (stem.empty())
		return false;
	for (uint32 i = 0; i < stem.size(); i++)
		if (stem[i] < '0' || stem[i] > '9')
			return false;
	index = (uint32)atoi(stem.c_str());
	return true;
}

bool ACX::hasFile(const Common::Path &name) const {
	uint32 index;
	return parseIndex(name.toString(), index) && index < _entries.size();
}

int ACX::listMembers(Common::ArchiveMemberList &list) const {
	for (uint32 i = 0; i < _entries.size(); i++)
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(Common::Path(Common::String::format("%u.adx", i)), *this)));
	return (int)_entries.size();
}

const Common::ArchiveMemberPtr ACX::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *ACX::createReadStreamForMember(const Common::Path &name) const {
	uint32 index;
	if (!parseIndex(name.toString(), index) || index >= _entries.size())
		return nullptr;

	const Entry &entry = _entries[index];

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

ACX *makeACXArchive(const Common::String &name) {
	return new ACX(name);
}

} // namespace Common
