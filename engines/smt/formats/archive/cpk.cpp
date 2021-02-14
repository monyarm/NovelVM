#include "smt/formats/archive/cpk.h"
namespace CPK
{

CPKFile::CPKFile(const char *path)
{
    isUtfEncrypted = false;

    Common::File f;

    if (f.open(path))
    {
        ReadCPKFile(f);
    }
}

bool CPKFile::ReadCPKFile(Common::SeekableReadStream &br)
{

    uint Files;
    ushort Align;

    bool LE = true;

    char cpkMagic[5];
    br.read(cpkMagic, 4);
    cpkMagic[4] = 0;

    if (!strcmp(cpkMagic, "CPK "))
    {
        return false;
    }

    ReadUTFData(br);

    CPK_packet = utf_packet;

    FileEntry CPAK_entry;
    CPAK_entry.FileName = "CPK_HDR";
    CPAK_entry.FileOffsetPos = br.pos() + 0x10;
    CPAK_entry.FileSize = CPK_packet.size();
    CPAK_entry.Encrypted = isUtfEncrypted;
    CPAK_entry.FileType = "CPK";

    fileTable.push_back(CPAK_entry);

    Common::MemoryReadStream ms(utf_packet.data(), utf_packet.size());

    if (!utf.ReadUTF(ms, false))
    {
        return false;
    }

    for (int i = 0; i < utf.columns.size(); i++)
    {
        cpkdata[utf.columns[i].name] = (utf.rows[0].rows[i].GetValue());
    }

    TocOffset = (uint64)GetColumsData(utf, 0, "TocOffset", E_ColumnDataType::DATA_TYPE_UINT64).getValue.u64;
    int64 TocOffsetPos = GetColumnPostion(utf, 0, "TocOffset");

    EtocOffset = (uint64)GetColumsData(utf, 0, "EtocOffset", E_ColumnDataType::DATA_TYPE_UINT64).getValue.u64;
    int64 ETocOffsetPos = GetColumnPostion(utf, 0, "EtocOffset");

    ItocOffset = (uint64)GetColumsData(utf, 0, "ItocOffset", E_ColumnDataType::DATA_TYPE_UINT64).getValue.u64;
    int64 ITocOffsetPos = GetColumnPostion(utf, 0, "ItocOffset");

    GtocOffset = (uint64)GetColumsData(utf, 0, "GtocOffset", E_ColumnDataType::DATA_TYPE_UINT64).getValue.u64;
    int64 GTocOffsetPos = GetColumnPostion(utf, 0, "GtocOffset");

    ContentOffset = (uint64)GetColumsData(utf, 0, "ContentOffset", E_ColumnDataType::DATA_TYPE_UINT64).getValue.u64;
    int64 ContentOffsetPos = GetColumnPostion(utf, 0, "ContentOffset");
    fileTable.push_back(CreateFileEntry("CONTENT_OFFSET", ContentOffset, Type::T_uint64, ContentOffsetPos, "CPK", "CONTENT", false));

    Files = (uint32)GetColumsData(utf, 0, "Files", E_ColumnDataType::DATA_TYPE_UINT32).getValue.u32;
    Align = (uint16)GetColumsData(utf, 0, "Align", E_ColumnDataType::DATA_TYPE_USHORT).getValue.u16;


    if (TocOffset != 0xFFFFFFFFFFFFFFFF)
                {
                    FileEntry entry = CreateFileEntry("TOC_HDR", TocOffset, Type::T_uint64, TocOffsetPos, "CPK", "HDR", false);
                    fileTable.push_back(entry);

                    if (!ReadTOC(br, TocOffset, ContentOffset))
                        return false;
                }

                if (EtocOffset != 0xFFFFFFFFFFFFFFFF)
                {
                    FileEntry entry = CreateFileEntry("ETOC_HDR", EtocOffset, Type::T_uint64, ETocOffsetPos, "CPK", "HDR", false);
                    fileTable.push_back(entry);

                    if (!ReadETOC(br, EtocOffset))
                        return false;
                }

                if (ItocOffset != 0xFFFFFFFFFFFFFFFF)
                {
                    FileEntry entry = CreateFileEntry("ITOC_HDR", ItocOffset, Type::T_uint64, ITocOffsetPos, "CPK", "HDR", false);
                    fileTable.push_back(entry);

                    if (!ReadITOC(br, ItocOffset, ContentOffset, Align))
                        return false;
                }

                if (GtocOffset != 0xFFFFFFFFFFFFFFFF)
                {
                    FileEntry entry = CreateFileEntry("GTOC_HDR", GtocOffset, Type::T_uint64, GTocOffsetPos, "CPK", "HDR", false);
                    fileTable.push_back(entry);

                    if (!ReadGTOC(br, GtocOffset))
                        return false;
                }

                return true;

}
FileEntry CPKFile::CreateFileEntry(string FileName, uint64 FileOffset, Type FileOffsetType, int64 FileOffsetPos, string TOCName, string FileType, bool encrypted)
{
    FileEntry entry;
    entry.FileName = FileName;
    entry.FileOffset = FileOffset;
    entry.FileOffsetType = FileOffsetType;
    entry.FileOffsetPos = FileOffsetPos;
    entry.TOCName = TOCName;
    entry.FileType = FileType;
    entry.Encrypted = encrypted;
    entry.Offset = 0;

    return entry;
}

bool CPKFile::ReadTOC(Common::SeekableReadStream &br, uint64 TocOffset, uint64 ContentOffset)

{
    uint64 fTocOffset = TocOffset;
    uint64 add_offset = 0;

    if (fTocOffset > (uint64)0x800)
        fTocOffset = (uint64)0x800;

    if (ContentOffset < 0)
        add_offset = fTocOffset;
    else
    {
        if (TocOffset < 0)
            add_offset = ContentOffset;
        else
        {
            if (ContentOffset < fTocOffset)
                add_offset = ContentOffset;
            else
                add_offset = fTocOffset;
        }
    }

    br.seek((int64)TocOffset);
    char utfMagic[5];
    br.read(utfMagic, 4);
    utfMagic[4] = 0;

    if (!strcmp(utfMagic, "TOC "))
    {
        return false;
    }

    ReadUTFData(br);

    // Store unencrypted TOC
    TOC_packet = utf_packet;

    FileEntry toc_entry;
    for (auto file : fileTable)
    {
        if (strcmp(file.FileName.c_str(), "TOC_HDR"))
        {
            toc_entry = file;
        }
    }

    toc_entry.Encrypted = isUtfEncrypted;
    toc_entry.FileSize = (TOC_packet.size());

    Common::MemoryReadStreamEndian ms(utf_packet.data(), utf_packet.size(), true);

    if (!files.ReadUTF(ms))
    {
        return false;
    }

    FileEntry temp;
    for (int i = 0; i < files.num_rows; i++)
    {

        temp.TOCName = "TOC";

        temp.DirName = GetColumnData(files, i, "DirName").getValue.s;
        temp.FileName = GetColumnData(files, i, "FileName").getValue.s;

        temp.FileSize = GetColumnData(files, i, "FileSize").getValue.i16;
        temp.FileSizePos = GetColumnPostion(files, i, "FileSize");
        temp.FileSizeType = GetColumnType(files, i, "FileSize");

        temp.ExtractSize = GetColumnData(files, i, "ExtractSize").getValue.i16;
        temp.ExtractSizePos = GetColumnPostion(files, i, "ExtractSize");
        temp.ExtractSizeType = GetColumnType(files, i, "ExtractSize");

        temp.FileOffset = ((uint64)GetColumnData(files, i, "FileOffset").getValue.u64 + (uint64)add_offset);
        temp.FileOffsetPos = GetColumnPostion(files, i, "FileOffset");
        temp.FileOffsetType = GetColumnType(files, i, "FileOffset");

        temp.FileType = "FILE";

        temp.Offset = add_offset;

        temp.ID = GetColumnData(files, i, "ID").getValue.i16;
        temp.UserString = GetColumnData(files, i, "UserString").getValue.s;

        fileTable.push_back(temp);
    }
    delete &files;

    return true;
}

object CPKFile::GetColumsData(UTF utf, int row, string Name, E_ColumnDataType type)
{
    object Temp = GetColumnData(utf, row, Name);

    if (Temp.type == Type::T_NULL)
    {
        switch (type)
        {
        case E_ColumnDataType::DATA_TYPE_BYTE: // byte
            return object((byte)0xFF);
        case E_ColumnDataType::DATA_TYPE_USHORT: // int16
            return object((uint16)0xFFFF);
        case E_ColumnDataType::DATA_TYPE_UINT32: // int
            return object((uint32)0xFFFFFFFF);
        case E_ColumnDataType::DATA_TYPE_UINT64: // int64
            return object((uint64)0xFFFFFFFFFFFFFFFF);
        }
    }

    if (Temp.is(Type::T_uint64))
    {
        return (Temp.type == Type::T_NULL) ? 0xFFFFFFFFFFFFFFFF : (uint64)Temp.getValue.u64;
    }

    if (Temp.is(Type::T_uint32))
    {
        return (Temp.type == Type::T_NULL) ? 0xFFFFFFFF : (uint32)Temp.getValue.u32;
    }

    if (Temp.is(Type::T_uint16))
    {
        return (Temp.type == Type::T_NULL) ? (uint16)0xFFFF : (uint16)Temp.getValue.u16;
    }

    if (Temp.is(Type::T_byte))
    {
        return (Temp.type == Type::T_NULL) ? (byte)0xFF : (byte)Temp.getValue.b;
    }

    return 0;
}

object CPKFile::GetColumnData(UTF utf, int row, string pName)
{
    object result;

    for (int i = 0; i < utf.num_columns; i++)
    {
        int storageFlag = utf.columns[i].flags & (int)UTF::COLUMN_FLAGS::STORAGE_MASK;
        int columnType = utf.columns[i].flags & (int)UTF::COLUMN_FLAGS::TYPE_MASK;
        if (storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_CONSTANT)
        {
            if (utf.columns[i].name == pName)
            {
                result = utf.columns[i].GetValue();
                break;
            }
        }

        if (storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_NONE || storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_ZERO)
        {
            continue;
        }

        if (utf.columns[i].name == pName)
        {
            result = utf.rows[row].rows[i].GetValue();
            break;
        }
    }

    return result;
}

int64 CPKFile::GetColumnPostion(UTF utf, int row, string pName)

{
    int64 result = -1;

    for (int i = 0; i < utf.num_columns; i++)
    {
        int storageFlag = utf.columns[i].flags & (int)UTF::COLUMN_FLAGS::STORAGE_MASK;
        int columnType = utf.columns[i].flags & (int)UTF::COLUMN_FLAGS::TYPE_MASK;
        if (storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_CONSTANT)
        {
            if (utf.columns[i].name == pName)
            {
                result = utf.columns[i].position;
                break;
            }
        }

        if (storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_NONE || storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_ZERO)
        {
            continue;
        }

        if (utf.columns[i].name == pName)
        {
            result = utf.rows[row].rows[i].position;
            break;
        }
    }

    return result;
}

Type CPKFile::GetColumnType(UTF utf, int row, string pName)

{
    Type result = Type::T_NULL;

    for (int i = 0; i < utf.num_columns; i++)
    {
        int storageFlag = utf.columns[i].flags & (int)UTF::COLUMN_FLAGS::STORAGE_MASK;
        int columnType = utf.columns[i].flags & (int)UTF::COLUMN_FLAGS::TYPE_MASK;
        if (storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_CONSTANT)
        {
            if (utf.columns[i].name == pName)
            {
                result = utf.columns[i].GetType();
                break;
            }
        }

        if (storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_NONE || storageFlag == (int)UTF::COLUMN_FLAGS::STORAGE_ZERO)
        {
            continue;
        }
        if (utf.columns[i].name == pName)
        {
            result = utf.rows[row].rows[i].GetType();
            break;
        }
    }

    return result;
}

uint16 CPKFile::get_next_bits(List<byte> input, int &offset_p, byte &bit_pool_p, int &bits_left_p, int bit_count)
{
    uint16 out_bits = 0;
    int num_bits_produced = 0;
    int bits_this_round;

    while (num_bits_produced < bit_count)
    {
        if (bits_left_p == 0)
        {
            bit_pool_p = input[offset_p];
            bits_left_p = 8;
            offset_p--;
        }

        if (bits_left_p > (bit_count - num_bits_produced))
            bits_this_round = bit_count - num_bits_produced;
        else
            bits_this_round = bits_left_p;

        out_bits <<= bits_this_round;

        out_bits |= (uint16)((uint16)(bit_pool_p >> (bits_left_p - bits_this_round)) & ((1 << bits_this_round) - 1));

        bits_left_p -= bits_this_round;
        num_bits_produced += bits_this_round;
    }

    return out_bits;
}

List<byte> CPKFile::DecompressCRILAYLA(List<byte> input, int USize)
{
    List<byte> result; // = new byte[USize];

    Common::MemoryReadStream br(input.data, USize); //LE

    br.seek(8); // Skip CRILAYLA
    int uncompressed_size = br.readSint32LE();
    int uncompressed_header_offset = br.readSint32LE();

    result = List<byte>(uncompressed_size + 0x100);

    // do some error checks here.........

    // copy uncompressed 0x100 header to start of file
    memcpy(result.data(), input.data() + uncompressed_header_offset + 0x10, 0x100);

    int input_end = input.size() - 0x100 - 1;
    int input_offset = input_end;
    int output_end = 0x100 + uncompressed_size - 1;
    byte bit_pool = 0;
    int bits_left = 0, bytes_output = 0;
    int vle_lens[4]{2, 3, 5, 8};

    while (bytes_output < uncompressed_size)
    {
        if (get_next_bits(input, input_offset, bit_pool, bits_left, 1) > 0)
        {
            int backreference_offset = output_end - bytes_output + get_next_bits(input, input_offset, bit_pool, bits_left, 13) + 3;
            int backreference_length = 3;
            int vle_level;

            for (vle_level = 0; vle_level < 4; vle_level++)
            {
                int this_level = get_next_bits(input, input_offset, bit_pool, bits_left, vle_lens[vle_level]);
                backreference_length += this_level;
                if (this_level != ((1 << vle_lens[vle_level]) - 1))
                    break;
            }

            if (vle_level == 4)
            {
                int this_level;
                do
                {
                    this_level = get_next_bits(input, input_offset, bit_pool, bits_left, 8);
                    backreference_length += this_level;
                } while (this_level == 255);
            }

            for (int i = 0; i < backreference_length; i++)
            {
                result[output_end - bytes_output] = result[backreference_offset--];
                bytes_output++;
            }
        }
        else
        {
            // verbatim byte
            result[output_end - bytes_output] = (byte)get_next_bits(input, input_offset, bit_pool, bits_left, 8);
            bytes_output++;
        }
    }
    return result;
}

List<byte> CPKFile::DecompressLegacyCRI(List<byte> input, int USize)
{

    List<byte> result; // = new byte[USize];

    Common::MemoryReadStream br(input.data, USize); //LE

    br.seek(8); // Skip CRILAYLA
    int uncompressed_size = br.readSint32LE();
    int uncompressed_header_offset = br.readSint32LE();

    result = List<byte>(uncompressed_size + 0x100);

    int input_end = input.size() - 0x100 - 1;
    int input_offset = input_end;
    int output_end = 0x100 + uncompressed_size - 1;
    byte bit_pool = 0;
    int bits_left = 0, bytes_output = 0;
    int vle_lens[4]{2, 3, 5, 8};

    while (bytes_output < uncompressed_size)
    {
        if (get_next_bits(input, input_offset, bit_pool, bits_left, 1) > 0)
        {
            int backreference_offset = output_end - bytes_output + get_next_bits(input, input_offset, bit_pool, bits_left, 13) + 3;
            int backreference_length = 3;
            int vle_level;

            for (vle_level = 0; vle_level < 4; vle_level++)
            {
                int this_level = get_next_bits(input, input_offset, bit_pool, bits_left, vle_lens[vle_level]);
                backreference_length += this_level;
                if (this_level != ((1 << vle_lens[vle_level]) - 1))
                    break;
            }

            if (vle_level == 4)
            {
                int this_level;
                do
                {
                    this_level = get_next_bits(input, input_offset, bit_pool, bits_left, 8);
                    backreference_length += this_level;
                } while (this_level == 255);
            }

            for (int i = 0; i < backreference_length; i++)
            {
                result[output_end - bytes_output] = result[backreference_offset--];
                bytes_output++;
            }
        }
        else
        {
            // verbatim byte
            result[output_end - bytes_output] = (byte)get_next_bits(input, input_offset, bit_pool, bits_left, 8);
            bytes_output++;
        }
    }
    return result;
}

List<byte> CPKFile::DecryptUTF(List<byte> input)
{
    List<byte> result(input.size());

    int m, t;
    byte d;

    m = 0x0000655f;
    t = 0x00004115;

    for (int i = 0; i < input.size(); i++)
    {
        d = input[i];
        d = (byte)(d ^ (byte)(m & 0xff));
        result[i] = d;
        m *= t;
    }

    return result;
}

bool CPKFile::ReadGTOC(Common::SeekableReadStream &br, uint64 startoffset)
{
    br.seek((int64)startoffset);

    char gtocMagic[5];
    br.read(gtocMagic, 4);
    gtocMagic[4] = 0;

    if (!strcmp(gtocMagic, "GTOC"))
    {
        return false;
    }

    ReadUTFData(br);

    GTOC_packet = utf_packet;
    FileEntry gtoc_entry;
    for (auto file : fileTable)
    {
        if (strcmp(file.FileName.c_str(), "GTOC_HDR"))
        {
            gtoc_entry = file;
        }
    }

    gtoc_entry.Encrypted = isUtfEncrypted;
    gtoc_entry.FileSize = GTOC_packet.size();

    return true;
}

bool CPKFile::ReadETOC(Common::SeekableReadStream &br, uint64 startoffset)

{
    br.seek((int64)startoffset);

    char etocMagic[5];
    br.read(etocMagic, 4);
    etocMagic[4] = 0;

    if (!strcmp(etocMagic, "GTOC"))
    {
        return false;
    }

    //br.BaseStream.Seek(0xC, SeekOrigin.Current); //skip header data

    ReadUTFData(br);

    ETOC_packet = utf_packet;

    FileEntry etoc_entry;

    for (auto file : fileTable)
    {
        if (strcmp(file.FileName.c_str(), "ETOC_HDR"))
        {
            etoc_entry = file;
        }
    }
    etoc_entry.Encrypted = isUtfEncrypted;
    etoc_entry.FileSize = ETOC_packet.size();

    Common::MemoryReadStream ms(utf_packet.data(), utf_packet.size());

    if (!files.ReadUTF(ms, false))
    {
        return false;
    }

    List<FileEntry> fileEntries;
    for (auto file : fileTable)
    {
        if (strcmp(file.FileType.c_str(), "FILE"))
        {
            fileEntries.push_back(file);
        }
    }

    for (int i = 0; i < fileEntries.size(); i++)
    {
        fileTable[i].LocalDir = GetColumnData(files, i, "LocalDir").getValue.s;
        auto tUpdateDateTime = GetColumnData(files, i, "UpdateDateTime");
        if (tUpdateDateTime.type == Type::T_NULL)
            tUpdateDateTime = 0;
        fileTable[i].UpdateDateTime = (uint64)tUpdateDateTime.getValue.u64;
    }

    return true;
}

void CPKFile::ReadUTFData(Common::SeekableReadStream &br)
{
    isUtfEncrypted = false;

    unk1 = br.readSint32LE();
    utf_size = br.readSint64LE();
    utf_packet = List<byte>(utf_size);
    br.read(utf_packet.data(), (int)utf_size);

    if (utf_packet[0] != 0x40 && utf_packet[1] != 0x55 && utf_packet[2] != 0x54 && utf_packet[3] != 0x46) //@UTF
    {
        utf_packet = DecryptUTF(utf_packet);
        isUtfEncrypted = true;
    }
}

int CPKFile::compareInt(const void *a, const void *b)
{
    if ((*(const int *)a) < (*(const int *)b))
        return -1;
    else if ((*(const int *)a) > (*(const int *)b))
        return 1;
    else
        return 0;
}

bool CPKFile::ReadITOC(Common::SeekableReadStream &br, uint64 startoffset, uint64 ContentOffset, uint16 Align)

{
    br.seek((int64)startoffset);

    char itocMagic[5];
    br.read(itocMagic, 4);
    itocMagic[4] = 0;

    if (!strcmp(itocMagic, "ITOC"))
    {
        return false;
    }

    ReadUTFData(br);

    ITOC_packet = utf_packet;

    FileEntry itoc_entry;
    for (auto file : fileTable)
    {
        if (strcmp(file.FileName.c_str(), "ITOC_HDT"))
        {
            itoc_entry = file;
        }
    }

    itoc_entry.Encrypted = isUtfEncrypted;
    itoc_entry.FileSize = ITOC_packet.size();

    Common::MemoryReadStream ms(utf_packet.data(), utf_packet.size());

    if (!files.ReadUTF(ms, false))
    {
        return false;
    }

    List<byte> DataL = GetColumnData(files, 0, "DataL").getValue.data;

    int64 DataLPos = GetColumnPostion(files, 0, "DataL");
    List<byte> DataH = GetColumnData(files, 0, "DataH").getValue.data;

    int64 DataHPos = GetColumnPostion(files, 0, "DataH");

    //MemoryStream ms;
    //EndianReader ir;
    UTF utfDataL, utfDataH;
    Dictionary<int, uint> SizeTable, CSizeTable;
    Dictionary<int, int64> SizePosTable, CSizePosTable;
    Dictionary<int, Type> SizeTypeTable, CSizeTypeTable;

    SortedList<int> IDs(compareInt);

    uint16 ID, size1;
    uint size2;
    int64 pos;
    Type type;

    if (DataL.size() != 0)
    {
        Common::MemoryReadStream ms(DataL.data(), DataL.size());
        utfDataL.ReadUTF(ms, false);

        for (int i = 0; i < utfDataL.num_rows; i++)
        {
            ID = (uint16)GetColumnData(utfDataL, i, "ID").getValue.u16;
            size1 = (uint16)GetColumnData(utfDataL, i, "FileSize").getValue.u16;
            SizeTable[(int)ID] = ((uint)size1);

            pos = GetColumnPostion(utfDataL, i, "FileSize");
            SizePosTable[(int)ID] = (pos + DataLPos);

            type = GetColumnType(utfDataL, i, "FileSize");
            SizePosTable[(int)ID] = (int64)type;

            if ((GetColumnData(utfDataL, i, "ExtractSize")).type != Type::T_NULL)
            {
                size1 = (uint16)GetColumnData(utfDataL, i, "ExtractSize").getValue.u16;
                CSizeTable[(int)ID] = (uint)size1;

                pos = GetColumnPostion(utfDataL, i, "ExtractSize");
                CSizePosTable[(int)ID] = (pos + DataLPos);

                type = GetColumnType(utfDataL, i, "ExtractSize");
                CSizeTypeTable[(int)ID] = (type);
            }
            IDs.insert((int)ID);
        }
    }

    if (DataH.size() != 0)
    {
        auto _ms = Common::MemoryReadStream(DataH.data(), DataH.size());
        utfDataH.ReadUTF(_ms, false);

        for (int i = 0; i < utfDataH.num_rows; i++)
        {
            ID = (uint16)GetColumnData(utfDataH, i, "ID").getValue.u16;
            size2 = (uint)GetColumnData(utfDataH, i, "FileSize").getValue.u16;
            SizeTable[ID] = (size2);

            pos = GetColumnPostion(utfDataH, i, "FileSize");
            SizePosTable[(int)ID] = (pos + DataHPos);

            type = GetColumnType(utfDataH, i, "FileSize");
            SizeTypeTable[(int)ID] = (type);

            if ((GetColumnData(utfDataH, i, "ExtractSize")).type != Type::T_NULL)
            {
                size2 = (uint)GetColumnData(utfDataH, i, "ExtractSize").getValue.u32;
                CSizeTable[ID] = (size2);

                pos = GetColumnPostion(utfDataH, i, "ExtractSize");
                CSizePosTable[(int)ID] = (pos + DataHPos);

                type = GetColumnType(utfDataH, i, "ExtractSize");
                CSizeTypeTable[(int)ID] = (type);
            }

            IDs.insert(ID);
        }
    }

    FileEntry temp;
    //int id = 0;
    uint value = 0, value2 = 0;
    uint64 baseoffset = ContentOffset;

    for (auto id : IDs)
    {

        auto value = SizeTable[id];
        auto value2 = CSizeTable[id];

        temp.TOCName = "ITOC";

        temp.DirName = "";
        temp.FileName = id + ".bin";

        temp.FileSize = value;
        temp.FileSizePos = SizePosTable[id];
        temp.FileSizeType = SizeTypeTable[id];

        if (CSizeTable.size() > 0 && CSizeTable.contains(id))
        {
            temp.ExtractSize = value2;
            temp.ExtractSizePos = CSizePosTable[id];
            temp.ExtractSizeType = CSizeTypeTable[id];
        }

        temp.FileType = "FILE";

        temp.FileOffset = baseoffset;
        temp.ID = id;
        temp.UserString = "";

        fileTable.push_back(temp);

        if ((value % Align) > 0)
            baseoffset += value + (Align - (value % Align));
        else
            baseoffset += value;

        //id++;
    }

    files = UTF();
    utfDataL = UTF();
    utfDataH = UTF();

    return true;
}
CPKFile::CPKFile(Common::SeekableReadStream *ms)
{
    isUtfEncrypted = false;

    ReadCPKFile(*ms);
}

object TypeData::GetValue()
{
    switch (this->type)
    {
    case (int)E_StructTypes::DATA_TYPE_UINT8:
    case (int)E_StructTypes::DATA_TYPE_UINT8_1:

        return object(this->_uint8);

    case (int)E_StructTypes::DATA_TYPE_UINT16:
    case (int)E_StructTypes::DATA_TYPE_UINT16_1:
        return object(this->_uint16);

    case (int)E_StructTypes::DATA_TYPE_UINT32:
    case (int)E_StructTypes::DATA_TYPE_UINT32_1:
        return object(this->_uint32);

    case (int)E_StructTypes::DATA_TYPE_UINT64:
    case (int)E_StructTypes::DATA_TYPE_UINT64_1:
        return object(this->_uint64);

    case (int)E_StructTypes::DATA_TYPE_FLOAT:
        return object(this->ufloat);

    case (int)E_StructTypes::DATA_TYPE_STRING:
        return object(this->str);

    case (int)E_StructTypes::DATA_TYPE_BYTEARRAY:
        return object(this->data);
    }
}

Type TypeData::GetType()
{
    object result(-1);

    switch (this->type)
    {
    case (int)E_StructTypes::DATA_TYPE_UINT8:
    case (int)E_StructTypes::DATA_TYPE_UINT8_1:
        return Type::T_byte;

    case (int)E_StructTypes::DATA_TYPE_UINT16:
    case (int)E_StructTypes::DATA_TYPE_UINT16_1:
        return Type::T_uint16;

    case (int)E_StructTypes::DATA_TYPE_UINT32:
    case (int)E_StructTypes::DATA_TYPE_UINT32_1:
        return Type::T_uint32;

    case (int)E_StructTypes::DATA_TYPE_UINT64:
    case (int)E_StructTypes::DATA_TYPE_UINT64_1:
        return Type::T_uint64;

    case (int)E_StructTypes::DATA_TYPE_FLOAT:
        return Type::T_float;

    case (int)E_StructTypes::DATA_TYPE_STRING:
        return Type::T_string;

    case (int)E_StructTypes::DATA_TYPE_BYTEARRAY:
        return Type::T_ListByte;
    }
}

void TypeData::UpdateTypedData(Common::SeekableReadStream &br, int flags, int64 strings_offset, int64 data_offset, bool LE)
{
    int type = flags & (int)UTF::COLUMN_FLAGS::TYPE_MASK;
    this->type = type;
    this->position = br.pos();
    switch (type)
    {
    case (int)E_StructTypes::DATA_TYPE_UINT8:
    case (int)E_StructTypes::DATA_TYPE_UINT8_1:
        this->_uint8 = br.readByte();
        break;
    case (int)E_StructTypes::DATA_TYPE_UINT16:
    case (int)E_StructTypes::DATA_TYPE_UINT16_1:

        if (LE)
        {
            this->_uint16 = br.readUint16LE();
        }
        else
        {
            this->_uint16 = br.readUint16BE();
        }
        break;

    case (int)E_StructTypes::DATA_TYPE_UINT32:
    case (int)E_StructTypes::DATA_TYPE_UINT32_1:

        if (LE)
        {
            this->_uint32 = br.readUint32LE();
        }
        else
        {
            this->_uint32 = br.readUint32BE();
        }
        break;

    case (int)E_StructTypes::DATA_TYPE_UINT64:
    case (int)E_StructTypes::DATA_TYPE_UINT64_1:

        if (LE)
        {
            this->_uint64 = br.readUint64LE();
        }
        else
        {
            this->_uint64 = br.readUint64BE();
        }

        break;

    case (int)E_StructTypes::DATA_TYPE_FLOAT:

        if (LE)
        {
            this->ufloat = br.readFloatLE();
        }
        else
        {
            this->ufloat = br.readFloatBE();
        }
        break;

    case 0xA:
        int64 offset;

        if (LE)
        {
            offset = br.readSint32LE() + strings_offset;
        }
        else
        {
            offset = br.readSint32BE() + strings_offset;
        }
        auto backup = br.pos();
        br.seek(offset);
        this->str = br.readString();
        br.seek(backup);

        break;

    case (int)E_StructTypes::DATA_TYPE_BYTEARRAY:
        int64 offset;
        int size;
        if (LE)
        {
            offset = br.readSint32LE() + data_offset;
            size = br.readSint32LE();
        }
        else
        {
            offset = br.readSint32BE() + data_offset;
            size = br.readSint32BE();
        }
        this->position = position;

        Common::Array<byte> result(size);
        int64 backup = br.pos();
        br.seek(offset);
        br.read(result.data(), size);
        br.seek(backup);
        this->data = result;
        break;
    }
}

bool UTF::ReadUTF(Common::SeekableReadStream &br, bool LE)
{
    int64 offset = br.pos();

    char utfMagic[5];
    br.read(utfMagic, 4);
    utfMagic[4] = 0;

    if (!strcmp(utfMagic, "@UTF"))
    {
        return false;
    }

    table_size = br.readSint32(LE);

    rows_offset = br.readSint32(LE);

    strings_offset = br.readSint32(LE);

    data_offset = br.readSint32(LE);

    // CPK Header & UTF Header are ignored, so add 8 to each offset
    rows_offset += (offset + 8);
    strings_offset += (offset + 8);
    data_offset += (offset + 8);

    table_name = br.readSint32(LE);

    num_columns = br.readSint16(LE);
    row_length = br.readSint16(LE);
    num_rows = br.readSint32(LE);

    //read Columns
    COLUMN column;

    for (int i = 0; i < num_columns; i++)
    {
        column.flags = br.readByte();
        if (column.flags == 0)
        {
            br.seek(3, br.pos());
            column.flags = br.readByte();
        }
        auto offset = (int64)(br.readSint32(LE)) + strings_offset;
        auto backup = br.pos();
        br.seek(offset);
        column.name = br.readString();
        br.seek(backup);
        if ((column.flags & (int)UTF::COLUMN_FLAGS::STORAGE_MASK) == (int)UTF::COLUMN_FLAGS::STORAGE_CONSTANT)
        {
            column.UpdateTypedData(br, column.flags, strings_offset, data_offset);
        }
        columns.push_back(column);
    }

    //read Rows

    ROWS current_entry;
    ROW current_row;
    int storage_flag;

    for (int j = 0; j < num_rows; j++)
    {
        br.seek(rows_offset + (j * row_length));

        for (int i = 0; i < num_columns; i++)
        {

            storage_flag = (columns[i].flags & (int)COLUMN_FLAGS::STORAGE_MASK);

            if (storage_flag == (int)COLUMN_FLAGS::STORAGE_NONE) // 0x00
            {
                current_entry.rows.push_back(current_row);
                continue;
            }

            if (storage_flag == (int)COLUMN_FLAGS::STORAGE_ZERO) // 0x10
            {
                current_entry.rows.push_back(current_row);
                continue;
            }

            if (storage_flag == (int)COLUMN_FLAGS::STORAGE_CONSTANT) // 0x30
            {
                current_entry.rows.push_back(current_row);
                continue;
            }
            if (storage_flag == (int)COLUMN_FLAGS::STORAGE_PERROW)
            {
                // 0x50

                current_row.type = columns[i].flags & (int)COLUMN_FLAGS::TYPE_MASK;

                current_row.position = br.pos();

                current_row.UpdateTypedData(br, columns[i].flags, strings_offset, data_offset);

                current_entry.rows.push_back(current_row);
            }
        }

        rows.push_back(current_entry);
    }

    return true;
}

} // namespace CPK
