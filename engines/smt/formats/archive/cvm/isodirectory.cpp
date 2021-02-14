#include "engines/smt/formats/archive/cvm/isodirectory.h"
namespace SMT
{

IsoDirectoryRecord::IsoDirectoryRecord(Common::SeekableReadStream &reader, IsoDirectoryRecord *parent) : mParent(parent)
{
    hasParent = true;
    InternalRead(reader);
}

IsoDirectoryRecord::IsoDirectoryRecord(Common::SeekableReadStream &reader): mParent(nullptr)
{
    hasParent = true;
    InternalRead(reader);
}

IsoDirectoryRecord::IsoDirectoryRecord(): mParent(nullptr)
{
    hasParent = false;
}

void IsoDirectoryRecord::InternalRead(Common::SeekableReadStream &reader)
{

    int64 posStart = reader.pos();
    byte length = reader.readByte();
    byte extLength = reader.readByte();

    mLba = reader.readSint32BE();
    reader.seek(0x04, reader.pos()); // LBA_BE

    mSize = reader.readSint32BE();
    reader.seek(0x04 + 0x07, reader.pos()); // size_BE + datetime

    mFlags = reader.readByte();
    reader.seek(0x06, reader.pos()); // unit size + interleave gap + volume seq number

    byte nameLength = reader.readByte();
    byte nameBytes[nameLength];
    reader.read(nameBytes, nameLength);

    if (nameLength == 1)
    {
        if (nameBytes[0] == 0)
            mName = ".";
        else if (nameBytes[0] == 1)
            mName = "..";
    }
    else
    {
        char _name[nameLength + 1];
        memcpy(_name, nameBytes, nameLength);
        _name[nameLength] = 0;
        mName = Common::String(_name);

        size_t index = mName.findFirstOf(';');
        mName = mName.substr(0, index);
        debug(mName.c_str());
    }
    bool isDirectory = (mFlags & (int)RecordFlags::DirectoryRecord) == (int)RecordFlags::DirectoryRecord;
    bool isNotParentOrGrandparentDirectory = nameLength != 1 || mParent == nullptr;

    if (isDirectory && isNotParentOrGrandparentDirectory)
    {
        reader.seek(CVM_HEADER_SIZE + ((int64)mLba * ISO_BLOCKSIZE));
        mSubEntries = Common::Array<IsoDirectoryRecord *>();

        // Set the initial sector start position
        int64 posSubEntriesSectorStart = reader.pos();
        int64 posSubEntriesDataEnd = posSubEntriesSectorStart + mSize;

        while (reader.pos() < posSubEntriesDataEnd)
        {
            IsoDirectoryRecord *record = new IsoDirectoryRecord(reader, this);

            mSubEntries.push_back(record);

            // Skip padding
            while (reader.readByte() == 0 && reader.pos() < posSubEntriesDataEnd);

            // Break out of the loop if we've read to or past the end position
            if (reader.pos() >= posSubEntriesDataEnd)
            {
                break;
            }

            // We've found a non-zero byte, seek back
            reader.seek(-1, reader.pos());
        }
    }

    reader.seek(posStart + length + extLength, reader.pos());
}
} // namespace SMT