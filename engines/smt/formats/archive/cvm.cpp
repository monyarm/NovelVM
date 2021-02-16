#include "common/archive.h"


#include "smt/formats/archive/cvm.h"

namespace SMT {

CVMArchive::CVMArchive(const Common::String &filename) : _cvmFilename(filename) {
	Common::File cvmFile;

	Common::ArchiveMemberList list;
	SearchMan.listMembers(list);

	if (!cvmFile.open(_cvmFilename)) {
		warning("CVMArchive::CVMArchive(): Could not find the archive file");
		return;
	}

	ReadFile(cvmFile);
}

void CVMArchive::ReadFile(Common::SeekableReadStream &cvmFile) {

	cvmFile.seek(CVM_HEADER_SIZE + ISO_RESERVED_SIZE, SEEK_SET);

	byte sectorType = cvmFile.readByte();

	while (sectorType != ID_PRIM_VOLUME_DESC) {
		cvmFile.seek(ISO_BLOCKSIZE - 1, SEEK_CUR);
		sectorType = cvmFile.readByte();
	}

	cvmFile.seek(ISO_ROOTDIRECTORY_OFFSET - 1, SEEK_CUR);
	ReadISORecord(cvmFile, true);

	//            mRootDirectory = new IsoDirectoryRecord(reader, null);

	//_entries[entry.name].reset(new CVMEntry(entry));
}

CVMArchive::~CVMArchive() {
}

CVMEntry CVMArchive::ReadISORecord(Common::SeekableReadStream &reader, bool isRoot, Common::String parentName) {
	CVMEntry record;

	long posStart = reader.pos();
	byte length = reader.readByte();
	byte ext_attr_length = reader.readByte();

	uint32 mLba = reader.readUint32LE();
	reader.seek(0x04, SEEK_CUR); //LBA_BE

	uint32 mSize = reader.readUint32LE();
	reader.seek(0x04 + 0x07, SEEK_CUR); //size_BE + datetime

	byte mFlags = reader.readByte();
	reader.seek(0x06, SEEK_CUR); // unit size + interleave gap + volume seq number

	byte nameLength = reader.readByte();
	byte nameBytes[nameLength];
	reader.read(nameBytes, nameLength);

	Common::String mName;

	if (sizeof(nameBytes) == 1) {
		if (nameBytes[0] == 0)
			mName = ".";
		else if (nameBytes[0] == 1)
			mName = "..";
	} else {
		mName = Common::String((char *)nameBytes, (char *)nameBytes + sizeof(nameBytes));
		if (parentName != "") {
			mName = parentName + '/' + mName.substr(0, mName.findFirstOf(';'));
		} else {
			mName = /*_cvmFilename +"/"+ */ mName.substr(0, mName.findFirstOf(';'));
		}
	}
	//debug(mName.c_str());

	bool isDirectory = ((mFlags & (int)RecordFlags::DirectoryRecord) == (int)RecordFlags::DirectoryRecord) || isRoot;

	if (!isDirectory) {
		if (mName == "EVENT/SYS/ARC_C002.TMX") {
			debug("%s", mName.c_str());
			debug("%i", mSize);
			debug("%li", CVM_HEADER_SIZE + ((long)mLba * ISO_BLOCKSIZE));
		}
		record.name = mName;
		record.size = mSize;
		record.offset = CVM_HEADER_SIZE + ((long)mLba * ISO_BLOCKSIZE);
		record.cvmFile = _cvmFilename;

		_entries[mName].reset(new CVMEntry(record));
	}

	bool isNotParentOrGrandparentDirectory = nameLength != 1 || isRoot;

	if (isDirectory && isNotParentOrGrandparentDirectory) {
		reader.seek(CVM_HEADER_SIZE + ((long)mLba * ISO_BLOCKSIZE), SEEK_SET);
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

	reader.seek(posStart + length + ext_attr_length, SEEK_SET);
	return record;
}

bool CVMArchive::hasFile(const Common::String &name) const {
	return _entries.contains(name);
}

int CVMArchive::listMembers(Common::ArchiveMemberList &list) const {
	int matches = 0;

	CVMEntrysMap::const_iterator it = _entries.begin();
	for (; it != _entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(it->_value->name, this)));
		matches++;
	}

	return matches;
}

const Common::ArchiveMemberPtr CVMArchive::getMember(const Common::String &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();

	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, this));
}

Common::SeekableReadStream *CVMArchive::createReadStreamForMember(const Common::String &name) const {
	if (!_entries.contains(name)) {
		return 0;
	}

	CVMEntry *hdr = _entries[name].get();

	Common::File f;
	f.open(hdr->cvmFile);
	f.seek(hdr->offset, SEEK_SET);
	byte* data = new byte[hdr->size];
	f.read(data, hdr->size);

	return new Common::MemoryReadStream(data, hdr->size, DisposeAfterUse::YES);
}

CVMArchive *makeCVMArchive(const Common::String &name) {
	return new CVMArchive(name);
}

} // End of namespace SMT

