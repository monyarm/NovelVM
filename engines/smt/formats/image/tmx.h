#ifndef TMX_H
#define TMX_H

#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/debug.h"
#include "common/hex.h"

enum PS2PixelFormat : byte
{
    PSMTC32 = 0x00,
    PSMTC24 = 0x01,
    PSMTC16 = 0x02,
    PSMTC16S = 0x0A,
    PSMT8 = 0x13,
    PSMT4 = 0x14,
    PSMT8H = 0x1B,
    PSMT4HL = 0x24,
    PSMT4HH = 0x2C,
    PSMZ32 = 0x30,
    PSMZ24 = 0x31,
    PSMZ16 = 0x32,
    PSMZ16S = 0x3A
};



struct TMXHeader
{
    uint16 tmxID;
    // tmxID is always 0x0002

    uint16 userID;
    // can be set by user

    uint32 length;
    // length of entire tmx data

    char tmxMagic[4];
    // "TMX0" or 0x30584D54

    uint32 pad1;
    // 0x00000000
};

struct TMXFormatSettings
{

    byte numPalettes;
    // typically either 0x00 or 0x01

    PS2PixelFormat paletteFmt;
    // PSMCT32, PSMCT16, PSMCT16S

    uint16 width;
    // width of image in pixels

    uint16 height;
    // width of image in pixels

    PS2PixelFormat pixelFmt;
    // PSMT8, PSMT8H, PSMT4, PSMT4HL, PSMT4HH

    byte numMipMaps;
    // number of mip maps present

    int16 mipKL;
    // if (mipKL == 0xFFFF) then values are not used
    // mipK = (mipKL & 0x0FFF) / (1 << 4)
    // mipL = (mipKL & 0xF000)
    // default mipK = -0,0625
    // default mipL = 3

    byte reserved;
    // always 0x00

    byte wrapModes;
    // if (wrapModes == 0xFF) then values are not used
    // wrapX = (wrapModes & 0xC) >> 2
    // wrapY = (wrapModes & 0x3)
    // 0 = REPEAT
    // 1 = CLAMP
    // default is REPEAT

    uint32 userTextureID;
    // can be set by user

    uint32 userClutID;
    // can be set by user

    char userComment[28];
    // can be set by user
};

struct TMXData
{
    TMXHeader header;
    TMXFormatSettings formatsettings;
};


class TMXFile
{
public:
    TMXFile();

    void ReadFile(char *path);

private:
    TMXData dat;
};

#endif