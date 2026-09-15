#include "ikura/formats/archive/cabinet.h"

#include "common/memstream.h"
#include "common/path.h"

#include <cxxtest/TestSuite.h>

class IkuraCabinetTestSuite : public CxxTest::TestSuite {
public:
	Ikura::Format::Archive::Cabinet *openBytes(const byte *data, uint32 size) {
		Common::MemoryReadStream *stream = new Common::MemoryReadStream(data, size, DisposeAfterUse::NO);
		return Ikura::Format::Archive::Cabinet::open(stream);
	}

	void test_sm2mpx10_valid() {
		static const byte kArchive[] = {
			'S', 'M', '2', 'M', 'P', 'X', '1', '0', 0, 0, 0, 0,
			52, 0, 0, 0, // dlen
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			32, 0, 0, 0, // dstart
			'T', 'E', 'S', 'T', '.', 'D', 'A', 'T', 0, 0, 0, 0, // entry name
			52, 0, 0, 0, // entry start
			4, 0, 0, 0,  // entry size
			'D', 'A', 'T', 'A' // payload
		};

		Ikura::Format::Archive::Cabinet *cabinet = openBytes(kArchive, sizeof(kArchive));
		TS_ASSERT(cabinet != nullptr);
		if (!cabinet)
			return;

		TS_ASSERT(cabinet->hasFile(Common::Path("TEST.DAT")));
		Common::SeekableReadStream *member = cabinet->createReadStreamForMember(Common::Path("TEST.DAT"));
		TS_ASSERT(member != nullptr);
		if (member) {
			char buf[5] = {0};
			TS_ASSERT_EQUALS(member->read(buf, 4), 4u);
			TS_ASSERT_SAME_DATA(buf, "DATA", 4);
			delete member;
		}
		delete cabinet;
	}

	void test_truncated_header_rejected() {
		static const byte kTooShort[] = {'S', 'M', '2', 'M', 'P', 'X', '1', '0'};
		TS_ASSERT(openBytes(kTooShort, sizeof(kTooShort)) == nullptr);
	}

	void test_directory_larger_than_file_rejected() {
		static const byte kBadDlen[] = {
			'S', 'M', '2', 'M', 'P', 'X', '1', '0', 0, 0, 0, 0,
			0xE8, 0x03, 0, 0, // dlen = 1000, far larger than this 32-byte file
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			32, 0, 0, 0
		};
		TS_ASSERT(openBytes(kBadDlen, sizeof(kBadDlen)) == nullptr);
	}

	void test_entry_past_eof_rejected() {
		static const byte kBadEntry[] = {
			'S', 'M', '2', 'M', 'P', 'X', '1', '0', 0, 0, 0, 0,
			52, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			32, 0, 0, 0,
			'T', 'E', 'S', 'T', '.', 'D', 'A', 'T', 0, 0, 0, 0,
			0xE8, 0x03, 0, 0, // start = 1000, past EOF
			4, 0, 0, 0
		};
		// No payload bytes - the only entry claims an out-of-range offset,
		// so the whole cabinet must be rejected rather than silently empty.
		TS_ASSERT(openBytes(kBadEntry, sizeof(kBadEntry)) == nullptr);
	}

	void test_neither_variant_matches_rejected() {
		static const byte kGarbage[32] = {0};
		TS_ASSERT(openBytes(kGarbage, sizeof(kGarbage)) == nullptr);
	}
};
