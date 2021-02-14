#include "smt/formats/image/tmx.h"

void TMXFile::ReadFile(char *path)
{
    Common::File f;
    if (f.open(path))
    {
        debug("TMX");

        dat.header.tmxID = f.readUint16BE();
        dat.header.userID = f.readUint16BE();
        dat.header.length = f.readUint32BE();

        f.read(&dat.header.tmxMagic, 4);
        dat.header.pad1 = f.readUint32BE();

        dat.formatsettings.numPalettes = f.readByte();
        dat.formatsettings.paletteFmt = (PS2PixelFormat)f.readByte();
        dat.formatsettings.width = f.readUint16LE();
        dat.formatsettings.height = f.readUint16LE();
        dat.formatsettings.pixelFmt = (PS2PixelFormat)f.readByte();
        dat.formatsettings.numMipMaps = f.readByte();
        dat.formatsettings.mipKL = f.readUint16BE();
        dat.formatsettings.reserved = f.readByte();
        dat.formatsettings.wrapModes = f.readByte();
        dat.formatsettings.userTextureID = f.readUint32BE();
        dat.formatsettings.userClutID = f.readUint32LE();

        f.read(&dat.formatsettings.userComment, 28);

        Common::DumpFile pal;
        Common::DumpFile img;

        Common::hexdump(toBytes(dat.formatsettings.pixelFmt), 1);

        switch (dat.formatsettings.pixelFmt)
        {
        case PSMT4:
        {
            pal.open("dumps/pal.data", true);
            img.open("dumps/image.data", true);

            byte *_pixels = new byte[(sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height) / 2];

            f.read(_pixels, (sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height) / 2);

            Common::Array<byte> pixels;
            for (int i = 0; i < (sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height); i++)
            {

                pixels.push_back(_pixels[i] & 0x0f);
                pixels.push_back(_pixels[i] >> 4);
            }

            img.write(pixels.data(), (sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height));

            img.flush();
            img.close();
            delete _pixels;
        }
        break;
        case PSMT8:
        {
            pal.open("dumps/pal.data", true);
            img.open("dumps/image.data", true);


            byte *_palette = (byte *)malloc(256);
            f.read(_palette, 256);

            byte* palette = new byte[(sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height)];
            memcpy(palette, _palette, 256);


            int newIndex = 0;
            int oldIndex = 0;
            for (int i = 0; i < 8; i++)
            {
                for (int x = 0; x < 8; x++)
                {
                    palette[newIndex++] = _palette[oldIndex++];
                }
                oldIndex += 8;
                for (int x = 0; x < 8; x++)
                {
                    palette[newIndex++] = _palette[oldIndex++];
                }
                oldIndex -= 16;
                for (int x = 0; x < 8; x++)
                {
                    palette[newIndex++] = _palette[oldIndex++];
                }
                oldIndex += 8;
                for (int x = 0; x < 8; x++)
                {
                    palette[newIndex++] = _palette[oldIndex++];
                }
            }

            pal.write(palette,256);
            pal.flush();
            pal.close();

            byte *pixels = new byte[(sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height)];

            f.read(pixels, sizeof(PS2PixelFormat) * dat.formatsettings.width * dat.formatsettings.height);

            img.write(pixels, sizeof(PS2PixelFormat) * dat.formatsettings.width * dat.formatsettings.height);


            img.flush();
            img.close();
            delete pixels;
        }
        break;
        case PSMTC32:
            break;
        case PSMTC24:
            break;
        case PSMTC16:
            break;
        case PSMTC16S:
            break;
        case PSMT8H:
            break;
        case PSMT4HL:
            break;
        case PSMT4HH:
            break;
        case PSMZ32:
            break;
        case PSMZ24:
            break;
        case PSMZ16:
            break;
        case PSMZ16S:
            break;

        default:
            break;
        }
    }
}

TMXFile::TMXFile()
{
}
