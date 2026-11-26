#include "ikura/script/interpreter.h"

#include "audio/mixer_intern.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/memstream.h"
#include "common/system.h"
#include "ikura/runtime/audio.h"
#include "ikura/runtime/input.h"
#include "ikura/runtime/presentation.h"
#include "../../system/null_osystem.h"

#include <cxxtest/TestSuite.h>

namespace {

// Minimal single-member in-memory Archive double, same as
// test/engines/ikura/presentation.h - kept separate since it's ~10 lines
// and pulling in a shared test-only header for that isn't worth it.
class InterpOneFileArchive : public Common::Archive {
public:
	InterpOneFileArchive(const Common::String &name, Common::Array<byte> data) : _name(name), _data(data) {}

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
Common::Array<byte> buildInterpGGAFixture(byte b, byte g, byte r, byte a) {
	Common::Array<byte> data(29, (byte)0);
	const char *magic = "GGA00000";
	for (int i = 0; i < 8; i++)
		data[i] = magic[i];
	data[8] = 1;
	data[10] = 1;
	data[16] = 24;
	data[20] = 5;
	data[24] = 12;
	data[25] = b;
	data[26] = g;
	data[27] = r;
	data[28] = a;
	return data;
}

// Smallest valid PCM WAV: RIFF/WAVE, one "fmt " chunk (8-bit mono @
// 8000Hz), one 1-byte "data" chunk. Same shape as test/engines/ikura/audio.h.
Common::Array<byte> buildInterpWAVFixture(byte sample) {
	static const byte kTemplate[] = {
		'R', 'I', 'F', 'F', 37, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x40, 0x1F, 0x00, 0x00, 0x40, 0x1F, 0x00, 0x00, 1, 0, 8, 0,
		'd', 'a', 't', 'a', 1, 0, 0, 0, 0,
	};
	Common::Array<byte> data(kTemplate, sizeof(kTemplate));
	data.back() = sample;
	return data;
}

} // namespace

// Fixtures are minimal synthetic ISF scripts (built with a throwaway
// Python script, not hand-typed) - each just enough bytecode to exercise
// one interpreter behavior. Ikura::VM::Context exposes getValue()/
// getFlag() directly, so most tests just run one instruction and read
// the resulting VM state back, rather than needing a second instruction
// to "report" it.
class IkuraInterpreterTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

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
		// 0x48 (IOP_EXA) - deliberately unimplemented: not implemented in
		// the reference either (falls through its own dispatch to
		// iop_unknown, and is even commented out of its shared no-op
		// case block), so this isn't a gap on our side to close.
		static const byte kUnimplementedFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x0C, 0x00,
		};
		Ikura::VM::Context *ctx = load(kUnimplementedFixture, sizeof(kUnimplementedFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kUnimplementedOpcode);
		TS_ASSERT_EQUALS(opcode, 0x48);
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

	void test_sts_sets_system_flag() {
		static const byte kStsFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x28, 0x14, 0x00,
			0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kStsFixture, sizeof(kStsFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		TS_ASSERT(!ctx->getSystem(5));
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT(ctx->getSystem(5));
		delete ctx;
	}

	void test_ssp_sets_system_flag() {
		static const byte kSspFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x31, 0x14, 0x1C, 0x00,
			0x24,
		};
		Ikura::VM::Context *ctx = load(kSspFixture, sizeof(kSspFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT(ctx->getSystem(9));
		delete ctx;
	}

	void test_exc_exs_are_noops() {
		static const byte kExcExsFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x08, 0x25, 0x08,
		};
		Ikura::VM::Context *ctx = load(kExcExsFixture, sizeof(kExcExsFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		delete ctx;
	}

	void test_calc_accumulates_add_and_subtract() {
		// result = 0 + 10 - 3 = 7, stored to var 3.
		static const byte kCalcFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x3C, 0x0C, 0x00,
			0x00, 0x28, 0x00, 0x00, 0x00, 0x04, 0x0C, 0x00, 0x00, 0x00, 0x14, 0x00,
			0x08,
		};
		Ikura::VM::Context *ctx = load(kCalcFixture, sizeof(kCalcFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(ctx->getValue(3), 7);
		delete ctx;
	}

	void test_calc_unknown_operator_leaves_dest_untouched() {
		// var4 starts at 77 (via HS); CALC hits an unknown operator (0xFF)
		// mid-expression and must abort without writing var4 at all - the
		// reference logs and returns before reaching its SetValue call.
		static const byte kCalcUnknownOpFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x10, 0x00,
			0x35, 0x00, 0x00, 0x00, 0x11, 0x28, 0x10, 0x00, 0x00, 0x14, 0x00, 0x00,
			0x00, 0xFF, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kCalcUnknownOpFixture, sizeof(kCalcUnknownOpFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // HS var4=77
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // CALC aborts silently
		TS_ASSERT_EQUALS(ctx->getValue(4), 77);
		delete ctx;
	}

	void test_calc_division_by_zero_does_not_crash() {
		// The reference has no zero guard here and would crash; we skip
		// the divide instead, leaving the accumulator (and so var5) at 0.
		static const byte kCalcDivZeroFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x28, 0x14, 0x00,
			0x0C, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kCalcDivZeroFixture, sizeof(kCalcDivZeroFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(ctx->getValue(5), 0);
		delete ctx;
	}

	void test_ml_then_ms_plays_then_stops_music() {
		static const byte kMlMsFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC1, 0x2C, 0x51, 0x2D,
			0xC0, 0xC4, 0xB8, 0x5D, 0x05, 0x59, 0x00, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kMlMsFixture, sizeof(kMlMsFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("TK01.WAV", buildInterpWAVFixture(200));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, &archive, nullptr);
		ctx->setAudio(&audio);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // ML
		TS_ASSERT(audio.isMusicPlaying());
		delete ctx;
	}

	void test_ml_without_audio_is_noop() {
		static const byte kMlOnlyFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC1, 0x2C, 0x51, 0x2D,
			0xC0, 0xC4, 0xB8, 0x5D, 0x05, 0x59, 0x00, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kMlOnlyFixture, sizeof(kMlOnlyFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		delete ctx;
	}

	void test_ses_stops_sound_same_as_set() {
		static const byte kSesFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xED, 0x28, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kSesFixture, sizeof(kSesFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("SE01.WAV", buildInterpWAVFixture(1));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, nullptr, &archive);
		audio.playSound("SE01.WAV", 2);
		TS_ASSERT(audio.isSoundPlaying(2));
		ctx->setAudio(&audio);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // SES, channel 2
		TS_ASSERT(!audio.isSoundPlaying(2));
		delete ctx;
	}

	void test_ser_then_set_plays_then_stops_sound() {
		static const byte kSerSetFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD1, 0x3C, 0x4D, 0x15,
			0xC0, 0xC4, 0xB8, 0x5D, 0x05, 0x59, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00,
			0x08,
		};
		Ikura::VM::Context *ctx = load(kSerSetFixture, sizeof(kSerSetFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("SE01.WAV", buildInterpWAVFixture(50));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, nullptr, &archive);
		ctx->setAudio(&audio);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // SER, channel 3
		TS_ASSERT(audio.isSoundPlaying(3));
		delete ctx;
	}

	void test_mp_is_noop() {
		static const byte kMpFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC5, 0x08,
		};
		Ikura::VM::Context *ctx = load(kMpFixture, sizeof(kMpFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		delete ctx;
	}

	void test_kidfn_is_noop() {
		static const byte kKidfnFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x96, 0x08,
		};
		Ikura::VM::Context *ctx = load(kKidfnFixture, sizeof(kKidfnFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		delete ctx;
	}

	void test_wo_and_wc_are_noops() {
		static const byte kWoWcFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA4, 0x08, 0xA8, 0x08,
		};
		Ikura::VM::Context *ctx = load(kWoWcFixture, sizeof(kWoWcFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // WO
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // WC
		delete ctx;
	}

	void test_timerset_then_timerget_reads_small_elapsed() {
		static const byte kTimerFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEB, 0x18, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x10, 0x24, 0x00,
		};
		Ikura::VM::Context *ctx = load(kTimerFixture, sizeof(kTimerFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // TIMERSET, duration=0
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // TIMERGET -> var9
		// Not exact (real wall clock) - just proves it's a small non-negative elapsed, not garbage.
		TS_ASSERT(ctx->getValue(9) >= 0);
		TS_ASSERT(ctx->getValue(9) < 1000);
		delete ctx;
	}

	void test_stx_jumps_when_system_flag_matches_val() {
		static const byte kStxJumpFixture[] = {
			0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00,
			0xDC, 0x28, 0x14, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xE8, 0x18,
			0x14, 0x04, 0x00, 0x00, 0x05, 0x20, 0x04, 0x00, 0x9F, 0x0C, 0x00, 0x00,
			0x00, 0x08, 0x05, 0x20, 0x08, 0x00, 0xE1, 0x0C, 0x00, 0x00, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kStxJumpFixture, sizeof(kStxJumpFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // STS: system(5)=true
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // STX: system(5)==true(val!=0) -> jumps
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // lands on the jump target
		TS_ASSERT_EQUALS(ctx->getValue(1), 0);   // fallthrough instruction never ran
		TS_ASSERT_EQUALS(ctx->getValue(2), 888); // jump target's instruction did
		delete ctx;
	}

	void test_stx_falls_through_when_system_flag_does_not_match_val() {
		static const byte kStxNoJumpFixture[] = {
			0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x00,
			0xDC, 0x28, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x18,
			0x14, 0x04, 0x00, 0x00, 0x05, 0x20, 0x04, 0x00, 0x9F, 0x0C, 0x00, 0x00,
			0x00, 0x08, 0x05, 0x20, 0x08, 0x00, 0xE1, 0x0C, 0x00, 0x00, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kStxNoJumpFixture, sizeof(kStxNoJumpFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // STS: system(5)=false
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // STX: system(5)==false, val!=0 -> no jump
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // falls through
		TS_ASSERT_EQUALS(ctx->getValue(1), 999); // fallthrough instruction ran
		TS_ASSERT_EQUALS(ctx->getValue(2), 0);   // jump target never reached
		delete ctx;
	}

	void test_kidclr_kidscan_mf_are_noops() {
		static const byte kNoopFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x82, 0x08, 0x9E, 0x08, 0xC9, 0x08,
		};
		Ikura::VM::Context *ctx = load(kNoopFixture, sizeof(kNoopFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // KIDCLR
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // KIDSCAN
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // MF
		delete ctx;
	}

	// The point of this test: navigate a whole two-line scene using only
	// the VM's wait/resume state machine - no image cabinet, no decoded
	// graphics, nothing visual at all. Presentation(nullptr) means any
	// accidental attempt to touch a real cabinet would crash instead of
	// silently passing.
	void test_navigates_a_two_line_scene_without_any_graphics() {
		static const byte kSceneFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAC, 0x1C, 0x00, 0xFF,
			0x21, 0x25, 0x00, 0xAC, 0x20, 0x00, 0xFF, 0x09, 0x65, 0x15, 0x00, 0x00,
			0x08,
		};
		Ikura::VM::Context *ctx = load(kSceneFixture, sizeof(kSceneFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		Ikura::Runtime::Presentation presentation(nullptr);
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::run(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForAdvance);
		TS_ASSERT_EQUALS(presentation.currentText(), Common::String("HI"));

		// Simulate the player advancing (matches ikura.cpp's own gating:
		// don't call run() again until an advance signal arrives).
		TS_ASSERT_EQUALS((int)Ikura::VM::run(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForAdvance);
		TS_ASSERT_EQUALS(presentation.currentText(), Common::String("BYE"));

		TS_ASSERT_EQUALS((int)Ikura::VM::run(*ctx, opcode), (int)Ikura::VM::StepResult::kEnd);
		delete ctx;
	}

	void test_pm_clear_text_command() {
		// win=0, cmd=0x03 (clear), then a second PM with cmd=0xFF text="X".
		static const byte kClearFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAC, 0x10, 0x00, 0x0C,
			0xAC, 0x18, 0x00, 0xFF, 0x61, 0x00,
		};
		Ikura::VM::Context *ctx = load(kClearFixture, sizeof(kClearFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		Ikura::Runtime::Presentation presentation(nullptr);
		ctx->setPresentation(&presentation);
		presentation.showText("stale");

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::run(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForAdvance); // clear only
		TS_ASSERT_EQUALS(presentation.currentText(), Common::String(""));
		TS_ASSERT_EQUALS((int)Ikura::VM::run(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForAdvance); // prints "X"
		TS_ASSERT_EQUALS(presentation.currentText(), Common::String("X"));
		delete ctx;
	}

	void test_pm_truncated_payload_is_malformed() {
		// cmd=0x11 (portrait index) but only 2 of its 4 operand bytes present.
		static const byte kTruncatedFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAC, 0x18, 0x00, 0x44, 0xAA, 0xEE,
		};
		Ikura::VM::Context *ctx = load(kTruncatedFixture, sizeof(kTruncatedFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kMalformedOpcode);
		delete ctx;
	}

	void test_clk_waits_for_advance() {
		static const byte kClkFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x08,
		};
		Ikura::VM::Context *ctx = load(kClkFixture, sizeof(kClkFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForAdvance);
		delete ctx;
	}

	void test_gc_fills_buffer_with_solid_color() {
		static const byte kGcFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4D, 0x24, 0x04, 0x00, 0x00, 0x00, 0x28, 0x50, 0x78,
		};
		Ikura::VM::Context *ctx = load(kGcFixture, sizeof(kGcFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("BG.GG0", buildInterpGGAFixture(1, 2, 3, 255));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadImage(1, "BG.GG0"); // size buffer 1 first - fillBuffer no-ops on an unsized buffer
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // GC: dst=1, r=10,g=20,b=30
		TS_ASSERT(presentation.buffer(1) != nullptr);
		if (!presentation.buffer(1))
			return;
		byte a, r, g, b;
		presentation.buffer(1)->format.colorToARGB(presentation.buffer(1)->getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 10);
		TS_ASSERT_EQUALS(g, 20);
		TS_ASSERT_EQUALS(b, 30);
		delete ctx;
	}

	void test_pb_sets_font_size_for_window_zero_only() {
		static const byte kPbFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9C, 0x1C, 0x00, 0x60, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kPbFixture, sizeof(kPbFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		Ikura::Runtime::Presentation presentation(nullptr);
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(presentation.fontSize(), 24);
		delete ctx;
	}

	void test_gge_blits_named_buffer_onto_screen_and_waits() {
		static const byte kGgeFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x38, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kGgeFixture, sizeof(kGgeFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("BG.GG0", buildInterpGGAFixture(200, 100, 50, 255));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadImage(1, "BG.GG0"); // num=1 in the fixture
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForAdvance);
		TS_ASSERT(presentation.buffer(0) != nullptr);
		if (!presentation.buffer(0))
			return;
		byte a, r, g, b;
		presentation.buffer(0)->format.colorToARGB(presentation.buffer(0)->getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 50);
		TS_ASSERT_EQUALS(g, 100);
		TS_ASSERT_EQUALS(b, 200);
		delete ctx;
	}

	void test_ls_replaces_running_script() {
		static const byte kMainFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x18, 0x4D, 0x55,
			0x09, 0x00,
		};
		static const byte kSubScript[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x24, 0x00,
			0x24, 0x0C, 0x00, 0x00, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kMainFixture, sizeof(kMainFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("SUB.ISF", Common::Array<byte>(kSubScript, sizeof(kSubScript)));
		ctx->setScriptCabinet(&archive);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // LS -> switches to SUB.ISF
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // SUB's HS var9=777
		TS_ASSERT_EQUALS(ctx->getValue(9), 777);
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kEnd); // SUB's own ED
		delete ctx;
	}

	void test_lsbs_calls_and_sret_resumes_caller() {
		static const byte kMainFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x18, 0x4D, 0x55,
			0x09, 0x00, 0x05, 0x20, 0x04, 0x00, 0xBD, 0x00, 0x00, 0x00, 0x00, 0x08,
		};
		static const byte kSubScript[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x24, 0x00,
			0x24, 0x0C, 0x00, 0x00, 0x0C, 0x08,
		};
		Ikura::VM::Context *ctx = load(kMainFixture, sizeof(kMainFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("SUB.ISF", Common::Array<byte>(kSubScript, sizeof(kSubScript)));
		ctx->setScriptCabinet(&archive);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // LSBS -> suspends main, switches to SUB.ISF
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // SUB's HS var9=777
		TS_ASSERT_EQUALS(ctx->getValue(9), 777);
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // SUB's SRET -> resumes main
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // main's HS var1=111
		TS_ASSERT_EQUALS(ctx->getValue(1), 111);
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kEnd); // main's own ED
		delete ctx;
	}

	void test_sret_without_a_call_reports_exhausted() {
		static const byte kSretAloneFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x08,
		};
		Ikura::VM::Context *ctx = load(kSretAloneFixture, sizeof(kSretAloneFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kScriptExhausted);
		delete ctx;
	}

	void test_ls_missing_script_is_noop() {
		static const byte kMainFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x18, 0x4D, 0x55,
			0x09, 0x00,
		};
		Ikura::VM::Context *ctx = load(kMainFixture, sizeof(kMainFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		// No scriptCabinet wired up at all.
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		delete ctx;
	}

	void test_igrelease_consumes_pending_signals() {
		static const byte kIgreleaseFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1A, 0x08,
		};
		Ikura::VM::Context *ctx = load(kIgreleaseFixture, sizeof(kIgreleaseFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		Ikura::Input input;
		Common::Event enter;
		enter.type = Common::EVENT_KEYDOWN;
		enter.kbd.keycode = Common::KEYCODE_RETURN;
		input.handleEvent(enter);
		ctx->setInput(&input);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT(!input.consumeAdvance()); // IGRELEASE already cleared it
		delete ctx;
	}

	void test_cwc_hides_text_window() {
		static const byte kCwcFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C, 0x0C, 0x00,
		};
		Ikura::VM::Context *ctx = load(kCwcFixture, sizeof(kCwcFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		Ikura::Runtime::Presentation presentation(nullptr);
		ctx->setPresentation(&presentation);
		TS_ASSERT(presentation.isTextVisible());

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT(!presentation.isTextVisible());
		delete ctx;
	}

	void test_timerend_waits_for_advance() {
		static const byte kTimerendFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEF, 0x08,
		};
		Ikura::VM::Context *ctx = load(kTimerendFixture, sizeof(kTimerendFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForAdvance);
		delete ctx;
	}

	void test_ig_yields_when_nothing_pressed() {
		static const byte kIgFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x20, 0x0C, 0x00,
			0x10, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kIgFixture, sizeof(kIgFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		Ikura::Input input;
		ctx->setInput(&input);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kYielded);
		TS_ASSERT_EQUALS(ctx->getValue(3), -1); // cur: no Presentation set, so no hotspot hit-test happens
		TS_ASSERT_EQUALS(ctx->getValue(4), 0);  // btn: nothing happened
		delete ctx;
	}

	void test_ig_reports_hotspot_index_but_yields_until_clicked() {
		static const byte kIgFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x20, 0x0C, 0x00,
			0x10, 0x00, 0x00, 0x00,
		};
		Ikura::Runtime::Presentation presentation(nullptr);
		presentation.defineHotspot(7, Common::Rect(0, 0, 100, 100));
		Ikura::Input input;
		Common::Event move;
		move.type = Common::EVENT_MOUSEMOVE;
		move.mouse = Common::Point(50, 50);
		input.handleEvent(move);

		Ikura::VM::Context *ctx1 = load(kIgFixture, sizeof(kIgFixture));
		TS_ASSERT(ctx1 != nullptr);
		if (!ctx1)
			return;
		ctx1->setPresentation(&presentation);
		ctx1->setInput(&input);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx1, opcode), (int)Ikura::VM::StepResult::kYielded); // hovering, not clicked
		TS_ASSERT_EQUALS(ctx1->getValue(3), 7);
		TS_ASSERT_EQUALS(ctx1->getValue(4), 0);
		delete ctx1;

		Common::Event click;
		click.type = Common::EVENT_LBUTTONDOWN;
		click.mouse = Common::Point(50, 50);
		input.handleEvent(click);

		Ikura::VM::Context *ctx2 = load(kIgFixture, sizeof(kIgFixture));
		TS_ASSERT(ctx2 != nullptr);
		if (!ctx2)
			return;
		ctx2->setPresentation(&presentation);
		ctx2->setInput(&input);

		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx2, opcode), (int)Ikura::VM::StepResult::kOk); // clicked on it
		TS_ASSERT_EQUALS(ctx2->getValue(3), 7);
		TS_ASSERT_EQUALS(ctx2->getValue(4), 2);
		delete ctx2;
	}

	void test_ig_reports_and_consumes_a_real_advance() {
		static const byte kIgFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x20, 0x0C, 0x00,
			0x10, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kIgFixture, sizeof(kIgFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		Ikura::Input input;
		Common::Event enter;
		enter.type = Common::EVENT_KEYDOWN;
		enter.kbd.keycode = Common::KEYCODE_RETURN;
		input.handleEvent(enter);
		ctx->setInput(&input);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // something happened, don't yield
		TS_ASSERT_EQUALS(ctx->getValue(4), 2); // btn: advance
		TS_ASSERT(!input.consumeAdvance());    // IG consumed it already
		delete ctx;
	}

	void test_svf_is_noop() {
		static const byte kSvfFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD7, 0x0C, 0x04,
		};
		Ikura::VM::Context *ctx = load(kSvfFixture, sizeof(kSvfFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		delete ctx;
	}

	void test_ppf_is_noop() {
		static const byte kPpfFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD3, 0x0C, 0x04,
		};
		Ikura::VM::Context *ctx = load(kPpfFixture, sizeof(kPpfFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		delete ctx;
	}

	void test_im_is_noop() {
		static const byte kImFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0C, 0x00,
		};
		Ikura::VM::Context *ctx = load(kImFixture, sizeof(kImFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		delete ctx;
	}

	void test_ih_defines_hotspot_and_ig_hit_tests_it() {
		// IH: index=5, rect (100,50)-(200,150).
		static const byte kIhFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x64, 0x14, 0x91,
			0x00, 0x00, 0x00, 0xC8, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x5A,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kIhFixture, sizeof(kIhFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		Ikura::Runtime::Presentation presentation(nullptr);
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(presentation.hitTestHotspot(Common::Point(150, 100)), 5);
		TS_ASSERT_EQUALS(presentation.hitTestHotspot(Common::Point(0, 0)), -1);
		delete ctx;
	}

	void test_gl_then_gp_composites_into_screen_buffer() {
		// GL loads "BG.GG0" into buffer 5, GP basic-blits it into buffer 0.
		static const byte kGlGpFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x59, 0x34, 0x14, 0x00,
			0x00, 0x00, 0x09, 0x1D, 0xB8, 0x1D, 0x1D, 0xC0, 0x00, 0x5D, 0x8C, 0x00,
			0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kGlGpFixture, sizeof(kGlGpFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("BG.GG0", buildInterpGGAFixture(200, 100, 50, 255));
		Ikura::Runtime::Presentation presentation(&archive);
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::run(*ctx, opcode), (int)Ikura::VM::StepResult::kEnd);

		TS_ASSERT(presentation.buffer(5) != nullptr);
		TS_ASSERT(presentation.buffer(0) != nullptr);
		if (!presentation.buffer(0)) {
			delete ctx;
			return;
		}
		byte a, r, g, b;
		presentation.buffer(0)->format.colorToARGB(presentation.buffer(0)->getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 50);
		TS_ASSERT_EQUALS(g, 100);
		TS_ASSERT_EQUALS(b, 200);
		delete ctx;
	}

	void test_gl_without_presentation_is_noop() {
		static const byte kGlFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x59, 0x34, 0x14, 0x00,
			0x00, 0x00, 0x09, 0x1D, 0xB8, 0x1D, 0x1D, 0xC0, 0x00, 0x00, 0x08,
		};
		Ikura::VM::Context *ctx = load(kGlFixture, sizeof(kGlFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::run(*ctx, opcode), (int)Ikura::VM::StepResult::kEnd);
		delete ctx;
	}

	void test_atimes_then_await_waits_for_stored_delay() {
		static const byte kAtimesAwaitFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x18, 0xD3, 0x04, 0x00, 0x00, 0xCB, 0x08,
		};
		Ikura::VM::Context *ctx = load(kAtimesAwaitFixture, sizeof(kAtimesAwaitFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // ATIMES
		TS_ASSERT_EQUALS(ctx->delayValue(), (uint32)500);

		uint32 before = g_system->getMillis();
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForTimer); // AWAIT
		TS_ASSERT(ctx->waitDeadline() >= before + 500);
		delete ctx;
	}

	void test_atimes_stores_delay_value() {
		static const byte kAtimesFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x18, 0xD3, 0x04, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kAtimesFixture, sizeof(kAtimesFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		TS_ASSERT_EQUALS(ctx->delayValue(), (uint32)0);
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(ctx->delayValue(), (uint32)500);
		delete ctx;
	}

	void test_pcml_plays_a_voice_clip() {
		static const byte kPcmlFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE1, 0x20, 0x59, 0xC4, 0xB8, 0x5D, 0x05, 0x59,
		};
		Ikura::VM::Context *ctx = load(kPcmlFixture, sizeof(kPcmlFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("V1.WAV", buildInterpWAVFixture(77));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, nullptr, nullptr, &archive);
		ctx->setAudio(&audio);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT(audio.isVoicePlaying());
		delete ctx;
	}

	void test_setfontstyle_sets_style_for_window_zero_only() {
		static const byte kSetFontStyleFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE3, 0x10, 0x00, 0x0C,
		};
		Ikura::VM::Context *ctx = load(kSetFontStyleFixture, sizeof(kSetFontStyleFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		Ikura::Runtime::Presentation presentation(nullptr);
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(presentation.fontStyle(), 3);
		delete ctx;
	}

	void test_ihk_ihkdef_are_noops() {
		static const byte kIhkFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x26, 0x18, 0x00, 0x00, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kIhkFixture, sizeof(kIhkFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // IHK
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // IHKDEF
		delete ctx;
	}

	void test_ihgl_loads_real_hitmap_for_hit_test() {
		static const byte kIhglFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x24, 0x35, 0x05,
			0x41, 0xB8, 0x1D, 0x1D, 0xC0,
		};
		Ikura::VM::Context *ctx = load(kIhglFixture, sizeof(kIhglFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("MAP.GG0", buildInterpGGAFixture(0, 5, 0, 255)); // green=5
		Ikura::Runtime::Presentation presentation(&archive);
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(presentation.hitTestHotspot(Common::Point(0, 0)), 5);
		delete ctx;
	}

	void test_setfontcolor_sets_color_for_window_zero_font_zero_only() {
		static const byte kSetFontColorFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE7, 0x1C, 0x00, 0x00, 0xFF, 0x02, 0x40,
		};
		Ikura::VM::Context *ctx = load(kSetFontColorFixture, sizeof(kSetFontColorFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;
		Ikura::Runtime::Presentation presentation(nullptr);
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(presentation.fontColorR(), 0xff);
		TS_ASSERT_EQUALS(presentation.fontColorG(), 0x80);
		TS_ASSERT_EQUALS(presentation.fontColorB(), 0x10);
		delete ctx;
	}

	void test_pcms_stops_a_playing_voice_clip() {
		static const byte kPcmsFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE5, 0x08,
		};
		Ikura::VM::Context *ctx = load(kPcmsFixture, sizeof(kPcmsFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("V1.WAV", buildInterpWAVFixture(77));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, nullptr, nullptr, &archive);
		audio.playVoice("V1.WAV");
		TS_ASSERT(audio.isVoicePlaying());
		ctx->setAudio(&audio);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT(!audio.isVoicePlaying());
		delete ctx;
	}

	void test_ihgc_clears_a_loaded_hitmap() {
		static const byte kIhgcFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2E, 0x08,
		};
		Ikura::VM::Context *ctx = load(kIhgcFixture, sizeof(kIhgcFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("MAP.GG0", buildInterpGGAFixture(0, 5, 0, 255));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadHitmap("MAP.GG0");
		TS_ASSERT_EQUALS(presentation.hitTestHotspot(Common::Point(0, 0)), 5);
		ctx->setPresentation(&presentation);

		byte opcode;
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk);
		TS_ASSERT_EQUALS(presentation.hitTestHotspot(Common::Point(0, 0)), -1); // hitmap cleared, falls back to (empty) rect spots
		delete ctx;
	}

	void test_go_fills_buffer_zero_and_waits_for_timer() {
		static const byte kGoFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x28, 0xA3, 0x0C,
			0x00, 0x00, 0x28, 0x50, 0x78, 0x00,
		};
		Ikura::VM::Context *ctx = load(kGoFixture, sizeof(kGoFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("BG.GG0", buildInterpGGAFixture(1, 2, 3, 255));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadImage(0, "BG.GG0"); // size buffer 0 first - fillBuffer no-ops on an unsized buffer
		ctx->setPresentation(&presentation);

		byte opcode;
		uint32 before = g_system->getMillis();
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kWaitingForTimer);
		TS_ASSERT(ctx->waitDeadline() >= before + 1000);

		TS_ASSERT(presentation.buffer(0) != nullptr);
		if (!presentation.buffer(0)) {
			delete ctx;
			return;
		}
		byte a, r, g, b;
		presentation.buffer(0)->format.colorToARGB(presentation.buffer(0)->getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 10);
		TS_ASSERT_EQUALS(g, 20);
		TS_ASSERT_EQUALS(b, 30);

		// A real animated crossfade started too: right at t=before,
		// compositeScreen() should still show the pre-GO content
		// (1,2,3), not the already-mutated buffer 0.
		Graphics::Surface frame;
		frame.create(1, 1, Graphics::PixelFormat::createFormatRGBA32());
		presentation.compositeScreen(frame, before);
		frame.format.colorToARGB(frame.getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 3);
		TS_ASSERT_EQUALS(g, 2);
		TS_ASSERT_EQUALS(b, 1);
		// Once the full delay has passed, it shows the new color.
		presentation.compositeScreen(frame, before + 1000);
		frame.format.colorToARGB(frame.getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 10);
		TS_ASSERT_EQUALS(g, 20);
		TS_ASSERT_EQUALS(b, 30);
		frame.free();

		delete ctx;
	}

	void test_gv_shakes_the_screen_then_settles() {
		// count=3, x=10, y=20, duration=1000 (-> 500ms/cycle).
		static const byte kGvFixture[] = {
			0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8D, 0x28, 0x0C, 0x00,
			0x28, 0x50, 0xA3, 0x0C, 0x00, 0x00,
		};
		Ikura::VM::Context *ctx = load(kGvFixture, sizeof(kGvFixture));
		TS_ASSERT(ctx != nullptr);
		if (!ctx)
			return;

		InterpOneFileArchive archive("BG.GG0", buildInterpGGAFixture(1, 2, 3, 255));
		Ikura::Runtime::Presentation presentation(&archive);
		presentation.loadImage(1, "BG.GG0");
		// Real buffer 0 is normally screen-sized already; build a 50x50
		// one here (copyGraphic sizes a fresh buffer to fit its first
		// blit) so there's room for a (10,20) shake to actually show
		// something instead of immediately clamping to nothing against a
		// 1x1 buffer.
		presentation.copyGraphic(0, 1, Common::Rect(0, 0, 1, 1), 0, Common::Point(49, 49));
		presentation.copyGraphic(0, 1, Common::Rect(0, 0, 1, 1), 0, Common::Point(0, 0));
		ctx->setPresentation(&presentation);

		byte opcode;
		uint32 before = g_system->getMillis();
		TS_ASSERT_EQUALS((int)Ikura::VM::step(*ctx, opcode), (int)Ikura::VM::StepResult::kOk); // never pauses

		Graphics::Surface frame;
		frame.create(50, 50, Graphics::PixelFormat::createFormatRGBA32());

		// Mid-way through the first cycle: partway between (0,0) and (10,20).
		presentation.compositeScreen(frame, before + 250);
		byte a, r, g, b;
		frame.format.colorToARGB(frame.getPixel(5, 10), a, r, g, b);
		TS_ASSERT_EQUALS(r, 3); // the shifted background pixel, not the black fill
		TS_ASSERT_EQUALS(g, 2);
		TS_ASSERT_EQUALS(b, 1);

		// After all 3 cycles (3 * 500ms), settled back at (0,0).
		presentation.compositeScreen(frame, before + 1500);
		frame.format.colorToARGB(frame.getPixel(0, 0), a, r, g, b);
		TS_ASSERT_EQUALS(r, 3);
		TS_ASSERT_EQUALS(g, 2);
		TS_ASSERT_EQUALS(b, 1);

		frame.free();
		delete ctx;
	}
};
