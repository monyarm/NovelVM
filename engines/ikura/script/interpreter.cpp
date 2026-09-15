#include "ikura/script/interpreter.h"

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
};

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

	// No-ops in the reference too: IOP_EC/IOP_ES/IOP_FT's handler bodies
	// don't do anything observable, and IOP_HLN has no case in
	// EventGameProcess's dispatch switch at all (falls into its shared
	// empty-break block alongside several other "misc unsupported"
	// opcodes).
	case kOpFlagSave:
	case kOpFlagLoad:
	case kOpFlagTransfer:
	case kOpVarSetCount:
		return StepResult::kOk;

	default:
		return StepResult::kUnimplementedOpcode;
	}
}

} // End of namespace Ikura::VM
