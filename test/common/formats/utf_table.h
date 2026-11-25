#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/formats/utf_table.h"
#include "common/str.h"

/**
 * Synthetic @UTF tables only. kPlain is a one-row table with a per-row u16
 * "A" = 0x1234, a constant u8 "B" = 7 and a per-row string "S" = "hi";
 * its table name is "T".
 */
class UTFTableTestSuite : public CxxTest::TestSuite {
public:
	void testPlainTable() {
		Common::UTFTable t;
		TS_ASSERT(t.parse(kPlain, sizeof(kPlain)));
		TS_ASSERT_EQUALS(t.numRows(), 1);
		TS_ASSERT(t.get(0, "A").valid);
		TS_ASSERT_EQUALS(t.get(0, "A").u, 0x1234u);
		TS_ASSERT_EQUALS(t.get(0, "A").type, 0x2);
		TS_ASSERT_EQUALS(t.get(0, "B").u, 7u);
		TS_ASSERT_EQUALS(t.get(0, "S").str, Common::String("hi"));
		TS_ASSERT_EQUALS(t.get(0, "S").asString(), Common::String("hi"));
		TS_ASSERT(!t.get(0, "T").valid);
		TS_ASSERT(!t.get(0, "a").valid);
		TS_ASSERT(!t.get(1, "A").valid);
		TS_ASSERT(!t.get(-1, "A").valid);
		TS_ASSERT_EQUALS(t.get(1, "A").asUint(42), 42u);
		TS_ASSERT_EQUALS(t.dataSegmentSize(), 0u);
	}

	void testTrailingBytesIgnored() {
		Common::Array<byte> buf = plain();
		buf.push_back(0xAA);
		buf.push_back(0xBB);
		Common::UTFTable t;
		TS_ASSERT(t.parse(buf.data(), buf.size()));
		TS_ASSERT_EQUALS(t.dataSegmentSize(), 0u);
		TS_ASSERT_EQUALS(t.get(0, "A").u, 0x1234u);
	}

	void testEmptyState() {
		Common::UTFTable t;
		TS_ASSERT_EQUALS(t.numRows(), 0);
		TS_ASSERT(t.dataSegment() == nullptr);
		TS_ASSERT_EQUALS(t.dataSegmentSize(), 0u);
		TS_ASSERT(!t.get(0, "A").valid);

		// A failed parse wipes an earlier successful one.
		TS_ASSERT(t.parse(kPlain, sizeof(kPlain)));
		TS_ASSERT(!t.parse(kPlain, 0x10));
		TS_ASSERT_EQUALS(t.numRows(), 0);
		TS_ASSERT(t.dataSegment() == nullptr);
		TS_ASSERT(!t.get(0, "A").valid);
	}

	void testNoValueColumn() {
		// "A" becomes 0x12: named, no stored value. Its 2 row bytes are now
		// unused, which is allowed.
		Common::Array<byte> buf = plain();
		buf[0x20] = 0x12;
		Common::UTFTable t;
		TS_ASSERT(t.parse(buf.data(), buf.size()));
		TS_ASSERT(!t.get(0, "A").valid);
		TS_ASSERT_EQUALS(t.get(0, "A").type, 0x2);
		TS_ASSERT_EQUALS(t.get(0, "A").asUint(5), 5u);
		// "S" now starts at row offset 0 and reads 0x12340000 as its string
		// offset, which is out of bounds.
		TS_ASSERT(!t.get(0, "S").valid);
	}

	void testMasking() {
		Common::Array<byte> buf = plain();
		TS_ASSERT(!Common::UTFTable::isMasked(buf.data(), buf.size()));
		Common::UTFTable::toggleMask(buf.data(), buf.size());
		TS_ASSERT_EQUALS(buf.size(), sizeof(kMasked));
		TS_ASSERT_SAME_DATA(buf.data(), kMasked, sizeof(kMasked));
		TS_ASSERT(Common::UTFTable::isMasked(buf.data(), buf.size()));
		TS_ASSERT(!Common::UTFTable::isMasked(buf.data(), 3));

		// parse() never unmasks by itself.
		Common::UTFTable t;
		TS_ASSERT(!t.parse(buf.data(), buf.size()));

		Common::UTFTable::toggleMask(buf.data(), buf.size());
		TS_ASSERT_SAME_DATA(buf.data(), kPlain, sizeof(kPlain));
		TS_ASSERT(t.parse(buf.data(), buf.size()));
		TS_ASSERT_EQUALS(t.get(0, "S").str, Common::String("hi"));
	}

	void testHostileInputs() {
		Common::UTFTable t;
		for (uint32 len = 0; len < 0x20; ++len)
			TS_ASSERT(!t.parse(kPlain, len));

		Common::Array<byte> buf = plain();
		buf[0x0A] = 0x00;
		buf[0x0B] = 0x30;
		TS_ASSERT(!t.parse(buf.data(), buf.size()));

		const byte badDesc[] = { 0x00, 0x42, 0x9A, 0x5D };
		for (uint i = 0; i < sizeof(badDesc); ++i) {
			buf = plain();
			buf[0x20] = badDesc[i];
			TS_ASSERT(!t.parse(buf.data(), buf.size()));
		}

		buf = plain();
		buf[0x1C] = 0x7F;
		buf[0x1D] = 0xFF;
		buf[0x1E] = 0xFF;
		buf[0x1F] = 0xFF;
		TS_ASSERT(!t.parse(buf.data(), buf.size()));

		buf = plain();
		buf[0x21] = 0x00;
		buf[0x22] = 0x00;
		buf[0x23] = 0x00;
		buf[0x24] = 0xFF;
		TS_ASSERT(!t.parse(buf.data(), buf.size()));
		TS_ASSERT_EQUALS(t.numRows(), 0);
	}

private:
	static const byte kPlain[0x48];
	static const byte kMasked[0x48];

	static Common::Array<byte> plain() {
		return Common::Array<byte>(kPlain, sizeof(kPlain));
	}
};

const byte UTFTableTestSuite::kPlain[0x48] = {
	0x40, 0x55, 0x54, 0x46, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x2e,
	0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x07, 0x00, 0x03, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01,
	0x52, 0x00, 0x00, 0x00, 0x09, 0x30, 0x00, 0x00, 0x00, 0x0b, 0x07, 0x5a, 0x00, 0x00, 0x00, 0x0d,
	0x12, 0x34, 0x00, 0x00, 0x00, 0x0f, 0x3c, 0x4e, 0x55, 0x4c, 0x4c, 0x3e, 0x00, 0x54, 0x00, 0x41,
	0x00, 0x42, 0x00, 0x53, 0x00, 0x68, 0x69, 0x00
};

const byte UTFTableTestSuite::kMasked[0x48] = {
	0x1f, 0x9e, 0xf3, 0xf5, 0xaf, 0x5b, 0x77, 0x83, 0xff, 0xeb, 0x47, 0xfb, 0x4f, 0x7b, 0x17, 0xcd,
	0x9f, 0x0b, 0xe7, 0xb3, 0xef, 0x9b, 0xb7, 0x04, 0x3f, 0x28, 0x87, 0x15, 0x8f, 0xbb, 0x57, 0x22,
	0x8d, 0x4b, 0x27, 0x33, 0x26, 0xeb, 0xf7, 0x43, 0x7f, 0x60, 0xc0, 0x09, 0xcf, 0xfb, 0x97, 0x6e,
	0x0d, 0xbf, 0x67, 0x73, 0x6f, 0x14, 0x0b, 0xcd, 0xea, 0xe7, 0x4b, 0xad, 0x0f, 0x6f, 0xd7, 0xe2,
	0x5f, 0x89, 0xa7, 0xe0, 0xaf, 0x33, 0x1e, 0xc3
};
