#include "ikura/formats/graphic/gga.h"

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/surface.h"

#include <cxxtest/TestSuite.h>

class IkuraGGATestSuite : public CxxTest::TestSuite {
public:
	// 1x1 image: 24-byte header (width=1, height=1, headersize=24,
	// cryptlength=5) + one literal-run control byte (ctrl=12 -> 1
	// pixel) + 4 pixel bytes (B,G,R,A).
	static Common::Array<byte> buildFixture(byte b, byte g, byte r, byte a) {
		Common::Array<byte> data(29, (byte)0);
		const char *magic = "GGA00000";
		for (int i = 0; i < 8; i++)
			data[i] = magic[i];
		data[8] = 1;  // width
		data[10] = 1; // height
		data[16] = 24; // headersize
		data[20] = 5;  // cryptlength
		data[24] = 12; // ctrl: literal run of 1 pixel
		data[25] = b;
		data[26] = g;
		data[27] = r;
		data[28] = a;
		return data;
	}

	void test_valid_opaque_pixel_decodes() {
		Common::Array<byte> data = buildFixture(200, 100, 50, 255);
		Common::MemoryReadStream stream(data.data(), data.size());
		Graphics::Surface *surface = Ikura::Format::Graphic::decodeGGA(stream);
		TS_ASSERT(surface != nullptr);
		if (!surface)
			return;
		TS_ASSERT_EQUALS(surface->w, 1);
		TS_ASSERT_EQUALS(surface->h, 1);
		byte a, r, g, b;
		surface->format.colorToARGB(surface->getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 50);
		TS_ASSERT_EQUALS(g, 100);
		TS_ASSERT_EQUALS(b, 200);
		TS_ASSERT_EQUALS(a, 255);
		surface->free();
		delete surface;
	}

	void test_all_zero_alpha_forced_opaque() {
		// Every literal pixel in the image has alpha 0x00; the reference
		// treats that as "not really per-pixel alpha" and forces the
		// whole image opaque instead of fully transparent.
		Common::Array<byte> data = buildFixture(10, 20, 30, 0);
		Common::MemoryReadStream stream(data.data(), data.size());
		Graphics::Surface *surface = Ikura::Format::Graphic::decodeGGA(stream);
		TS_ASSERT(surface != nullptr);
		if (!surface)
			return;
		byte a, r, g, b;
		surface->format.colorToARGB(surface->getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 30);
		TS_ASSERT_EQUALS(g, 20);
		TS_ASSERT_EQUALS(b, 10);
		TS_ASSERT_EQUALS(a, 255);
		surface->free();
		delete surface;
	}

	void test_wrong_magic_rejected() {
		Common::Array<byte> data = buildFixture(1, 2, 3, 4);
		data[0] = 'X';
		Common::MemoryReadStream stream(data.data(), data.size());
		TS_ASSERT(Ikura::Format::Graphic::decodeGGA(stream) == nullptr);
	}

	void test_truncated_header_rejected() {
		static const byte kTooShort[8] = {'G', 'G', 'A', '0', '0', '0', '0', '0'};
		Common::MemoryReadStream stream(kTooShort, sizeof(kTooShort));
		TS_ASSERT(Ikura::Format::Graphic::decodeGGA(stream) == nullptr);
	}

	void test_backreference_before_any_pixel_rejected() {
		// First (and only) op is a back-reference (ctrl=0x02, 1-byte
		// distance) with nothing written yet.
		Common::Array<byte> data(27, (byte)0);
		const char *magic = "GGA00000";
		for (int i = 0; i < 8; i++)
			data[i] = magic[i];
		data[8] = 1;
		data[10] = 1;
		data[16] = 24;
		data[20] = 2; // cryptlength: ctrl + 1 distance byte
		data[24] = 0x02;
		data[25] = 1; // distance = 1 pixel back - but opos is 0
		Common::MemoryReadStream stream(data.data(), data.size());
		TS_ASSERT(Ikura::Format::Graphic::decodeGGA(stream) == nullptr);
	}
};
