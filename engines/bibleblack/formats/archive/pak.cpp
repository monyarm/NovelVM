#include "bibleblack/bibleblack.h"
#include "common/archive.h"
#include "common/hex.h"

#include "bibleblack/formats/archive/pak.h"

namespace BibleBlack
{

PAKArchive::PAKArchive(const Common::String &filename) : _pakFilename(filename)
{
    Common::File pakFile;
    debug(_pakFilename.c_str());


	Common::ArchiveMemberList list;
	SearchMan.listMembers(list);

    if (!pakFile.open(_pakFilename))
    {
        warning("PAKArchive::PAKArchive(): Could not find the archive file");
        return;
    }

    uint16 numHeaders = pakFile.readUint16LE();

    Common::Array<PakHeader> _tempHeaders;

    for (int i = 0; i < numHeaders; i++)
    {
        PakHeader header;
        char __name[9];
        char __extension[5];

        pakFile.read(__name, 8);
        __name[8] = '\0';
        __extension[4] = '\0';

        pakFile.read(__extension, 4);

        Common::String _name(__name);
        Common::String _extension(__extension);


        _name.trim();
        _extension.trim();
        _name.toLowercase();
        _extension.toLowercase();
        _name = _name + "." + _extension;

        header.name = _name;
        //debug(header.name.c_str());
        header.position = pakFile.readUint32LE();
        _tempHeaders.push_back(header);
    }

    for (char i = numHeaders - 1; i >= 0; i--)
    {
        if (i == numHeaders - 1)
        {
            _tempHeaders[i].size = pakFile.size() - _tempHeaders[i].size;
        }
        else
        {
            _tempHeaders[i].size = _tempHeaders[i + 1].position - _tempHeaders[i].position;
        }
    }
    for (PakHeader header : _tempHeaders)
    {
        _headers[header.name].reset(new PakHeader(header));
    }

    
}

PAKArchive::~PAKArchive()
{
}

bool PAKArchive::hasFile(const Common::String &name) const
{
    return _headers.contains(name);
}

int PAKArchive::listMembers(Common::ArchiveMemberList &list) const
{
    int matches = 0;

    PakHeadersMap::const_iterator it = _headers.begin();
    for (; it != _headers.end(); ++it)
    {
        list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(it->_value->name, this)));
        matches++;
    }

    return matches;
}

const Common::ArchiveMemberPtr PAKArchive::getMember(const Common::String &name) const
{
    if (!hasFile(name))
        return Common::ArchiveMemberPtr();

    return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, this));
}

Common::SeekableReadStream *PAKArchive::createReadStreamForMember(const Common::String &name) const
{
    if (!_headers.contains(name)) {
		return 0;
	}

	PakHeader *hdr = _headers[name].get();

	Common::File archiveFile;
	archiveFile.open(_pakFilename);
	archiveFile.seek(hdr->position, SEEK_SET);

	byte *data = (byte *)malloc(hdr->size);
	assert(data);

	uint32 len = archiveFile.read(data, hdr->size);
	assert(len == hdr->size);

	return new Common::MemoryReadStream(data, hdr->size, DisposeAfterUse::YES);
}

PAKArchive *makePAKArchive(const Common::String &name)
{
    return new PAKArchive(name);
}

} // End of namespace BibleBlack
