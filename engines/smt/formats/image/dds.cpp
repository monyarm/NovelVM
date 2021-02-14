#include "smt/formats/image/dds.h"

DDSFile::DDSFile(const char *path) {
	debug(path);
	Common::File f;
	if (f.open(path)) {
		readFile(&f);
	}
}

DDSFile::DDSFile(Common::SeekableReadStream *stream) {
	readFile(stream);
}

Graphics::TransparentSurface *const DDSFile::getSurface() {
	return &_surface;
}

bool DDSFile::isValid(Common::SeekableReadStream *stream) {
	if (stream->readUint32LE() == 0x44445320 /*DDS0x20*/) {
		return true;
	}
    return false;
}

const Common::Array<Common::String> DDSFile::getFileExtensions() {
	return Common::Array<Common::String> (FileExtensions, 2);
}

void DDSFile::readFile(Common::SeekableReadStream *stream) {
	readHeader(stream);
	readTexture(stream);
}
void DDSFile::readHeader(Common::SeekableReadStream *f) {
	dat.dwMagic = f->readFourCC();

	dat.header.dwSize = f->readUint32LE();
	dat.header.dwFlags = (dwFlag)f->readUint32LE();
	dat.header.dwHeight = f->readUint32LE();
	dat.header.dwWidth = f->readUint32LE();
	dat.header.dwPitchOrLinearSize = f->readUint32LE();
	dat.header.dwDepth = f->readUint32LE();
	dat.header.dwMipMapCount = f->readUint32LE();
	f->read(dat.header.dwReserverd, sizeof(uint32) * 11);

	dat.header.ddpfPixelFormat.dwSize = f->readUint32LE();
	dat.header.ddpfPixelFormat.dwFlags = (PaletteFormat)f->readUint32LE();
	dat.header.ddpfPixelFormat.dwFourCC = f->readFourCC();
	dat.header.ddpfPixelFormat.dwRGBBitCount = f->readUint32LE();

	dat.header.ddpfPixelFormat.dwRBitMask = f->readUint32LE();
	dat.header.ddpfPixelFormat.dwGBitMask = f->readUint32LE();
	dat.header.ddpfPixelFormat.dwBBitMask = f->readUint32LE();
	dat.header.ddpfPixelFormat.dwRGBAlphaBitMask = f->readUint32LE();

	dat.header.ddsCaps.dwCaps1 = (dwCap)f->readUint32LE();
	dat.header.ddsCaps.dwCaps2 = (dwCap2)f->readUint32LE();

	dat.header.ddsCaps.dwReserved = f->readUint64LE();
	dat.header.dwReserved2 = f->readUint32LE();
	debug(dat.header.ddpfPixelFormat.dwFourCC.c_str());
	Common::hexdump((byte *)dat.header.ddpfPixelFormat.dwFourCC.c_str(), 4);
	Common::hexdump((byte *)&dat.header.ddpfPixelFormat.dwFlags, 4);
}

void DDSFile::readTexture(Common::SeekableReadStream *f) {
	const Graphics::PixelFormat *format = new Graphics::PixelFormat(4, 8, 8, 8, 8, 0, 8, 16, 24); //24, 16, 8, 0);
	uint32 *data = new uint32[dat.header.dwHeight * dat.header.dwWidth];

	switch (dat.header.ddpfPixelFormat.dwFlags) {
	case PaletteFormat::DDPF_ALPHAPIXELS:
		error("DDPF_ALPHAPIXELS not supported");
		break;
	case PaletteFormat::DDPF_RGB: {
		uint32 size = dat.header.dwPitchOrLinearSize * dat.header.dwHeight;
		f->read(data, size * sizeof(uint32));
		break;
	}
	case PaletteFormat::DDPF_FOURCC: {
		uint32 size = dat.header.dwPitchOrLinearSize;
		Common::Array<byte> image(size);
		f->read(image.data(), size);

		if (strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "DXT1") == 0 ||
		    strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "S3TC") == 0 ||
		    strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "BC1 ") == 0) {
			BlockDecompressImageDXT1(dat.header.dwWidth, dat.header.dwWidth, image.data(), data);
		} else if (strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "DXT2") == 0 ||
		           strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "DXT3") == 0 ||
		           strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "BC2 ") == 0) {
			debug("using DXT5 decompression on DXT2/3");
			BlockDecompressImageDXT5(dat.header.dwWidth, dat.header.dwWidth, image.data(), data);
		}

		else if (strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "DXT4") == 0 ||
		         strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "DXT5") == 0 ||
		         strcmp(dat.header.ddpfPixelFormat.dwFourCC.c_str(), "BC3 ") == 0) {

			BlockDecompressImageDXT5(dat.header.dwWidth, dat.header.dwWidth, image.data(), data);
		}
		/* df.write(data, outImage.size() * sizeof(uint32));
            df.flush();
            df.close(); */

		break;
	}
	default:
		break;
	}

	Common::DumpFile df;
	df.open("dumps/dds.data", true);

	for (size_t i = 0; i < dat.header.dwWidth * dat.header.dwHeight; i++) {

		((uint32 *)data)[i] = (((uint32 *)data)[i] & 0x0000FFFF) << 16 | (((uint32 *)data)[i] & 0xFFFF0000) >> 16;
		((uint32 *)data)[i] = (((uint32 *)data)[i] & 0x00FF00FF) << 8 | (((uint32 *)data)[i] & 0xFF00FF00) >> 8;
	}
	_surface.create(dat.header.dwWidth, dat.header.dwHeight,
	                *format);

	_surface.setPixels(data);
}
