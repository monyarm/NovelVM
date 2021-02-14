#ifndef SMT_ISODIRECTORY_H
#define SMT_ISODIRECTORY_H

#include "common/ptr.h"
#include "common/str.h"

#include "common/file.h"
#include "common/hash-str.h"
#include "common/memstream.h"
#include "common/bufferedstream.h"
#include "common/debug.h"

#include "engines/smt/formats/archive/cvm/cvmfile.h"

namespace SMT
{

enum class RecordFlags : byte
{
    FileRecord = 1 << 0,
    DirectoryRecord = 1 << 1
};
 class IsoDirectoryRecord
{
public:
    IsoDirectoryRecord();
    IsoDirectoryRecord(Common::SeekableReadStream &reader);
    IsoDirectoryRecord(Common::SeekableReadStream &reader, IsoDirectoryRecord *parent);

    int32 LBA()
    {
        return mLba;
    };

    int32 Size()
    {
        return mSize;
    };

    int32 Flags()
    {
        return mFlags;
    };

    Common::String Name()
    {
        return mName;
    };

    IsoDirectoryRecord *ParentDirectory()
    {
        return mParent;
    };

    Common::Array<IsoDirectoryRecord*> SubEntries()
    {
        return mSubEntries;
    };

private:
    int32 mLba;
    int32 mSize;
    int32 mFlags;
    bool hasParent;
    Common::String mName;

    IsoDirectoryRecord *mParent;
    Common::Array<IsoDirectoryRecord*> mSubEntries;

    void InternalRead(Common::SeekableReadStream &reader);
};
} // namespace SMT

#endif