#include "smt/formats/graphic/tmx.h"
#include "common/file.h"

TMXFile::TMXFile(const char *path) {

	Common::File f;
	if (f.open(path)) {
		readFile(&f);
	}
}

TMXFile::TMXFile(Common::SeekableReadStream *stream) {
	readFile(stream);
}

Graphics::TransparentSurface *TMXFile::getSurface() {
	return &_surface;
}

void TMXFile::readFile(Common::SeekableReadStream *stream) {

	readHeader(stream);
	Common::hexdump((byte *)&dat.formatsettings.paletteFmt, 1);
	Common::hexdump((byte *)&dat.formatsettings.pixelFmt, 1);

	readPalette(stream);
	readIndex(stream);
}

void TMXFile::readHeader(Common::SeekableReadStream *f) {

	dat.header.tmxID = f->readUint16BE();
	dat.header.userID = f->readUint16BE();
	dat.header.length = f->readUint32BE();

	f->read(&dat.header.tmxMagic, 4);
	dat.header.pad1 = f->readUint32BE();

	dat.formatsettings.numPalettes = f->readByte();
	debug("%i", dat.formatsettings.numPalettes);
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

void TMXFile::readPalette(Common::SeekableReadStream *f) {

	switch (dat.formatsettings.paletteFmt) {
	case PSMTC32: {
		if (dat.formatsettings.pixelFmt == PSMT8) {

			Common::Array<byte> _palette(256 * 4);
			f->read(_palette.data(), 256 * 4);

			Common::Array<byte> palette = tilePalette(_palette);

			for (size_t i = 0; i < 256; i++) {
				((uint32 *)palette.data())[i] = (((uint32 *)palette.data())[i] & 0x00FF00FF) << 8 | (((uint32 *)palette.data())[i] & 0xFF00FF00) >> 8;
				((uint32 *)palette.data())[i] = (((uint32 *)palette.data())[i] & 0x0000FFFF) << 16 | (((uint32 *)palette.data())[i] & 0xFFFF0000) >> 16;
			}

			dat.palette = ((uint32 *)palette.data());
		} else if (dat.formatsettings.pixelFmt == PSMT4) {
			Common::Array<byte> _palette(16 * 4);

			f->read(_palette.data(), 16 * 4);

			for (size_t i = 0; i < 16; i++) {
				((uint32 *)_palette.data())[i] = (((uint32 *)_palette.data())[i] & 0x00FF00FF) << 8 | (((uint32 *)_palette.data())[i] & 0xFF00FF00) >> 8;
				((uint32 *)_palette.data())[i] = (((uint32 *)_palette.data())[i] & 0x0000FFFF) << 16 | (((uint32 *)_palette.data())[i] & 0xFFFF0000) >> 16;
			}

			dat.palette = ((uint32 *)_palette.data());
		}

		break;
	}
	default:

		error("Unrecognised palette format");
		break;
	};
}

void TMXFile::readIndex(Common::SeekableReadStream *f) {

	const Graphics::PixelFormat *format = new Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0);
	switch (dat.formatsettings.pixelFmt) {
	case PSMT4: {

		_surface.create(dat.formatsettings.width, dat.formatsettings.height,
		                *format);

		byte *_pixels = new byte[(sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height) / 2];

		f->read(_pixels, (sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height) / 2);

		Common::Array<byte> pixels;
		for (size_t i = 0; i < (sizeof(byte) * dat.formatsettings.width * dat.formatsettings.height); i++) {

			pixels.push_back(_pixels[i] & 0x0f);
			pixels.push_back(_pixels[i] >> 4);
		}

		uint32 *destP = (uint32 *)_surface.getPixels();

		for (int i = 0; i < dat.formatsettings.width * dat.formatsettings.height; i++, ++destP) {
			*destP = dat.palette[pixels[i]];
		}
		delete[] _pixels;
	} break;
	case PSMT8: {

		_surface.create(dat.formatsettings.width, dat.formatsettings.height,
		                *format);

		debug("%s", format->toString().c_str());
		byte *pixels = new byte[dat.formatsettings.width * dat.formatsettings.height];
		f->read(pixels, dat.formatsettings.width * dat.formatsettings.height);

		uint32 *destP = (uint32 *)_surface.getPixels();

		for (uint32 i = 0; i < (dat.formatsettings.width * dat.formatsettings.height); i++, ++destP) {
			//*destP = dat.palette[pixels[i]];
			*destP = dat.palette[pixels[i] % 256];
		}

		delete[] pixels;
	} break;

	default:
		error("Unrecognised pixel format");
		break;
	}
}

Common::Array<byte> TMXFile::tilePalette(Common::Array<byte> input) {

	Common::Array<byte> palette;

	int index = 0;
	for (int i = 0; i < 8; i++) {
		for (int x = 0; x < 8; x++) {
			palette.push_back(input[(index + x) * 4 + 0]);
			palette.push_back(input[(index + x) * 4 + 1]);
			palette.push_back(input[(index + x) * 4 + 2]);
			palette.push_back(input[(index + x) * 4 + 3]);
		}
		index += 16;

		for (int x = 0; x < 8; x++) {

			palette.push_back(input[(index + x) * 4 + 0]);
			palette.push_back(input[(index + x) * 4 + 1]);
			palette.push_back(input[(index + x) * 4 + 2]);
			palette.push_back(input[(index + x) * 4 + 3]);
		}
		index -= 8;

		for (int x = 0; x < 8; x++) {

			palette.push_back(input[(index + x) * 4 + 0]);
			palette.push_back(input[(index + x) * 4 + 1]);
			palette.push_back(input[(index + x) * 4 + 2]);
			palette.push_back(input[(index + x) * 4 + 3]);
		}
		index += 16;

		for (int x = 0; x < 8; x++) {

			palette.push_back(input[(index + x) * 4 + 0]);
			palette.push_back(input[(index + x) * 4 + 1]);
			palette.push_back(input[(index + x) * 4 + 2]);
			palette.push_back(input[(index + x) * 4 + 3]);
		}
		index += 8;
	}
	return palette;
}
