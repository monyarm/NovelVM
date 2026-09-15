#include "ikura/formats/graphic/gan.h"

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/surface.h"

#include <cxxtest/TestSuite.h>

class IkuraGANTestSuite : public CxxTest::TestSuite {
public:
	// 1 solid (ref=0) frame whose compressed data is just a single
	// 3-byte color - the rest of the 640x480 canvas is left black.
	static Common::Array<byte> buildOneFrameFixture() {
		const uint32 kFrameOffset = 0x2020;
		Common::Array<byte> data(kFrameOffset + 3, (byte)0);
		data[12] = 1; // frame count
		uint32 hdr = 0x2010;
		// ref = 0 already (zero-initialized)
		data[hdr + 8] = kFrameOffset & 0xFF;
		data[hdr + 9] = (kFrameOffset >> 8) & 0xFF;
		data[hdr + 12] = 3; // srclen = 3 (one color, no repeat)
		data[kFrameOffset + 0] = 77;
		data[kFrameOffset + 1] = 88;
		data[kFrameOffset + 2] = 99;
		return data;
	}

	void test_one_frame_decodes() {
		Common::Array<byte> data = buildOneFrameFixture();
		Common::MemoryReadStream stream(data.data(), data.size());
		Common::Array<Graphics::Surface *> frames = Ikura::Format::Graphic::decodeGAN(stream);
		TS_ASSERT_EQUALS(frames.size(), 1u);
		if (frames.empty())
			return;
		Graphics::Surface *frame = frames[0];
		TS_ASSERT_EQUALS(frame->w, 640);
		TS_ASSERT_EQUALS(frame->h, 480);
		byte r, g, b;
		frame->format.colorToRGB(frame->getPixel(0, 0), r, g, b);
		TS_ASSERT_EQUALS(r, 77);
		TS_ASSERT_EQUALS(g, 88);
		TS_ASSERT_EQUALS(b, 99);
		// Only the first pixel had source data; the rest of the canvas
		// must stay black rather than read uninitialized memory.
		frame->format.colorToRGB(frame->getPixel(1, 0), r, g, b);
		TS_ASSERT_EQUALS(r, 0);
		TS_ASSERT_EQUALS(g, 0);
		TS_ASSERT_EQUALS(b, 0);
		for (auto *f : frames) {
			f->free();
			delete f;
		}
	}

	void test_too_short_to_read_framecount_rejected() {
		static const byte kTooShort[4] = {0, 0, 0, 0};
		Common::MemoryReadStream stream(kTooShort, sizeof(kTooShort));
		Common::Array<Graphics::Surface *> frames = Ikura::Format::Graphic::decodeGAN(stream);
		TS_ASSERT(frames.empty());
	}

	void test_zero_framecount_rejected() {
		Common::Array<byte> data(16, (byte)0);
		Common::MemoryReadStream stream(data.data(), data.size());
		Common::Array<Graphics::Surface *> frames = Ikura::Format::Graphic::decodeGAN(stream);
		TS_ASSERT(frames.empty());
	}

	void test_frame_offset_past_eof_rejected() {
		Common::Array<byte> data = buildOneFrameFixture();
		// Point the frame's data offset far past the end of the file.
		uint32 hdr = 0x2010;
		data[hdr + 8] = 0xFF;
		data[hdr + 9] = 0xFF;
		Common::MemoryReadStream stream(data.data(), data.size());
		Common::Array<Graphics::Surface *> frames = Ikura::Format::Graphic::decodeGAN(stream);
		TS_ASSERT(frames.empty());
	}
};
