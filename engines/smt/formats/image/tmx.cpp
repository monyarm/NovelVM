#include "smt/formats/image/tmx.h"

TMXFile::TMXFile(const char *path) {

    Common::File f;
    if (f.open(path))
    {
        readHeader(&f);
        debug(path);
        debug("%04X", dat.formatsettings.paletteFmt);
        debug("%04X", dat.formatsettings.pixelFmt);

        readPalette(&f);
        readIndex(&f);
    }
}



const Graphics::Surface *TMXFile::getSurface() const
{
    return &_surface;
}

void TMXFile::readHeader(Common::File *f)
{

    dat.header.tmxID = f->readUint16BE();
    dat.header.userID = f->readUint16BE();
    dat.header.length = f->readUint32BE();

    f->read(&dat.header.tmxMagic, 4);
    dat.header.pad1 = f->readUint32BE();

    dat.formatsettings.numPalettes = f->readByte();
    dat.formatsettings.paletteFmt = (PS2PixelFormat)f->readByte();
    dat.formatsettings.width = f->readUint16LE();
    dat.formatsettings.height = f->readUint16LE();
    dat.formatsettings.pixelFmt = (PS2PixelFormat)f->readByte();
    dat.formatsettings.numMipMaps = f->readByte();
    dat.formatsettings.mipKL = f->readUint16BE();
    dat.formatsettings.reserved = f->readByte();
    dat.formatsettings.wrapModes = f->readByte();
    dat.formatsettings.userTextureID = f->readUint32BE();
    dat.formatsettings.userClutID = f->readUint32LE();

    f->read(&dat.formatsettings.userComment, 28);
}

void TMXFile::readPalette(Common::File *f)
{

    switch (dat.formatsettings.paletteFmt)
    {
    case PSMTC32:
    {

        if (dat.formatsettings.pixelFmt == PSMT8)
        {

            Common::Array<byte> _palette(256 * 4);
            f->read(_palette.data(), 256 * 4);

            Common::Array<byte> palette = tilePalette(_palette);

            dat.palette = palette.data();
        }
        else if (dat.formatsettings.pixelFmt == PSMT4)
        {
            byte *_palette = new byte[16 * 4];
            f->read(_palette, 16 * 4);
            dat.palette = _palette;
        }

        break;
    }
    default:

        error("Unrecognised palette format");
        break;
    };
}

void TMXFile::readIndex(Common::File *f)
{

    switch (dat.formatsettings.pixelFmt)
    {
    case PSMT4:
    {
        byte *_pixels = new byte[(sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height) / 2];

        f->read(_pixels, (sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height) / 2);

        Common::Array<byte> pixels;
        for (size_t i = 0; i < (sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height); i++)
        {

            pixels.push_back(_pixels[i] & 0x0f);
            pixels.push_back(_pixels[i] >> 4);
        }
        delete _pixels;
    }
    break;
    case PSMT8:
    {

        const Graphics::PixelFormat *format = new Graphics::PixelFormat(4, 8, 8, 8, 8, 0, 8, 16, 24);
        _surface.create(dat.formatsettings.width, dat.formatsettings.height,
                        *format);

        debug(format->toString().c_str());
        byte *pixels = new byte[(sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height)];
        f->read(pixels, sizeof(PS2PixelFormat) * dat.formatsettings.width * dat.formatsettings.height);

        uint32 *truepalette = (uint32 *)dat.palette;
        uint32 *destP = (uint32 *)_surface.getPixels();

        for (int i = 0; i < dat.formatsettings.width * dat.formatsettings.height; i++, ++destP)
        {
            *destP = truepalette[pixels[i]];
        }

        
        Common::DumpFile img;

        img.open("dumps/image.data", true);

        img.write(_surface.getPixels(),dat.formatsettings.width*dat.formatsettings.height*4);
        img.flush();
        img.close();
        
        delete[] pixels;
    }
    break;

    default:
        error("Unrecognised pixel format");
        break;
    }
}

Common::Array<byte> TMXFile::tilePalette(Common::Array<byte> input)
{

    Common::Array<byte> palette;

    int index = 0;
    for (int i = 0; i < 8; i++)
    {
        for (int x = 0; x < 8; x++)
        {
            palette.push_back(input[(index + x) * 4 + 0]);
            palette.push_back(input[(index + x) * 4 + 1]);
            palette.push_back(input[(index + x) * 4 + 2]);
            palette.push_back(input[(index + x) * 4 + 3]);
        }
        index += 16;

        for (int x = 0; x < 8; x++)
        {

            palette.push_back(input[(index + x) * 4 + 0]);
            palette.push_back(input[(index + x) * 4 + 1]);
            palette.push_back(input[(index + x) * 4 + 2]);
            palette.push_back(input[(index + x) * 4 + 3]);
        }
        index -= 8;

        for (int x = 0; x < 8; x++)
        {

            palette.push_back(input[(index + x) * 4 + 0]);
            palette.push_back(input[(index + x) * 4 + 1]);
            palette.push_back(input[(index + x) * 4 + 2]);
            palette.push_back(input[(index + x) * 4 + 3]);
        }
        index += 16;

        for (int x = 0; x < 8; x++)
        {

            palette.push_back(input[(index + x) * 4 + 0]);
            palette.push_back(input[(index + x) * 4 + 1]);
            palette.push_back(input[(index + x) * 4 + 2]);
            palette.push_back(input[(index + x) * 4 + 3]);
        }
        index += 8;
    }
    return palette;
}
