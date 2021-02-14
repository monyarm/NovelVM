#ifndef CPK_H
#define CPK_H

#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/debug.h"
#include "common/hex.h"
#include <vector>
#include <map>

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

    void ReadFile();

private:
    std::vector<byte> DecryptUTF(std::vector<byte> byte);
    std::vector<byte> ReadUTFData();
    bool isUtfEncrypted;

    std::vector<FileEntry> fileTable;
};

#endif