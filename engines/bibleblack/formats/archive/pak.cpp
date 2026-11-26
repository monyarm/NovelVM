/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "bibleblack/bibleblack.h"
#include "common/archive.h"


#include "bibleblack/formats/archive/pak.h"

namespace BibleBlack::Format::Archive {

PAK::PAK(const Common::String &filename) : _pakFilename(filename)
{
    Common::File pakFile;
    debug("%s", _pakFilename.c_str());


	Common::ArchiveMemberList list;
	SearchMan.listMembers(list);

    if (!pakFile.open(Common::Path(_pakFilename)))
    {
        warning("PAK::PAK(): Could not find the archive file");
        return;
    }

    uint16 numHeaders = pakFile.readUint16LE();

    Common::Array<PAKHeader> _tempHeaders;

    for (int i = 0; i < numHeaders; i++)
    {
        PAKHeader header;
        char _name[9];
        char _extension[5];

        pakFile.read(_name, 8);
        _name[8] = '\0';
        _extension[4] = '\0';

        pakFile.read(_extension, 4);

        Common::String name(_name);
        Common::String extension(_extension);


        name.trim();
        extension.trim();
        name.toLowercase();
        extension.toLowercase();
        name = name + "." + extension;

        header.name = _name;
        //debug(header.name.c_str());
        header.position = pakFile.readUint32LE();
        _tempHeaders.push_back(header);
    }

    for (uint16 i = numHeaders - 1; i >= 0; i--)
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
    for (const PAKHeader& header : _tempHeaders)
    {
        _headers[header.name].reset(new PAKHeader(header));
    }

    
}

PAK::~PAK()
{
}

bool PAK::hasFile(const Common::Path &name) const
{
    return _headers.contains(name.toString());
}

int PAK::listMembers(Common::ArchiveMemberList &list) const
{
    int matches = 0;

    PAKHeadersMap::const_iterator it = _headers.begin();
    for (; it != _headers.end(); ++it)
    {
        list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(Common::Path(it->_value->name), *this)));
        matches++;
    }

    return matches;
}

const Common::ArchiveMemberPtr PAK::getMember(const Common::Path &name) const
{
    if (!hasFile(name))
        return Common::ArchiveMemberPtr();

    return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *PAK::createReadStreamForMember(const Common::Path &name) const
{
    if (!_headers.contains(name.toString())) {
		return 0;
	}

	PAKHeader *hdr = _headers[name.toString()].get();

	Common::File archiveFile;
	archiveFile.open(Common::Path(_pakFilename));
	archiveFile.seek(hdr->position);

	byte *data = (byte *)malloc(hdr->size);
	assert(data);

	uint32 len = archiveFile.read(data, hdr->size);
	assert(len == hdr->size);

	return new Common::MemoryReadStream(data, hdr->size, DisposeAfterUse::YES);
}

PAK *PAKFactory(const Common::String &name)
{
    return new PAK(name);
}

} // namespace BibleBlack::Format::Archive
