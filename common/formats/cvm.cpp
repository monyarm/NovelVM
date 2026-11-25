#include "common/archive.h"

#include "common/formats/cvm.h"

// TODO: every real CVM has structured, non-zero data at header offset 0x1000 whose purpose is
// unknown; nothing here needs it, so it is not parsed. The game finds the ISO image through the
// CVMH/ZONE headers; here it is assumed to start at CVM_HEADER_SIZE (0x1800), as in every real
// P3 FES/P4 CVM.

namespace Common {

CVM::CVM(const Common::String &filename) : _cvmFilename(filename) {
	Common::File cvmFile;

	Common::ArchiveMemberList list;
	SearchMan.listMembers(list);

	if (!cvmFile.open(Common::Path(_cvmFilename))) {
		warning("CVMArchive::CVMArchive(): Could not find the archive file");
		return;
	}

	ReadFile(cvmFile);
}

void CVM::ReadFile(Common::SeekableReadStream &cvmFile) {

	cvmFile.seek(CVM_HEADER_SIZE + ISO_RESERVED_SIZE);

	byte sectorType = cvmFile.readByte();

	// Volume descriptors follow each other until the set terminator (type 255).
	while (sectorType != ID_PRIM_VOLUME_DESC) {
		if (sectorType == 0xFF || cvmFile.eos() || cvmFile.pos() + ISO_BLOCKSIZE > cvmFile.size()) {
			warning("CVM: no primary volume descriptor in %s", _cvmFilename.c_str());
			return;
		}
		cvmFile.seek(ISO_BLOCKSIZE - 1, SEEK_CUR);
		sectorType = cvmFile.readByte();
	}

	cvmFile.seek(ISO_ROOTDIRECTORY_OFFSET - 1, SEEK_CUR);
	ReadISORecord(cvmFile, true);

	//            mRootDirectory = new IsoDirectoryRecord(reader, null);

	//_entries[entry.name].reset(new CVMEntry(entry));
}

CVM::~CVM() {
}

CVMEntry CVM::ReadISORecord(Common::SeekableReadStream &reader, bool isRoot, Common::String parentName) {
	CVMEntry record;

	long posStart = reader.pos();
	byte length = reader.readByte();
	reader.readByte(); // extended attribute record length (not part of this record)

	uint32 mLba = reader.readUint32LE();
	reader.seek(0x04, SEEK_CUR); //LBA_BE

	uint32 mSize = reader.readUint32LE();
	reader.seek(0x04 + 0x07, SEEK_CUR); //size_BE + datetime

	byte mFlags = reader.readByte();
	reader.seek(0x06, SEEK_CUR); // unit size + interleave gap + volume seq number

	byte nameLength = reader.readByte();
	Common::Array<byte> nameBytes(nameLength);
	reader.read(nameBytes.data(), nameLength);

	Common::String mName;

	if (nameBytes.size() == 1) {
		if (nameBytes[0] == 0)
			mName = ".";
		else if (nameBytes[0] == 1)
			mName = "..";
	} else {
		mName = Common::String((const char *)nameBytes.data(), nameBytes.size());
		mName = mName.substr(0, mName.findFirstOf(';'));
		// "NAME." is an ISO name without an extension; the game drops the dot.
		if (mName.size() > 1 && mName.lastChar() == '.')
			mName.deleteLastChar();
		if (parentName != "")
			mName = parentName + '/' + mName;
	}
	//debug(mName.c_str());

	bool isDirectory = ((mFlags & (int)RecordFlags::DirectoryRecord) == (int)RecordFlags::DirectoryRecord) || isRoot;

	if (!isDirectory) {
		record.name = mName;
		record.size = mSize;
		record.offset = CVM_HEADER_SIZE + ((long)mLba * ISO_BLOCKSIZE);
		record.cvmFile = _cvmFilename;

		_entries[mName].reset(new CVMEntry(record));
	}

	bool isNotParentOrGrandparentDirectory = nameLength != 1 || isRoot;

	if (isDirectory && isNotParentOrGrandparentDirectory) {
		reader.seek(CVM_HEADER_SIZE + ((long)mLba * ISO_BLOCKSIZE));
		//mSubEntries = new List<IsoDirectoryRecord>();

		// Set the initial sector start position
		long posSubEntriesSectorStart = reader.pos();
		long posSubEntriesDataEnd = posSubEntriesSectorStart + mSize;

		while (reader.pos() < posSubEntriesDataEnd) {
			CVMEntry subrecord;
			if (mName == ".") {
				subrecord = ReadISORecord(reader, false);
			} else {
				subrecord = ReadISORecord(reader, false, mName);
			}

			record.subEntries.push_back(subrecord);

			// Skip padding
			while (reader.readByte() == 0 && reader.pos() < posSubEntriesDataEnd)
				;

			// Break out of the loop if we've read to or past the end position
			if (reader.pos() >= posSubEntriesDataEnd) {
				break;
			}

			// We've found a non-zero byte, seek back
			reader.seek(-1, SEEK_CUR);
		}
	}

	// The extended attribute record lives with the file data, not after this record.
	reader.seek(posStart + length);
	return record;
}

bool CVM::hasFile(const Common::Path &name) const {
	return _entries.contains(name.toString());
}

int CVM::listMembers(Common::ArchiveMemberList &list) const {
	int matches = 0;

	CVMEntrysMap::const_iterator it = _entries.begin();
	for (; it != _entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(Common::Path(it->_value->name), *this)));
		matches++;
	}

	return matches;
}

const Common::ArchiveMemberPtr CVM::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();

	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *CVM::createReadStreamForMember(const Common::Path &name) const {
	if (!_entries.contains(name.toString())) {
		return 0;
	}

	CVMEntry *hdr = _entries[name.toString()].get();

	Common::File f;
	f.open(Common::Path(hdr->cvmFile));
	f.seek(hdr->offset);
	byte *data = new byte[hdr->size];
	f.read(data, hdr->size);

	return new Common::MemoryReadStream(data, hdr->size, DisposeAfterUse::YES);
}

CVM *makeCVMArchive(const Common::String &name) {
	return new CVM(name);
}

} // namespace Common
