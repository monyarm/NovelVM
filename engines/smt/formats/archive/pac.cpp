#include "common/archive.h"
#include "common/hex.h"
#include "smt/smt.h"

#include "smt/formats/archive/pac.h"

namespace SMT {
PACArchive::PACArchive() {
}

PACArchive::PACArchive(const Common::String &filename) : _pakFilename(filename) {
	Common::File pacFile;

	if (!pacFile.open(_pakFilename)) {
		warning("PACArchive::PACArchive(): Could not find the archive file");
		return;
	}

	readFile(pacFile);
}

PACArchive::PACArchive(Common::SeekableReadStream &stream) {

	readFile(stream);
}

PACArchive::~PACArchive() {
}

void PACArchive::readFile(Common::SeekableReadStream &stream) {

	Version = DetectVersion(stream);
	ReadEntries(stream);


}

bool PACArchive::hasFile(const Common::String &name) const {
	return entries.contains(name);
}

int PACArchive::listMembers(Common::ArchiveMemberList &list) const {
	int matches = 0;

	EntriesMap::const_iterator it = entries.begin();
	for (; it != entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(it->_value->name, this)));
		matches++;
	}

	return matches;
}

const Common::ArchiveMemberPtr PACArchive::getMember(const Common::String &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();

	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, this));
}

Common::SeekableReadStream *PACArchive::createReadStreamForMember(const Common::String &name) const {
	if (!entries.contains(name)) {
		return 0;
	}

	Entry *hdr = entries[name].get();

	Common::File archiveFile;
	archiveFile.open(_pakFilename);
	archiveFile.seek(hdr->position, SEEK_SET);

	byte *data = (byte *)malloc(hdr->size);
	assert(data);

	uint32 len = archiveFile.read(data, hdr->size);
	assert(len == hdr->size);

	return new Common::MemoryReadStream(data, hdr->size, DisposeAfterUse::YES);
}

const Common::Array<Common::String> PACArchive::getFileExtensions() {
	return Common::Array<Common::String>(FileFormats, 14);
}

bool PACArchive::isValid(Common::SeekableReadStream &stream) {
	return DetectVersion(stream) != FormatVersion::Unknown;
}

PACArchive::FormatVersion PACArchive::DetectVersion(Common::SeekableReadStream &stream) {
	bool isBigEndian;
	if (IsValidFormatVersion1(stream))
		return FormatVersion::Version1;

	if (IsValidFormatVersion2And3(stream, 36, isBigEndian))
		return isBigEndian ? FormatVersion::Version2BE : FormatVersion::Version2;

	if (IsValidFormatVersion2And3(stream, 28, isBigEndian))
		return isBigEndian ? FormatVersion::Version3BE : FormatVersion::Version3;

	return FormatVersion::Unknown;
}

bool PACArchive::IsValidFormatVersion1(Common::SeekableReadStream &stream) {
	stream.seek(0, SEEK_SET);
	if (stream.size() <= 256) {
		return false;
	}

	// read some test data
	Common::Array<byte> testData = Common::Array<byte>(256);
	stream.read(testData.data(), 256);
	stream.seek(0, SEEK_SET);

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

	int testLength = *(int*)&testData[252]; 

	// sanity check, if the length of the first file is >= 100 mb, fail the test
	if (testLength >= stream.size() || testLength < 0) {
		return false;
	}

	return true;
}

bool PACArchive::IsValidFormatVersion2And3(Common::SeekableReadStream &stream, int entrySize, bool &isBigEndian) {
	stream.seek(0, SEEK_SET);
	isBigEndian = false;

	// check stream length
	if (stream.size() <= 4 + entrySize)
		return false;

	byte *testData = new byte[4 + entrySize];
	stream.read(testData, 4 + entrySize);
	stream.seek(0, SEEK_SET);

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

	// check if the name field is correct
	bool nameTerminated = false;
	for (int i = 0; i < entrySize - 4; i++) {
		if (testData[4 + i] == 0x00) {
			if (i == 0)
				return false;

			nameTerminated = true;
		}

		if (testData[4 + i] != 0x00 && nameTerminated)
			return false;

		int length = *(int*)&testData[entrySize]; // int((testData[entrySize]) << 24 |
		             //    (testData[entrySize + 1]) << 16 |
		             //    (testData[entrySize + 2]) << 8 |
		             //    (testData[entrySize + 3]));

		if (length >= stream.size() || length < 0) {
			length = ((length >> 24) & 0xff) |
			         ((length << 8) & 0xff0000) |
			         ((length >> 8) & 0xff00) |
			         ((length << 24) & 0xff000000);

			if (length >= stream.size() || length < 0)
				return false;

			isBigEndian = true;
		}

		return true;
	}
}

void PACArchive::ReadEntries(Common::SeekableReadStream &stream) {
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

			Common::String fileName = Common::String(stringBuilder.data());//, stringBuilder.data() + stringBuilder.size());
			fileName.trim();

			// set position to length field
			stream.seek(entryStartPosition + 252, SEEK_SET);

			// read entry length
			int length = stream.readSint32(IsLittleEndian());

			if (fileName.size() == 0 || length <= 0 || length > 1024 * 1024 * 100) {
				break;
			}

			// make an entry
			Entry entry;
			entry.name = fileName;
			entry.position = stream.pos();
			entry.size = length;

			// clear string builder for next iteration
			stringBuilder.clear();
			int pos = (stream.pos() + entry.size + 64 - 1) & ~(64 - 1);
			stream.seek(pos, SEEK_SET);

			entries[entry.name].reset(new Entry(entry));
		}
	} else if (Version == FormatVersion::Version2 || Version == FormatVersion::Version2BE || Version == FormatVersion::Version3 || Version == FormatVersion::Version3BE) {
		int entryCount = stream.readSint32(IsLittleEndian());
		int nameLength = 32;
		if (Version == FormatVersion::Version3)
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

			Common::String fileName = Common::String(stringBuilder.data());//, stringBuilder.data() + stringBuilder.size());
			fileName.trim();

			// read entry length
			int length = stream.readSint32(IsLittleEndian());

			if (fileName.size() == 0 || length <= 0 || length > 1024 * 1024 * 100) {
				break;
			}

			// make an entry

			Entry entry;
			entry.name = fileName;
			entry.position = stream.pos();
			entry.size = length;

			// clear string builder for next iteration
			stringBuilder.clear();
			stream.seek(entry.size, SEEK_CUR);

			entries[entry.name].reset(new Entry(entry));
		}
	}
}

PACArchive *makePACArchive(const Common::String &name) {
	return new PACArchive(name);
}

} // End of namespace SMT
