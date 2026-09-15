#include "ikura/runtime/loader.h"

#include "common/memstream.h"
#include "ikura/script/interpreter.h"

#include <cxxtest/TestSuite.h>

// Proves the full production pipeline connects: a synthetic "isf" cabinet
// (same SM2MPX10 format as test/engines/ikura/cabinet.h) holding one
// TITLE.ISF member (same encrypted-ISF format as test/engines/ikura/
// script.h) gets parsed by the real Cabinet/Script/Context/interpreter
// classes end to end, with no game data required.
class IkuraLoaderTestSuite : public CxxTest::TestSuite {
public:
	// Cabinet: 32-byte header (dlen=52, dstart=32) + one 20-byte directory
	// entry named "TITLE.ISF" (start=52, size=10) + a 10-byte ISF payload:
	// header ssoffset=8 (0 jump table entries), body is the rotate-
	// encrypted form of opcode 0x00 (IOP_ED) with length field 2, i.e.
	// a script that immediately ends.
	static const byte *cabinetFixture() {
		static const byte kCabinet[] = {
			'S', 'M', '2', 'M', 'P', 'X', '1', '0', 0, 0, 0, 0,
			52, 0, 0, 0, // dlen
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			32, 0, 0, 0, // dstart
			'T', 'I', 'T', 'L', 'E', '.', 'I', 'S', 'F', 0, 0, 0, // entry name
			52, 0, 0, 0, // entry start
			10, 0, 0, 0, // entry size
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, // ISF payload
		};
		return kCabinet;
	}
	static const uint32 kCabinetSize = 62;

	static Common::SeekableReadStream *stream(const byte *data, uint32 size) {
		return new Common::MemoryReadStream(data, size, DisposeAfterUse::NO);
	}

	void test_loads_and_runs_title_script_to_end() {
		Ikura::VM::Context *ctx = Ikura::loadTitleScript(stream(cabinetFixture(), kCabinetSize));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		byte lastOpcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::run(*ctx, lastOpcode), (int)Ikura::VM::StepResult::kEnd);
		TS_ASSERT_EQUALS(lastOpcode, 0x00);
		delete ctx;
	}

	void test_missing_title_entry_rejected() {
		static const byte kNoTitle[] = {
			'S', 'M', '2', 'M', 'P', 'X', '1', '0', 0, 0, 0, 0,
			52, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			32, 0, 0, 0,
			'O', 'T', 'H', 'E', 'R', '.', 'I', 'S', 'F', 0, 0, 0,
			52, 0, 0, 0,
			10, 0, 0, 0,
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
		};
		TS_ASSERT(Ikura::loadTitleScript(stream(kNoTitle, sizeof(kNoTitle))) == nullptr);
	}

	void test_malformed_cabinet_rejected() {
		static const byte kGarbage[32] = {0};
		TS_ASSERT(Ikura::loadTitleScript(stream(kGarbage, sizeof(kGarbage))) == nullptr);
	}
};
