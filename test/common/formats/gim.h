#include <cxxtest/TestSuite.h>

#include "common/formats/gim.h"
#include "common/memstream.h"

class GIMTestSuite : public CxxTest::TestSuite {
public:
	static void w16(byte *&p, uint16 v) { *p++ = v; *p++ = v >> 8; }
	static void w32(byte *&p, uint32 v) { for (int i = 0; i < 4; ++i) *p++ = v >> (i * 8); }

	void testRGBA8888Image() {
		byte data[256] = {};
		memcpy(data, "MIG.00.1PSP", 11);
		byte *p = data + 16;
		w16(p, 2); w16(p, 0); w32(p, 0); w32(p, 0); w32(p, 16);
		byte *root = p - 16;
		byte *picture = p;
		w16(p, 3); w16(p, 0); w32(p, 0); w32(p, 0); w32(p, 16);
		byte *image = p;
		w16(p, 4); w16(p, 0); w32(p, 0); w32(p, 0); w32(p, 16);
		byte *block = image;
		(void)picture;
		w16(p, 0x30); w16(p, 0); w16(p, 3); w16(p, 0); w16(p, 2); w16(p, 1);
		w16(p, 32); w16(p, 0x10); w16(p, 8); w16(p, 2); w32(p, 0); w32(p, 0x30); w32(p, 0x40); w32(p, 0x48);
		w32(p, 0); w16(p, 1); w16(p, 1); w16(p, 3); w16(p, 1);
		w32(p, 0x40);
		p += 16; // index area ends at the frame offset
		*p++ = 255; *p++ = 0; *p++ = 0; *p++ = 255;
		*p++ = 0; *p++ = 255; *p++ = 0; *p++ = 128;
		uint32 imageSize = (uint32)(p - block);
		block[4] = imageSize; block[5] = imageSize >> 8; block[6] = imageSize >> 16; block[7] = imageSize >> 24;
		uint32 pictureSize = (uint32)(p - (picture));
		picture[4] = pictureSize; picture[5] = pictureSize >> 8; picture[6] = pictureSize >> 16; picture[7] = pictureSize >> 24;
		uint32 rootSize = (uint32)(p - root);
		root[4] = rootSize; root[5] = rootSize >> 8; root[6] = rootSize >> 16; root[7] = rootSize >> 24;
		Common::MemoryReadStream stream(data, (uint32)(p - data));
		Common::GIM gim(&stream);
		TS_ASSERT(gim.ok());
		TS_ASSERT_EQUALS(gim.width(), 2);
		TS_ASSERT_EQUALS(gim.height(), 1);
		TS_ASSERT_EQUALS(gim.getSurface()->format.bytesPerPixel, 4);
	}
};
