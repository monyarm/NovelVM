// exwlcs, v1.0 2008/07/26
// coded by asmodean

// contact:
//   web:   http://asmodean.reverse.net
//   email: asmodean [at] hush.com
//   irc:   asmodean on efnet (irc.efnet.net)


#include "koihime_doki/koihime_doki.h"
#include "common/archive.h"


#include "koihime_doki/formats/archive/exwlcs.h"

namespace KoihimeDoki::Format::Archive
{


unsigned long EXWLCSArchive::unobfuscate() {
  unsigned long key = (unsigned char) (entry_count >> 24);
  key = (key << 24) | (key << 16) | (key << 8) | key;

  entry_count ^= key;

  return key;
}

void EXWLCSArchive::unobfuscate(LCSENTRY& entry, unsigned long key) {
  entry.offset ^= key;
  entry.length ^= key;

  for (unsigned long i = 0; entry.filename[i]; i++) {
    entry.filename[i] ^=  key;
  }
}

void EXWLCSArchive::unobfuscate(unsigned char* buff, unsigned long len) const {
  unsigned char  key = buff[3]; // Hopefully always :)
  unsigned char* end = buff + len;

  while (buff < end) {
    *buff++ ^= key;
  }
}



Common::String guess_file_extension(unsigned char *buff, unsigned long len)
{
	if (*(unsigned short*)buff == 'MB')
		return ".bmp";
	if (*(unsigned long*)buff == 'GNP\x89')
		return ".png";
	if (*(unsigned long*)&buff[6] == 'FIFJ')
		return ".jpg";

	return ".bin";
}

EXWLCSArchive::EXWLCSArchive(const Common::String &filename) : _exwlcsFilename(filename)
{
    Common::File exwlcsFile;
    Common::File exwlcsLst;

    debug("%s", _exwlcsFilename.c_str());

    if (!exwlcsFile.open(_exwlcsFilename))
    {
        warning("EXWLCSArchive::EXWLCSArchive(): Could not find the archive file");
        return;
    }

    if (!exwlcsLst.open(_exwlcsFilename + ".lst"))
    {
        warning("EXWLCSArchive::EXWLCSArchive(): Could not find the list file");
        return;
    }


    entry_count = exwlcsLst.readUint32LE();
    unsigned long key = unobfuscate();

    //Common::Array<LCSENTRY> entries(sizeof(LCSENTRY) * entry_count);

    //LCSENTRY * entries = new LCSENTRY [entry_count];
    Common::Array<LCSENTRY> entries;
    //entries.reserve(sizeof(LCSENTRY) * entry_count);

    //exwlcsLst.read(entries,sizeof(LCSENTRY) * entry_count);


  for (unsigned long i = 0; i < entry_count; i++) {
    LCSENTRY hdr;

    hdr.offset = exwlcsLst.readUint32LE();
    hdr.length = exwlcsLst.readUint32LE();
    exwlcsLst.read(hdr.filename,64);
    hdr.unknown = exwlcsLst.readUint32LE();

    entries.push_back(hdr);
    unobfuscate(entries[i], key);


    unsigned char* buff = new unsigned char[entries[i].length];

    exwlcsFile.seek(entries[i].offset, SEEK_SET);

    exwlcsFile.read(buff, entries[i].length);
    Common::String name(entries[i].filename + guess_file_extension(buff, entries[i].length));
    name.trim();
    //name = name + guess_file_extension(buff, entries[i].length);

    _headers[name].reset(new LCSHEADER({entries[i].offset,entries[i].length,name,0}));

    delete [] buff;

  }
    //delete [] entries;





}

EXWLCSArchive::~EXWLCSArchive()
{
}

bool EXWLCSArchive::hasFile(const Common::String &name) const
{
    return _headers.contains(name);
}

int EXWLCSArchive::listMembers(Common::ArchiveMemberList &list) const
{
    int matches = 0;

    PakHeadersMap::const_iterator it = _headers.begin();
    for (; it != _headers.end(); ++it)
    {
        list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(it->_value->filename, this)));
        matches++;
    }

    return matches;
}

const Common::ArchiveMemberPtr EXWLCSArchive::getMember(const Common::String &name) const
{
    if (!hasFile(name))
        return Common::ArchiveMemberPtr();

    return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, this));
}

Common::SeekableReadStream *EXWLCSArchive::createReadStreamForMember(const Common::String &name) const
{
    if (!_headers.contains(name)) {
		return 0;
	}

	LCSHEADER *hdr = _headers[name].get();

	Common::File archiveFile;
	archiveFile.open(_exwlcsFilename);
	archiveFile.seek(hdr->offset, SEEK_SET);

    unsigned char* buff = new unsigned char[hdr->length];
    unsigned long len = hdr->length;
    archiveFile.read(buff, len);

    // Assume that any data we don't recognize is obfuscated
    if (strcmp(guess_file_extension(buff, len).c_str(), ".bin") == 0) {
      unobfuscate(buff, len);
    }

	return new Common::MemoryReadStream(buff, len, DisposeAfterUse::YES);
}

EXWLCSArchive *makeEXWLCSArchive(const Common::String &name)
{
    return new EXWLCSArchive(name);
}

} // End of namespace KoihimeDoki
