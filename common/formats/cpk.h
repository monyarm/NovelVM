#ifndef CPK_H
#define CPK_H

#include "common/array.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"

#include "common/hashmap.h"
#include "common/memstream.h"
#include "common/stream.h"

#include "common/csharpshim.h"
#include "common/quicksort.h"

using namespace CSharpShim;
namespace Common {

enum class E_StructTypes : int {
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

enum class E_ColumnDataType {
	DATA_TYPE_BYTE = 0,
	DATA_TYPE_USHORT = 1,
	DATA_TYPE_UINT32 = 2,
	DATA_TYPE_UINT64 = 3,
};

class TypeData {
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
	Common::Array<byte> data;
	int64 position;
};

class COLUMN : public TypeData {
public:
	COLUMN(){};

	byte flags;
	string name;
};

class ROW : public TypeData {
public:
	ROW(){};
};

class ROWS {
public:
	Common::Array<ROW> rows;

	ROWS(){};
};

class FileEntry {
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
	FileEntry(const FileEntry &) = default;
	FileEntry &operator=(const FileEntry &) = default;
};

class UTF {
public:
	enum COLUMN_FLAGS : int {
		STORAGE_MASK = 0xf0,
		STORAGE_NONE = 0x00,
		STORAGE_ZERO = 0x10,
		STORAGE_CONSTANT = 0x30,
		STORAGE_PERROW = 0x50,

		TYPE_MASK = 0x0f,
	};

	Common::Array<COLUMN> columns;
	Common::Array<ROWS> rows;

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

class CPK {
public:
	CPK(const char *path);
	CPK(Common::SeekableReadStream *ms);
	bool ReadCPKFile(Common::SeekableReadStream &br);
	Common::Array<FileEntry> fileTable;
	Dictionary<string, object> cpkdata;
	UTF utf;

	bool isUtfEncrypted;
	int unk1;
	int64 utf_size;
	Common::Array<byte> utf_packet;
	Common::Array<byte> CPK_packet;
	Common::Array<byte> TOC_packet;
	Common::Array<byte> ITOC_packet;
	Common::Array<byte> ETOC_packet;
	Common::Array<byte> GTOC_packet;
	uint64 TocOffset, EtocOffset, ItocOffset, GtocOffset, ContentOffset;

	FileEntry CreateFileEntry(string _fileName, uint64 &_fileOffset, Type _fileOffsetType, int64 &_fileOffsetPos, string _tocName, string _fileType, bool encrypted);

	bool ReadTOC(Common::SeekableReadStream &br, uint64 TocOffset, uint64 ContentOffset);

	object GetColumsData(UTF utf, int row, string Name, E_ColumnDataType type);

private:
	object GetColumnData(UTF utf, int row, string pName);

	int64 GetColumnPostion(UTF utf, int row, string pName);

	Type GetColumnType(UTF utf, int row, string pName);

	uint16 get_next_bits(Common::Array<byte> input, int &offset_p, byte &bit_pool_p, int &bits_left_p, int bit_count);

	Common::Array<byte> DecompressCRILAYLA(Common::Array<byte> input, int USize);

	Common::Array<byte> DecompressLegacyCRI(Common::Array<byte> input, int USize);

	Common::Array<byte> DecryptUTF(Common::Array<byte> input);

	bool ReadGTOC(Common::SeekableReadStream &br, uint64 startoffset);

	bool ReadETOC(Common::SeekableReadStream &br, uint64 startoffset);

	void ReadUTFData(Common::SeekableReadStream &br);

	bool ReadITOC(Common::SeekableReadStream &br, uint64 startoffset, uint64 ContentOffset, uint16 Align);

	UTF files;
};

} // namespace Common

#endif
