#include <cxxtest/TestSuite.h>

#include "smt/formats/archive/pac.h"
#include "common/memstream.h"

class PACTestSuite : public CxxTest::TestSuite {
public:
	void testReadsMemberFromMemoryArchive() {
		byte data[4 + 36 + 4] = {0};
		data[0] = 1; // one Version2 entry
		memcpy(data + 4, "image.tmx", 9);
		data[4 + 32] = 4; // member size
		memcpy(data + 4 + 36, "GIM!", 4);

		Common::MemoryReadStream source(data, sizeof(data), DisposeAfterUse::NO);
		SMT::Format::Archive::PAC archive(source);
		Common::SeekableReadStream *member = archive.createReadStreamForMember("image.tmx");
		TS_ASSERT(member);
		if (!member)
			return;

		byte actual[4] = {0};
		TS_ASSERT_EQUALS(member->read(actual, sizeof(actual)), sizeof(actual));
		TS_ASSERT_EQUALS(memcmp(actual, "GIM!", 4), 0);
		delete member;
	}
};
