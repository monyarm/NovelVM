#include <cxxtest/TestSuite.h>

#include "smt/formats/texture/tmx.h"

#include "common/memstream.h"

#include "graphics/surface.h"

/**
 * Decode tests for the SMT engine's TMX container (Atlus PS2 texture).
 *
 * Fixtures are SYNTHETIC: built in-test from small generators, no copyrighted
 * game assets. The generator logic is written straight from the documented
 * TMX layout (little-endian header, CSM1-swizzled 256-colour CLUTs, linear
 * 16-colour CLUTs, 4-bit/8-bit index bitmaps) and is independent of the
 * production decoder.
 *
 * Expected pixels are computed from the palette each generator emits, so the
 * tests assert "decoded image == palette[index]" exactly, including the
 * half-range (0..128) to full-range alpha scale used when displaying.
 */

class TmxSyntheticHelper {
public:
	static const int kHalfRangeScale = 1;

	// CSM1 swizzle: within each 32-entry group, swap blocks [8..16) and
	// [16..24). Converting stored->linear is a self-inverse swap.
	static void swizzleClut256(byte *rgba) {
		for (int i = 0; i < 256; i += 32) {
			for (int j = 0; j < 8; j++) {
				byte tmp[4];
				memcpy(tmp, rgba + (i + 8 + j) * 4, 4);
				memcpy(rgba + (i + 8 + j) * 4, rgba + (i + 16 + j) * 4, 4);
				memcpy(rgba + (i + 16 + j) * 4, tmp, 4);
			}
		}
	}

	static void putLE16(byte *dst, uint16 v) {
		dst[0] = v & 0xFF;
		dst[1] = v >> 8;
	}

	static void putLE32(byte *dst, uint32 v) {
		dst[0] = v & 0xFF;
		dst[1] = (v >> 8) & 0xFF;
		dst[2] = (v >> 16) & 0xFF;
		dst[3] = (v >> 24) & 0xFF;
	}

	static void putLE16s(byte *dst, int16 v) { putLE16(dst, (uint16)v); }

	// Build a TMX header; returns bytes written.
	static int buildHeader(byte *out, byte numPalettes, byte palFmt,
			uint16 w, uint16 h, byte pixFmt, byte mipCount, uint32 totalLen) {
		int o = 0;
		putLE16(out + o, 0x0002); o += 2;      // tmxID
		putLE16(out + o, 0x1234); o += 2;      // userID
		putLE32(out + o, totalLen); o += 4;    // length
		memcpy(out + o, "TMX0", 4); o += 4;    // magic
		putLE32(out + o, 0); o += 4;           // pad1
		out[o++] = numPalettes;                // numPalettes
		out[o++] = palFmt;                     // paletteFmt
		putLE16(out + o, w); o += 2;           // width
		putLE16(out + o, h); o += 2;           // height
		out[o++] = pixFmt;                     // pixelFmt
		out[o++] = mipCount;                   // numMipMaps
		putLE16s(out + o, -1); o += 2;         // mipKL (unused)
		out[o++] = 0;                          // reserved
		out[o++] = 0xFF;                       // wrapModes (unused)
		putLE32(out + o, 0); o += 4;           // userTextureID
		putLE32(out + o, 0); o += 4;           // userClutID
		memset(out + o, 0, 28); o += 28;       // userComment
		TS_ASSERT_EQUALS(o, 0x40);
		return o;
	}

	// Linear 256-colour PSMCT32 clut: colour i = (i, 255-i, i, alpha(i)).
	static void buildLinearClut256(byte *out, byte stubAlpha) {
		for (int i = 0; i < 256; i++) {
			out[i * 4 + 0] = (byte)i;
			out[i * 4 + 1] = (byte)(255 - i);
			out[i * 4 + 2] = (byte)i;
			out[i * 4 + 3] = stubAlpha;
		}
	}

	// Linear 16-colour PSMCT32 clut for PSMT4.
	static void buildLinearClut16(byte *out) {
		for (int i = 0; i < 16; i++) {
			out[i * 4 + 0] = (byte)(i * 8);
			out[i * 4 + 1] = (byte)(255 - i * 8);
			out[i * 4 + 2] = 128;
			out[i * 4 + 3] = (i & 1) ? 200 : 100;
		}
	}

	static byte scaleAlpha(byte a) {
		int v = (int)((a / 128.0f) * 255);
		return v > 255 ? 255 : (byte)v;
	}
};

class TmxDecodeTestSuite : public CxxTest::TestSuite {
public:
	// PSMT8, 4x4, 256-colour CSM1-swizzled PSMTC32 clut.
	void test_psmt8_swizzled() {
		const int w = 4, h = 4;
		byte clut[256 * 4];
		TmxSyntheticHelper::buildLinearClut256(clut, 240);
		TmxSyntheticHelper::swizzleClut256(clut);

		byte idx[16];
		for (int i = 0; i < 16; i++)
			idx[i] = (byte)i;

		uint32 total = 0x40 + sizeof(clut) + sizeof(idx);
		byte file[0x40 + 256 * 4 + 16];
		int o = TmxSyntheticHelper::buildHeader(file, 1, 0x00, w, h, 0x13, 0, total);
		memcpy(file + o, clut, sizeof(clut));
		memcpy(file + o + sizeof(clut), idx, sizeof(idx));

		Common::MemoryReadStream stream(file, sizeof(file), DisposeAfterUse::NO);
		SMT::TMX tmx(&stream);
		Graphics::Surface *surf = tmx.getSurface();
		TS_ASSERT(surf);
		if (!surf)
			return;
		TS_ASSERT_EQUALS((int)surf->w, w);
		TS_ASSERT_EQUALS((int)surf->h, h);

		const byte *pix = (const byte *)surf->getPixels();
		for (int k = 0; k < w * h; k++) {
			byte r = pix[k * 4 + 2], g = pix[k * 4 + 1], b = pix[k * 4 + 0], a = pix[k * 4 + 3];
			// linear colour i = (i, 255-i, i, 240)
			TS_ASSERT_EQUALS((int)r, k);
			TS_ASSERT_EQUALS((int)g, 255 - k);
			TS_ASSERT_EQUALS((int)b, k);
			TS_ASSERT_EQUALS((int)a, (int)TmxSyntheticHelper::scaleAlpha(240));
		}
	}

	// PSMT4, 8x8, 16-colour linear PSMTC32 clut, two texels per byte.
	void test_psmt4_linear() {
		const int w = 8, h = 8;
		byte clut[16 * 4];
		TmxSyntheticHelper::buildLinearClut16(clut);

		byte idx[32];
		for (int i = 0; i < 32; i++)
			idx[i] = (byte)((((i * 2 + 1) & 0xF) << 4) | (i * 2 & 0xF));

		uint32 total = 0x40 + sizeof(clut) + sizeof(idx);
		byte file[0x40 + 16 * 4 + 32];
		int o = TmxSyntheticHelper::buildHeader(file, 1, 0x00, w, h, 0x14, 0, total);
		memcpy(file + o, clut, sizeof(clut));
		memcpy(file + o + sizeof(clut), idx, sizeof(idx));

		Common::MemoryReadStream stream(file, sizeof(file), DisposeAfterUse::NO);
		SMT::TMX tmx(&stream);
		Graphics::Surface *surf = tmx.getSurface();
		TS_ASSERT(surf);
		if (!surf)
			return;
		TS_ASSERT_EQUALS((int)surf->w, w);
		TS_ASSERT_EQUALS((int)surf->h, h);

		const byte *pix = (const byte *)surf->getPixels();
		for (int k = 0; k < w * h; k++) {
			int j = k / 2;
			int ci = (k & 1) ? (idx[j] >> 4) : (idx[j] & 0x0F);
			byte er = (byte)(ci * 8);
			byte eg = (byte)(255 - ci * 8);
			byte eb = 128;
			byte ea = (ci & 1) ? 200 : 100;
			byte r = pix[k * 4 + 2], g = pix[k * 4 + 1], b = pix[k * 4 + 0], a = pix[k * 4 + 3];
			TS_ASSERT_EQUALS((int)r, (int)er);
			TS_ASSERT_EQUALS((int)g, (int)eg);
			TS_ASSERT_EQUALS((int)b, (int)eb);
			TS_ASSERT_EQUALS((int)a, (int)TmxSyntheticHelper::scaleAlpha(ea));
		}
	}

	// PSMT8, 2x2, 256-colour PSMTC24 clut (alpha forced opaque).
	void test_psmt8_psmct24_clut() {
		const int w = 2, h = 2;
		byte clut[256 * 4];
		for (int i = 0; i < 256; i++) {
			clut[i * 4 + 0] = (byte)i;
			clut[i * 4 + 1] = (byte)(255 - i * 2);
			clut[i * 4 + 2] = (byte)(i * 3);
			clut[i * 4 + 3] = 0xAA; // ignored by 24-bit clut decode
		}
		TmxSyntheticHelper::swizzleClut256(clut);

		byte idx[4] = { 0x00, 0x55, 0xAA, 0xFF };

		uint32 total = 0x40 + sizeof(clut) + sizeof(idx);
		byte file[0x40 + 256 * 4 + 4];
		int o = TmxSyntheticHelper::buildHeader(file, 1, 0x01, w, h, 0x13, 0, total);
		memcpy(file + o, clut, sizeof(clut));
		memcpy(file + o + sizeof(clut), idx, sizeof(idx));

		Common::MemoryReadStream stream(file, sizeof(file), DisposeAfterUse::NO);
		SMT::TMX tmx(&stream);
		Graphics::Surface *surf = tmx.getSurface();
		TS_ASSERT(surf);
		if (!surf)
			return;

		const byte *pix = (const byte *)surf->getPixels();
		for (int k = 0; k < w * h; k++) {
			int ci = idx[k];
			byte er = (byte)ci;
			byte eg = (byte)(255 - ci * 2);
			byte eb = (byte)(ci * 3);
			byte r = pix[k * 4 + 2], g = pix[k * 4 + 1], b = pix[k * 4 + 0], a = pix[k * 4 + 3];
			TS_ASSERT_EQUALS((int)r, (int)er);
			TS_ASSERT_EQUALS((int)g, (int)eg);
			TS_ASSERT_EQUALS((int)b, (int)eb);
			TS_ASSERT_EQUALS((int)a, 0xFF); // PSMTC24 cluts are opaque
		}
	}

	// Corrupt header must fail cleanly (no crash, no surface).
	void test_bad_magic() {
		byte file[0x40] = { 0 };
		file[0] = 0x02; // tmxID ok
		// magic left as NULs -> must fail

		Common::MemoryReadStream stream(file, sizeof(file), DisposeAfterUse::NO);
		SMT::TMX tmx(&stream);
		TS_ASSERT(tmx.getSurface() == nullptr);
	}
};
