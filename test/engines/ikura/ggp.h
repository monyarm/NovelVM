#include "ikura/formats/graphic/ggp.h"

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/surface.h"

#include <cxxtest/TestSuite.h>

class IkuraGGPTestSuite : public CxxTest::TestSuite {
public:
	// 1x1 red-ish PNG (200,40,60), XOR-"encrypted" with the GGP scheme
	// (magic repeating key, zero secondary key) exactly as a real GGP file
	// would be - built with a throwaway Python script, not hand-typed.
	static const byte *fixture() {
		static const byte kGGP[] = {
			0x47, 0x47, 0x50, 0x46, 0x41, 0x49, 0x4B, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xCE, 0x17, 0x1E, 0x01, 0x4C, 0x43, 0x51, 0x4F, 0x47, 0x47, 0x50, 0x4B,
			0x08, 0x01, 0x0F, 0x17, 0x47, 0x47, 0x50, 0x47, 0x41, 0x49, 0x4B, 0x44, 0x4F, 0x45, 0x50, 0x46,
			0x41, 0xD9, 0x3C, 0x16, 0x99, 0x47, 0x50, 0x46, 0x4D, 0x00, 0x0F, 0x04, 0x13, 0x3F, 0x8A, 0x25,
			0x79, 0xE8, 0x2A, 0x46, 0x47, 0x45, 0xB8, 0x47, 0x6C, 0xFF, 0xF6, 0xCF, 0x8E, 0x47, 0x50, 0x46,
			0x41, 0x00, 0x0E, 0x0B, 0x03, 0xE9, 0x12, 0x26, 0xC3,
		};
		return kGGP;
	}
	static const uint32 fixtureSize = 105;

	void test_valid_ggp_decodes() {
		Common::MemoryReadStream stream(fixture(), fixtureSize);
		Graphics::Surface *surface = Ikura::Format::Graphic::decodeGGP(stream);
		TS_ASSERT(surface != nullptr);
		if (!surface)
			return;
		TS_ASSERT_EQUALS(surface->w, 1);
		TS_ASSERT_EQUALS(surface->h, 1);
		byte r, g, b;
		surface->format.colorToRGB(surface->getPixel(0, 0), r, g, b);
		TS_ASSERT_EQUALS(r, 200);
		TS_ASSERT_EQUALS(g, 40);
		TS_ASSERT_EQUALS(b, 60);
		surface->free();
		delete surface;
	}

	void test_wrong_magic_rejected() {
		static const byte kBadMagic[36] = {0};
		Common::MemoryReadStream stream(kBadMagic, sizeof(kBadMagic));
		TS_ASSERT(Ikura::Format::Graphic::decodeGGP(stream) == nullptr);
	}

	void test_truncated_header_rejected() {
		static const byte kTooShort[8] = {'G', 'G', 'P', 'F', 'A', 'I', 'K', 'E'};
		Common::MemoryReadStream stream(kTooShort, sizeof(kTooShort));
		TS_ASSERT(Ikura::Format::Graphic::decodeGGP(stream) == nullptr);
	}

	void test_length_past_eof_rejected() {
		Common::Array<byte> data(fixture(), fixtureSize);
		// Claim a payload far larger than the file actually has.
		data[24] = 0xFF;
		data[25] = 0xFF;
		data[26] = 0x00;
		data[27] = 0x00;
		Common::MemoryReadStream stream(data.data(), data.size());
		TS_ASSERT(Ikura::Format::Graphic::decodeGGP(stream) == nullptr);
	}
};
