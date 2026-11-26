#include "ikura/formats/graphic/ggd24.h"

#include "common/memstream.h"
#include "graphics/surface.h"

#include <cxxtest/TestSuite.h>

class IkuraGGD24TestSuite : public CxxTest::TestSuite {
public:
	// 2x1 image, width=2 height=1 in the header, followed by one literal
	// run (ctrl=6 -> 2 literal pixels): (10,20,30) then (40,50,60).
	static const byte *fixture() {
		static const byte kGGD24[] = {
			0, 0, 0, 0, 2, 0, 1, 0,
			6, 10, 20, 30, 40, 50, 60,
		};
		return kGGD24;
	}
	static const uint32 fixtureSize = 15;

	void test_valid_ggd24_decodes() {
		Common::MemoryReadStream stream(fixture(), fixtureSize);
		Graphics::Surface *surface = Ikura::Format::Graphic::decodeGGD24(stream);
		TS_ASSERT(surface != nullptr);
		if (!surface)
			return;
		TS_ASSERT_EQUALS(surface->w, 2);
		TS_ASSERT_EQUALS(surface->h, 1);
		byte r, g, b;
		surface->format.colorToRGB(surface->getPixel(0, 0), r, g, b);
		TS_ASSERT_EQUALS(r, 10);
		TS_ASSERT_EQUALS(g, 20);
		TS_ASSERT_EQUALS(b, 30);
		surface->format.colorToRGB(surface->getPixel(1, 0), r, g, b);
		TS_ASSERT_EQUALS(r, 40);
		TS_ASSERT_EQUALS(g, 50);
		TS_ASSERT_EQUALS(b, 60);
		surface->free();
		delete surface;
	}

	void test_truncated_header_rejected() {
		static const byte kTooShort[4] = {0, 0, 0, 0};
		Common::MemoryReadStream stream(kTooShort, sizeof(kTooShort));
		TS_ASSERT(Ikura::Format::Graphic::decodeGGD24(stream) == nullptr);
	}

	void test_backreference_before_start_rejected() {
		// ctrl=1 (back-reference run), count=1, distance=5 pixels back -
		// nothing has been written yet, so this must be rejected rather
		// than reading before the start of the output buffer.
		static const byte kBadRef[] = {
			0, 0, 0, 0, 1, 0, 1, 0,
			1, 1, 5,
		};
		Common::MemoryReadStream stream(kBadRef, sizeof(kBadRef));
		TS_ASSERT(Ikura::Format::Graphic::decodeGGD24(stream) == nullptr);
	}

	void test_literal_run_past_eof_rejected() {
		// ctrl=6 (2 literal pixels = 6 bytes needed) but only 2 data
		// bytes are actually present after it.
		static const byte kBadLiteral[] = {
			0, 0, 0, 0, 2, 0, 1, 0,
			6, 10, 20,
		};
		Common::MemoryReadStream stream(kBadLiteral, sizeof(kBadLiteral));
		TS_ASSERT(Ikura::Format::Graphic::decodeGGD24(stream) == nullptr);
	}
};
