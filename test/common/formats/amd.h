#include <cxxtest/TestSuite.h>

#include "common/formats/amd.h"
#include "common/memstream.h"

class AMDTestSuite : public CxxTest::TestSuite {
public:
	static void w16(byte *&p, uint16 v) { *p++ = v; *p++ = v >> 8; }
	static void w32(byte *&p, uint32 v) { for (int i = 0; i < 4; ++i) *p++ = v >> (i * 8); }

	void testRejectsTruncatedChunk() {
		byte data[32] = {};
		memcpy(data, "CHNK", 4);
		data[4] = 1;
		Common::MemoryReadStream stream(data, sizeof(data));
		Common::AMD amd(&stream);
		TS_ASSERT(!amd.ok());
	}

	void testRejectsNonAMD() {
		byte data[32] = {};
		memcpy(data, "OMG.00.1PSP", 11);
		Common::MemoryReadStream stream(data, sizeof(data));
		Common::AMD amd(&stream);
		TS_ASSERT(!amd.ok());
	}
};
