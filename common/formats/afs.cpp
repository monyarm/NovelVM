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

	// The metadata (filename) table, if present, starts at the next real
	// sector boundary at or after the last entry's data ends.
	uint32 metaStart = alignUp(lastEnd, kSectorSize);
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
