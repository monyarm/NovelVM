#include <cxxtest/TestSuite.h>

#include "graphics/ps2texture.h"

#include "common/memstream.h"

#include "graphics/surface.h"

/**
 * Tests for the shared PS2 GS texture decode module (Graphics::ps2*).
 *
 * All fixtures are synthetic. Decode helpers are fed hand-built payloads and
 * asserted against colours computed directly from the build spec, so the test
 * is independent of the implementation under test.
 */

class PS2TextureTestSuite : public CxxTest::TestSuite {
public:
	static byte scaleHalfToFull(byte a) {
		int v = (int)((a / 128.0f) * 255);
		return v > 255 ? 255 : (byte)v;
	}

	// CSM1 clut swizzle must round-trip (swap blocks [8..16)<->[16..24)).
	void test_clut_swizzle_roundtrip() {
		byte clut[256 * 4];
		for (int i = 0; i < 256; i++) {
			clut[i * 4 + 0] = (byte)i;
			clut[i * 4 + 1] = (byte)(255 - i);
			clut[i * 4 + 2] = (byte)(i * 3);
			clut[i * 4 + 3] = 0x0F;
		}

		byte swizzled[256 * 4];
		memcpy(swizzled, clut, sizeof(clut));
		Graphics::ps2UnswizzleClut(swizzled, 256);

		// unswizzle uses the same self-inverse swap, so applying it twice
		// restores the original
		Graphics::ps2UnswizzleClut(swizzled, 256);
		TS_ASSERT(memcmp(swizzled, clut, sizeof(clut)) == 0);

		// and a single pass produces a different (reordered) clu
		memcpy(swizzled, clut, sizeof(clut));
		Graphics::ps2UnswizzleClut(swizzled, 256);
		// block 8 and block 16 differ (colours 8 and 16 differ), so the
		// first entry of the swapped region must change
		TS_ASSERT(memcmp(&swizzled[8 * 4], &clut[8 * 4], 4) != 0);
	}

	// GS page-block-column tiling must be a bijection: every source byte in a
	// fully-covered single 16x16 GS block is read by exactly one destination
	// texel. w*h == 256 so source[i] = i covers every byte value exactly
	// once, making a permutation check on the output values meaningful.
	void test_unswizzle_indices_is_bijection() {
		const uint32 w = 16, h = 16;
		byte source[w * h];
		for (uint32 i = 0; i < w * h; i++)
			source[i] = (byte)i;

		byte dest[w * h];
		TS_ASSERT(Graphics::ps2UnswizzleIndices(source, sizeof(source), dest, w, h, w));

		bool seen[256] = {};
		for (uint32 i = 0; i < w * h; i++) {
			TS_ASSERT(!seen[dest[i]]);
			seen[dest[i]] = true;
		}
	}

	// Indexed decode: PSMT8 + 256-colour swizzled PSMTC32 clut.
	void test_indexed_psmt8() {
		byte clut[256 * 4];
		for (int i = 0; i < 256; i++) {
			clut[i * 4 + 0] = (byte)i;
			clut[i * 4 + 1] = (byte)(200 - i);
			clut[i * 4 + 2] = 55;
			clut[i * 4 + 3] = 240;
		}

		const int w = 2, h = 2;
		byte indices[4] = { 0, 1, 2, 3 };

		Graphics::Surface *s = Graphics::ps2CreateIndexedSurface(
			Graphics::kPSMCT32, clut, 256, indices, Graphics::kPSMT8,
			w, h, true, true);
		TS_ASSERT(s);
		if (!s)
			return;
		TS_ASSERT_EQUALS((int)s->w, w);
		TS_ASSERT_EQUALS((int)s->h, h);

		const byte *pix = (const byte *)s->getPixels();
		for (int k = 0; k < w * h; k++) {
			byte r = pix[k * 4 + 2], g = pix[k * 4 + 1], b = pix[k * 4 + 0], a = pix[k * 4 + 3];
			TS_ASSERT_EQUALS((int)r, (int)clut[k * 4 + 0]);
			TS_ASSERT_EQUALS((int)g, (int)clut[k * 4 + 1]);
			TS_ASSERT_EQUALS((int)b, (int)clut[k * 4 + 2]);
			TS_ASSERT_EQUALS((int)a, (int)scaleHalfToFull(clut[k * 4 + 3]));
		}
		s->free();
		delete s;
	}

	// Indexed decode: PSMT4, 16-colour linear clut, two texels/byte.
	void test_indexed_psmt4() {
		byte clut[16 * 4];
		for (int i = 0; i < 16; i++) {
			clut[i * 4 + 0] = (byte)(i * 8);
			clut[i * 4 + 1] = (byte)(255 - i * 8);
			clut[i * 4 + 2] = 128;
			clut[i * 4 + 3] = 200;
		}

		const int w = 4, h = 4;
		byte indices[8];
		for (int i = 0; i < 8; i++)
			indices[i] = (byte)((((i * 2 + 1) & 0xF) << 4) | (i * 2 & 0xF));

		Graphics::Surface *s = Graphics::ps2CreateIndexedSurface(
			Graphics::kPSMCT32, clut, 16, indices, Graphics::kPSMT4,
			w, h, false, true);
		TS_ASSERT(s);
		if (!s)
			return;

		const byte *pix = (const byte *)s->getPixels();
		for (int k = 0; k < w * h; k++) {
			int ci = (k & 1) ? (indices[k / 2] >> 4) : (indices[k / 2] & 0x0F);
			byte r = pix[k * 4 + 2], g = pix[k * 4 + 1], b = pix[k * 4 + 0], a = pix[k * 4 + 3];
			TS_ASSERT_EQUALS((int)r, (int)clut[ci * 4 + 0]);
			TS_ASSERT_EQUALS((int)g, (int)clut[ci * 4 + 1]);
			TS_ASSERT_EQUALS((int)b, (int)clut[ci * 4 + 2]);
			TS_ASSERT_EQUALS((int)a, (int)scaleHalfToFull(clut[ci * 4 + 3]));
		}
		s->free();
		delete s;
	}

	// Direct colour: PSMCT32. GS memory stores pixels R,G,B,A (confirmed
	// against a real byte-exact AmicitiaLibrary decode of P3F PS092.RMD's
	// p_sraim_t01 -- an earlier version of this test assumed BGRA input
	// with no swap, which was never verified and turned out backwards).
	// The surface itself stores BGRA (byte0=B), so R/B swap on write, and
	// alpha is GS half-range (0..0x80), doubled to 0..255 like CLUT alpha.
	void test_color_psmct32() {
		const int w = 2, h = 2;
		byte data[16];
		for (int i = 0; i < w * h; i++) {
			data[i * 4 + 0] = 50;              // R
			data[i * 4 + 1] = 200;              // G
			data[i * 4 + 2] = (byte)(100 + i); // B
			data[i * 4 + 3] = 128;             // A (half-range)
		}

		Graphics::Surface *s = Graphics::ps2CreateColorSurface(
			Graphics::kPSMCT32, data, w, h);
		TS_ASSERT(s);
		if (!s)
			return;

		const byte *pix = (const byte *)s->getPixels();
		for (int k = 0; k < w * h; k++) {
			// surface is BGRA: byte0=B, byte1=G, byte2=R, byte3=A
			TS_ASSERT_EQUALS((int)pix[k * 4 + 0], (int)data[k * 4 + 2]);
			TS_ASSERT_EQUALS((int)pix[k * 4 + 1], 200);
			TS_ASSERT_EQUALS((int)pix[k * 4 + 2], 50);
			TS_ASSERT_EQUALS((int)pix[k * 4 + 3], 255);
		}
		s->free();
		delete s;
	}

	// PSMT4 texel size: 8x4 texels = 16 bytes.
	void test_texel_data_size() {
		TS_ASSERT_EQUALS(Graphics::ps2TexelDataSize(Graphics::kPSMCT32, 8, 8), 256);
		TS_ASSERT_EQUALS(Graphics::ps2TexelDataSize(Graphics::kPSMCT16, 8, 8), 128);
		TS_ASSERT_EQUALS(Graphics::ps2TexelDataSize(Graphics::kPSMT8, 8, 8), 64);
		TS_ASSERT_EQUALS(Graphics::ps2TexelDataSize(Graphics::kPSMT4, 8, 8), 32);
	}

	// TM2 container with a synthetic 8-bit indexed picture.
	void test_tm2_indexed8() {
		byte clut[256 * 4];
		for (int i = 0; i < 256; i++) {
			clut[i * 4 + 0] = (byte)i;
			clut[i * 4 + 1] = 100;
			clut[i * 4 + 2] = 200;
			clut[i * 4 + 3] = 255;
		}

		byte file[16 + 48 + 16 + 256 * 4];
		Common::MemoryReadStream *stream = buildSyntheticTm2(
			file, 4, 4, 5, clut, 256);
		Graphics::TM2 tm2(stream);
		delete stream;

		TS_ASSERT_EQUALS(tm2.pictureCount(), 1);
		TS_ASSERT_EQUALS(tm2.width(0), 4);
		TS_ASSERT_EQUALS(tm2.height(0), 4);

		Graphics::Surface *s = tm2.getSurface(0);
		TS_ASSERT(s);
		if (!s)
			return;
		const byte *pix = (const byte *)s->getPixels();
		// indices are 0..15, clut colour i = (i, 100, 200, 255)
		for (int k = 0; k < 16; k++) {
			byte r = pix[k * 4 + 2], g = pix[k * 4 + 1], b = pix[k * 4 + 0], a = pix[k * 4 + 3];
			TS_ASSERT_EQUALS((int)r, k);
			TS_ASSERT_EQUALS((int)g, 100);
			TS_ASSERT_EQUALS((int)b, 200);
			TS_ASSERT_EQUALS((int)a, 255);
		}
		s->free();
		delete s;
	}

	// TM2 with a CSM1-swizzled CLUT (clutType high bit clear): the swap of
	// entries [8..16) <-> [16..24) must be applied exactly once.
	void test_tm2_indexed8_swizzled() {
		byte clut[256 * 4];
		for (int i = 0; i < 256; i++) {
			clut[i * 4 + 0] = (byte)i;
			clut[i * 4 + 1] = 100;
			clut[i * 4 + 2] = 200;
			clut[i * 4 + 3] = 255;
		}

		byte file[16 + 48 + 16 + 256 * 4];
		Common::MemoryReadStream *stream = buildSyntheticTm2(
			file, 4, 4, 5, clut, 256, 0x03);
		Graphics::TM2 tm2(stream);
		delete stream;

		Graphics::Surface *s = tm2.getSurface(0);
		TS_ASSERT(s);
		if (!s)
			return;
		const byte *pix = (const byte *)s->getPixels();
		// index 8..15 must now read palette entries 16..23.
		for (int k = 8; k < 16; k++) {
			byte r = pix[k * 4 + 2];
			TS_ASSERT_EQUALS((int)r, k + 8);
		}
		s->free();
		delete s;
	}

	// A TIM2 declaring zero pictures: getSurface() must return nullptr for any
	// index rather than read past the empty picture array.
	void test_tm2_zero_pictures() {
		byte file[64];
		memset(file, 0, sizeof(file));
		memcpy(file, "TIM2", 4);
		file[4] = 4; // version
		file[5] = 0; // format
		// picture count (LE16) left 0

		Common::MemoryReadStream stream(file, sizeof(file), DisposeAfterUse::NO);
		Graphics::TM2 tm2(&stream);
		TS_ASSERT_EQUALS(tm2.pictureCount(), 0);
		TS_ASSERT(tm2.getSurface(0) == nullptr);
		TS_ASSERT(tm2.getSurface(-1) == nullptr);
	}

	// A picture whose declared w*h needs more bytes than imageSize must be
	// rejected before decode (otherwise getSurface() indexes past the buffer).
	void test_tm2_dims_exceed_imagesize() {
		byte file[64 + 16];
		memset(file, 0, sizeof(file));
		// 64x64 PSMT8 needs 4096 bytes but only imageSize=16 are present.
		int n = buildTm2Header(file, 64, 64, 5, 16, 0, 0x30 + 16);
		Common::MemoryReadStream stream(file, n, DisposeAfterUse::NO);
		Graphics::TM2 tm2(&stream);
		TS_ASSERT_EQUALS(tm2.pictureCount(), 0);
		TS_ASSERT(tm2.getSurface(0) == nullptr);
	}

	// Huge or negative imageSize must not drive a giant allocation; both are
	// rejected cleanly.
	void test_tm2_bad_imagesize() {
		{
			byte file[64 + 16];
			memset(file, 0, sizeof(file));
			int n = buildTm2Header(file, 4, 4, 5, 0x7FFFFFFF, 0, 0x30 + 16);
			Common::MemoryReadStream stream(file, n, DisposeAfterUse::NO);
			Graphics::TM2 tm2(&stream);
			TS_ASSERT_EQUALS(tm2.pictureCount(), 0);
			TS_ASSERT(tm2.getSurface(0) == nullptr);
		}
		{
			byte file[64 + 16];
			memset(file, 0, sizeof(file));
			int n = buildTm2Header(file, 4, 4, 5, -1, 0, 0x30 + 16);
			Common::MemoryReadStream stream(file, n, DisposeAfterUse::NO);
			Graphics::TM2 tm2(&stream);
			TS_ASSERT_EQUALS(tm2.pictureCount(), 0);
			TS_ASSERT(tm2.getSurface(0) == nullptr);
		}
	}

private:
	static void w16le(byte *p, uint16 v) {
		p[0] = (byte)(v & 0xFF);
		p[1] = (byte)((v >> 8) & 0xFF);
	}

	static void w32le(byte *p, uint32 v) {
		p[0] = (byte)(v & 0xFF);
		p[1] = (byte)((v >> 8) & 0xFF);
		p[2] = (byte)((v >> 16) & 0xFF);
		p[3] = (byte)(v >> 24);
	}

	// Writes just the 16-byte file header + 48-byte layout record for a
	// single picture. All size fields are written verbatim, so tests can
	// produce malformed records. Returns the bytes written (64).
	static int buildTm2Header(byte *file, int w, int h, byte imageType,
			int32 imageSize, int clutCount, int32 totalSize) {
		int o = 0;
		memcpy(file + o, "TIM2", 4); o += 4;
		file[o++] = 4; // version
		file[o++] = 0; // format
		file[o++] = 1; file[o++] = 0; // picture count
		memset(file + o, 0, 8); o += 8; // reserved

		w32le(file + o, (uint32)totalSize); o += 4;
		w32le(file + o, 0); o += 4;               // clut size
		w32le(file + o, (uint32)imageSize); o += 4;
		w16le(file + o, 0x30); o += 2;            // header size
		w16le(file + o, (uint16)clutCount); o += 2;
		file[o++] = 0;    // picture format
		file[o++] = 0;    // mipmap count
		file[o++] = 0x83; // CLUT type (linear)
		file[o++] = imageType;
		w16le(file + o, (uint16)w); o += 2;
		w16le(file + o, (uint16)h); o += 2;
		uint64 tex = (uint64)Graphics::kPSMT8 << 20;
		for (int i = 0; i < 8; i++)
			file[o++] = (byte)((tex >> (i * 8)) & 0xFF);
		memset(file + o, 0, 8); o += 8; // GsTex1
		memset(file + o, 0, 4); o += 4; // GsRegs
		memset(file + o, 0, 4); o += 4; // GsClut
		return o;
	}

	// Builds a synthetic single-picture TIM2/TM2 stream.
	static Common::MemoryReadStream *buildSyntheticTm2(byte *file,
			int w, int h, byte imageType, const byte *clut, int clutCount,
			byte clutType = 0x83) {
		int o = 0;
		memcpy(file + o, "TIM2", 4); o += 4;
		file[o++] = 4;   // version
		file[o++] = 0;   // format
		file[o++] = 1;   // picture count (LE16)
		file[o++] = 0;
		memset(file + o, 0, 8); o += 8; // reserved

		byte indices[16];
		for (int i = 0; i < 16; i++)
			indices[i] = (byte)i;

		int imageSize = sizeof(indices);
		int clutBytes = clutCount * 4;
		int totalSize = 48 + imageSize + clutBytes;

		file[o++] = totalSize & 0xFF; file[o++] = (totalSize >> 8) & 0xFF;
		file[o++] = (totalSize >> 16) & 0xFF; file[o++] = (totalSize >> 24) & 0xFF;
		file[o++] = clutBytes & 0xFF; file[o++] = (clutBytes >> 8) & 0xFF;
		file[o++] = (clutBytes >> 16) & 0xFF; file[o++] = (clutBytes >> 24) & 0xFF;
		file[o++] = imageSize & 0xFF; file[o++] = (imageSize >> 8) & 0xFF;
		file[o++] = (imageSize >> 16) & 0xFF; file[o++] = (imageSize >> 24) & 0xFF;
		int hdrSize = 0x30;
		file[o++] = hdrSize & 0xFF; file[o++] = (hdrSize >> 8) & 0xFF;
		file[o++] = clutCount & 0xFF; file[o++] = (clutCount >> 8) & 0xFF;
		file[o++] = 0;      // picture format
		file[o++] = 0;      // mipmap count
		// CLUT type: 0x80 set = linear palette, clear = CSM1-swizzled.
		file[o++] = clutType;
		file[o++] = imageType;
		file[o++] = w & 0xFF; file[o++] = (w >> 8) & 0xFF;
		file[o++] = h & 0xFF; file[o++] = (h >> 8) & 0xFF;

		// GsTex0: PSM = PSMT8 (0x13) -> bits 20..25, TW/TH = log2 sizes
		uint64 psm = (uint64)Graphics::kPSMT8 << 20;
		uint64 tw = (uint64)(w == 4 ? 2 : 1) << 26; // log2(w)=2 for 4
		uint64 th = (uint64)(h == 4 ? 2 : 1) << 30;
		uint64 tex = psm | tw | th;
		for (int i = 0; i < 8; i++)
			file[o++] = (byte)((tex >> (i * 8)) & 0xFF);
		memset(file + o, 0, 8); o += 8;  // GsTex1
		memset(file + o, 0, 4); o += 4;  // GsRegs
		memset(file + o, 0, 4); o += 4;  // GsClut
		TS_ASSERT_EQUALS(o, 16 + 48);

		memcpy(file + o, indices, sizeof(indices)); o += sizeof(indices);
		memcpy(file + o, clut, clutBytes); o += clutBytes;

		return new Common::MemoryReadStream(file, o, DisposeAfterUse::NO);
	}
};
