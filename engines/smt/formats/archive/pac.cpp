#include "common/archive.h"
#include "common/endian.h"
#include "common/substream.h"

#include "smt/smt.h"

#include "smt/formats/archive/pac.h"

// TODO: Persona 4 also has a second package reader (a count at +4, then u32 sector offsets, read
// in 2048-byte sectors, each size being the next offset minus its own); no file on the P4 disc
// was found to use it, so it isn't supported. The game registers every entry in order and can
// address them by index; here duplicate names keep only the last entry (all duplicates in P4
// are byte-identical) and listing order isn't preserved.

namespace SMT::Format::Archive {
PAC::PAC() {
}

PAC::PAC(const Common::String &filename) : _pakFilename(filename) {
	Common::File pacFile;

	if (!pacFile.open(Common::Path(_pakFilename))) {
		warning("PAC::PAC(): Could not find the archive file");
		return;
	}

	readFile(pacFile);
}

PAC::PAC(Common::SeekableReadStream &stream) {
	_streamData.resize(stream.size());
	stream.seek(0);
	stream.read(_streamData.data(), _streamData.size());
	Common::MemoryReadStream copy(_streamData.data(), _streamData.size(), DisposeAfterUse::NO);
	readFile(copy);
}

PAC::PAC(Common::SeekableReadStream *stream) : _stream(stream) {
	if (_stream)
		readFile(*_stream);
}

PAC::~PAC() {
}

void PAC::readFile(Common::SeekableReadStream &stream) {

	Version = DetectVersion(stream);
	ReadEntries(stream);


}

bool PAC::hasFile(const Common::Path &name) const {
	return entries.contains(name.toString());
}

int PAC::listMembers(Common::ArchiveMemberList &list) const {
	int matches = 0;

	EntriesMap::const_iterator it = entries.begin();
	for (; it != entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(Common::Path(it->_value->name), *this)));
		matches++;
	}

	return matches;
}

const Common::ArchiveMemberPtr PAC::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();

	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *PAC::createReadStreamForMember(const Common::Path &name) const {
	if (!entries.contains(name.toString())) {
		return 0;
	}

	Entry *hdr = entries[name.toString()].get();

	if (_stream) {
		if (hdr->position > (uint32)_stream->size() || hdr->size > (uint32)_stream->size() - hdr->position)
			return nullptr;
		return new Common::SafeSeekableSubReadStream(_stream.get(), hdr->position, hdr->position + hdr->size);
	}

	byte *data = (byte *)malloc(hdr->size);
	assert(data);
	uint32 len = 0;
	if (!_pakFilename.empty()) {
		Common::File archiveFile;
		archiveFile.open(Common::Path(_pakFilename));
		archiveFile.seek(hdr->position);
		len = archiveFile.read(data, hdr->size);
	} else if (hdr->position + hdr->size <= _streamData.size()) {
		memcpy(data, _streamData.data() + hdr->position, hdr->size);
		len = hdr->size;
	}
	assert(len == hdr->size);

	return new Common::MemoryReadStream(data, hdr->size, DisposeAfterUse::YES);
}

bool PAC::getMemberRange(const Common::Path &name, uint32 &offset, uint32 &size) const {
	if (!entries.contains(name.toString()))
		return false;
	Entry *hdr = entries[name.toString()].get();
	offset = hdr->position;
	size = hdr->size;
	return true;
}

const Common::Array<Common::String> PAC::getFileExtensions() {
	return Common::Array<Common::String>(FileFormats, 14);
}

bool PAC::isValid(Common::SeekableReadStream &stream) {
	return DetectVersion(stream) != FormatVersion::Unknown;
}

PAC::FormatVersion PAC::DetectVersion(Common::SeekableReadStream &stream) {
	bool isBigEndian;
	if (IsValidFormatVersion1(stream))
		return FormatVersion::Version1;

	if (IsValidFormatVersion2And3(stream, 36, isBigEndian))
		return isBigEndian ? FormatVersion::Version2BE : FormatVersion::Version2;

	if (IsValidFormatVersion2And3(stream, 28, isBigEndian))
		return isBigEndian ? FormatVersion::Version3BE : FormatVersion::Version3;

	return FormatVersion::Unknown;
}

bool PAC::IsValidFormatVersion1(Common::SeekableReadStream &stream) {
	stream.seek(0);
	if (stream.size() <= 256) {
		return false;
	}

	// read some test data
	Common::Array<byte> testData = Common::Array<byte>(256);
	stream.read(testData.data(), 256);
	stream.seek(0);

	// check if first byte is zero, if so then no name can be stored thus making the file corrupt
	if (testData[0] == 0x00)
		return false;

	bool nameTerminated = false;
	for (int i = 0; i < 252; i++) {
		if (testData[i] == 0x00)
			nameTerminated = true;

		// If the name has already been terminated but there's still data in the reserved space,
		// fail the test
		if (nameTerminated && testData[i] != 0x00)
			return false;
	}

	int testLength = (int)READ_LE_UINT32(&testData[252]);

	// sanity check, if the length of the first file is >= 100 mb, fail the test
	if (testLength >= stream.size() || testLength < 0) {
		return false;
	}

	return true;
}

bool PAC::IsValidFormatVersion2And3(Common::SeekableReadStream &stream, int entrySize, bool &isBigEndian) {
	stream.seek(0);
	isBigEndian = false;

	// check stream length
	if (stream.size() <= 4 + entrySize)
		return false;

	Common::Array<byte> testData = Common::Array<byte>(4 + entrySize);
	stream.read(testData.data(), 4 + entrySize);
	stream.seek(0);

	int numOfFiles = *(int*)&testData[0]; ;//int((testData[0]) << 24 |
	                 //    (testData[0 + 1]) << 16 |
	                 //    (testData[0 + 2]) << 8 |
	                 //    (testData[0 + 3]));

	if (numOfFiles > 1024 || numOfFiles < 1 || (numOfFiles * entrySize) > stream.size()) {
		numOfFiles = ((numOfFiles >> 24) & 0xff) |
		             ((numOfFiles << 8) & 0xff0000) |
		             ((numOfFiles >> 8) & 0xff00) |
		             ((numOfFiles << 24) & 0xff000000);

		if (numOfFiles > 1024 || numOfFiles < 1 || (numOfFiles * entrySize) > stream.size())
			return false;

		isBigEndian = true;
	}

	// check that the whole name field is a non-empty, NUL-padded name
	bool nameTerminated = false;
	for (int i = 0; i < entrySize - 4; i++) {
		if (testData[4 + i] == 0x00) {
			if (i == 0)
				return false;

			nameTerminated = true;
		} else if (nameTerminated) {
			return false;
		}
	}

	int length = (int)READ_LE_UINT32(&testData[entrySize]);
	if (length >= stream.size() || length < 0) {
		length = (int)READ_BE_UINT32(&testData[entrySize]);
		if (length >= stream.size() || length < 0)
			return false;

		isBigEndian = true;
	}

	return true;
}

void PAC::ReadEntries(Common::SeekableReadStream &stream) {
	Common::Array<char> stringBuilder;

	if (Version == FormatVersion::Version1) {

		while (true) {
			long entryStartPosition = stream.pos();

			if (entryStartPosition == stream.size()) {
				break;
			}

			// read entry name
			while (true) {
				byte b = stream.readByte();

				if (b == 0)
					break;

				stringBuilder.push_back((char)b);

				// just to be safe
				if (stringBuilder.size() == 252)
					break;
			}
			stringBuilder.push_back(0x00);
			const Common::String fileName = Common::String(stringBuilder.data());

			// set position to length field
			stream.seek(entryStartPosition + 252);

			// read entry length
			int length = IsLittleEndian() ? stream.readSint32LE() : stream.readSint32BE();

			// The game stops at the first empty name only; empty files are real entries. Some P4
			// archives (EFFECT/EFFPACK.BIN, two BUSTUP/*.BIN) have no terminator and end at EOF.
			if (fileName.size() == 0 || length < 0 || length > 1024 * 1024 * 100) {
				break;
			}

			// make an entry
			Entry entry;
			entry.name = fileName;
			entry.position = stream.pos();
			entry.size = length;
			if (entry.position > stream.size() || entry.size > stream.size() - entry.position)
				break;

			// clear string builder for next iteration
			stringBuilder.clear();
			int pos = (stream.pos() + entry.size + 64 - 1) & ~(64 - 1);
			stream.seek(pos);

			entries[entry.name].reset(new Entry(entry));
		}
	} else if (Version == FormatVersion::Version2 || Version == FormatVersion::Version2BE || Version == FormatVersion::Version3 || Version == FormatVersion::Version3BE) {
		int entryCount = IsLittleEndian() ? stream.readSint32LE() : stream.readSint32BE();
		int nameLength = 32;
		if (Version == FormatVersion::Version3 || Version == FormatVersion::Version3BE)
			nameLength = 24;

		for (int i = 0; i < entryCount; i++) {
			long entryStartPosition = stream.pos();
			if (entryStartPosition == stream.size()) {
				break;
			}

			// read entry name
			for (int j = 0; j < nameLength; j++) {
				byte b = stream.readByte();

				if (b != 0)
					stringBuilder.push_back((char)b);
			}
			stringBuilder.push_back(0x00);

			Common::String fileName = Common::String(stringBuilder.data());//, stringBuilder.data() + stringBuilder.size());
			fileName.trim();

			// read entry length
			int length = IsLittleEndian() ? stream.readSint32LE() : stream.readSint32BE();

			if (fileName.size() == 0 || length <= 0 || length > 1024 * 1024 * 100) {
				break;
			}

			// make an entry

			Entry entry;
			entry.name = fileName;
			entry.position = stream.pos();
			entry.size = length;
			if (entry.position > stream.size() || entry.size > stream.size() - entry.position)
				break;

			// clear string builder for next iteration
			stringBuilder.clear();
			stream.seek(entry.size, SEEK_CUR);

			entries[entry.name].reset(new Entry(entry));
		}
	}
}

PAC *PACFactory(const Common::String &name) {
	return new PAC(name);
}

} // namespace SMT::Format::Script
