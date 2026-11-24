#include "common/formats/awb.h"

#include "common/file.h"
#include "common/memstream.h"
#include "common/textconsole.h"

namespace Common {

namespace {

const uint32 kMinOffsetAlign = 32;

Common::String extensionForSubfile(const byte *header, uint32 size) {
	if (size < 4)
		return "dat";

	// HCA: "HCA\0" masked with 0x7F7F7F7F
	if ((header[0] & 0x7F) == 'H' && (header[1] & 0x7F) == 'C' &&
	    (header[2] & 0x7F) == 'A' && (header[3] & 0x7F) == '\0')
		return "hca";

	// ADX: big-endian 0x8000
	if (header[0] == 0x80 && header[1] == 0x00)
		return "adx";

	// VAGp
	if (header[0] == 'V' && header[1] == 'A' && header[2] == 'G' && header[3] == 'p')
		return "vag";

	// RIFF (WAV)
	if (header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F')
		return "wav";

	return "dat";
}

} // namespace

AWB::AWB(const Common::String &filename)
	: _embeddedStream(nullptr, DisposeAfterUse::NO), _filename(filename) {
	Common::File f;
	if (!f.open(Common::Path(_filename))) {
		warning("AWB::AWB(): Could not open %s", _filename.c_str());
		return;
	}
	loadFromStream(f);
}

AWB::AWB(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeStream)
	: _embeddedStream(stream, disposeStream) {
	if (_embeddedStream)
		loadFromStream(*_embeddedStream);
}

AWB::~AWB() {
}

bool AWB::loadFromStream(Common::SeekableReadStream &f) {
	char magic[4];
	if (f.read(magic, 4) != 4 || memcmp(magic, "AFS2", 4) != 0) {
		warning("AWB::AWB(): stream is not an AWB (AFS2) archive");
		return false;
	}

	f.readByte(); // version
	uint8 offsetSizeByte = f.readByte();
	f.readUint16LE(); // waveid alignment
	int32 count = f.readSint32LE();
	uint16 offsetAlign = f.readUint16LE();
	_subkey = f.readUint16LE();

	if (f.err() || f.eos()) {
		warning("AWB::AWB(): failed to read header");
		return false;
	}

	if (count < 0 || count > 100000) {
		warning("AWB::AWB(): implausible entry count %d", count);
		return false;
	}

	if (offsetAlign < kMinOffsetAlign)
		offsetAlign = kMinOffsetAlign;

	// Read ID table (N x u16 LE)
	Common::Array<uint16> ids(count);
	for (int32 i = 0; i < count; i++) {
		ids[i] = f.readUint16LE();
	}
	if (f.err() || f.eos()) {
		warning("AWB::AWB(): truncated ID table");
		return false;
	}

	// Read offset table ((N+1) x offset_size)
	Common::Array<uint32> offsets(count + 1);
	for (int32 i = 0; i <= count; i++) {
		if (offsetSizeByte == 4)
			offsets[i] = f.readUint32LE();
		else
			offsets[i] = f.readUint16LE();
	}
	if (f.err() || f.eos()) {
		warning("AWB::AWB(): truncated offset table");
		return false;
	}

	// Offsets are absolute but sometimes misaligned (specially the first,
	// which points at the offset-table end) -- real data begins at the next
	// offset_alignment boundary. Verified against Persona 5 Royal AWB files
	// (SOUND_J/E.CPK): raw offsets 602/39600... with the ADX magic (0x8000)
	// actually found at 608/39616, i.e. offset_alignment (0x20) aligned.
	// Mirrors vgmstream's awb.c.
	int64 streamSize = f.size();
	const uint32 fileEnd = streamSize > 0 ? (uint32)streamSize : 0;

	// Build entries
	_entries.resize(count);
	for (int32 i = 0; i < count; i++) {
		_entries[i].id = ids[i];
		uint64 off = offsets[i];
		uint64 next = offsets[i + 1];
		uint32 offAlign = off % offsetAlign;
		if (offAlign)
			off += offsetAlign - offAlign;
		uint32 nextAlign = next % offsetAlign;
		if (nextAlign && (next + (offsetAlign - nextAlign) < fileEnd || fileEnd == 0))
			next += offsetAlign - nextAlign;
		// Malformed/tightly packed tables can put the aligned start past the next entry or
		// the file end: give such entries no data rather than an underflowed ~4GB size.
		if (fileEnd)
			next = MIN<uint64>(next, fileEnd);
		if (next < off)
			next = off;
		_entries[i].offset = (uint32)MIN<uint64>(off, 0xFFFFFFFF);
		_entries[i].size = (uint32)(next - off);

		// Detect codec from the first bytes of each subfile
		byte header[4] = {0};
		uint32 readSize = MIN(_entries[i].size, (uint32)4);
		uint32 savedPos = f.pos();
		f.seek(_entries[i].offset);
		if (f.read(header, readSize) != readSize)
			memset(header, 0, 4);
		f.seek(savedPos);

		Common::String ext = extensionForSubfile(header, _entries[i].size);
		_entries[i].name = Common::String::format("id_%04u.%s", ids[i], ext.c_str());
	}

	return true;
}

bool AWB::hasFile(const Common::Path &name) const {
	return findEntryByName(name.toString()) >= 0;
}

int AWB::listMembers(Common::ArchiveMemberList &list) const {
	for (uint32 i = 0; i < _entries.size(); i++) {
		list.push_back(Common::ArchiveMemberList::value_type(
			new Common::GenericArchiveMember(Common::Path(_entries[i].name), *this)));
	}
	return (int)_entries.size();
}

const Common::ArchiveMemberPtr AWB::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

int AWB::findEntryByName(const Common::String &name) const {
	for (uint32 i = 0; i < _entries.size(); i++) {
		if (_entries[i].name.equalsIgnoreCase(name))
			return (int)i;
	}
	return -1;
}

int AWB::findEntryById(uint16 id) const {
	for (uint32 i = 0; i < _entries.size(); i++) {
		if (_entries[i].id == id)
			return (int)i;
	}
	return -1;
}

Common::SeekableReadStream *AWB::createReadStreamForEntry(const Entry &entry) const {
	byte *data = new byte[entry.size];
	if (_embeddedStream) {
		uint32 savedPos = _embeddedStream->pos();
		if (!_embeddedStream->seek(entry.offset)) {
			delete[] data;
			return nullptr;
		}
		if (_embeddedStream->read(data, entry.size) != entry.size) {
			_embeddedStream->seek(savedPos);
			delete[] data;
			return nullptr;
		}
		_embeddedStream->seek(savedPos);
		return new Common::MemoryReadStream(data, entry.size, DisposeAfterUse::YES);
	}

	// File-backed: re-open the archive for this subfile.
	Common::File f;
	if (!f.open(Common::Path(_filename)))
		return nullptr;
	f.seek(entry.offset);

	if (f.read(data, entry.size) != entry.size) {
		delete[] data;
		return nullptr;
	}
	return new Common::MemoryReadStream(data, entry.size, DisposeAfterUse::YES);
}

Common::SeekableReadStream *AWB::createReadStreamForMember(const Common::Path &name) const {
	int index = findEntryByName(name.toString());
	if (index < 0)
		return nullptr;

	return createReadStreamForEntry(_entries[index]);
}

Common::SeekableReadStream *AWB::createReadStreamForId(uint16 id) const {
	int index = findEntryById(id);
	if (index < 0)
		return nullptr;

	return createReadStreamForEntry(_entries[index]);
}

AWB *makeAWBArchive(const Common::String &name) {
	return new AWB(name);
}

} // namespace Common
