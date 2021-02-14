#include "smt/smt.h"
#include "common/archive.h"
#include "common/hex.h"

#include "engines/smt/formats/archive/cvm/cvmfile.h"

namespace SMT
{

CVMArchive::CVMArchive(const Common::String &filename) : _cvmFilename(filename)
{
    Common::File cvmFile;
    debug(_cvmFilename.c_str());

    Common::ArchiveMemberList list;
    SearchMan.listMembers(list);

    if (!cvmFile.open(_cvmFilename))
    {
        warning("CVMArchive::CVMArchive(): Could not find the archive file");
        return;
    }

    cvmFile.seek(CVM_HEADER_SIZE + ISO_RESERVED_SIZE);

    byte sectorType = cvmFile.readByte();

    while (sectorType != ID_PRIM_VOLUME_DESC)
    {
        cvmFile.seek(ISO_BLOCKSIZE - 1, cvmFile.pos());
        sectorType = cvmFile.readByte();
    }

    cvmFile.seek(ISO_ROOTDIRECTORY_OFFSET - 1, cvmFile.pos());
    mRootDirectory = new IsoDirectoryRecord(cvmFile, nullptr);

    //for (CVMHeader header : _tempHeaders)
    //{
    //    _headers[header.name].reset(new CVMHeader(header));
    //}
}

CVMArchive::~CVMArchive()
{
}

bool CVMArchive::hasFile(const Common::String &name) const
{
    return _headers.contains(name);
}

int32 CVMArchive::listMembers(Common::ArchiveMemberList &list) const
{
    int32 matches = 0;

    CVMHeadersMap::const_iterator it = _headers.begin();
    for (; it != _headers.end(); ++it)
    {
        list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(it->_value->path, this)));
        matches++;
    }

    return matches;
}

const Common::ArchiveMemberPtr CVMArchive::getMember(const Common::String &name) const
{
    if (!hasFile(name))
        return Common::ArchiveMemberPtr();

    return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, nullptr));
}

Common::SeekableReadStream *CVMArchive::createReadStreamForMember(const Common::String &name) const
{
    if (!_headers.contains(name))
    {
        return 0;
    }

    CVMHeader *hdr = _headers[name].get();

    Common::File archiveFile;
    archiveFile.open(_cvmFilename);
    archiveFile.seek(hdr->position, SEEK_SET);

    byte *data = (byte *)malloc(hdr->size);
    assert(data);

    uint32 len = archiveFile.read(data, hdr->size);
    assert(len == hdr->size);

    return new Common::MemoryReadStream(data, hdr->size, DisposeAfterUse::YES);
}

CVMArchive *makeCVMArchive(const Common::String &name)
{
    return new CVMArchive(name);
}

} // End of namespace SMT
