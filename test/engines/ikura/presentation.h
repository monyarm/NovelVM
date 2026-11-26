#include "ikura/runtime/presentation.h"

#include "common/archive.h"
#include "common/array.h"
#include "common/memstream.h"
#include "../../system/null_osystem.h"

#include <cxxtest/TestSuite.h>

namespace {

// Minimal single-member in-memory Archive double - Presentation only needs
// hasFile/createReadStreamForMember, so this skips everything Cabinet
// handles for real cabinets (directory parsing, multiple members, etc).
class OneFileArchive : public Common::Archive {
public:
	OneFileArchive(const Common::String &name, Common::Array<byte> data) : _name(name), _data(data) {}

	bool hasFile(const Common::Path &path) const override { return path.toString() == _name; }
	int listMembers(Common::ArchiveMemberList &list) const override { return 0; }
	const Common::ArchiveMemberPtr getMember(const Common::Path &path) const override { return Common::ArchiveMemberPtr(); }
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override {
		if (!hasFile(path))
			return nullptr;
		return new Common::MemoryReadStream(_data.data(), _data.size(), DisposeAfterUse::NO);
	}

private:
	Common::String _name;
	Common::Array<byte> _data;
};

// Same 1x1 GGA fixture shape as test/engines/ikura/gga.h.
Common::Array<byte> buildGGAFixture(byte b, byte g, byte r, byte a) {
	Common::Array<byte> data(29, (byte)0);
	const char *magic = "GGA00000";
	for (int i = 0; i < 8; i++)
		data[i] = magic[i];
	data[8] = 1;   // width
	data[10] = 1;  // height
	data[16] = 24; // headersize
	data[20] = 5;  // cryptlength
	data[24] = 12; // ctrl: literal run of 1 pixel
	data[25] = b;
	data[26] = g;
	data[27] = r;
	data[28] = a;
	return data;
}

} // namespace

class IkuraPresentationTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override { Common::install_null_g_system(); }
	void tearDown() override { Common::uninstall_null_g_system(); }

	void test_load_image_by_extension() {
		OneFileArchive archive("BG.GG0", buildGGAFixture(200, 100, 50, 255));
		Ikura::Runtime::Presentation presentation(&archive);

		TS_ASSERT(presentation.buffer(5) == nullptr);
		presentation.loadImage(5, "BG.GG0");
		TS_ASSERT(presentation.buffer(5) != nullptr);
		if (!presentation.buffer(5))
			return;
		TS_ASSERT_EQUALS(presentation.buffer(5)->w, 1);
		TS_ASSERT_EQUALS(presentation.buffer(5)->h, 1);
	}

	void test_missing_file_is_noop() {
		OneFileArchive archive("BG.GG0", buildGGAFixture(1, 2, 3, 4));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadImage(5, "NOPE.GG0");
		TS_ASSERT(presentation.buffer(5) == nullptr);
	}

	void test_out_of_range_index_is_noop() {
		OneFileArchive archive("BG.GG0", buildGGAFixture(1, 2, 3, 4));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadImage(-1, "BG.GG0");
		presentation.loadImage(Ikura::Runtime::Presentation::kBufferCount, "BG.GG0");
		TS_ASSERT(presentation.buffer(-1) == nullptr);
	}

	void test_copy_graphic_blits_between_buffers() {
		OneFileArchive archive("BG.GG0", buildGGAFixture(200, 100, 50, 255));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadImage(1, "BG.GG0");

		presentation.copyGraphic(0 /* basic blit */, 1, Common::Rect(0, 0, 1, 1), 0, Common::Point(0, 0));
		TS_ASSERT(presentation.buffer(0) != nullptr);
		if (!presentation.buffer(0))
			return;
		byte a, r, g, b;
		presentation.buffer(0)->format.colorToARGB(presentation.buffer(0)->getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 50);
		TS_ASSERT_EQUALS(g, 100);
		TS_ASSERT_EQUALS(b, 200);
	}

	void test_copy_graphic_effect_cmd_targets_buffer_zero_regardless_of_dst() {
		// VileVN reference: iop_gp's cmd 7/15/16/17/19 branches hardcode
		// buffer 0 as the real blit target (dst is repurposed as an
		// animation duration) - passing dst=9 here must still land in
		// buffer 0, not buffer 9.
		OneFileArchive archive("BG.GG0", buildGGAFixture(10, 20, 30, 255));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadImage(1, "BG.GG0");

		presentation.copyGraphic(19 /* fade */, 1, Common::Rect(0, 0, 1, 1), 9, Common::Point(0, 0));
		TS_ASSERT(presentation.buffer(0) != nullptr);
		TS_ASSERT(presentation.buffer(9) == nullptr);
		if (!presentation.buffer(0))
			return;
		byte a, r, g, b;
		presentation.buffer(0)->format.colorToARGB(presentation.buffer(0)->getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 30);
		TS_ASSERT_EQUALS(g, 20);
		TS_ASSERT_EQUALS(b, 10);
	}

	void test_fade_crossfades_from_old_content_to_new() {
		OneFileArchive redArchive("RED.GG0", buildGGAFixture(0, 0, 255, 255));
		Ikura::Runtime::Presentation presentation(&redArchive);
		presentation.loadImage(1, "RED.GG0");
		presentation.copyGraphic(0, 1, Common::Rect(0, 0, 1, 1), 0, Common::Point(0, 0)); // buffer 0 = red

		uint32 start = g_system->getMillis();
		presentation.beginFade(1000);
		presentation.fillBuffer(0, 0, 0, 255); // buffer 0 mutates to blue, same as IOP_GO would

		Graphics::Surface target;
		target.create(1, 1, Graphics::PixelFormat::createFormatRGBA32());

		presentation.compositeScreen(target, start); // 0% elapsed: still the old (red) content
		byte a, r, g, b;
		target.format.colorToARGB(target.getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 255);
		TS_ASSERT_EQUALS(b, 0);

		presentation.compositeScreen(target, start + 1000); // 100% elapsed: the new (blue) content
		target.format.colorToARGB(target.getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 0);
		TS_ASSERT_EQUALS(b, 255);

		target.free();
	}

	void test_complete_fade_ends_it_immediately() {
		OneFileArchive redArchive("RED.GG0", buildGGAFixture(0, 0, 255, 255));
		Ikura::Runtime::Presentation presentation(&redArchive);
		presentation.loadImage(1, "RED.GG0");
		presentation.copyGraphic(0, 1, Common::Rect(0, 0, 1, 1), 0, Common::Point(0, 0)); // buffer 0 = red

		uint32 start = g_system->getMillis();
		presentation.beginFade(10000); // long fade
		presentation.fillBuffer(0, 0, 0, 255); // buffer 0 mutates to blue
		presentation.completeFade(); // VileVN reference: SkipAnimation()

		Graphics::Surface target;
		target.create(1, 1, Graphics::PixelFormat::createFormatRGBA32());
		presentation.compositeScreen(target, start); // still "0%" by time, but fade already ended
		byte a, r, g, b;
		target.format.colorToARGB(target.getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 0); // shows buffer 0's real content, not a blend
		TS_ASSERT_EQUALS(b, 255);
		target.free();
	}

	void test_copy_graphic_from_empty_buffer_is_noop() {
		OneFileArchive archive("BG.GG0", buildGGAFixture(1, 2, 3, 4));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.copyGraphic(0, 9 /* never loaded */, Common::Rect(0, 0, 1, 1), 0, Common::Point(0, 0));
		TS_ASSERT(presentation.buffer(0) == nullptr);
	}

	static bool hasNonBlackPixel(const Graphics::Surface &target) {
		for (int y = 0; y < target.h; y++) {
			for (int x = 0; x < target.w; x++) {
				byte a, r, g, b;
				target.format.colorToARGB(target.getPixel(x, y), a, r, g, b);
				if (r || g || b)
					return true;
			}
		}
		return false;
	}

	void test_draw_text_renders_real_glyphs() {
		Ikura::Runtime::Presentation presentation(nullptr);
		presentation.showText("Hello, Ikura!");
		presentation.completeTextReveal(); // skip the typewriter delay for this test

		Graphics::Surface target;
		target.create(640, 480, Graphics::PixelFormat::createFormatRGBA32());
		target.fillRect(Common::Rect(0, 0, 640, 480), target.format.RGBToColor(0, 0, 0));

		presentation.drawText(target);
		TS_ASSERT(hasNonBlackPixel(target));
		target.free();
	}

	void test_draw_text_noop_when_hidden() {
		Ikura::Runtime::Presentation presentation(nullptr);
		presentation.showText("Hello, Ikura!");
		presentation.setTextVisible(false);

		Graphics::Surface target;
		target.create(640, 480, Graphics::PixelFormat::createFormatRGBA32());
		target.fillRect(Common::Rect(0, 0, 640, 480), target.format.RGBToColor(0, 0, 0));

		presentation.drawText(target);
		TS_ASSERT(!hasNonBlackPixel(target));
		target.free();
	}

	void test_draw_text_noop_when_no_text() {
		Ikura::Runtime::Presentation presentation(nullptr);

		Graphics::Surface target;
		target.create(640, 480, Graphics::PixelFormat::createFormatRGBA32());
		target.fillRect(Common::Rect(0, 0, 640, 480), target.format.RGBToColor(0, 0, 0));

		presentation.drawText(target);
		TS_ASSERT(!hasNonBlackPixel(target));
		target.free();
	}
};
