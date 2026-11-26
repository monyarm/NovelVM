#include "ikura/formats/script/script.h"

#include "common/memstream.h"

#include <cxxtest/TestSuite.h>

class IkuraScriptTestSuite : public CxxTest::TestSuite {
public:
	// ISF fixture: ssoffset=16 (2 jump table entries), jumpTable[0]=0,
	// jumpTable[1]=5. Script body: opcode 0x2B len=3 payload
	// (0xAA,0xBB,0xCC) at offset 16, then opcode 0x00 len=0 at offset 21.
	// Bytes are the rotate-"encrypted" (not real encryption) on-disk form
	// - built with a throwaway Python script, not hand-typed.
	static const byte *isfFixture() {
		static const byte kISF[] = {
			0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00,
			0xAC, 0x14, 0xAA, 0xEE, 0x33, 0x00, 0x08,
		};
		return kISF;
	}
	static const uint32 kISFSize = 23;

	void test_isf_walks_opcodes_and_ends() {
		Common::MemoryReadStream stream(isfFixture(), kISFSize);
		Ikura::Format::Script::Script *script = Ikura::Format::Script::Script::load(stream);
		TS_ASSERT(script != nullptr);
		if (!script)
			return;

		byte opcode;
		const byte *data;
		uint32 length;
		TS_ASSERT(script->getOpcode(opcode, data, length));
		TS_ASSERT_EQUALS(opcode, 0x2B);
		TS_ASSERT_EQUALS(length, 3u);
		TS_ASSERT_EQUALS(data[0], 0xAA);
		TS_ASSERT_EQUALS(data[1], 0xBB);
		TS_ASSERT_EQUALS(data[2], 0xCC);

		TS_ASSERT(script->getOpcode(opcode, data, length));
		TS_ASSERT_EQUALS(opcode, 0x00);
		TS_ASSERT_EQUALS(length, 0u);

		TS_ASSERT(!script->getOpcode(opcode, data, length));
		delete script;
	}

	void test_isf_jump_to_second_entry() {
		Common::MemoryReadStream stream(isfFixture(), kISFSize);
		Ikura::Format::Script::Script *script = Ikura::Format::Script::Script::load(stream);
		TS_ASSERT(script != nullptr);
		if (!script)
			return;

		TS_ASSERT(script->jump(1));
		byte opcode;
		const byte *data;
		uint32 length;
		TS_ASSERT(script->getOpcode(opcode, data, length));
		TS_ASSERT_EQUALS(opcode, 0x00);
		TS_ASSERT_EQUALS(length, 0u);
		delete script;
	}

	void test_isf_call_then_return() {
		Common::MemoryReadStream stream(isfFixture(), kISFSize);
		Ikura::Format::Script::Script *script = Ikura::Format::Script::Script::load(stream);
		TS_ASSERT(script != nullptr);
		if (!script)
			return;

		byte opcode;
		const byte *data;
		uint32 length;
		TS_ASSERT(script->getOpcode(opcode, data, length)); // consumes the 0x2B entry, now at offset 21

		TS_ASSERT(script->call(0)); // pushes 21, jumps to jumpTable[0] = offset 16
		TS_ASSERT(script->getOpcode(opcode, data, length));
		TS_ASSERT_EQUALS(opcode, 0x2B); // back at the start of the script body

		TS_ASSERT(script->ret()); // pops back to the saved offset 21
		TS_ASSERT(script->getOpcode(opcode, data, length));
		TS_ASSERT_EQUALS(opcode, 0x00);
		delete script;
	}

	void test_jump_out_of_range_rejected() {
		Common::MemoryReadStream stream(isfFixture(), kISFSize);
		Ikura::Format::Script::Script *script = Ikura::Format::Script::Script::load(stream);
		TS_ASSERT(script != nullptr);
		if (!script)
			return;
		TS_ASSERT(!script->jump(99));
		delete script;
	}

	void test_truncated_header_rejected() {
		static const byte kTooShort[4] = {0, 0, 0, 0};
		Common::MemoryReadStream stream(kTooShort, sizeof(kTooShort));
		TS_ASSERT(Ikura::Format::Script::Script::load(stream) == nullptr);
	}

	void test_misaligned_jumptable_rejected() {
		// ssoffset=9: (9-8) isn't a multiple of 4.
		static const byte kBadAlign[16] = {9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
		Common::MemoryReadStream stream(kBadAlign, sizeof(kBadAlign));
		TS_ASSERT(Ikura::Format::Script::Script::load(stream) == nullptr);
	}

	// DRS fixture: header + a 1-entry jump table (jumpTable[0]=0) + two
	// empty skipped tables + a script body of one opcode (0x00, len=0)
	// at offset 57. Built the same way as the ISF fixture.
	static const byte *drsFixture() {
		static const byte kDRS[] = {
			0x44, 0x69, 0x67, 0x69, 0x74, 0x61, 0x6C, 0x52, 0x6F, 0x6D, 0x61, 0x6E,
			0x63, 0x65, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x2E, 0x00, 0x00, 0x00,
			0x00, 0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x08,
		};
		return kDRS;
	}
	static const uint32 kDRSSize = 59;

	void test_drs_detected_and_walks_opcodes() {
		Common::MemoryReadStream stream(drsFixture(), kDRSSize);
		Ikura::Format::Script::Script *script = Ikura::Format::Script::Script::load(stream);
		TS_ASSERT(script != nullptr);
		if (!script)
			return;
		byte opcode;
		const byte *data;
		uint32 length;
		TS_ASSERT(script->getOpcode(opcode, data, length));
		TS_ASSERT_EQUALS(opcode, 0x00);
		TS_ASSERT_EQUALS(length, 0u);
		TS_ASSERT(script->jump(0)); // only table entry, points back to offset 0 of the script body
		delete script;
	}

	void test_drs_truncated_rejected() {
		// Full DRS magic (so it's correctly identified as DRS, not
		// mistaken for ISF), but cut off before the unencrypted 0x19-byte
		// header prefix even finishes.
		static const byte kTooShort[23] = {
			'D', 'i', 'g', 'i', 't', 'a', 'l', 'R', 'o', 'm', 'a', 'n',
			'c', 'e', 'S', 'y', 's', 't', 'e', 'm', '.', 0, 0,
		};
		Common::MemoryReadStream stream(kTooShort, sizeof(kTooShort));
		TS_ASSERT(Ikura::Format::Script::Script::load(stream) == nullptr);
	}
};
