#include "ikura/script/interpreter.h"

#include "common/path.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/system.h"
#include "ikura/runtime/audio.h"
#include "ikura/runtime/input.h"
#include "ikura/runtime/presentation.h"

namespace Ikura::VM {

namespace {

// Ikura/GDL opcode values dispatched below (VileVN reference:
// src/ikura/iopcodes.h names the full ~140-entry set; only these have
// handlers so far, the rest fall through to kUnimplementedOpcode).
enum : byte {
	kOpEnd = 0x00,
	kOpJump = 0x04,
	kOpCall = 0x05,
	kOpReturn = 0x06,
	kOpJumpIndexed = 0x07,
	kOpCallIndexed = 0x08,
	kOpFlagSet = 0x31,
	kOpFlagSetRange = 0x32,
	kOpFlagJumpIfSet = 0x33,
	kOpFlagTransfer = 0x34,
	kOpFlagSave = 0x38,
	kOpFlagLoad = 0x39,
	kOpFlagJumpIfClear = 0x3B,
	kOpVarSetCount = 0x40,
	kOpVarSet = 0x41,
	kOpVarInc = 0x42,
	kOpVarDec = 0x43,
	kOpVarSetGroup = 0x45,
	kOpVarTransfer = 0x46,
	kOpIf = 0x47,
	kOpCalc = 0x44, // IOP_CALC - second-most-common opcode in real scripts
	kOpSystemSet = 0x37,   // IOP_STS
	kOpExtraSet = 0x49,    // IOP_EXS - no-op in the reference
	kOpExtraSecure = 0x4A, // IOP_EXC - no-op in the reference
	kOpSystemCopy = 0x4C,  // IOP_SSP
	kOpGraphicLoad = 0x56, // IOP_GL - by far the most common opcode in real scripts
	kOpGraphicCopy = 0x57, // IOP_GP
	kOpSaveFlag = 0xF5,    // IOP_SVF - no-op in the reference (both branches are empty)
	kOpPopupFlag = 0xF4,   // IOP_PPF - same shape, also a no-op in the reference
	// Real bug caught by real data: this was previously (wrongly)
	// assigned 0x84 and labeled IOP_IM - iopcodes.h actually puts
	// IOP_IM at 0x80 (its handler really is just a debug log, no state
	// change - the no-op grouping below was correct, just filed under
	// the wrong opcode value). 0x84 is IOP_IH, a real opcode - see
	// kOpDefineHotspot.
	kOpMouseCursorData = 0x80, // IOP_IM - reference handler is just a debug log, no state change
	kOpDefineHotspot = 0x84,   // IOP_IH
	kOpMusicLoad = 0x70,    // IOP_ML
	kOpMusicStop = 0x73,    // IOP_MS
	kOpSoundLoad = 0x74,    // IOP_SER
	kOpSoundEnable = 0x75,  // IOP_SEP - no-op in the reference
	kOpSoundDisable = 0x76, // IOP_SET
	kOpSoundStop = 0x7B,    // IOP_SES - same StopSound(channel) as IOP_SET, just an 8-byte payload with an unused trailing duration field
	kOpMusicPlay = 0x71,    // IOP_MP - no-op in the reference (music already plays on ML load)
	kOpReadFlagCount = 0xA5, // IOP_KIDFN - same shared no-op block as IOP_MP in the reference
	kOpReadFlagClear = 0xA0, // IOP_KIDCLR - same shared no-op block
	kOpReadFlagCheck = 0xA7, // IOP_KIDSCAN - same shared no-op block
	kOpMusicFade = 0x72,     // IOP_MF - same shared no-op block
	kOpWindowOpen = 0x29,   // IOP_WO - no-op in the reference (just a debug log)
	kOpWindowClose = 0x2A,  // IOP_WC - same, no-op
	kOpTimerSet = 0xFA,     // IOP_TIMERSET
	kOpTimerGet = 0xFC,     // IOP_TIMERGET
	kOpSystemJump = 0x3A,   // IOP_STX
	kOpMessage = 0x2B,      // IOP_PM
	kOpClickWait = 0x8D,    // IOP_CLK
	kOpTimerEnd = 0xFB,     // IOP_TIMEREND
	kOpGraphicClear = 0x53, // IOP_GC
	kOpGrayscaleEffect = 0x60, // IOP_GGE
	kOpFontSize = 0x27,     // IOP_PB
	kOpTextInterval = 0x26, // IOP_PF
	kOpCharacterName = 0x25, // IOP_CNS
	kOpScriptJump = 0x01,   // IOP_LS
	kOpScriptCall = 0x02,   // IOP_LSBS
	kOpScriptReturn = 0x03, // IOP_SRET
	kOpInputRelease = 0x86, // IOP_IGRELEASE
	kOpCursorChange = 0x81, // IOP_IC - no-op in the reference (shared empty case block)
	kOpCommandWindowClose = 0x17, // IOP_CWC
	kOpMouseInput = 0x85,   // IOP_IG
	kOpDelayValue = 0xF1,   // IOP_ATIMES
	kOpAwait = 0xF2,        // IOP_AWAIT
	kOpVoiceLoad = 0x78,    // IOP_PCML
	kOpVoiceStop = 0x79,    // IOP_PCMS
	kOpHitmapClose = 0x8B,  // IOP_IHGC
	kOpGraphicOpenFade = 0x55, // IOP_GO
	kOpFontStyle = 0xF8,    // IOP_SETFONTSTYLE
	kOpFontColor = 0xF9,    // IOP_SETFONTCOLOR
	kOpKeyMap = 0x88,       // IOP_IHK - reference configures a custom keyboard remap table; no-op here, same as IOP_IHGC, since nothing in our Input consumes arbitrary keycodes (only fixed advance/cancel/skip signals)
	kOpKeyMapDefault = 0x89, // IOP_IHKDEF - same no-op reasoning as IOP_IHK
	kOpHitmapLoad = 0x8A,   // IOP_IHGL
	kOpScreenShake = 0x63,  // IOP_GV
};

// Reads a name string out of an opcode payload: from `from` up to the
// first NUL byte, or the whole rest of the payload if there isn't one.
// Shared shape for IOP_GL/IOP_ML/IOP_SER, which all just embed a raw
// resource filename in their tail bytes.
Common::String readName(const byte *data, uint32 length, uint32 from) {
	uint32 end = from;
	while (end < length && data[end] != 0)
		end++;
	return Common::String((const char *)data + from, end - from);
}

// IOP_LS/IOP_LSBS: looks up name in ctx's script cabinet and parses it.
// The reference calls LoadScript(Data, "ISF") - the script provides a
// bare name and the engine appends the extension itself - but real
// cabinet members are stored with it already (e.g. "EVS_006.ISF"; see
// engines/ikura/README.md's real-data validation), so this tries the
// literal name first and falls back to name+".ISF". Returns nullptr if
// there's no cabinet wired up, neither name is in it, or it fails to
// parse - matching the reference's own silent-no-op guard chain
// (`if((blob=LoadScript(...)))`) on any of those failures.
Format::Script::Script *loadNamedScript(Context &ctx, const Common::String &name) {
	if (!ctx.scriptCabinet())
		return nullptr;

	Common::Path path(name);
	if (!ctx.scriptCabinet()->hasFile(path))
		path = Common::Path(name + ".ISF");
	if (!ctx.scriptCabinet()->hasFile(path))
		return nullptr;

	Common::SeekableReadStream *stream = ctx.scriptCabinet()->createReadStreamForMember(path);
	if (!stream)
		return nullptr;
	Format::Script::Script *script = Format::Script::Script::load(*stream);
	delete stream;
	return script;
}

uint32 readWord(const byte *data) { return data[0] | (data[1] << 8); }
uint32 readDword(const byte *data) { return data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32)data[3] << 24); }

void applyFlagCommand(Context &ctx, uint32 index, byte cmd) {
	switch (cmd) {
	case 0:
		ctx.setFlag(index, false);
		break;
	case 1:
		ctx.setFlag(index, true);
		break;
	case 2:
		ctx.setFlag(index, !ctx.getFlag(index));
		break;
	}
}

// IOP_IF (VileVN reference: IkuraDecoder::iop_if). A chain of 10-byte
// comparison blocks: [v1:4][cmpOp:1][v2:4][thenOp:1]. thenOp selects what
// happens once a comparison succeeds - jump, set a variable, AND into the
// next block (0x02), or just stop (0xFF) - and a jump/set thenOp carries
// its own trailing operand bytes outside the 10-byte stride. Any
// comparison failing stops the chain with no action, matching the
// reference exactly (including that an unrecognized thenOp behaves like
// 0x02 there, so this does too).
StepResult stepIf(Context &ctx, const byte *data, uint32 length) {
	for (uint32 i = 0; i + 10 <= length; i += 10) {
		int32 v1 = ctx.decodeValue(readDword(data + i));
		byte cmpOp = data[i + 4];
		int32 v2 = ctx.decodeValue(readDword(data + i + 5));

		bool result;
		switch (cmpOp) {
		case 0x00: result = (v1 == v2); break;
		case 0x01: result = (v1 < v2); break;
		case 0x02: result = (v1 <= v2); break;
		case 0x03: result = (v1 > v2); break;
		case 0x04: result = (v1 >= v2); break;
		case 0x05: result = (v1 != v2); break;
		default: result = false; break;
		}
		if (!result)
			return StepResult::kOk;

		byte thenOp = data[i + 9];
		if (thenOp == 0x00) {
			if (i + 12 > length)
				return StepResult::kMalformedOpcode;
			ctx.script().jump(readWord(data + i + 10));
			return StepResult::kOk;
		}
		if (thenOp == 0x01) {
			if (i + 16 > length)
				return StepResult::kMalformedOpcode;
			ctx.setValue(readWord(data + i + 10), ctx.decodeValue(readDword(data + i + 12)));
			return StepResult::kOk;
		}
		if (thenOp == 0xFF)
			return StepResult::kOk;
		// 0x02 (AND) or anything unrecognized: fall through to the next block.
	}
	return StepResult::kOk;
}

// IOP_CALC (VileVN reference: IkuraDecoder::iop_calc - the version it
// actually runs; an earlier two-accumulator scheme is left dead-code
// commented out there, "does not seem to be working very well either").
// [dest:2][{op:1, value:4}...], dest is a raw variable index (not a
// decodeValue operand). Accumulates left-to-right into one running
// result starting at 0; op 5 is an explicit no-op end marker. Any other
// op aborts the whole instruction *without* writing dest, matching the
// reference (it logs and returns before reaching its SetValue call).
StepResult stepCalc(Context &ctx, const byte *data, uint32 length) {
	if (length < 2)
		return StepResult::kMalformedOpcode;
	uint32 dest = readWord(data);
	int32 result = 0;
	uint32 i = 2;
	while (i < length) {
		byte op = data[i++];
		if (op < 5) {
			if (i + 4 > length)
				return StepResult::kMalformedOpcode;
			int32 value = ctx.decodeValue(readDword(data + i));
			i += 4;
			switch (op) {
			case 0: result += value; break;
			case 1: result -= value; break;
			case 2: result *= value; break;
			// The reference has no zero guard on / or % here and would
			// crash (integer division by zero, undefined behavior); we
			// skip the operation instead.
			case 3: if (value) result /= value; break;
			case 4: if (value) result %= value; break;
			}
		} else if (op == 5) {
			// Explicit end marker: no-op.
		} else {
			return StepResult::kOk; // unknown operator: reference aborts without writing dest
		}
	}
	ctx.setValue(dest, (uint32)result);
	return StepResult::kOk;
}

// IOP_PM (VileVN reference: IkuraDecoder::iop_pm). [win:1][inline
// commands...]. win is a text-window index the reference only logs on
// (never acted on further), so it's read and ignored. The inline command
// stream is a mini bytecode of its own. The reference itself returns
// true (= "now waiting for input") unconditionally at the end,
// regardless of which inline commands were actually present, so this
// does too.
StepResult stepMessage(Context &ctx, const byte *data, uint32 length) {
	if (length < 1)
		return StepResult::kMalformedOpcode;
	uint32 i = 1; // data[0] (window index) has nothing further to consume
	while (i < length) {
		byte cmd = data[i++];
		if (cmd == 0x03) {
			if (ctx.presentation())
				ctx.presentation()->clearText();
		} else if (cmd == 0x01) {
			if (i >= length)
				return StepResult::kMalformedOpcode;
			byte selector = data[i];
			if (selector == 0x00) {
				if (i + 4 > length) // selector + r + g + b
					return StepResult::kMalformedOpcode;
				if (ctx.presentation())
					ctx.presentation()->setFontColor(data[i + 1], data[i + 2], data[i + 3]);
				i += 4;
			} else if (selector == 0x01) {
				if (i + 2 > length)
					return StepResult::kMalformedOpcode;
				i += 2; // unknown even in the reference ("???")
			}
			// Any other selector: reference does nothing further either.
		} else if (cmd == 0x04) {
			if (i >= length)
				return StepResult::kMalformedOpcode;
			i++; // unknown even in the reference ("???")
		} else if (cmd == 0x06) {
			// No operand.
		} else if (cmd == 0x08) {
			if (i + 4 > length)
				return StepResult::kMalformedOpcode;
			i += 4; // "previously read" line number - read-tracking not implemented yet
		} else if (cmd == 0x11) {
			// Character-name nameplate image (VileVN reference:
			// "FW%3.3d" naming, w_textview->Blit) - see
			// Presentation::setNameplate()'s comment for why this isn't
			// called "portrait" (a prior comment here was wrong).
			if (i + 4 > length)
				return StepResult::kMalformedOpcode;
			if (ctx.presentation())
				ctx.presentation()->setNameplate(ctx.decodeValue(readDword(data + i)));
			i += 4;
		} else if (cmd == 0x13) {
			if (i >= length)
				return StepResult::kMalformedOpcode;
			if (data[i] > 0x1F) {
				// Voice filename, NUL-terminated.
				uint32 nameStart = i;
				while (i < length && data[i] != 0)
					i++;
				if (ctx.audio())
					ctx.audio()->playVoice(readName(data, i, nameStart));
				if (i < length)
					i++;
			} else if (data[i] == 0x04) {
				if (i + 3 > length)
					return StepResult::kMalformedOpcode;
				uint32 index = readWord(data + i + 1);
				Common::String name;
				if (ctx.presentation() && ctx.getCharacterName(index, name))
					ctx.presentation()->setNameText(name);
				i += 3;
			}
			// Any other selector: reference just logs and does nothing further.
		} else if (cmd == 0xFF) {
			Common::String text;
			while (i < length && data[i] != 0) {
				if (data[i] > 0x1F && data[i] < 0x7F)
					text += (char)data[i];
				i++;
			}
			if (i < length)
				i++; // consume the NUL
			if (ctx.presentation())
				ctx.presentation()->showText(text);
		}
		// Any other nonzero cmd: unknown inline command, reference just logs.
	}
	return StepResult::kWaitingForAdvance;
}

} // namespace

StepResult step(Context &ctx, byte &lastOpcode) {
	const byte *data;
	uint32 length;
	if (!ctx.script().getOpcode(lastOpcode, data, length))
		return StepResult::kScriptExhausted;

	switch (lastOpcode) {
	case kOpEnd:
		return StepResult::kEnd;

	case kOpJump:
		if (length != 2)
			return StepResult::kMalformedOpcode;
		ctx.script().jump(readWord(data));
		return StepResult::kOk;

	case kOpCall:
		if (length != 2)
			return StepResult::kMalformedOpcode;
		ctx.script().call(readWord(data));
		return StepResult::kOk;

	case kOpReturn:
		ctx.script().ret();
		return StepResult::kOk;

	case kOpJumpIndexed:
	case kOpCallIndexed: {
		if (length < 5)
			return StepResult::kMalformedOpcode;
		int32 selector = ctx.decodeValue(readDword(data));
		byte count = data[4];
		if (selector >= 0 && selector < count) {
			uint32 offset = 5 + (uint32)selector * 2;
			if (offset + 2 > length)
				return StepResult::kMalformedOpcode;
			uint16 target = readWord(data + offset);
			if (lastOpcode == kOpJumpIndexed)
				ctx.script().jump(target);
			else
				ctx.script().call(target);
		}
		return StepResult::kOk;
	}

	case kOpVarSet:
		if (length != 6)
			return StepResult::kMalformedOpcode;
		ctx.setValue(readWord(data), ctx.decodeValue(readDword(data + 2)));
		return StepResult::kOk;

	case kOpVarInc:
		if (length != 2)
			return StepResult::kMalformedOpcode;
		ctx.setValue(readWord(data), ctx.getValue(readWord(data)) + 1);
		return StepResult::kOk;

	case kOpVarDec:
		if (length != 2)
			return StepResult::kMalformedOpcode;
		ctx.setValue(readWord(data), ctx.getValue(readWord(data)) - 1);
		return StepResult::kOk;

	case kOpVarSetGroup: {
		if (length != 8)
			return StepResult::kMalformedOpcode;
		uint32 start = readWord(data);
		uint32 end = readWord(data + 2);
		uint32 value = (uint32)ctx.decodeValue(readDword(data + 4));
		for (uint32 i = start; i < end; i++)
			ctx.setValue(i, value);
		return StepResult::kOk;
	}

	case kOpVarTransfer: {
		if (length != 6)
			return StepResult::kMalformedOpcode;
		uint32 src = readWord(data);
		uint32 dst = readWord(data + 2);
		uint32 count = readWord(data + 4);
		for (uint32 i = 0; i < count; i++)
			ctx.setValue(dst + i, (uint32)ctx.getValue(src + i));
		return StepResult::kOk;
	}

	case kOpFlagSet:
		if (length != 3)
			return StepResult::kMalformedOpcode;
		applyFlagCommand(ctx, readWord(data), data[2]);
		return StepResult::kOk;

	case kOpFlagSetRange: {
		if (length != 5)
			return StepResult::kMalformedOpcode;
		uint32 start = readWord(data);
		uint32 end = readWord(data + 2);
		byte cmd = data[4];
		for (uint32 i = start; i < end; i++)
			applyFlagCommand(ctx, i, cmd);
		return StepResult::kOk;
	}

	case kOpFlagJumpIfSet:
		if (length != 4)
			return StepResult::kMalformedOpcode;
		if (ctx.getFlag(readWord(data)))
			ctx.script().jump(readWord(data + 2));
		return StepResult::kOk;

	case kOpFlagJumpIfClear:
		if (length != 4)
			return StepResult::kMalformedOpcode;
		if (!ctx.getFlag(readWord(data)))
			ctx.script().jump(readWord(data + 2));
		return StepResult::kOk;

	case kOpIf:
		return stepIf(ctx, data, length);

	case kOpCalc:
		return stepCalc(ctx, data, length);

	case kOpMessage:
		return stepMessage(ctx, data, length);

	// IOP_CLK (VileVN reference: IkuraDecoder::iop_clk). No payload;
	// unconditionally pauses, same as IOP_PM - the caller must gate on a
	// real advance signal before calling step()/run() again.
	case kOpClickWait:
		return StepResult::kWaitingForAdvance;

	// IOP_TIMEREND. The reference's own opcode dispatch has an empty case
	// body for this one (`case IOP_TIMEREND: break;`) - it never calls a
	// handler or assigns its "still waiting" result variable, so that
	// variable's initial value (true, i.e. waiting) passes through
	// untouched. Whether that's an intentional "pause a frame" or a bug
	// in the reference, this replicates the observed behavior exactly:
	// unconditionally pauses, same as IOP_CLK.
	case kOpTimerEnd:
		return StepResult::kWaitingForAdvance;

	// IOP_GC (VileVN reference: IkuraDecoder::iop_gc). [dst:1][par2:1]
	// [par3:1][par4:1][r:1][g:1][b:1] - par2-4 are unused even in the
	// reference (it just logs if they're nonzero).
	case kOpGraphicClear:
		if (length != 7)
			return StepResult::kMalformedOpcode;
		if (ctx.presentation())
			ctx.presentation()->fillBuffer(data[0], data[4], data[5], data[6]);
		return StepResult::kOk;

	// IOP_GO (VileVN reference: IkuraDecoder::iop_go). Reference reads
	// [delay:4][r:1][g:1][b:1] (7 bytes) and never validates length, but
	// every real occurrence across 3 real scripts (TITLE.ISF, EVC_007.ISF,
	// EVT_011.ISF) carries an 8th trailing pad byte the reference just
	// never reads - same shape as IOP_GC's unused par2-4 bytes. Reference
	// instantly fills buffer 0 with the given color (SetSurface, matches
	// fillBuffer below) and separately registers a Fade animation object
	// over it - this now does both: beginFade() snapshots whatever was
	// onscreen before the fill as the animation's start point, so the
	// real per-frame crossfade (Presentation::compositeScreen, driven
	// from IkuraEngine::run()) plays from the old scene to the solid
	// color over `delay`, the same interval the wait below blocks for.
	// The wait is real script-observable state too: same escape-hatch
	// shape as IOP_PM/IOP_CLK/IOP_TIMEREND's kWaitingForAdvance, except
	// gated on a wall-clock deadline instead of (or in addition to) a
	// player signal - see StepResult::kWaitingForTimer and
	// IkuraEngine::run()'s gating (which also completeFade()s the
	// instant this wait ends, matching the reference's own
	// EventGameTick calling SkipAnimation() there either way).
	case kOpGraphicOpenFade: {
		if (length != 8)
			return StepResult::kMalformedOpcode;
		uint32 delay = (uint32)ctx.decodeValue(readDword(data));
		if (ctx.presentation()) {
			ctx.presentation()->beginFade(delay);
			ctx.presentation()->fillBuffer(0, data[4], data[5], data[6]);
		}
		ctx.setWaitDeadline(g_system->getMillis() + delay);
		return StepResult::kWaitingForTimer;
	}

	// IOP_GGE (VileVN reference: IkuraDecoder::iop_gge). Its own comment
	// says it all: "No support for effect files (yet)" - despite reading
	// type/num/tick/dir/len fields plus a trailing filename (shaped like
	// "load a named grayscale mask and use it to drive a custom wipe/
	// dissolve transition"), the reference never implements that and
	// just falls back to a bare full-buffer blit onto the screen (via
	// the same copyGraphic() used by IOP_GP - no new blit logic needed)
	// plus a real `Fade` animation over `tick` milliseconds - the same
	// mechanism IOP_GO already has, wired in the same way (beginFade()
	// before the blit). Real effect-file support (an actual grayscale-
	// mask-driven wipe, not a plain crossfade) would be a deliberate
	// improvement over the reference, not a port of it - worth doing
	// later, once the engine matches the reference everywhere else.
	case kOpGrayscaleEffect: {
		if (length < 12)
			return StepResult::kMalformedOpcode;
		int32 num = ctx.decodeValue(readDword(data + 4));
		int32 tick = ctx.decodeValue(readDword(data + 8));
		if (ctx.presentation()) {
			const Graphics::ManagedSurface *src = ctx.presentation()->buffer(num);
			if (src) {
				ctx.presentation()->beginFade(tick > 0 ? (uint32)tick : 0);
				ctx.presentation()->copyGraphic(0, num, Common::Rect(0, 0, src->w, src->h), 0, Common::Point(0, 0));
			}
		}
		return StepResult::kWaitingForAdvance;
	}

	// IOP_GV (VileVN reference: IkuraDecoder::iop_gv, "Shaking effect").
	// [count:2][x:1][y:1][duration:4]. Never pauses in the reference
	// (`return false;` unconditionally) - the shake plays out
	// asynchronously exactly like IOP_GO/IOP_GP's fades do. See
	// Presentation::beginShake's comment - not exercised by any real
	// script in this engine's test corpus, ported from the reference's
	// literal Slide/AddAnimation behavior and unverified against real
	// bytes.
	case kOpScreenShake:
		if (length != 8)
			return StepResult::kMalformedOpcode;
		if (ctx.presentation()) {
			uint32 count = readWord(data);
			uint32 duration = (uint32)ctx.decodeValue(readDword(data + 4));
			ctx.presentation()->beginShake(data[2], data[3], (int)count, duration / 2);
		}
		return StepResult::kOk;

	// IOP_PB (VileVN reference: IkuraDecoder::iop_pb). [window:1][size:4]
	// - only window 0 (the reference's sole real text window) sets it.
	case kOpFontSize:
		if (length != 5)
			return StepResult::kMalformedOpcode;
		if (data[0] == 0 && ctx.presentation())
			ctx.presentation()->setFontSize(ctx.decodeValue(readDword(data + 1)));
		return StepResult::kOk;

	// IOP_PF (VileVN reference: IkuraDecoder::iop_pf). [window:1][ms:4] -
	// same window-0 gating as IOP_PB/IOP_SETFONTSTYLE/IOP_SETFONTCOLOR.
	case kOpTextInterval:
		if (length != 5)
			return StepResult::kMalformedOpcode;
		if (data[0] == 0 && ctx.presentation())
			ctx.presentation()->setTextInterval((uint32)ctx.decodeValue(readDword(data + 1)));
		return StepResult::kOk;

	// IOP_CNS (VileVN reference: IkuraDecoder::iop_cns). [par1:1][index:1]
	// [name: rest of payload]. par1 is read and ignored in the reference
	// too (never used after being read).
	case kOpCharacterName:
		if (length < 2)
			return StepResult::kMalformedOpcode;
		ctx.setCharacterName(data[1], readName(data, length, 2));
		return StepResult::kOk;

	// IOP_SETFONTSTYLE (VileVN reference: IkuraDecoder::iop_setfontstyle).
	// [window:1][style:1] - a raw bold/italic/shadow bitmask, only applied
	// for window 0 in the reference (same gating as IOP_PB above).
	case kOpFontStyle:
		if (length != 2)
			return StepResult::kMalformedOpcode;
		if (data[0] == 0 && ctx.presentation())
			ctx.presentation()->setFontStyle(data[1]);
		return StepResult::kOk;

	// IOP_SETFONTCOLOR (VileVN reference: IkuraDecoder::iop_setfontcolor).
	// [window:1][font:1][r:1][g:1][b:1] - only the reference's 5-byte
	// variant, real data's actual shape (see Presentation::setFontColor).
	// Only applied for window 0, font 0 in the reference.
	case kOpFontColor:
		if (length != 5)
			return StepResult::kMalformedOpcode;
		if (data[0] == 0 && data[1] == 0 && ctx.presentation())
			ctx.presentation()->setFontColor(data[2], data[3], data[4]);
		return StepResult::kOk;

	// IOP_IG (VileVN reference: IkuraDecoder::iop_ig). [cur:2][btn:2]
	// [cnt:1][bflag:1] - cnt/bflag are unused even in the reference.
	// Writes the current hotspot index (see kOpDefineHotspot - -1 if the
	// live mouse position isn't inside any registered one) and an event
	// flag into cur/btn. Hovering a hotspot takes the reference's
	// "hotspot input" branch (flag=2 only once it's also been clicked);
	// otherwise the "ordinary clicks" branch (4=cancel, 2=advance,
	// checked in that order - matches the reference's two independent
	// ifs, not an else-if).
	//
	// Simplified from the reference: it separately tracks "currently
	// hovered" (GetSelected, from MouseMove) vs. "hovered at the moment
	// of the last click" (GetResult, from MouseLeftDown) so a click
	// only counts on whatever was under the cursor *at click time*, even
	// if the mouse has moved by the time this opcode polls again. This
	// does one live hit-test per poll instead - good enough unless the
	// mouse crosses hotspot boundaries within a single poll tick, which
	// real per-frame polling makes vanishingly rare.
	//
	// Deviation from the reference: iop_ig always returns "not waiting"
	// and relies on an external per-frame tick budget (EventGameProcess
	// is called in a bounded loop - see engines/ikura/README.md) to keep
	// a script-side poll loop (IG, then jump back to it if the flag came
	// back 0) from freezing the app. We have no such budget, and Input's
	// state can't change mid-run() anyway, so a poll that finds nothing
	// yields kYielded here instead of spinning forever - unlike
	// kWaitingForAdvance, the caller doesn't need to gate anything: next
	// frame's run() re-enters the script's own poll loop and IG consumes
	// whatever's fresh in Input itself.
	case kOpMouseInput: {
		if (length != 6)
			return StepResult::kMalformedOpcode;
		uint32 curVar = readWord(data);
		uint32 btnVar = readWord(data + 2);
		int32 pos = -1;
		int32 flag = 0;
		if (ctx.input() && ctx.presentation())
			pos = ctx.presentation()->hitTestHotspot(ctx.input()->mousePosition());
		if (pos != -1) {
			if (ctx.input() && ctx.input()->consumeAdvance())
				flag = 2;
		} else if (ctx.input()) {
			if (ctx.input()->consumeCancel())
				flag = 4;
			if (ctx.input()->consumeAdvance())
				flag = 2;
		}
		ctx.setValue(curVar, (uint32)pos);
		ctx.setValue(btnVar, (uint32)flag);
		return flag == 0 ? StepResult::kYielded : StepResult::kOk;
	}

	// IOP_IH (VileVN reference: IkuraDecoder::iop_ih, w_display->SetSpot).
	// [index:1][x1:4][y1:4][x2:4][y2:4][type:1][flag:2][c2:1][c3:1]
	// [c4:1] = 23 bytes, or a 31-byte variant with 8 more trailing bytes
	// the reference itself never reads either. type/flag/c2-c4 are
	// commented out (unread) in the reference too. Registers a
	// rectangular clickable region IOP_IG hit-tests against - the
	// reference's other, pixel-precise "hitmap" hotspot model (IOP_IHGL/
	// IOP_IHGC) isn't implemented, same reasoning those two are no-ops.
	case kOpDefineHotspot:
		if (length != 23 && length != 31)
			return StepResult::kMalformedOpcode;
		if (ctx.presentation())
			ctx.presentation()->defineHotspot(data[0], Common::Rect(readDword(data + 1), readDword(data + 5), readDword(data + 9), readDword(data + 13)));
		return StepResult::kOk;

	// IOP_IHGL (VileVN reference: IkuraDecoder::iop_ihgl). [name: whole
	// payload, raw C string]. See Presentation::loadHitmap's comment -
	// not exercised by any real script in this engine's test corpus,
	// ported from the reference's stated logic and unverified against
	// real bytes.
	case kOpHitmapLoad:
		if (ctx.presentation())
			ctx.presentation()->loadHitmap(readName(data, length, 0));
		return StepResult::kOk;

	// IOP_IHGC (VileVN reference: IkuraDecoder::iop_ihgc, w_display->
	// DropMap). No payload fields.
	case kOpHitmapClose:
		if (ctx.presentation())
			ctx.presentation()->clearHitmap();
		return StepResult::kOk;

	// [dst:4][val:4], both decodeValue'd operands. The reference's own
	// decompiled source claims a 2-byte [dst:1][val:1] payload here, but
	// every real call in Snow Sakura's scripts is 8 bytes shaped exactly
	// like this. Snow Sakura is currently our only game with a working
	// ISF parse-and-step pipeline (see "Known compatibility gaps" in
	// engines/ikura/README.md), so this is single-game evidence, not a
	// cross-game-confirmed fact - it might be this build's convention
	// rather than universal across the ikura family. Went with what
	// actually works against the one real dataset available rather than
	// a decompile that rejects 100% of its real STS calls.
	case kOpSystemSet:
		if (length != 8)
			return StepResult::kMalformedOpcode;
		ctx.setSystem(ctx.decodeValue(readDword(data)), ctx.decodeValue(readDword(data + 4)) != 0);
		return StepResult::kOk;

	case kOpSystemCopy:
		if (length != 3)
			return StepResult::kMalformedOpcode;
		ctx.setSystem(data[2], readWord(data) != 0);
		return StepResult::kOk;

	// IOP_TIMERSET (VileVN reference: IkuraDecoder::iop_timerset).
	// [duration:4], a raw dword (not a decodeValue operand in the
	// reference). Starts a stopwatch duration milliseconds in the future.
	case kOpTimerSet:
		if (length != 4)
			return StepResult::kMalformedOpcode;
		ctx.setTimerStart(g_system->getMillis() + readDword(data));
		return StepResult::kOk;

	// IOP_TIMERGET (VileVN reference: IkuraDecoder::iop_timerget).
	// [dst:2]. Reads elapsed time since timerStart, clamped to 0 (matches
	// the reference clamping a possibly-negative signed difference).
	case kOpTimerGet: {
		if (length != 2)
			return StepResult::kMalformedOpcode;
		uint32 now = g_system->getMillis();
		uint32 elapsed = now > ctx.timerStart() ? now - ctx.timerStart() : 0;
		ctx.setValue(readWord(data), elapsed);
		return StepResult::kOk;
	}

	// IOP_ATIMES (VileVN reference: IkuraDecoder::iop_atimes). [value:4],
	// a decodeValue operand. Stores a millisecond delay for IOP_AWAIT to
	// read.
	case kOpDelayValue:
		if (length != 4)
			return StepResult::kMalformedOpcode;
		ctx.setDelayValue((uint32)ctx.decodeValue(readDword(data)));
		return StepResult::kOk;

	// IOP_AWAIT (VileVN reference: IkuraDecoder::iop_await). No payload
	// fields read at all in the reference - it just waits out whatever
	// IOP_ATIMES last stored. Same kWaitingForTimer pause as IOP_GO, no
	// graphic side effect.
	case kOpAwait:
		ctx.setWaitDeadline(g_system->getMillis() + ctx.delayValue());
		return StepResult::kWaitingForTimer;

	// IOP_STX (VileVN reference: IkuraDecoder::iop_stx). [cmd:1][val:1]
	// [entry:2]. Jumps to entry if a condition on cmd holds. Most cmd
	// values just compare a system flag against val; a few are special-
	// cased in the reference. cmd 0x08/0x0A depend on live "was
	// advance/ctrl pressed this frame" input state the VM has no way to
	// observe yet (there's no wait-for-input step here - see
	// script/interpreter.h) - faithfully unimplementable until that
	// exists, so they always report not-pressed instead of guessing.
	case kOpSystemJump: {
		if (length != 4)
			return StepResult::kMalformedOpcode;
		byte cmd = data[0];
		byte val = data[1];
		bool jump;
		if (cmd == 0x0C) // Automode: unsupported in the reference too, always false.
			jump = false;
		else if (cmd == 0x0D) // "Active window?" - reference checks val, not the flag.
			jump = (val == 1);
		else if (cmd == 0x08 || cmd == 0x0A) // Needs live input state - see comment above.
			jump = false;
		else
			jump = ctx.getSystem(cmd) ? (val != 0) : (val == 0);
		if (jump)
			ctx.script().jump(readWord(data + 2));
		return StepResult::kOk;
	}

	// IOP_LS (VileVN reference: IkuraDecoder::iop_ls / IParser::
	// JumpScript). Whole payload is a raw script name, NUL-terminated
	// within it. Replaces the running script outright - no-op if the
	// named script can't be found/parsed (see loadNamedScript above).
	case kOpScriptJump: {
		Format::Script::Script *script = loadNamedScript(ctx, readName(data, length, 0));
		if (script)
			ctx.jumpScript(script);
		return StepResult::kOk;
	}

	// IOP_LSBS (VileVN reference: IkuraDecoder::iop_lsbs / IParser::
	// CallScript). Same payload shape as IOP_LS, but suspends the
	// running script instead of replacing it - IOP_SRET resumes it.
	case kOpScriptCall: {
		Format::Script::Script *script = loadNamedScript(ctx, readName(data, length, 0));
		if (script)
			ctx.callScript(script);
		return StepResult::kOk;
	}

	// IOP_SRET (VileVN reference: IkuraDecoder::iop_sret / IParser::
	// ReturnScript). No payload. If this wasn't inside an IOP_LSBS call,
	// there's nothing to return to - the reference just lets the script
	// end there, so this reports kScriptExhausted the same way running
	// off the end of a script's instructions does.
	case kOpScriptReturn:
		return ctx.returnScript() ? StepResult::kOk : StepResult::kScriptExhausted;

	// IOP_IGRELEASE (VileVN reference: IkuraDecoder::iop_igrelease).
	// Clears any pending advance/cancel signal without reporting it to
	// the script - matches the reference resetting keyok/keycancel
	// directly. No-op if ctx has no Input wired up.
	case kOpInputRelease:
		if (ctx.input()) {
			ctx.input()->consumeAdvance();
			ctx.input()->consumeCancel();
		}
		return StepResult::kOk;

	// IOP_CWC (VileVN reference: IkuraDecoder::iop_cwc). [win:1]. Hides
	// the text window.
	case kOpCommandWindowClose:
		if (ctx.presentation())
			ctx.presentation()->setTextVisible(false);
		return StepResult::kOk;

	// IOP_GL (VileVN reference: IkuraDecoder::iop_gl). [index:4][name:rest
	// of payload, NUL-terminated within it]. No-op if ctx has no
	// Presentation wired up (e.g. tests running headless).
	case kOpGraphicLoad: {
		if (length < 5)
			return StepResult::kMalformedOpcode;
		int32 index = ctx.decodeValue(readDword(data));
		if (ctx.presentation())
			ctx.presentation()->loadImage(index, readName(data, length, 4));
		return StepResult::kOk;
	}

	// IOP_GP (VileVN reference: IkuraDecoder::iop_gp). [cmd:1][src:4]
	// [srcx:4][srcy:4][srcw:4][srch:4][dst:4][dstx:4][dsty:4], plus two
	// optional trailing fields (color/ccut, past length 36/40) that are
	// read in the reference but never actually used by any cmd branch -
	// dead reads there too, not a gap on this side. See
	// Presentation::copyGraphic for what each cmd does.
	case kOpGraphicCopy: {
		if (length < 33)
			return StepResult::kMalformedOpcode;
		byte cmd = data[0];
		int32 src = ctx.decodeValue(readDword(data + 1));
		int32 srcx = ctx.decodeValue(readDword(data + 5));
		int32 srcy = ctx.decodeValue(readDword(data + 9));
		int32 srcw = ctx.decodeValue(readDword(data + 13));
		int32 srch = ctx.decodeValue(readDword(data + 17));
		int32 dst = ctx.decodeValue(readDword(data + 21));
		int32 dstx = ctx.decodeValue(readDword(data + 25));
		int32 dsty = ctx.decodeValue(readDword(data + 29));
		if (ctx.presentation())
			ctx.presentation()->copyGraphic(cmd, src, Common::Rect(srcx, srcy, srcx + srcw, srcy + srch), dst, Common::Point(dstx, dsty));
		return StepResult::kOk;
	}

	// IOP_ML (VileVN reference: IkuraDecoder::iop_ml). The whole payload
	// is a raw track name, NUL-terminated within it - no other fields.
	case kOpMusicLoad:
		if (ctx.audio())
			ctx.audio()->playMusic(readName(data, length, 0));
		return StepResult::kOk;

	// IOP_PCML (VileVN reference: IkuraDecoder::iop_pcml, PlayVoice(Data,
	// VA_VOICES)). [name:rest of payload], same raw-C-string shape as
	// IOP_ML/IOP_GL - no trailing channel field, voice playback is a
	// single slot.
	case kOpVoiceLoad:
		if (ctx.audio())
			ctx.audio()->playVoice(readName(data, length, 0));
		return StepResult::kOk;

	// IOP_PCMS (VileVN reference: IkuraDecoder::iop_pcms). No payload
	// fields.
	case kOpVoiceStop:
		if (ctx.audio())
			ctx.audio()->stopVoice();
		return StepResult::kOk;

	// IOP_MS (VileVN reference: IkuraDecoder::iop_ms). No payload fields.
	case kOpMusicStop:
		if (ctx.audio())
			ctx.audio()->stopMusic();
		return StepResult::kOk;

	// IOP_SER (VileVN reference: IkuraDecoder::iop_ser). [name:rest of
	// payload up to the last 4 bytes][channel:4], channel trailing the
	// name rather than leading it.
	case kOpSoundLoad: {
		if (length < 4)
			return StepResult::kMalformedOpcode;
		int32 channel = ctx.decodeValue(readDword(data + length - 4));
		if (ctx.audio())
			ctx.audio()->playSound(readName(data, length - 4, 0), channel);
		return StepResult::kOk;
	}

	// IOP_SET (VileVN reference: IkuraDecoder::iop_set). [channel:4].
	case kOpSoundDisable:
		if (length != 4)
			return StepResult::kMalformedOpcode;
		if (ctx.audio())
			ctx.audio()->stopSound(ctx.decodeValue(readDword(data)));
		return StepResult::kOk;

	// IOP_SES (VileVN reference: IkuraDecoder::iop_ses). [channel:4]
	// [duration:4] - duration is read but never used even in the
	// reference (commented out), so this is otherwise identical to
	// IOP_SET.
	case kOpSoundStop:
		if (length != 8)
			return StepResult::kMalformedOpcode;
		if (ctx.audio())
			ctx.audio()->stopSound(ctx.decodeValue(readDword(data)));
		return StepResult::kOk;

	// No-ops in the reference too: IOP_EC/IOP_ES/IOP_FT/IOP_EXS/IOP_EXC's
	// handler bodies don't do anything observable, and IOP_HLN has no
	// case in EventGameProcess's dispatch switch at all (falls into its
	// shared empty-break block alongside several other "misc unsupported"
	// opcodes).
	case kOpFlagSave:
	case kOpFlagLoad:
	case kOpFlagTransfer:
	case kOpVarSetCount:
	case kOpExtraSet:
	case kOpExtraSecure:
	case kOpSaveFlag:
	case kOpPopupFlag:
	case kOpMouseCursorData:
	case kOpCursorChange:
	case kOpSoundEnable:
	case kOpMusicPlay:
	case kOpReadFlagCount:
	case kOpReadFlagClear:
	case kOpReadFlagCheck:
	case kOpMusicFade:
	case kOpWindowOpen:
	case kOpWindowClose:
	case kOpKeyMap:
	case kOpKeyMapDefault:
		return StepResult::kOk;

	default:
		return StepResult::kUnimplementedOpcode;
	}
}

StepResult run(Context &ctx, byte &lastOpcode) {
	StepResult result;
	do {
		result = step(ctx, lastOpcode);
	} while (result == StepResult::kOk);
	return result;
}

} // End of namespace Ikura::VM
