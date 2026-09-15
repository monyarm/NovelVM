#include "ikura/script/interpreter.h"

#include "common/memstream.h"

#include <cxxtest/TestSuite.h>

// Fixtures are minimal synthetic ISF scripts (built with a throwaway
// Python script, not hand-typed) - each just enough bytecode to exercise
// one interpreter behavior. Ikura::VM::Context exposes getValue()/
// getFlag() directly, so most tests just run one instruction and read
// the resulting VM state back, rather than needing a second instruction
// to "report" it.
class IkuraInterpreterTestSuite : public CxxTest::TestSuite {
public:
	static Ikura::VM::Context *load(const byte *data, uint32 size) {
		Common::MemoryReadStream stream(data, size);
		Ikura::Format::Script::Script *script = Ikura::Format::Script::Script::load(stream);
		if (!script)
			return nullptr;
		return new Ikura::VM::Context(script);
	}

	void test_hs_sets_variable() {
		static const byte kHsFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x14, 0x00,
			0xA8, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kHsFixture, sizeof(kHsFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(ctx->getValue(5), 42);
		delete ctx;
	}

	void test_hinc_and_hdec() {
		static const byte kHincFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x10, 0x14, 0x00,
		};
		Ikura::VM::Context *ctx = load(kHincFixture, sizeof(kHincFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(ctx->getValue(5), 1); // fresh variable starts at 0
		delete ctx;

		static const byte kHdecFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x10, 0x14, 0x00,
		};
		ctx = load(kHdecFixture, sizeof(kHdecFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(ctx->getValue(5), -1);
		delete ctx;
	}

	void test_jp_follows_jumptable() {
		static const byte kJpFixture[] = {
			0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
			0x10, 0x10, 0x00, 0x00, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kJpFixture, sizeof(kJpFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // JP
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kEnd); // landed on ED
		delete ctx;
	}

	void test_js_call_resumes_after_return() {
		static const byte kJsRtFixture[] = {
			0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
			0x14, 0x10, 0x00, 0x00, 0x00, 0x08, 0x05, 0x20, 0x24, 0x00, 0x8D, 0x00,
			0x00, 0x00, 0x18, 0x08,
		};
		Ikura::VM::Context *ctx = load(kJsRtFixture, sizeof(kJsRtFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // JS -> call
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // HS var9=99 inside the subroutine
		TS_ASSERT_EQUALS(ctx->getValue(9), 99);
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // RT
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kEnd); // back after the JS, hits ED
		delete ctx;
	}

	void test_if_chain_and_then_set() {
		// Two AND-chained comparisons (10==10, 3<7), both true, second
		// one's thenOp sets var20=55.
		static const byte kIfFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1D, 0x70, 0x28, 0x00,
			0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x08, 0x0C, 0x00, 0x00, 0x00,
			0x04, 0x1C, 0x00, 0x00, 0x00, 0x04, 0x50, 0x00, 0xDC, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kIfFixture, sizeof(kIfFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(ctx->getValue(20), 55);
		delete ctx;
	}

	void test_onjp_picks_indexed_target() {
		// selector=1 of 2 targets: must land on the second ED-marker
		// (var2=222), not the first (var1=111).
		static const byte kOnjpFixture[] = {
			0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00,
			0x4C, 0x00, 0x00, 0x00, 0x1C, 0x2C, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00,
			0x00, 0x04, 0x00, 0x05, 0x20, 0x04, 0x00, 0xBD, 0x00, 0x00, 0x00, 0x05,
			0x20, 0x08, 0x00, 0x7B, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kOnjpFixture, sizeof(kOnjpFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // ONJP
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // HS var2=222
		TS_ASSERT_EQUALS(ctx->getValue(1), 0);   // never touched
		TS_ASSERT_EQUALS(ctx->getValue(2), 222);
		delete ctx;
	}

	void test_sk_set_flag() {
		static const byte kSkSetFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC4, 0x14, 0x1C, 0x00,
			0x04,
		};
		Ikura::VM::Context *ctx = load(kSkSetFixture, sizeof(kSkSetFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		TS_ASSERT(!ctx->getFlag(7));
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT(ctx->getFlag(7));
		delete ctx;
	}

	void test_hf_jumps_only_when_flag_set() {
		static const byte kHfFixture[] = {
			0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00,
			0xCC, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x05, 0x20, 0x04, 0x00, 0x35, 0x00,
			0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kHfFixture, sizeof(kHfFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		ctx->setFlag(3, true);
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // HF, flag set -> jumps
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // HS var1=77
		TS_ASSERT_EQUALS(ctx->getValue(1), 77);
		delete ctx;
	}

	void test_unimplemented_opcode_reported() {
		static const byte kUnimplementedFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4D, 0x08,
		};
		Ikura::VM::Context *ctx = load(kUnimplementedFixture, sizeof(kUnimplementedFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kUnimplementedOpcode);
		TS_ASSERT_EQUALS(opcode, 0x53);
		delete ctx;
	}

	void test_malformed_payload_length_reported() {
		// HS (needs exactly 6 payload bytes) with only 4.
		static const byte kMalformedFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x18, 0x14, 0x00,
			0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kMalformedFixture, sizeof(kMalformedFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kMalformedOpcode);
		delete ctx;
	}
};
