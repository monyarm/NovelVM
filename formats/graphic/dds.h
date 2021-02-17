#ifndef DDS_H
#define DDS_H

#include "common/file-format.h"
#include "common/file.h"
#include "common/str-array.h"

#include "common/debug.h"
#include "common/fs.h"
#include "common/str.h"

#include "graphics/colormasks.h"
#include "graphics/surface.h"
#include "graphics/transparent_surface.h"

namespace Format::Graphic {

enum class dwFlag : uint32 {
	DDSD_CAPS = 0x00000001,
	DDSD_HEIGHT = 0x00000002,
	DDSD_WIDTH = 0x00000004,
	DDSD_PITCH = 0x00000008,
	DDSD_PIXELFORMAT = 0x00001000,
	DDSD_MIPMAPCOUNT = 0x00020000,
	DDSD_LINEARSIZE = 0x00080000,
	DDSD_DEPTH = 0x00800000,
};

enum class PaletteFormat : uint32 {
	DDPF_ALPHAPIXELS = 0x00000001,
	DDPF_FOURCC = 0x00000004,
	DDPF_RGB = 0x00000040
};

enum class dwCap : uint32 {
	DDSCAPS_COMPLEX = 0x00000008,
	DDSCAPS_TEXTURE = 0x00001000,
	DDSCAPS_MIPMAP = 0x00400000,
};

enum class dwCap2 : uint32 {
	DDSCAPS2_CUBEMAP = 0x00000200,
	DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400,
	DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800,
	DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000,
	DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000,
	DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000,
	DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x00008000,
	DDSCAPS2_VOLUME = 0x00200000,
};

struct DDPIXELFORMAT {
	uint32 dwSize;
	PaletteFormat dwFlags;
	Common::String dwFourCC;
	uint32 dwRGBBitCount;
	uint32 dwRBitMask;
	uint32 dwGBitMask;
	uint32 dwBBitMask;
	uint32 dwRGBAlphaBitMask;
};

struct DDCAPS2 {
	dwCap dwCaps1;
	dwCap2 dwCaps2;
	uint64 dwReserved;
};

struct DDSURFACEDESC2 {
	uint32 dwSize;  //Size of structure. This member must be set to 124.
	dwFlag dwFlags; //Flags to indicate valid fields. Always include DDSD_CAPS, DDSD_PIXELFORMAT, DDSD_WIDTH, DDSD_HEIGHT.
	uint32 dwHeight;
	uint32 dwWidth;
	uint32 dwPitchOrLinearSize; //For uncompressed formats, this is the number of bytes per scan line (DWORD> aligned) for the main image. dwFlags should include DDSD_PITCH in this case. For compressed formats, this is the total number of bytes for the main image. dwFlags should be include DDSD_LINEARSIZE in this case.
	uint32 dwDepth;             //For volume textures, this is the depth of the volume. dwFlags should include DDSD_DEPTH in this case.
	uint32 dwMipMapCount;       //For items with mipmap levels, this is the total number of levels in the mipmap chain of the main image. dwFlags should include DDSD_MIPMAPCOUNT in this case.
	uint32 dwReserverd[11];
	DDPIXELFORMAT ddpfPixelFormat;
	DDCAPS2 ddsCaps;
	uint32 dwReserved2;
};

struct DDSData {
	Common::String dwMagic;

	DDSURFACEDESC2 header;

	Common::Array<byte> MainSurfaceData;
	Common::Array<byte> AttachedSurfaceData;
};

class DDSFile : public Common::FileFormat {
public:
	DDSFile(const char *path);
	DDSFile(Common::SeekableReadStream *stream);
	~DDSFile() {
		_surface.free();
	}

	Graphics::TransparentSurface *getSurface();

	bool isValid(Common::SeekableReadStream *stream);

	const Common::String FileExtensions[2]{".dds", ".DDS"};
	const Common::Array<Common::String> getFileExtensions();

private:
	Graphics::TransparentSurface _surface;
	DDSData dat;

	void readFile(Common::SeekableReadStream *stream);

	void readHeader(Common::SeekableReadStream *f);

	void readTexture(Common::SeekableReadStream *f);

	unsigned long PackRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
	void DecompressBlockDXT1(unsigned long x, unsigned long y, unsigned long width, const unsigned char *blockStorage, uint32 *image);
	void BlockDecompressImageDXT1(unsigned long width, unsigned long height, const unsigned char *blockStorage, uint32 *image);
	void DecompressBlockDXT5(unsigned long x, unsigned long y, unsigned long width, const unsigned char *blockStorage, uint32 *image);
	void BlockDecompressImageDXT5(unsigned long width, unsigned long height, const unsigned char *blockStorage, uint32 *image);
};
} // namespace Format::Graphic

#endif
