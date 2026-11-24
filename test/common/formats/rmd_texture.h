#include <cxxtest/TestSuite.h>

#include "common/formats/rmd.h"
#include "common/memstream.h"
#include "graphics/ps2texture.h"
#include "graphics/surface.h"

class RMDTextureTestSuite : public CxxTest::TestSuite {
private:
	static void append32(Common::Array<byte> &out, uint32 value) {
		out.push_back((byte)value); out.push_back((byte)(value >> 8));
		out.push_back((byte)(value >> 16)); out.push_back((byte)(value >> 24));
	}

	static void append64(Common::Array<byte> &out, uint64 value) {
		append32(out, (uint32)value); append32(out, (uint32)(value >> 32));
	}

	static Common::Array<byte> node(uint32 type, const Common::Array<byte> &payload) {
		Common::Array<byte> out;
		append32(out, type); append32(out, payload.size()); append32(out, 0x1C020037);
		for (uint32 i = 0; i < payload.size(); ++i) out.push_back(payload[i]);
		return out;
	}

	static void appendNode(Common::Array<byte> &out, uint32 type, const Common::Array<byte> &payload) {
		Common::Array<byte> child = node(type, payload);
		for (uint32 i = 0; i < child.size(); ++i) out.push_back(child[i]);
	}

	static Common::Array<byte> swizzle8(const Common::Array<byte> &linear, int w, int h) {
		Common::Array<byte> out;
		out.resize(linear.size());
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				const int block = (y & ~0xF) * w + (x & ~0xF) * 2;
				const int swap = (((y + 2) >> 2) & 1) * 4;
				const int positionY = (((y & ~3) >> 1) + (y & 1)) & 7;
				const int column = positionY * w * 2 + ((x + swap) & 7) * 4;
				const int byteNumber = ((y >> 1) & 1) + ((x >> 2) & 2);
				out[block + column + byteNumber] = linear[y * w + x];
			}
		}
		return out;
	}

	static Common::Array<byte> makeRmd(Graphics::PS2PixelFormat fmt, int w, int h,
			bool shortRaster = false, bool shortPalette = false, int storageWidth = 0,
			bool malformedNative = false, bool swizzledDirect = false, byte paletteAlpha = 0x40) {
		const bool indexed = Graphics::ps2IsIndexed(fmt);
		const int colors = Graphics::ps2ClutColorCount(fmt);
		if (storageWidth == 0) {
			// PSMT8 real payloads pad to a 64-texel row stride; a real
			// PSMT4 sample (P3F PS02B.RMD's p_nrkso_t04, byte-exact
			// verified against AmicitiaLibrary) showed the swizzle stride
			// is the logical width, not a TEX0.TBW-derived padded one --
			// the TBW field doesn't reliably describe PSMT4 row padding.
			// The odd-pixel-count PSMT4 case is real-decode-rejected before
			// the swizzle stride even matters, so it keeps the padded
			// construction purely to stay in-bounds while building it.
			const bool oddPsmt4 = fmt == Graphics::kPSMT4 && ((w * h) & 1);
			storageWidth = (indexed && (fmt != Graphics::kPSMT4 || oddPsmt4)) ? ((w + 63) / 64) * 64 : w;
		}
		const int storageTexels = storageWidth * h;
		const int texelBytes = indexed ? Graphics::ps2TexelDataSize(fmt, storageWidth, h) : Graphics::ps2TexelDataSize(fmt, w, h);
		Common::Array<byte> linear;
		linear.resize(indexed ? storageTexels : w * h);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				linear[y * (indexed ? storageWidth : w) + x] = (byte)((y * w + x) % (indexed ? colors : 256));
		Common::Array<byte> raster = (indexed && !((fmt == Graphics::kPSMT4) && ((w * h) & 1))) ? swizzle8(linear, storageWidth, h) : linear;
		Common::Array<byte> data;
		for (int i = 0; i < 16; ++i) data.push_back(0);
		// Both indexed and direct-color rasters carry the same 16-byte
		// local header + 64-byte reserved block (a full 0x50-byte
		// sceGsLoadImage/PS2StandardImageHeader) before the texel payload,
		// matching AmicitiaLibrary and the palette section below.
		for (int i = 0; i < 64; ++i) data.push_back(0);
		if (fmt == Graphics::kPSMT4) {
			Common::Array<byte> packed;
			for (int i = 0; i < storageTexels; i += 2)
				packed.push_back((byte)((raster[i] & 0x0F) | ((raster[i + 1] & 0x0F) << 4)));
			for (uint32 i = 0; i < packed.size(); ++i) data.push_back(packed[i]);
		} else if (indexed) {
			for (uint32 i = 0; i < raster.size(); ++i) data.push_back(raster[i]);
		}
		if (!shortRaster) {
			if (indexed) {
				for (int i = 0; i < 16 + 64; ++i) data.push_back(0);
				for (int i = 0; i < colors; ++i) {
					int paletteIndex = i;
					if (fmt == Graphics::kPSMT8 && i >= 8 && i < 24)
						paletteIndex = i < 16 ? i + 8 : i - 8;
					data.push_back((byte)paletteIndex);
					data.push_back(0x40);
					data.push_back(0x80);
					data.push_back(paletteAlpha);
				}
			} else if (fmt == Graphics::kPSMCT32) {
				for (int i = 0; i < w * h; ++i) {
					data.push_back((byte)(10 + i)); data.push_back(20); data.push_back(30); data.push_back((byte)(40 + i));
				}
			} else if (fmt == Graphics::kPSMCT24) {
				for (int i = 0; i < w * h; ++i) {
					data.push_back((byte)(10 + i)); data.push_back(20); data.push_back(30); data.push_back(0xEE);
				}
			} else {
				for (int i = 0; i < w * h; ++i) {
					uint16 value = (uint16)(1 | (2 << 5) | (3 << 10) | (1 << 15));
					data.push_back((byte)value); data.push_back((byte)(value >> 8));
				}
			}
		}
		const int declaredTexelLength = 16 + 64 + texelBytes;
		if (shortPalette && indexed && data.size() >= (uint32)(declaredTexelLength + 16 + 64 + colors * 4))
			data.resize(declaredTexelLength + 16 + 64 + colors * 4 - 1);

		Common::Array<byte> info;
		append32(info, w); append32(info, h); append32(info, fmt == Graphics::kPSMT4 ? 4 : (indexed ? 8 : (fmt == Graphics::kPSMCT16 || fmt == Graphics::kPSMCT16S ? 16 : (fmt == Graphics::kPSMCT24 ? 24 : 32))));
		uint32 rasterFormat = indexed ? (fmt == Graphics::kPSMT4 ? 0x24504 : 0x22504) : 0x20504;
		if (swizzledDirect) rasterFormat |= 0x10000;
		append32(info, rasterFormat);
		// TBW only needs to satisfy the >=width/nonzero sanity check in
		// decodeTexture(); it no longer drives the actual swizzle stride
		// for PSMT4 (see storageWidth above), so pad it independently.
		const int tbwWidth = indexed ? ((w + 63) / 64) * 64 : storageWidth;
		uint64 tex0 = ((uint64)fmt << 20) | ((uint64)(tbwWidth / 64) << 14) | ((uint64)2 << 26) | ((uint64)2 << 30);
		append64(info, tex0);
		for (int i = 0; i < 6; ++i) append32(info, 0);
		append32(info, declaredTexelLength);
		append32(info, indexed ? 16 + 64 + colors * 4 : 0);
		append32(info, 0); append32(info, 1);

		Common::Array<byte> rasterStruct;
		appendNode(rasterStruct, 1, info);
		appendNode(rasterStruct, 1, data);
		Common::Array<byte> native;
		Common::Array<byte> textureStruct;
		append32(textureStruct, 0x00325350); append32(textureStruct, 0x00000211);
		appendNode(native, 1, textureStruct);
		Common::Array<byte> name;
		name.push_back('t'); name.push_back(0); appendNode(native, 2, name);
		Common::Array<byte> mask; mask.push_back(0); appendNode(native, 2, mask);
		appendNode(native, 1, rasterStruct);
		Common::Array<byte> extension;
		appendNode(native, 3, extension);
		if (malformedNative) {
			append32(native, 1); append32(native, 0xFFFFFFFF); append32(native, 0x1C020037);
		}
		Common::Array<byte> dictionaryStruct;
		append32(dictionaryStruct, 1);
		Common::Array<byte> dictionary;
		appendNode(dictionary, 1, dictionaryStruct);
		appendNode(dictionary, 21, native);
		Common::Array<byte> file;
		appendNode(file, 22, dictionary);
		return file;
	}

	static Common::RMD parse(const Common::Array<byte> &data) {
		Common::MemoryReadStream stream(data.data(), data.size(), DisposeAfterUse::NO);
		return Common::RMD(&stream);
	}

	static void freeSurface(Graphics::Surface *surface) {
		if (surface) { surface->free(); delete surface; }
	}

public:
	void testDecodesSwizzledPsmT8PaletteAndAlpha() {
		Common::RMD rmd = parse(makeRmd(Graphics::kPSMT8, 16, 16));
		TS_ASSERT_EQUALS(rmd.textures()[0].horizontalAddressing, 2u);
		TS_ASSERT_EQUALS(rmd.textures()[0].verticalAddressing, 0u);
		Graphics::Surface *surface = rmd.getTextureSurface(0);
		TS_ASSERT(surface);
		if (!surface) return;
		TS_ASSERT_EQUALS((int)surface->w, 16);
		TS_ASSERT_EQUALS((int)surface->h, 16);
		const byte *pixel = (const byte *)surface->getBasePtr(3, 2);
		TS_ASSERT_EQUALS((int)pixel[2], 35);
		TS_ASSERT_EQUALS((int)pixel[1], 64);
		TS_ASSERT_EQUALS((int)pixel[0], 128);
		TS_ASSERT_EQUALS((int)pixel[3], 127); // 0x40 raw, scaled half-range: (64/128)*255
		freeSurface(surface);
	}

	// Palette alpha is a PS2 GS half-range value (0..128 == 0.0..1.0) and
	// must be scaled to the surface's full 0..255 alpha, not preserved raw.
	void testRMDScalesPaletteAlphaHalfRange() {
		Common::RMD rmd = parse(makeRmd(Graphics::kPSMT8, 16, 16, false, false, 0, false, false, 0x80));
		Graphics::Surface *surface = rmd.getTextureSurface(0);
		TS_ASSERT(surface);
		if (!surface) return;
		const byte *pixel = (const byte *)surface->getPixels();
		TS_ASSERT_EQUALS((int)pixel[3], 255); // 0x80 raw is fully opaque, scales/clamps to 255
		freeSurface(surface);
	}

	void testDecodesPaddedPsmT8UsingTex0Pitch() {
		Common::RMD rmd = parse(makeRmd(Graphics::kPSMT8, 16, 16, false, false, 64));
		Graphics::Surface *surface = rmd.getTextureSurface(0);
		TS_ASSERT(surface);
		if (!surface) return;
		const byte *pixel = (const byte *)surface->getBasePtr(3, 2);
		TS_ASSERT_EQUALS((int)pixel[2], 35);
		freeSurface(surface);
	}

	void testDecodesSwizzledPsmT4RowNibbles() {
		Common::RMD rmd = parse(makeRmd(Graphics::kPSMT4, 16, 16));
		Graphics::Surface *surface = rmd.getTextureSurface(0);
		TS_ASSERT(surface);
		if (!surface) return;
		const byte *pixel = (const byte *)surface->getBasePtr(7, 3);
		TS_ASSERT_EQUALS((int)pixel[2], 7);
		TS_ASSERT_EQUALS((int)pixel[1], 64);
		TS_ASSERT_EQUALS((int)pixel[0], 128);
		freeSurface(surface);
	}

	void testDecodesDirectColorPackingAndAlpha() {
		const Graphics::PS2PixelFormat formats[] = { Graphics::kPSMCT16, Graphics::kPSMCT16S, Graphics::kPSMCT24, Graphics::kPSMCT32 };
		for (uint32 i = 0; i < 4; ++i) {
			Common::RMD rmd = parse(makeRmd(formats[i], 2, 1));
			Graphics::Surface *surface = rmd.getTextureSurface(0);
			TS_ASSERT(surface);
			if (!surface) continue;
			const byte *pixel = (const byte *)surface->getPixels();
			if (formats[i] == Graphics::kPSMCT16 || formats[i] == Graphics::kPSMCT16S) {
				TS_ASSERT_EQUALS((int)pixel[2], 8);
				TS_ASSERT_EQUALS((int)pixel[1], 16);
				TS_ASSERT_EQUALS((int)pixel[0], 24);
				TS_ASSERT_EQUALS((int)pixel[3], 255);
			} else {
				// Raw payload is R,G,B,(A): surface is BGRA so R/B swap on
				// write, and PSMCT32 alpha is GS half-range (doubled).
				TS_ASSERT_EQUALS((int)pixel[2], 10);
				TS_ASSERT_EQUALS((int)pixel[1], 20);
				TS_ASSERT_EQUALS((int)pixel[0], 30);
				TS_ASSERT_EQUALS((int)pixel[3], formats[i] == Graphics::kPSMCT32 ? 79 : 255);
			}
			freeSurface(surface);
		}
	}

	void testDecodesDirectColor() {
		Common::RMD rmd = parse(makeRmd(Graphics::kPSMCT32, 2, 2));
		Graphics::Surface *surface = rmd.getTextureSurface(0);
		TS_ASSERT(surface);
		if (!surface) return;
		const byte *pixel = (const byte *)surface->getPixels() + 4;
		// Raw payload is R,G,B,A: surface is BGRA so R/B swap on write,
		// and alpha is GS half-range (0..0x80), doubled to 0..255.
		TS_ASSERT_EQUALS((int)pixel[0], 30);
		TS_ASSERT_EQUALS((int)pixel[1], 20);
		TS_ASSERT_EQUALS((int)pixel[2], 11);
		TS_ASSERT_EQUALS((int)pixel[3], 81);
		freeSurface(surface);
	}

	void testRejectsShortRasterAndPalette() {
		Common::RMD shortRaster = parse(makeRmd(Graphics::kPSMT8, 16, 16, true));
		TS_ASSERT(shortRaster.getTextureSurface(0) == nullptr);
		Common::RMD shortPalette = parse(makeRmd(Graphics::kPSMT8, 16, 16, false, true));
		TS_ASSERT(shortPalette.getTextureSurface(0) == nullptr);
	}

	void testRejectsOutOfRangeTextureIndex() {
		Common::RMD rmd = parse(makeRmd(Graphics::kPSMT8, 16, 16));
		TS_ASSERT(rmd.getTextureSurface(1) == nullptr);
	}

	void testRejectsOddPsmT4AndMalformedNativeNode() {
		Common::RMD odd = parse(makeRmd(Graphics::kPSMT4, 3, 1));
		TS_ASSERT(odd.getTextureSurface(0) == nullptr);
		Common::RMD malformed = parse(makeRmd(Graphics::kPSMT8, 16, 16, false, false, 0, true));
		TS_ASSERT(!malformed.ok());
		TS_ASSERT(malformed.getTextureSurface(0) == nullptr);
	}

	void testRejectsSwizzledDirectColor() {
		Common::RMD rmd = parse(makeRmd(Graphics::kPSMCT32, 2, 1, false, false, 0, false, true));
		TS_ASSERT(rmd.getTextureSurface(0) == nullptr);
	}

};
