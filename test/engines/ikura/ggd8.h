#include "ikura/formats/graphic/ggd8.h"

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/surface.h"

#include <cxxtest/TestSuite.h>

class IkuraGGD8TestSuite : public CxxTest::TestSuite {
public:
	// 1x1 image. 16-byte header (width=1, height=1) + 28 unknown bytes +
	// 256-entry palette (only index 5 set, to (100,150,200)) + 4 skip
	// bytes + one control byte (bit0=1: literal) + one literal palette
	// index (5). Input runs out right after, which cleanly ends
	// decompression with exactly the 1 pixel we need.
	static Common::Array<byte> buildValidFixture() {
		Common::Array<byte> data(1074, (byte)0);
		data[8] = 1;  // width
		data[12] = 1; // height
		uint32 paletteOffset = 16 + 28 + 5 * 4;
		data[paletteOffset + 0] = 100; // R
		data[paletteOffset + 1] = 150; // G
		data[paletteOffset + 2] = 200; // B
		data[1072] = 0xFF;             // control byte, bit0 = literal
		data[1073] = 5;                // literal: palette index 5
		return data;
	}

	void test_valid_ggd8_decodes() {
		Common::Array<byte> data = buildValidFixture();
		Common::MemoryReadStream stream(data.data(), data.size());
		Graphics::Surface *surface = Ikura::Format::Graphic::decodeGGD8(stream);
		TS_ASSERT(surface != nullptr);
		if (!surface)
			return;
		TS_ASSERT_EQUALS(surface->w, 1);
		TS_ASSERT_EQUALS(surface->h, 1);
		byte r, g, b;
		surface->format.colorToRGB(surface->getPixel(0, 0), r, g, b);
		TS_ASSERT_EQUALS(r, 100);
		TS_ASSERT_EQUALS(g, 150);
		TS_ASSERT_EQUALS(b, 200);
		surface->free();
		delete surface;
	}

	void test_truncated_header_rejected() {
		static const byte kTooShort[8] = {0};
		Common::MemoryReadStream stream(kTooShort, sizeof(kTooShort));
		TS_ASSERT(Ikura::Format::Graphic::decodeGGD8(stream) == nullptr);
	}

	void test_missing_palette_rejected() {
		// Valid-looking width/height but the file ends before the
		// palette does.
		Common::Array<byte> data(100, (byte)0);
		data[8] = 1;
		data[12] = 1;
		Common::MemoryReadStream stream(data.data(), data.size());
		TS_ASSERT(Ikura::Format::Graphic::decodeGGD8(stream) == nullptr);
	}

	void test_truncated_pixel_data_rejected() {
		// Same header/palette as the valid fixture, but with no
		// compressed data at all after the palette - 0 pixels
		// decompressed for a 1x1 image.
		Common::Array<byte> data = buildValidFixture();
		data.resize(1072);
		Common::MemoryReadStream stream(data.data(), data.size());
		TS_ASSERT(Ikura::Format::Graphic::decodeGGD8(stream) == nullptr);
	}
};
