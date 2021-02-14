#include "smt/formats/archive/cpk.h"
Common::File f;


void CPKFile::ReadFile(char* path)
{

    if (f.open(path))
    {

        uint32 magic = f.readUint32BE();

        Common::Array<byte> CPK_Packet = ReadUTFData();

        FileEntry CPAK_entry;

        CPAK_entry.FileName = "CPK_HDR";
        CPAK_entry.FileOffsetPos = f.pos() + 0x10;
        CPAK_entry.FileSize = CPK_Packet.size();
        CPAK_entry.Encrypted = isUtfEncrypted;
        CPAK_entry.FileType = "CPK";

        fileTable.push_back(CPAK_entry);

        debug(tag2str(magic));
        Common::hexdump(CPK_Packet.begin(), CPK_Packet.size());
    }
}

Common::Array<byte> CPKFile::ReadUTFData()
{
    f.readSint32LE();
    int64 utf_size = f.readSint64LE();
    byte* _utf_packet = new byte[utf_size];
    f.read(_utf_packet, utf_size);
    Common::Array<byte> utf_packet(_utf_packet, utf_size);

    if (utf_packet[0] != 0x40 && utf_packet[1] != 0x55 && utf_packet[2] != 0x54 && utf_packet[3] != 0x46) //@UTF
    {
        utf_packet = DecryptUTF(utf_packet);
        isUtfEncrypted = true;
    }
    return utf_packet;
}
Common::Array<byte> CPKFile::DecryptUTF(Common::Array<byte> utf)
{

    Common::Array<byte> result;

    int m, t;
    byte d;

    m = 0x0000655f;
    t = 0x00004115;

    for (ulong i = 0; i < sizeof(utf); i++)
    {
        d = utf[i];
        d = (byte)(d ^ (byte)(m & 0xff));
        result[i] = d;
        m *= t;
    }

    return result;
}

CPKFile::CPKFile()
{
}
