#ifndef CPK_H
#define CPK_H

#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/debug.h"
#include "common/hex.h"

struct FileEntry
{
    Common::String DirName;
    Common::String FileName;
    int FileSize;
    int ExtractSize;
    Common::String ID;
    Common::String UserString;
    Common::String LocalDir;
    int FileOffset;

    bool Encrypted;
    Common::String FileType;
    int FileOffsetPos;
};

class CPKFile
{
public:
    CPKFile();

    void ReadFile(char* path);

private:
    Common::Array<byte> DecryptUTF(Common::Array<byte> byte);
    Common::Array<byte> ReadUTFData();
    bool isUtfEncrypted;

    Common::Array<FileEntry> fileTable;
};

#endif