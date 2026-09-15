#include "ikura/formats/archive/cabinet.h"

#include "common/array.h"
#include "common/memstream.h"

namespace Ikura::Format::Archive {

Cabinet::Cabinet(Common::SeekableReadStream *stream) : _stream(stream) {
}

Cabinet::~Cabinet() {
	delete _stream;
}

Cabinet *Cabinet::open(Common::SeekableReadStream *stream) {
	Cabinet *cabinet = new Cabinet(stream);
	if (!cabinet->parseSM2MPX10() && !cabinet->parseHeaderless()) {
		delete cabinet;
		return nullptr;
	}
	return cabinet;
}

bool Cabinet::addEntry(const Common::String &name, uint32 start, uint32 size) {
	Common::String trimmed(name);
	trimmed.trim();
	if (trimmed.empty())
		return false;

	// Reference parser trusts header offsets blindly; we don't.
	uint32 fileSize = _stream->size();
	if (size > fileSize || start > fileSize - size)
		return false;

	Entry entry;
	entry.start = start;
	entry.size = size;
	_entries[trimmed] = entry;
	return true;
}

bool Cabinet::parseSM2MPX10() {
	byte hdr[32];
	_stream->seek(0);
	if (_stream->read(hdr, sizeof(hdr)) != sizeof(hdr))
		return false;
	if (memcmp(hdr, "SM2MPX10", 8) != 0)
		return false;

	uint32 dlen = hdr[12] | (hdr[13] << 8) | (hdr[14] << 16) | (hdr[15] << 24);
	uint32 dstart = hdr[28] | (hdr[29] << 8) | (hdr[30] << 16) | (hdr[31] << 24);
	if (dlen < sizeof(hdr) || dlen > (uint32)_stream->size() || dstart >= dlen)
		return false;

	Common::Array<byte> dir(dlen);
	_stream->seek(0);
	if (_stream->read(dir.data(), dlen) != dlen)
		return false;

	for (uint32 i = dstart; i + 20 <= dlen; i += 20) {
		uint32 start = dir[i + 12] | (dir[i + 13] << 8) | (dir[i + 14] << 16) | (dir[i + 15] << 24);
		uint32 size = dir[i + 16] | (dir[i + 17] << 8) | (dir[i + 18] << 16) | (dir[i + 19] << 24);
		char name[13];
		memcpy(name, &dir[i], 12);
		name[12] = '\0';
		addEntry(Common::String(name), start, size);
	}

	return !_entries.empty();
}

// No real magic here, just a heuristic on the header bytes.
bool Cabinet::parseHeaderless() {
	byte hdr[32];
	_stream->seek(0);
	if (_stream->read(hdr, sizeof(hdr)) != sizeof(hdr))
		return false;

	if (!(hdr[1] && hdr[1] == hdr[15] && !hdr[16] && !hdr[17]))
		return false;

	int td1 = -1;
	for (int i = 2; i < 0x0D; i++) {
		if (hdr[i] == '.') {
			td1 = i;
			break;
		}
	}
	int td2 = -1;
	for (int i = 12; i < 0x1D; i++) {
		if (hdr[i] == '.') {
			td2 = i;
			break;
		}
	}
	uint32 ti1 = hdr[0] | (hdr[1] << 8);
	uint32 ti2 = hdr[0xE] | (hdr[0xF] << 8) | (hdr[0x10] << 16) | (hdr[0x11] << 24);
	if (!(td1 > 1 && td2 > td1 && ti1 > 0 && ti2 > ti1))
		return false;

	uint32 dlen = ti1;
	uint32 fullSize = _stream->size();
	if (dlen < sizeof(hdr) || dlen > fullSize)
		return false;

	Common::Array<byte> dir(dlen);
	_stream->seek(0);
	if (_stream->read(dir.data(), dlen) != dlen)
		return false;

	for (uint32 i = 2; i + 16 < dlen && dir[i + 4]; i += 16) {
		uint32 start = dir[i + 12] | (dir[i + 13] << 8) | (dir[i + 14] << 16) | (dir[i + 15] << 24);
		uint32 end = (i + 31 < dlen) ? (uint32)(dir[i + 28] | (dir[i + 29] << 8) | (dir[i + 30] << 16) | (dir[i + 31] << 24)) : 0;
		char name[13];
		memcpy(name, &dir[i], 12);
		name[12] = '\0';

		bool lastEntry = !(end > start);
		uint32 size = lastEntry ? (fullSize > start ? fullSize - start : 0) : end - start;
		addEntry(Common::String(name), start, size);
		if (lastEntry)
			break;
	}

	return !_entries.empty();
}

bool Cabinet::hasFile(const Common::Path &path) const {
	return _entries.contains(path.toString());
}

int Cabinet::listMembers(Common::ArchiveMemberList &list) const {
	int count = 0;
	for (EntryMap::const_iterator it = _entries.begin(); it != _entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(Common::Path(it->_key), *this)));
		count++;
	}
	return count;
}

const Common::ArchiveMemberPtr Cabinet::getMember(const Common::Path &path) const {
	if (!hasFile(path))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(path, *this));
}

Common::SeekableReadStream *Cabinet::createReadStreamForMember(const Common::Path &path) const {
	EntryMap::const_iterator it = _entries.find(path.toString());
	if (it == _entries.end())
		return nullptr;

	const Entry &entry = it->_value;
	if (!_stream->seek(entry.start))
		return nullptr;

	byte *data = (byte *)malloc(entry.size);
	if (!data)
		return nullptr;
	if (_stream->read(data, entry.size) != entry.size) {
		free(data);
		return nullptr;
	}
	return new Common::MemoryReadStream(data, entry.size, DisposeAfterUse::YES);
}

} // End of namespace Ikura::Format::Archive
