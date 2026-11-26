/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

#include "common/file.h"
#include "common/random.h"
#include "common/textconsole.h"

#include "ai5/ai5.h"
#include "ai5/animplay.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/backlog.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/menu.h"
#include "ai5/text.h"
#include "ai5/vm.h"

namespace AI5 {

VM *g_vm = nullptr;
Memory *g_mem = nullptr;
MemoryPtr g_memPtr;
Game *g_game = nullptr;
AiwMenuEntry g_aiwMenuEntries[kAiwMaxMenus][kAiwMaxMenuEntries];
uint g_aiwMenuNrEntries[kAiwMaxMenus];
bool g_hanLineBreaks = false;

void vmInit() {
	g_vm->ip.code = g_mem->fileData;
}

static bool logging() {
	return vmFlagIsOn(kFlagLog) && vmFlagIsOn(kFlagLogEnable);
}

static byte vmReadByte() {
	byte c = g_vm->ip.code[g_vm->ip.ptr++];
	if (logging())
		backlogPushByte(c);
	return c;
}

static byte vmPeekByte() {
	return g_vm->ip.code[g_vm->ip.ptr];
}

static uint16 vmReadWord() {
	uint16 v = READ_LE_UINT16(g_vm->ip.code + g_vm->ip.ptr);
	if (logging()) {
		backlogPushByte(g_vm->ip.code[g_vm->ip.ptr]);
		backlogPushByte(g_vm->ip.code[g_vm->ip.ptr + 1]);
	}
	g_vm->ip.ptr += 2;
	return v;
}

static uint32 vmReadDword() {
	uint32 v = READ_LE_UINT32(g_vm->ip.code + g_vm->ip.ptr);
	if (logging()) {
		for (int i = 0; i < 4; i++)
			backlogPushByte(g_vm->ip.code[g_vm->ip.ptr + i]);
	}
	g_vm->ip.ptr += 4;
	return v;
}

static void vmStackPush(uint32 val) {
	g_vm->stack[g_vm->stackPtr++] = val;
	if (g_vm->stackPtr >= kVMStackSize)
		error("VM: stack overflow");
}

static uint32 vmStackPop() {
	if (!g_vm->stackPtr)
		error("VM: tried to pop from empty stack");
	return g_vm->stack[--g_vm->stackPtr];
}

void vmLoadFile(const byte *data, uint32 size, uint32 offset) {
	memcpy(g_mem->fileData + offset, data, size);
}

static bool loadStream(Common::SeekableReadStream *s, uint32 offset, const char *name) {
	if (!s) {
		warning("VM: failed to read data file \"%s\"", name);
		return false;
	}
	if (offset + s->size() > kMemFileDataSize) {
		warning("VM: tried to read file beyond end of buffer");
		delete s;
		return false;
	}
	s->read(g_mem->fileData + offset, s->size());
	delete s;
	return true;
}

void vmLoadDataFile(const char *name, uint32 offset) {
	loadStream(assetDataLoad(name), offset, name);
}

void vmLoadMes(const char *name) {
	char *memName = memMesName();
	if (name != memName)
		Common::strlcpy(memName, name, kMemMem16MaxSize);
	for (int i = 0; memName[i]; i++)
		memName[i] = toupper(memName[i]);
	if (!loadStream(assetMesLoad(memName), 0, memName))
		error("VM: failed to load MES file \"%s\"", memName);
}

void vmExprVar16() {
	vmStackPush(memGetVar16(vmReadByte()));
}

void vmExprVar16Const16() {
	vmStackPush(memGetVar16(vmReadWord()));
}

void vmExprVar16Expr() {
	vmStackPush(memGetVar16(vmStackPop()));
}

void vmExprSysvar16Const16() {
	vmStackPush(memGetSysvar16((uint)vmReadWord()));
}

void vmExprSysvar16Expr() {
	vmStackPush(memGetSysvar16((uint)vmStackPop()));
}

void vmExprPtr16Get16() {
	int32 i = vmStackPop();
	byte var = vmReadByte();
	byte *src = g_memPtr.systemVar16;
	if (var)
		src = memRaw() + memGetVar16(var - 1);
	if (!memPtrValid(src + i * 2, 2))
		error("VM: out of bounds read");
	vmStackPush(READ_LE_UINT16(src + i * 2));
}

void vmExprPtr16Get8() {
	uint32 i = vmStackPop();
	byte var = vmReadByte();
	byte *src = memRaw() + memGetVar16(var);
	if (!memPtrValid(src + i, 1))
		error("VM: out of bounds read");
	vmStackPush(src[i]);
}

#define VM_EXPR_OPERATOR(name, op) \
	void name() { \
		uint32 b = vmStackPop(); \
		uint32 a = vmStackPop(); \
		vmStackPush(a op b); \
	}
VM_EXPR_OPERATOR(vmExprPlus, +)
VM_EXPR_OPERATOR(vmExprMinus, -)
VM_EXPR_OPERATOR(vmExprMul, *)
VM_EXPR_OPERATOR(vmExprAnd, &&)
VM_EXPR_OPERATOR(vmExprOr, ||)
VM_EXPR_OPERATOR(vmExprBitand, &)
VM_EXPR_OPERATOR(vmExprBitior, |)
VM_EXPR_OPERATOR(vmExprBitxor, ^)
VM_EXPR_OPERATOR(vmExprLt, <)
VM_EXPR_OPERATOR(vmExprGt, >)
VM_EXPR_OPERATOR(vmExprLte, <=)
VM_EXPR_OPERATOR(vmExprGte, >=)
VM_EXPR_OPERATOR(vmExprEq, ==)
VM_EXPR_OPERATOR(vmExprNeq, !=)
#undef VM_EXPR_OPERATOR

void vmExprDiv() {
	uint32 b = vmStackPop();
	uint32 a = vmStackPop();
	if (!b)
		error("VM: division by zero");
	vmStackPush(a / b);
}

void vmExprMod() {
	uint32 b = vmStackPop();
	uint32 a = vmStackPop();
	if (!b)
		error("VM: division by zero");
	vmStackPush(a % b);
}

void vmExprMinusUnsigned() {
	uint32 b = vmStackPop();
	uint32 a = vmStackPop();
	vmStackPush(b > a ? 0 : a - b);
}

static uint32 vmRandom(uint32 range) {
	if (!range)
		error("VM: random number with zero range");
	return g_ai5->getRandom().getRandomNumber(range - 1);
}

void vmExprRand() {
	vmStackPush(vmRandom(vmStackPop()));
}

// Doukyuusei
void vmExprRandWithImmRange() {
	vmStackPush(vmRandom(vmReadWord()));
}

void vmExprImm16() {
	vmStackPush(vmReadWord());
}

void vmExprImm32() {
	vmStackPush(vmReadDword());
}

void vmExprCflag() {
	vmStackPush(memGetVar4(vmReadWord()));
}

void vmExprCflagPacked() {
	vmStackPush(memGetVar4Packed(vmReadWord()));
}

void vmExprEflag() {
	vmStackPush(memGetVar4(vmStackPop()));
}

void vmExprEflagPacked() {
	vmStackPush(memGetVar4Packed(vmStackPop()));
}

void vmExprPtr32Get32() {
	int32 i = vmStackPop();
	byte var = vmReadByte();
	byte *src = g_memPtr.systemVar32;
	if (var)
		src = memRaw() + memGetVar32(var - 1);
	if (!memPtrValid(src + i * 4, 4))
		error("VM: out of bounds read");
	vmStackPush(READ_LE_UINT32(src + i * 4));
}

void vmExprPtr32Get16() {
	int32 i = vmStackPop();
	byte var = vmReadByte();
	byte *src = memRaw() + memGetVar32(var - 1);
	if (!memPtrValid(src + i * 2, 2))
		error("VM: out of bounds read");
	vmStackPush(READ_LE_UINT16(src + i * 2));
}

void vmExprPtr32Get8() {
	int32 i = vmStackPop();
	byte var = vmReadByte();
	byte *src = memRaw() + memGetVar32(var - 1);
	if (!memPtrValid(src + i, 1))
		error("VM: out of bounds read");
	vmStackPush(src[i]);
}

void vmExprVar32() {
	vmStackPush(memGetVar32(vmReadByte()));
}

static uint32 vmExprEnd() {
	uint32 r = vmStackPop();
	if (g_vm->stackPtr > 0)
		error("VM: stack pointer is non-zero at end of expression");
	return r;
}

uint32 vmEval() {
	while (true) {
		byte op = vmReadByte();
		if (op == 0xff)
			return vmExprEnd();
		if (g_game->exprOp[op])
			g_game->exprOp[op]();
		else
			vmStackPush(op);
	}
}

uint32 vmEvalAiw() {
	while (true) {
		byte op = vmReadByte();
		if (op == 0xff)
			return vmExprEnd();
		if (op < 0x80)
			vmStackPush(op);
		else if (op < 0xa0)
			vmStackPush(memGetVar32(op - 0x80));
		else if (op >= 0xe0 && g_game->exprOp[op])
			g_game->exprOp[op]();
		else
			error("VM: invalid expression opcode: %02x", op);
	}
}

static void readStringParam(char *str, byte term) {
	uint strI;
	byte c;
	for (strI = 0; (c = vmReadByte()) != term; strI++) {
		if (strI >= kStringParamSize - 1)
			error("VM: string parameter overflowed buffer");
		str[strI] = c;
	}
	str[strI] = '\0';
}

void vmReadParams(ParamList &params) {
	uint i;
	byte b;
	for (i = 0; (b = vmReadByte()); i++) {
		if (i >= kMaxParams)
			error("VM: too many parameters");
		params.params[i].type = (MesParamType)b;
		if (b == kMesParamExpression)
			params.params[i].val = g_game->vm.eval();
		else
			readStringParam(params.params[i].str, 0);
	}
	params.nrParams = i;
}

void vmReadParamsAiw(ParamList &params) {
	uint i;
	byte b;
	for (i = 0; (b = vmPeekByte()) != 0xff; i++) {
		if (i >= kMaxParams)
			error("VM: too many parameters");
		if (b == 0xf5) {
			vmReadByte();
			params.params[i].type = kMesParamString;
			readStringParam(params.params[i].str, 0xff);
		} else {
			params.params[i].type = kMesParamExpression;
			params.params[i].val = g_game->vm.eval();
		}
	}
	params.nrParams = i;
	vmReadByte();
}

char *vmStringParam(ParamList &params, uint i) {
	if (i >= params.nrParams)
		error("VM: too few parameters");
	if (params.params[i].type != kMesParamString)
		error("VM: expected string parameter");
	return params.params[i].str;
}

uint32 vmExprParam(ParamList &params, uint i) {
	if (i >= params.nrParams) {
		warning("VM: too few parameters");
		return 0;
	}
	if (params.params[i].type != kMesParamExpression)
		error("VM: expected expression parameter %u / %u", i, params.nrParams);
	return params.params[i].val;
}

static const byte charOpener[] = {
	0x69, // （
	0x6b, // 〔
	0x6d, // ［
	0x6f, // ｛
	0x71, // 〈
	0x73, // 《
	0x75, // 「
	0x77, // 『
	0x79, // 【
};

static const byte charCloser[] = {
	0x41, // 、
	0x42, // 。
	0x45, // ・
	0x48, // ？
	0x49, // ！
	0x6a, // ）
	0x6c, // 〕
	0x6e, // ］
	0x70, // ｝
	0x72, // 〉
	0x74, // 》
	0x76, // 」
	0x78, // 』
	0x7a, // 】
};

bool charIsOpener(const char *s) {
	const byte *ch = (const byte *)s;
	if (ch[0] != 0x81 || ch[1] < 0x69 || ch[1] > 0x79)
		return false;
	for (uint i = 0; i < ARRAYSIZE(charOpener); i++) {
		if (charOpener[i] == ch[1])
			return true;
	}
	return false;
}

bool charIsCloser(const char *s) {
	const byte *ch = (const byte *)s;
	if (ch[0] != 0x81 || ch[1] < 0x41 || ch[1] > 0x7a)
		return false;
	for (uint i = 0; i < ARRAYSIZE(charCloser); i++) {
		if (charCloser[i] == ch[1])
			return true;
	}
	return false;
}

void vmDrawText(const char *text, uint mult) {
	const uint16 surface = memGetSysvar16(kSysVarDstSurface);
	const uint16 startX = memGetSysvar16(kSysVarTextStartX);
	const uint16 endX = memGetSysvar16(kSysVarTextEndX);
	const uint16 charSpace = memGetSysvar16(kSysVarCharSpace);
	const uint16 lineSpace = memGetSysvar16(kSysVarLineSpace);
	uint16 x = memGetSysvar16(kSysVarTextCursorX);
	uint16 y = memGetSysvar16(kSysVarTextCursorY);
	bool lastCharIsClose = false;
	while (*text) {
		bool zenkaku = sjisIs2Byte(*text);
		uint16 thisCharSpace = zenkaku ? charSpace / mult : charSpace / (mult * 2);
		uint16 thisX = x;
		if (x + (g_hanLineBreaks ? thisCharSpace : charSpace) > endX) {
			x = startX;
			y += lineSpace;
		}

		// opener at end of line: drop to next line
		if (x + thisCharSpace * 2 > endX && charIsOpener(text)) {
			x = startX;
			y += lineSpace;
		}

		// closer at start of line: go to last column of previous line
		// (the game allows some extra space for this)
		if (x == startX && !lastCharIsClose && charIsCloser(text)) {
			x = thisX;
			y -= lineSpace;
			lastCharIsClose = true;
		} else {
			lastCharIsClose = false;
		}

		uint32 ch;
		text = sjisNextChar(text, ch);
		gfxTextDrawGlyph(x * mult, y, surface, ch);
		x += thisCharSpace;
	}
	memSetSysvar16(kSysVarTextCursorX, x);
	memSetSysvar16(kSysVarTextCursorY, y);
}

static void readZenkaku(char *str) {
	byte c;
	int strI = 0;
	while ((c = vmPeekByte())) {
		if (!mesCharIsZenkaku(c)) {
			str[strI] = 0;
			return;
		}
		str[strI++] = vmReadByte();
		str[strI++] = vmReadByte();
	}
	vmReadByte();
	str[strI] = 0;
}

static void readHankaku(char *str) {
	byte c;
	int strI = 0;
	while ((c = vmPeekByte())) {
		if (!mesCharIsHankaku(c)) {
			str[strI] = 0;
			return;
		}
		str[strI++] = c;
		vmReadByte();
	}
	vmReadByte();
	str[strI] = 0;
}

void vmReadTextAiw(char *str, byte term) {
	byte c;
	int strI = 0;
	while ((c = vmReadByte()) != term) {
		str[strI++] = c;
		if (vmPeekByte() == term) {
			warning("VM: string is not word-aligned");
			str[strI++] = ' ';
			break;
		}
		str[strI++] = vmReadByte();
	}
	str[strI] = '\0';
}

// Zenkaku text without logging.
void vmStmtTxtNoLog() {
	char str[kVMTxtBufSize];
	readZenkaku(str);

	gfxTextSetWeight(memGetSysvar16(kSysVarFontWeight));
	if (g_game->drawTextZen)
		g_game->drawTextZen(str);
	else
		vmDrawText(str, 1);
}

// Hankaku text without logging.
void vmStmtStrNoLog() {
	char str[kVMTxtBufSize];
	readHankaku(str);

	gfxTextSetWeight(memGetSysvar16(kSysVarFontWeight));
	if (g_game->drawTextHan)
		g_game->drawTextHan(str);
	else
		vmDrawText(str, 1);
}

static bool newLogOn() {
	return vmFlagIsOn(kFlagLogEnable) && vmFlagIsOn(kFlagLogText);
}

// Zenkaku text with newer logging logic.
void vmStmtTxtNewLog() {
	if (newLogOn()) {
		backlogPrepare();
		vmFlagOn(kFlagLog);
		backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtZenkaku]);
	}
	vmStmtTxtNoLog();
	if (newLogOn())
		vmFlagOff(kFlagLog);
}

// Zenkaku text with older logging logic (Kakyuusei).
void vmStmtTxtOldLog() {
	if (!vmFlagIsOn(kFlagLogText) && !vmFlagIsOn(kFlagLog))
		backlogPrepareOld();
	if (vmFlagIsOn(kFlagLogText))
		vmFlagOn(kFlagLog);
	if (vmFlagIsOn(kFlagLogEnable) && vmFlagIsOn(kFlagLog))
		backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtZenkaku]);

	vmStmtTxtNoLog();

	if (vmFlagIsOn(kFlagLog))
		vmFlagOff(kFlagLog);
}

// Hankaku text with newer logging logic.
void vmStmtStrNewLog() {
	if (newLogOn()) {
		backlogPrepare();
		vmFlagOn(kFlagLog);
		backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtHankaku]);
	}
	vmStmtStrNoLog();
	if (newLogOn())
		vmFlagOff(kFlagLog);
}

// Hankaku text with older logging logic (Doukyuusei 2).
void vmStmtStrOldLog() {
	if (vmFlagIsOn(kFlagLogText))
		vmFlagOn(kFlagLog);
	if (vmFlagIsOn(kFlagLogEnable) && vmFlagIsOn(kFlagLog))
		backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtHankaku]);

	vmStmtStrNoLog();

	if (vmFlagIsOn(kFlagLog))
		vmFlagOff(kFlagLog);
}

// Unprefixed zenkaku text with newer logging logic.
void vmUnprefixedTxtNewLog() {
	if (newLogOn()) {
		backlogPrepare();
		vmFlagOn(kFlagLog);
	}
	vmStmtTxtNoLog();
	if (newLogOn())
		vmFlagOff(kFlagLog);
}

// Unprefixed hankaku text with newer logging logic.
void vmUnprefixedStrNewLog() {
	if (newLogOn()) {
		backlogPrepare();
		vmFlagOn(kFlagLog);
	}
	vmStmtStrNoLog();
	if (newLogOn())
		vmFlagOff(kFlagLog);
}

static void checkVar4(uint32 i) {
	if (!memPtrValid(g_memPtr.var4 + i, 1))
		error("VM: out of bounds write");
}

static void checkVar16(uint32 i) {
	if (!memPtrValid(g_memPtr.var16 + i * 2, 2))
		error("VM: out of bounds write");
}

static void checkVar32(uint32 i) {
	if (!memPtrValid(g_memPtr.var32 + i * 4, 4))
		error("VM: out of bounds write");
}

static void checkSysvar16(uint32 i) {
	if (!memPtrValid(g_memPtr.systemVar16 + i * 2, 2))
		error("VM: out of bounds write");
}

enum FlagMode {
	kFlagModePlain,
	kFlagModeWrap,
	kFlagModeSaturate
};

static void setFlags(uint32 i, FlagMode mode) {
	do {
		checkVar4(i);
		uint32 v = g_game->vm.eval();
		if (mode == kFlagModeWrap)
			v &= 0xf;
		else if (mode == kFlagModeSaturate)
			v = MIN<uint32>(v, 0xf);
		memSetVar4(i++, v);
	} while (vmReadByte());
}

void vmStmtSetFlagConst16() { setFlags(vmReadWord(), kFlagModePlain); }
void vmStmtSetFlagConst16_4bitWrap() { setFlags(vmReadWord(), kFlagModeWrap); }
void vmStmtSetFlagConst16_4bitSaturate() { setFlags(vmReadWord(), kFlagModeSaturate); }
void vmStmtSetFlagExpr() { setFlags(g_game->vm.eval(), kFlagModePlain); }
void vmStmtSetFlagExpr4bitWrap() { setFlags(g_game->vm.eval(), kFlagModeWrap); }
void vmStmtSetFlagExpr4bitSaturate() { setFlags(g_game->vm.eval(), kFlagModeSaturate); }

static void setFlagsAiw(uint32 i) {
	do {
		checkVar4(i);
		memSetVar4Packed(i++, MIN<uint32>(g_game->vm.eval(), 0xf));
	} while (vmPeekByte() != 0xff);
	vmReadByte();
}

void vmStmtSetFlagConst16Aiw() { setFlagsAiw(vmReadWord()); }
void vmStmtSetFlagExprAiw() { setFlagsAiw(g_game->vm.eval()); }

void vmStmtSetVar16Const8() {
	byte i = vmReadByte();
	do {
		checkVar16(i);
		memSetVar16(i++, g_game->vm.eval());
	} while (vmReadByte());
}

static void setVar16Aiw(uint32 i) {
	do {
		checkVar16(i);
		memSetVar16(i++, MIN<uint32>(0xffff, g_game->vm.eval()));
	} while (vmPeekByte() != 0xff);
	vmReadByte();
}

void vmStmtSetVar16Const16Aiw() { setVar16Aiw(vmReadWord()); }
void vmStmtSetVar16ExprAiw() { setVar16Aiw(g_game->vm.eval()); }

void vmStmtSetVar32Const8() {
	int32 i = vmReadByte();
	do {
		checkVar32(i);
		memSetVar32(i++, g_game->vm.eval());
	} while (vmReadByte());
}

void vmStmtSetVar32Const8Aiw() {
	byte i = vmReadByte();
	checkVar32(i);
	memSetVar32(i, g_game->vm.eval());
}

static void setSysvar16Aiw(uint32 i) {
	do {
		checkSysvar16(i);
		memSetSysvar16(i++, MIN<uint32>(g_game->vm.eval(), 0xffff));
	} while (vmPeekByte() != 0xff);
	vmReadByte();
}

void vmStmtSetSysvar16Const16Aiw() { setSysvar16Aiw(vmReadWord()); }
void vmStmtSetSysvar16ExprAiw() { setSysvar16Aiw(g_game->vm.eval()); }

void vmStmtPtr16Set8() {
	int32 i = g_game->vm.eval();
	byte var = vmReadByte();
	byte *dst = memRaw() + memGetVar16(var) + i;
	do {
		if (!memPtrValid(dst, 1))
			error("VM: out of bounds write");
		*dst++ = g_game->vm.eval();
	} while (vmReadByte());
}

void vmStmtPtr16Set16() {
	int32 i = g_game->vm.eval();
	byte var = vmReadByte();
	byte *dst = g_memPtr.systemVar16;
	if (var)
		dst = memRaw() + memGetVar16(var - 1);
	do {
		if (!memPtrValid(dst + i * 2, 2))
			error("VM: out of bounds write");
		WRITE_LE_UINT16(dst + i * 2, g_game->vm.eval());
		i++;
	} while (vmReadByte());
}

void vmStmtPtr32Set32() {
	int32 i = g_game->vm.eval();
	byte var = vmReadByte();
	byte *dst = g_memPtr.systemVar32;
	if (var)
		dst = memRaw() + memGetVar32(var - 1);
	do {
		if (!memPtrValid(dst + i * 4, 4))
			error("VM: out of bounds write");
		WRITE_LE_UINT32(dst + i * 4, g_game->vm.eval());
		i++;
	} while (vmReadByte());
}

void vmStmtPtr32Set16() {
	int32 i = g_game->vm.eval();
	byte var = vmReadByte();
	byte *dst = memRaw() + memGetVar32(var - 1);
	do {
		if (!memPtrValid(dst + i * 2, 2))
			error("VM: out of bounds write");
		WRITE_LE_UINT16(dst + i * 2, g_game->vm.eval());
		i++;
	} while (vmReadByte());
}

void vmStmtPtr32Set8() {
	int32 i = g_game->vm.eval();
	byte var = vmReadByte();
	byte *dst = memRaw() + memGetVar32(var - 1) + i;
	do {
		if (!memPtrValid(dst, 1))
			error("VM: out of bounds write");
		*dst++ = g_game->vm.eval();
	} while (vmReadByte());
}

void vmStmtJz() {
	uint32 val = g_game->vm.eval();
	uint32 ptr = vmReadDword();
	if (val == 1)
		return;
	g_vm->ip.ptr = ptr;
}

void vmStmtJmp() {
	g_vm->ip.ptr = READ_LE_UINT32(g_vm->ip.code + g_vm->ip.ptr);
}

static void doStmtSys() {
	uint32 no = g_game->vm.eval();
	ParamList params = ParamList();
	g_game->vm.readParams(params);

	if (no >= kGameMaxSys)
		error("VM: invalid System call number: %u", no);
	if (!g_game->sys[no])
		error("VM: System.function[%u] not implemented", no);
	g_game->sys[no](params);
}

void vmStmtSys() {
	bool log = vmFlagIsOn(kFlagLogEnable) && vmFlagIsOn(kFlagLogSys);
	if (log) {
		vmFlagOn(kFlagLog);
		backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtSys]);
	}
	doStmtSys();
	if (vmFlagIsOn(kFlagLogEnable) && vmFlagIsOn(kFlagLogSys))
		vmFlagOff(kFlagLog);
}

void vmStmtSysOldLog() {
	if (vmFlagIsOn(kFlagLogText)) {
		vmFlagOn(kFlagLog);
		if (vmFlagIsOn(kFlagLogEnable))
			backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtSys]);
	}
	doStmtSys();
	if (vmFlagIsOn(kFlagLog))
		vmFlagOff(kFlagLog);
}

void vmStmtMesjmp() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	vmLoadMes(vmStringParam(params, 0));
	vmFlagOn(kFlagReturn);
}

static void doStmtMescall(bool saveProcedures) {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	vmStringParam(params, 0);

	// save current VM state
	VMMesCall *frame = &g_vm->mesCallStack[g_vm->mesCallStackPtr++];
	frame->ip = g_vm->ip;
	memcpy(frame->mesName, memMesName(), 12);
	frame->mesName[12] = '\0';
	if (saveProcedures)
		memcpy(frame->procedures, g_vm->procedures, sizeof(g_vm->procedures));

	// load and execute mes file
	g_vm->ip.ptr = 0;
	g_vm->ip.code = g_mem->fileData;
	vmLoadMes(params.params[0].str);
	g_game->vm.exec();

	// restore previous VM state
	frame = &g_vm->mesCallStack[--g_vm->mesCallStackPtr];
	g_vm->ip.code = frame->ip.code;
	if (!vmFlagIsOn(kFlagReturn)) {
		g_vm->ip.ptr = frame->ip.ptr;
		if (saveProcedures)
			memcpy(g_vm->procedures, frame->procedures, sizeof(g_vm->procedures));
		vmLoadMes(frame->mesName);
		frame->ip.ptr = 0;
		frame->ip.code = nullptr;
		frame->mesName[0] = '\0';
	}
}

void vmStmtMescall() {
	doStmtMescall(false);
}

// YU-NO
void vmStmtMescallSaveProcedures() {
	doStmtMescall(true);
}

void vmStmtDefmenu() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	uint32 addr = vmReadDword();
	menuDefine(vmExprParam(params, 0), addr == g_vm->ip.ptr + 1);
	g_vm->ip.ptr = addr;
}

void vmStmtDefmenuAiw() {
	uint32 no = g_game->vm.eval();
	if (no >= kAiwMaxMenus)
		error("VM: invalid menu index: %u", no);

	g_vm->ip.ptr = vmReadDword();
	byte nrEntries = vmReadByte();
	if (nrEntries > kAiwMaxMenuEntries)
		error("VM: too many menu entries: %u", nrEntries);
	for (uint i = 0; i < nrEntries; i++) {
		g_aiwMenuEntries[no][i].condAddr = vmReadDword();
		g_aiwMenuEntries[no][i].bodyAddr = vmReadDword();
	}
	g_aiwMenuNrEntries[no] = nrEntries;
}

void vmCallProcedure(uint no) {
	if (no >= kVMMaxProcedures)
		error("VM: invalid procedure number: %u", no);

	VMPointer savedIp = g_vm->ip;
	g_vm->ip = g_vm->procedures[no];
	g_game->vm.exec();
	g_vm->ip = savedIp;
}

void vmStmtCall() {
	bool flagOn = vmFlagIsOn(kFlagProcClear);
	vmFlagOff(kFlagProcClear);

	ParamList params = ParamList();
	g_game->vm.readParams(params);
	vmCallProcedure(vmExprParam(params, 0));

	if (flagOn)
		vmFlagOn(kFlagProcClear);
}

void vmStmtCallOldLog() {
	if (vmFlagIsOn(kFlagLogText)) {
		vmFlagOn(kFlagLog);
		if (vmFlagIsOn(kFlagLogEnable))
			backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtCallProc]);
	}
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	if (vmFlagIsOn(kFlagLog))
		vmFlagOff(kFlagLog);
	vmCallProcedure(vmExprParam(params, 0));
}

void vmStmtUtil() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	if (params.nrParams < 1)
		error("VM: Util without parameters");
	uint32 no = vmExprParam(params, 0);
	if (no >= kGameMaxUtil)
		error("VM: invalid Util number: %u", no);
	if (!g_game->util[no])
		error("VM: Util.function[%u] not implemented", no);
	g_game->util[no](params);
}

void vmStmtLine() {
	if (vmReadByte())
		return;

	uint16 startX = memGetSysvar16(kSysVarTextStartX);
	uint16 curY = memGetSysvar16(kSysVarTextCursorY);
	uint16 lineSpace = memGetSysvar16(kSysVarLineSpace);
	memSetSysvar16(kSysVarTextCursorX, startX);
	memSetSysvar16(kSysVarTextCursorY, curY + lineSpace);
}

void vmStmtLineOldLog() {
	if (vmFlagIsOn(kFlagLogText)) {
		vmFlagOn(kFlagLog);
		if (vmFlagIsOn(kFlagLogEnable))
			backlogPushByte(g_mesCodes.stmtOpToInt[kMesStmtCallProc]);
	}
	vmStmtLine();
	if (vmFlagIsOn(kFlagLog))
		vmFlagOff(kFlagLog);
}

void vmStmtDefproc() {
	uint32 i = g_game->vm.eval();
	if (i >= kVMMaxProcedures)
		error("VM: invalid procedure number: %u", i);
	g_vm->procedures[i] = g_vm->ip;
	g_vm->procedures[i].ptr += 4;
	g_vm->ip.ptr = vmReadDword();
}

void vmStmtMenuexec() {
	menuExec();
}

static bool vmExecStatement() {
	byte op = vmPeekByte();
	if (!g_game->stmtOp[op]) {
		if (op == g_game->vm.endCode)
			return false;
		if (mesCharIsHankaku(op)) {
			if (g_game->unprefixedHan)
				g_game->unprefixedHan();
			else
				vmUnprefixedStrNewLog();
		} else {
			if (g_game->unprefixedZen)
				g_game->unprefixedZen();
			else
				vmUnprefixedTxtNewLog();
		}
	} else {
		vmReadByte();
		g_game->stmtOp[op]();
	}
	return true;
}

void vmPeek() {
	handleEvents();
	animExecute();
	audioUpdate();

	// prevent re-entrant update calls
	static bool inGameUpdate = false;
	if (g_game->update && !inGameUpdate) {
		inGameUpdate = true;
		g_game->update();
		inGameUpdate = false;
	}

	gfxUpdate();
}

void vmExec() {
	g_vm->scopeCounter++;
	while (!g_engine->shouldQuit()) {
		if (vmFlagIsOn(kFlagReturn)) {
			if (g_vm->scopeCounter != 1)
				break;
			vmFlagOff(kFlagReturn);
			g_vm->ip.ptr = 0;
		}
		if (!vmExecStatement())
			break;
		vmPeek();
	}
	g_vm->scopeCounter--;
}

static char nextMes[33];
static bool loadNextMes = false;

void vmMesjmpAiw(const char *name) {
	Common::strlcpy(nextMes, name, sizeof(nextMes));
	loadNextMes = true;
}

void vmStmtMesjmpAiw() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	vmMesjmpAiw(vmStringParam(params, 0));
}

void vmStmtMescallAiw() {
	ParamList params = ParamList();
	g_game->vm.readParams(params);
	vmStringParam(params, 0);

	// save current VM state
	VMMesCall *frame = &g_vm->mesCallStack[g_vm->mesCallStackPtr++];
	frame->ip = g_vm->ip;
	memcpy(frame->mesName, memMesName(), 31);
	frame->mesName[31] = '\0';

	// load and execute mes file
	g_vm->ip.ptr = 0;
	g_vm->ip.code = g_mem->fileData;
	vmLoadMes(params.params[0].str);
	g_game->vm.exec();

	// restore previous VM state
	frame = &g_vm->mesCallStack[--g_vm->mesCallStackPtr];
	g_vm->ip.code = frame->ip.code;
	g_vm->ip.ptr = frame->ip.ptr;
	vmLoadMes(frame->mesName);
	frame->ip.ptr = 0;
	frame->ip.code = nullptr;
	frame->mesName[0] = '\0';
}

void vmExecAiw() {
	while (!g_engine->shouldQuit()) {
		if (!vmExecStatement())
			break;
		if (loadNextMes) {
			vmLoadMes(nextMes);
			g_vm->ip.ptr = 0;
			loadNextMes = false;
			nextMes[0] = '\0';
		}
		vmPeek();
	}
}

void vmSetDefaultOps(Game &game) {
	game.exprOp[0x80] = vmExprVar16;
	game.exprOp[0xa0] = vmExprPtr16Get16;
	game.exprOp[0xc0] = vmExprPtr16Get8;
	game.exprOp[0xe0] = vmExprPlus;
	game.exprOp[0xe1] = vmExprMinus;
	game.exprOp[0xe2] = vmExprMul;
	game.exprOp[0xe3] = vmExprDiv;
	game.exprOp[0xe4] = vmExprMod;
	game.exprOp[0xe5] = vmExprRand;
	game.exprOp[0xe6] = vmExprAnd;
	game.exprOp[0xe7] = vmExprOr;
	game.exprOp[0xe8] = vmExprBitand;
	game.exprOp[0xe9] = vmExprBitior;
	game.exprOp[0xea] = vmExprBitxor;
	game.exprOp[0xeb] = vmExprLt;
	game.exprOp[0xec] = vmExprGt;
	game.exprOp[0xed] = vmExprLte;
	game.exprOp[0xee] = vmExprGte;
	game.exprOp[0xef] = vmExprEq;
	game.exprOp[0xf0] = vmExprNeq;
	game.exprOp[0xf1] = vmExprImm16;
	game.exprOp[0xf2] = vmExprImm32;
	game.exprOp[0xf3] = vmExprCflag;
	game.exprOp[0xf4] = vmExprEflag;
	game.exprOp[0xf5] = vmExprPtr32Get32;
	game.exprOp[0xf6] = vmExprVar32;

	game.stmtOp[0x01] = vmStmtTxtNewLog;
	game.stmtOp[0x02] = vmStmtStrNewLog;
	game.stmtOp[0x03] = vmStmtSetFlagConst16;
	game.stmtOp[0x04] = vmStmtSetVar16Const8;
	game.stmtOp[0x05] = vmStmtSetFlagExpr;
	game.stmtOp[0x06] = vmStmtPtr16Set8;
	game.stmtOp[0x07] = vmStmtPtr16Set16;
	game.stmtOp[0x08] = vmStmtPtr32Set32;
	game.stmtOp[0x09] = vmStmtJz;
	game.stmtOp[0x0a] = vmStmtJmp;
	game.stmtOp[0x0b] = vmStmtSys;
	game.stmtOp[0x0c] = vmStmtMesjmp;
	game.stmtOp[0x0d] = vmStmtMescall;
	game.stmtOp[0x0e] = vmStmtDefmenu;
	game.stmtOp[0x0f] = vmStmtCall;
	game.stmtOp[0x10] = vmStmtUtil;
	game.stmtOp[0x11] = vmStmtLine;
	game.stmtOp[0x12] = vmStmtDefproc;
	game.stmtOp[0x13] = vmStmtMenuexec;
	game.stmtOp[0x14] = vmStmtSetVar32Const8;
}

} // End of namespace AI5
