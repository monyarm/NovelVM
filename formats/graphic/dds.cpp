#include "formats/graphic/dds.h"

namespace Format::Graphic {

DDSFile::DDSFile(const char *path) {
	debug("%s", path);
	Common::File f;
	if (f.open(path)) {
		readFile(&f);
	}
}

DDSFile::DDSFile(Common::SeekableReadStream *stream) {
	readFile(stream);
}

Graphics::TransparentSurface *DDSFile::getSurface() {
	return &_surface;
}

bool DDSFile::isValid(Common::SeekableReadStream *stream) {
	if (stream->readUint32LE() == 0x44445320 /*DDS0x20*/) {
		return true;
	}
	return false;
}

const Common::Array<Common::String> DDSFile::getFileExtensions() {
	return Common::Array<Common::String>(FileExtensions, 2);
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
	debug("%s", dat.header.ddpfPixelFormat.dwFourCC.c_str());
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

// Helper method that packs RGBA channels into a single 4 byte pixel.

unsigned long DDSFile::PackRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
	return ((r << 24) | (g << 16) | (b << 8) | a);
}

// Decompresses one block of a DXT1 texture and stores the resulting pixels at the appropriate offset in 'image'.

void DDSFile::DecompressBlockDXT1(unsigned long x, unsigned long y, unsigned long width, const unsigned char *blockStorage, uint32 *image) {
	unsigned short color0 = *reinterpret_cast<const unsigned short *>(blockStorage);
	unsigned short color1 = *reinterpret_cast<const unsigned short *>(blockStorage + 2);

	unsigned long temp;

	temp = (color0 >> 11) * 255 + 16;
	unsigned char r0 = (unsigned char)((temp / 32 + temp) / 32);
	temp = ((color0 & 0x07E0) >> 5) * 255 + 32;
	unsigned char g0 = (unsigned char)((temp / 64 + temp) / 64);
	temp = (color0 & 0x001F) * 255 + 16;
	unsigned char b0 = (unsigned char)((temp / 32 + temp) / 32);

	temp = (color1 >> 11) * 255 + 16;
	unsigned char r1 = (unsigned char)((temp / 32 + temp) / 32);
	temp = ((color1 & 0x07E0) >> 5) * 255 + 32;
	unsigned char g1 = (unsigned char)((temp / 64 + temp) / 64);
	temp = (color1 & 0x001F) * 255 + 16;
	unsigned char b1 = (unsigned char)((temp / 32 + temp) / 32);

	unsigned long code = *reinterpret_cast<const unsigned long *>(blockStorage + 4);

	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			unsigned long finalColor = 0;
			unsigned char positionCode = (code >> 2 * (4 * j + i)) & 0x03;

			if (color0 > color1) {
				switch (positionCode) {
				case 0:
					finalColor = PackRGBA(r0, g0, b0, 255);
					break;
				case 1:
					finalColor = PackRGBA(r1, g1, b1, 255);
					break;
				case 2:
					finalColor = PackRGBA((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, 255);
					break;
				case 3:
					finalColor = PackRGBA((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, 255);
					break;
				}
			} else {
				switch (positionCode) {
				case 0:
					finalColor = PackRGBA(r0, g0, b0, 255);
					break;
				case 1:
					finalColor = PackRGBA(r1, g1, b1, 255);
					break;
				case 2:
					finalColor = PackRGBA((r0 + r1) / 2, (g0 + g1) / 2, (b0 + b1) / 2, 255);
					break;
				case 3:
					finalColor = PackRGBA(0, 0, 0, 255);
					break;
				}
			}

			if (x + i < width)
				image[(y + j) * width + (x + i)] = finalColor;
		}
	}
}

// Decompresses all the blocks of a DXT1 compressed texture and stores the resulting pixels in 'image'.

void DDSFile::BlockDecompressImageDXT1(unsigned long width, unsigned long height, const unsigned char *blockStorage, uint32 *image) {
	unsigned long blockCountX = (width + 3) / 4;
	unsigned long blockCountY = (height + 3) / 4;
	unsigned long blockWidth = (width < 4) ? width : 4;
	unsigned long blockHeight = (height < 4) ? height : 4;

	for (unsigned long j = 0; j < blockCountY; j++) {
		for (unsigned long i = 0; i < blockCountX; i++)
			DecompressBlockDXT1(i * 4, j * 4, width, blockStorage + i * 8, image);
		blockStorage += blockCountX * 8;
	}
}

// Decompresses one block of a DXT5 texture and stores the resulting pixels at the appropriate offset in 'image'.

void DDSFile::DecompressBlockDXT5(unsigned long x, unsigned long y, unsigned long width, const unsigned char *blockStorage, uint32 *image) {
	unsigned char alpha0 = *reinterpret_cast<const unsigned char *>(blockStorage);
	unsigned char alpha1 = *reinterpret_cast<const unsigned char *>(blockStorage + 1);

	const unsigned char *bits = blockStorage + 2;
	unsigned long alphaCode1 = bits[2] | (bits[3] << 8) | (bits[4] << 16) | (bits[5] << 24);
	unsigned short alphaCode2 = bits[0] | (bits[1] << 8);

	unsigned short color0 = *reinterpret_cast<const unsigned short *>(blockStorage + 8);
	unsigned short color1 = *reinterpret_cast<const unsigned short *>(blockStorage + 10);

	unsigned long temp;

	temp = (color0 >> 11) * 255 + 16;
	unsigned char r0 = (unsigned char)((temp / 32 + temp) / 32);
	temp = ((color0 & 0x07E0) >> 5) * 255 + 32;
	unsigned char g0 = (unsigned char)((temp / 64 + temp) / 64);
	temp = (color0 & 0x001F) * 255 + 16;
	unsigned char b0 = (unsigned char)((temp / 32 + temp) / 32);

	temp = (color1 >> 11) * 255 + 16;
	unsigned char r1 = (unsigned char)((temp / 32 + temp) / 32);
	temp = ((color1 & 0x07E0) >> 5) * 255 + 32;
	unsigned char g1 = (unsigned char)((temp / 64 + temp) / 64);
	temp = (color1 & 0x001F) * 255 + 16;
	unsigned char b1 = (unsigned char)((temp / 32 + temp) / 32);

	unsigned long code = *reinterpret_cast<const unsigned long *>(blockStorage + 12);

	for (int j = 0; j < 4; j++) {
		for (int i = 0; i < 4; i++) {
			int alphaCodeIndex = 3 * (4 * j + i);
			int alphaCode;

			if (alphaCodeIndex <= 12) {
				alphaCode = (alphaCode2 >> alphaCodeIndex) & 0x07;
			} else if (alphaCodeIndex == 15) {
				alphaCode = (alphaCode2 >> 15) | ((alphaCode1 << 1) & 0x06);
			} else // alphaCodeIndex >= 18 && alphaCodeIndex <= 45
			{
				alphaCode = (alphaCode1 >> (alphaCodeIndex - 16)) & 0x07;
			}

			unsigned char finalAlpha;
			if (alphaCode == 0) {
				finalAlpha = alpha0;
			} else if (alphaCode == 1) {
				finalAlpha = alpha1;
			} else {
				if (alpha0 > alpha1) {
					finalAlpha = ((8 - alphaCode) * alpha0 + (alphaCode - 1) * alpha1) / 7;
				} else {
					if (alphaCode == 6)
						finalAlpha = 0;
					else if (alphaCode == 7)
						finalAlpha = 255;
					else
						finalAlpha = ((6 - alphaCode) * alpha0 + (alphaCode - 1) * alpha1) / 5;
				}
			}

			unsigned char colorCode = (code >> 2 * (4 * j + i)) & 0x03;

			unsigned long finalColor;
			switch (colorCode) {
			case 0:
				finalColor = PackRGBA(r0, g0, b0, finalAlpha);
				break;
			case 1:
				finalColor = PackRGBA(r1, g1, b1, finalAlpha);
				break;
			case 2:
				finalColor = PackRGBA((2 * r0 + r1) / 3, (2 * g0 + g1) / 3, (2 * b0 + b1) / 3, finalAlpha);
				break;
			case 3:
				finalColor = PackRGBA((r0 + 2 * r1) / 3, (g0 + 2 * g1) / 3, (b0 + 2 * b1) / 3, finalAlpha);
				break;
			}

			if (x + i < width)
				image[(y + j) * width + (x + i)] = finalColor;
		}
	}
}

// Decompresses all the blocks of a DXT5 compressed texture and stores the resulting pixels in 'image'.

void DDSFile::BlockDecompressImageDXT5(unsigned long width, unsigned long height, const unsigned char *blockStorage, uint32 *image) {
	unsigned long blockCountX = (width + 3) / 4;
	unsigned long blockCountY = (height + 3) / 4;
	unsigned long blockWidth = (width < 4) ? width : 4;
	unsigned long blockHeight = (height < 4) ? height : 4;

	for (unsigned long j = 0; j < blockCountY; j++) {
		for (unsigned long i = 0; i < blockCountX; i++)
			DecompressBlockDXT5(i * 4, j * 4, width, blockStorage + i * 16, image);
		blockStorage += blockCountX * 16;
	}
}

} // namespace Format::Graphic