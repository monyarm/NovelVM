#ifndef SMT_FORMATS_TEXTURE_TMX_H
#define SMT_FORMATS_TEXTURE_TMX_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/stream.h"

#include "graphics/ps2texture.h"
#include "graphics/surface.h"

namespace SMT {

/**
 * TMX: Atlus' PS2 texture container (Persona 3/4 et al.).
 *
 * A thin container: a 0x10-byte chunk header followed by format settings,
 * an optionally CSM1-swizzled CLUT (PSMCT32/24/16/16S) and a linear index
 * bitmap (PSMT8/8H/4/4HL/4HH). Pixel/CLUT decoding is delegated to
 * Graphics::ps2* helpers shared with TM2 and other PS2 texture containers.
 *
 * Header layout is little-endian (verified against real P3/P4 corpus).
 * mipKL and wrapModes values are informational; mip levels are not decoded.
 */
class TMX {
public:
	TMX(const byte *data, uint32 size);
	TMX(Common::SeekableReadStream *stream);
	TMX(const char *path);
	~TMX() {}

	/** Decoded RGBA8888 surface, or nullptr on parse failure. */
	Graphics::Surface *getSurface();

	int width() const { return _w; }
	int height() const { return _h; }
	int mipMapCount() const { return _formatSettings.mipMapCount; }

private:
	struct FormatSettings {
		uint32 length;
		byte numPalettes;
		Graphics::PS2PixelFormat paletteFmt;
		uint16 width;
		uint16 height;
		Graphics::PS2PixelFormat pixelFmt;
		byte mipMapCount;
		int16 mipKL;
		byte wrapModes;
	} _formatSettings;

	int _w = 0;
	int _h = 0;
	Common::Array<byte> _clut;
	Common::Array<byte> _indices;
	bool _ok = false;

	void readFile(Common::SeekableReadStream *stream);
	void readHeader(Common::SeekableReadStream *s);
	void readClut(Common::SeekableReadStream *s);
	void readIndices(Common::SeekableReadStream *s);
};

} // namespace SMT

#endif
