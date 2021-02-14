#ifndef CPK_H
#define CPK_H

#include "common/file.h"
#include "common/array.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/debug.h"
#include "common/hex.h"
#include "common/stream.h"
#include "common/hashmap.h"
#include "common/memstream.h"

#include "common/quicksort.h"
#include "common/csharpshim.h"

using namespace CSharpShim;
namespace CPK
{

enum class E_StructTypes : int
{
    DATA_TYPE_UINT8 = 0,
    DATA_TYPE_UINT8_1 = 1,
    DATA_TYPE_UINT16 = 2,
    DATA_TYPE_UINT16_1 = 3,
    DATA_TYPE_UINT32 = 4,
    DATA_TYPE_UINT32_1 = 5,
    DATA_TYPE_UINT64 = 6,
    DATA_TYPE_UINT64_1 = 7,
    DATA_TYPE_FLOAT = 8,
    DATA_TYPE_STRING = 0xA,
    DATA_TYPE_BYTEARRAY = 0xB,
    DATA_TYPE_MASK = 0xf,
    DATA_TYPE_NONE = -1,
};

enum class E_ColumnDataType
{
    DATA_TYPE_BYTE = 0,
    DATA_TYPE_USHORT = 1,
    DATA_TYPE_UINT32 = 2,
    DATA_TYPE_UINT64 = 3,
};

class TypeData
{
public:
    int type = -1;
    object GetValue();

    Type GetType();

    void UpdateTypedData(Common::SeekableReadStream &br, int flags, int64 strings_offset, int64 data_offset, bool BE = true);

    //column based datatypes
    byte _uint8;
    uint16 _uint16;
    uint32 _uint32;
    uint64 _uint64;
    float ufloat;
    string str;
    List<byte> data;
    int64 position;
};

class COLUMN : public TypeData
{
public:
    COLUMN(){};

    byte flags;
    string name;
};

class ROW : public TypeData
{
public:
    ROW(){};
};

class ROWS
{
public:
    List<ROW> rows;

    ROWS(){};
};

class FileEntry
{
public:
    string DirName;  // string
    string FileName; // string

    uint FileSize;
    int64 FileSizePos;
    Type FileSizeType;

    int ExtractSize; // int
    int64 ExtractSizePos;
    Type ExtractSizeType;
    uint64 FileOffset;
    int64 FileOffsetPos;
    Type FileOffsetType;
    uint64 Offset;
    int ID;            // int
    string UserString; // string
    uint64 UpdateDateTime;
    string LocalDir; // string
    string TOCName;
    bool Encrypted;

    string FileType;

    FileEntry() = default;
    FileEntry(const CPK::FileEntry &) = default;
    FileEntry &operator=(const CPK::FileEntry &) = default;
};

class UTF
{
public:
    enum COLUMN_FLAGS : int
    {
        STORAGE_MASK = 0xf0,
        STORAGE_NONE = 0x00,
        STORAGE_ZERO = 0x10,
        STORAGE_CONSTANT = 0x30,
        STORAGE_PERROW = 0x50,

        TYPE_MASK = 0x0f,
    };

    List<COLUMN> columns;
    List<ROWS> rows;

    UTF(){};

    bool ReadUTF(Common::SeekableReadStream &br, bool LE = true);

    int table_size;

    int64 rows_offset;
    int64 strings_offset;
    int64 data_offset;
    int table_name;
    int16 num_columns;
    int16 row_length;
    int num_rows;
};

class CPKFile
{
public:
    CPKFile(const char *path);
    CPKFile(Common::SeekableReadStream *ms);
    bool ReadCPKFile(Common::SeekableReadStream &br);
    List<FileEntry> fileTable;
    Dictionary<string, object> cpkdata;
    UTF utf;

    bool isUtfEncrypted;
    int unk1;
    int64 utf_size;
    List<byte> utf_packet;
    List<byte> CPK_packet;
    List<byte> TOC_packet;
    List<byte> ITOC_packet;
    List<byte> ETOC_packet;
    List<byte> GTOC_packet;
    uint64 TocOffset, EtocOffset, ItocOffset, GtocOffset, ContentOffset;

    FileEntry CreateFileEntry(string FileName, uint64 FileOffset, Type FileOffsetType, int64 FileOffsetPos, string TOCName, string FileType, bool encrypted);

    bool ReadTOC(Common::SeekableReadStream &br, uint64 TocOffset, uint64 ContentOffset);

    object GetColumsData(UTF utf, int row, string Name, E_ColumnDataType type);

private:
    object GetColumnData(UTF utf, int row, string pName);

    int64 GetColumnPostion(UTF utf, int row, string pName);

    Type GetColumnType(UTF utf, int row, string pName);

    uint16 get_next_bits(List<byte> input, int &offset_p, byte &bit_pool_p, int &bits_left_p, int bit_count);

    List<byte> DecompressCRILAYLA(List<byte> input, int USize);

    List<byte> DecompressLegacyCRI(List<byte> input, int USize);

    List<byte> DecryptUTF(List<byte> input);

    bool ReadGTOC(Common::SeekableReadStream &br, uint64 startoffset);

    bool ReadETOC(Common::SeekableReadStream &br, uint64 startoffset);

    void ReadUTFData(Common::SeekableReadStream &br);

    bool ReadITOC(Common::SeekableReadStream &br, uint64 startoffset, uint64 ContentOffset, uint16 Align);

    UTF files;
};

} // namespace CPK

#endif