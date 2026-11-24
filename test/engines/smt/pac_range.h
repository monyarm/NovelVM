#include <cxxtest/TestSuite.h>

#include "smt/formats/archive/pac.h"
#include "common/memstream.h"

class PACRangeTestSuite : public CxxTest::TestSuite {
public:
	void testPointerArchiveKeepsMemberRange() {
		byte data[4 + 36 + 4] = {0};
		data[0] = 1;
		memcpy(data + 4, "model.rmd", 10);
		data[4 + 32] = 4;
		memcpy(data + 4 + 36, "RMD!", 4);

		SMT::Format::Archive::PAC archive(new Common::MemoryReadStream(data, sizeof(data), DisposeAfterUse::NO));
		uint32 offset = 0, size = 0;
		TS_ASSERT(archive.getMemberRange("model.rmd", offset, size));
		TS_ASSERT_EQUALS(offset, 40U);
		TS_ASSERT_EQUALS(size, 4U);
		Common::SeekableReadStream *member = archive.createReadStreamForMember("model.rmd");
		TS_ASSERT(member);
		if (!member)
			return;
		byte actual[4] = {0};
		TS_ASSERT_EQUALS(member->read(actual, sizeof(actual)), sizeof(actual));
		TS_ASSERT_EQUALS(memcmp(actual, "RMD!", 4), 0);
		delete member;
	}
};
