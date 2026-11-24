#ifndef GRAPHICS_PS2TEXTURE_H
#define GRAPHICS_PS2TEXTURE_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/stream.h"

#include "graphics/surface.h"

namespace Graphics {

/**
 * PlayStation 2 GS pixel storage modes (PSM).
 * These enum values are shared by every PS2 texture container (TMX, TM2/TIM2,
 * RenderWare TXD, ...) since they describe how the GS packs texels in VRAM.
 */
enum PS2PixelFormat : byte {
	kPSMCT32 = 0x00,
	kPSMCT24 = 0x01,
	kPSMCT16 = 0x02,
	kPSMCT16S = 0x0A,
	kPSMT8 = 0x13,
	kPSMT4 = 0x14,
	kPSMT8H = 0x1B,
	kPSMT4HL = 0x24,
	kPSMT4HH = 0x2C,
	kPSMZ32 = 0x30,
	kPSMZ24 = 0x31,
	kPSMZ16 = 0x32,
	kPSMZ16S = 0x3A
};

/**
 * True for indexed (CLUT) pixel formats.
 */
bool ps2IsIndexed(PS2PixelFormat fmt);

/**
 * Number of CLUT entries for an indexed format (256 for PSMT8/8H, 16 otherwise).
 */
int ps2ClutColorCount(PS2PixelFormat fmt);

/**
 * Bytes occupied by w*h texels in the given format (as stored on the GS and
 * in file payloads).
 */
int ps2TexelDataSize(PS2PixelFormat fmt, int w, int h);

/**
 * GS CSM1 CLUT swizzle: a 256-entry CLUT is stored as a 16x16 tile whose
 * colours are reordered every 0x20 bytes. Reorders the given RGBA8888 CLUT
 * in-place back to linear order. 16-entry CLUTs (4-bit) are not swizzled and
 * must not be passed here.
 */
void ps2UnswizzleClut(byte *rgba, int colorCount);

/**
 * GS 8/4-bit page-block-column tiling: de-swizzles a raw indexed raster
 * (as stored contiguously in VRAM/file, `storageWidth` texels per row) into
 * linear row-major index bytes. `storageWidth` is the row stride the raster
 * was actually packed at (TEX0.TBW-derived where padded, texture width
 * otherwise) and may differ from the logical `width`.
 */
bool ps2UnswizzleIndices(const byte *source, uint32 sourceSize, byte *destination,
		uint32 width, uint32 height, uint32 storageWidth);

/**
 * Converts a PS2 CLUT payload (colour pixels in valFmt) plus an index bitmap
 * into an RGBA8888 surface ready for rendering.
 *
 * @param clut       Clut colour data; size derived from clutColorCount/clutFmt
 * @param clutFmt    CLUT storage format (PSMCT32/24/16/16S)
 * @param clutColorCount  Number of clut entries
 * @param indices    Index bitmap payload (PSMT8/8H one byte per texel,
 *                   PSMT4/4HL/4HH two texels per byte)
 * @param texFmt     Index bitmap format
 * @param w,h        Texture size in texels
 * @param clutIsSwizzled  Whether clut payload is CSM1-swizzled (256-colour)
 * @param clutAlphaHalfRange  Scale PSMCT32 clut alpha from the half range
 *                   (0..128) used by some containers up to full 0..255
 */
Graphics::Surface *ps2CreateIndexedSurface(PS2PixelFormat clutFmt, byte *clut,
		int clutColorCount, byte *indices, PS2PixelFormat texFmt, int w, int h,
		bool clutIsSwizzled, bool clutAlphaHalfRange);

/**
 * Converts a non-indexed (direct colour) PS2 payload into an RGBA8888 surface.
 */
Graphics::Surface *ps2CreateColorSurface(PS2PixelFormat fmt, byte *data,
		int w, int h);

namespace PS2GsTex {
/**
 * GS TEX0 register (64-bit): texture layout + CLUT pointer metadata.
 * Fields extracted from the raw register value; see ps2tek / OpenKh.
 */
class Register {
public:
	explicit Register(uint64 raw) : _raw(raw) {}

	uint16 tbp0()  const { return (uint16)(_raw & 0x3FFF); }
	uint8  tbw()    const { return (uint8)((_raw >> 14) & 0x3F); }
	PS2PixelFormat psm() const { return (PS2PixelFormat)((_raw >> 20) & 0x3F); }
	uint8  tw()     const { return (uint8)((_raw >> 26) & 0x0F); }
	uint8  th()     const { return (uint8)((_raw >> 30) & 0x0F); }
	bool   tcc()    const { return (_raw >> 34) & 1; }
	uint16 cbp()    const { return (uint16)((_raw >> 37) & 0x3FFF); }
	uint8  cpsm()   const { return (uint8)((_raw >> 51) & 0x0F); }
	bool   csm2()   const { return (_raw >> 55) & 1; }
	uint8  csa()    const { return (uint8)((_raw >> 56) & 0x1F); }

private:
	uint64 _raw;
};
} // namespace PS2GsTex

/**
 * Sony TIM2 / TM2 container. Holds one or more pictures, each optionally with
 * a CLUT. See OpenKh docs/common/tm2.md.
 */
class TM2 {
public:
	TM2(Common::SeekableReadStream *stream);
	~TM2();

	int pictureCount() const { return _pictures.size(); }

	/** Decoded RGBA8888 surface for picture i (owned by caller). */
	Graphics::Surface *getSurface(int i);

	/** Raw index/colour bitmap payload for picture i (bytes in ps2 layout). */
	const byte *getData(int i) const { return _pictures[i]->data; }
	/** Raw clut payload for picture i. */
	const byte *getClut(int i) const { return _pictures[i]->clut; }
	int getDataSize(int i) const { return _pictures[i]->dataSize; }
	int getClutSize(int i) const { return _pictures[i]->clutSize; }

	int width(int i) const { return _pictures[i]->w; }
	int height(int i) const { return _pictures[i]->h; }
	byte imageType(int i) const { return _pictures[i]->imageType; }
	byte clutType(int i) const { return _pictures[i]->clutType; }

private:
	struct Picture {
		int w = 0, h = 0;
		int dataSize = 0, clutSize = 0;
		int clutCount = 0;
		byte imageType = 0, clutType = 0;
		bool clutIsSwizzled = false;
		PS2PixelFormat texFormat = kPSMCT32;
		byte *data = nullptr;
		byte *clut = nullptr;
		~Picture() {
			delete[] data;
			delete[] clut;
		}
	};
	Common::Array<Picture *> _pictures;

	void readPicture(Common::SeekableReadStream *stream);
};

} // namespace Graphics

#endif
