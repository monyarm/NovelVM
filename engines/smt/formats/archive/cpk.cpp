#include "smt/formats/archive/cpk.h"
Common::File f;


void CPKFile::ReadFile()
{

    if (f.open("umd0.cpk"))
    {

        char magic[5];
        f.read(magic, 4);

        std::vector<byte> CPK_Packet = ReadUTFData();

        FileEntry CPAK_entry;

        CPAK_entry.FileName = "CPK_HDR";
        CPAK_entry.FileOffsetPos = f.pos() + 0x10;
        CPAK_entry.FileSize = CPK_Packet.size();
        CPAK_entry.Encrypted = isUtfEncrypted;
        CPAK_entry.FileType = "CPK";

        fileTable.push_back(CPAK_entry);

        debug(magic);
        debug(VectorToHex(CPK_Packet));
    }
}

std::vector<byte> CPKFile::ReadUTFData()
{
    f.readSint32LE();
    int64 utf_size = f.readSint64LE();
    byte* _utf_packet = new byte[utf_size];
    f.read(_utf_packet, utf_size);
    std::vector<byte> utf_packet(_utf_packet, _utf_packet + (sizeof(_utf_packet) / sizeof(_utf_packet[0])));

    if (utf_packet[0] != 0x40 && utf_packet[1] != 0x55 && utf_packet[2] != 0x54 && utf_packet[3] != 0x46) //@UTF
    {
        utf_packet = DecryptUTF(utf_packet);
        isUtfEncrypted = true;
    }
    return utf_packet;
}
std::vector<byte> CPKFile::DecryptUTF(std::vector<byte> utf)
{

    std::vector<byte> result;

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
