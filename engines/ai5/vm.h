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

#ifndef AI5_VM_H
#define AI5_VM_H

#include "common/scummsys.h"

#include "ai5/game.h"
#include "ai5/memory.h"
#include "ai5/mes.h"

namespace AI5 {

enum {
	kVMStackSize = 1024,
	kVMMaxProcedures = 150,
	kStringParamSize = 64,
	kMaxParams = 30,
	kVMTxtBufSize = 4096,
	kAiwMaxMenus = 5,
	kAiwMaxMenuEntries = 100
};

struct VMPointer {
	uint32 ptr;
	byte *code;
};

struct VMMesCall {
	VMPointer ip;
	char mesName[32];
	VMPointer procedures[kVMMaxProcedures];
};

struct VM {
	VMPointer ip;
	uint scopeCounter;
	// stack for expressions
	uint16 stackPtr;
	uint32 stack[kVMStackSize];
	// stack for CALL statement
	uint16 mesCallStackPtr;
	VMMesCall mesCallStack[128];
	// procedures defined with PROCD
	VMPointer procedures[kVMMaxProcedures];
};

struct Param {
	MesParamType type;
	char str[kStringParamSize];
	uint32 val;
};

struct ParamList {
	Param params[kMaxParams];
	uint nrParams;
};

struct AiwMenuEntry {
	uint32 condAddr;
	uint32 bodyAddr;
};

extern VM *g_vm;
extern AiwMenuEntry g_aiwMenuEntries[kAiwMaxMenus][kAiwMaxMenuEntries];
extern uint g_aiwMenuNrEntries[kAiwMaxMenus];
extern bool g_hanLineBreaks;

void vmInit();
void vmExec();
void vmExecAiw();
void vmPeek();
void vmLoadFile(const byte *data, uint32 size, uint32 offset);
void vmLoadDataFile(const char *name, uint32 offset);
void vmLoadMes(const char *name);
void vmCallProcedure(uint no);
uint32 vmEval();
uint32 vmEvalAiw();
void vmReadParams(ParamList &params);
void vmReadParamsAiw(ParamList &params);
char *vmStringParam(ParamList &params, uint i);
uint32 vmExprParam(ParamList &params, uint i);
void vmReadTextAiw(char *str, uint size, byte term);
void vmMesjmpAiw(const char *name);
void vmDrawText(const char *text, uint mult);
/** Whether the Shift-JIS character at s is an opening/closing bracket. */
bool charIsOpener(const char *s);
bool charIsCloser(const char *s);

// generic stack operations
void vmExprPlus();
void vmExprMinus();
void vmExprMinusUnsigned();
void vmExprMul();
void vmExprDiv();
void vmExprMod();
void vmExprAnd();
void vmExprOr();
void vmExprBitand();
void vmExprBitior();
void vmExprBitxor();
void vmExprLt();
void vmExprGt();
void vmExprLte();
void vmExprGte();
void vmExprEq();
void vmExprNeq();
void vmExprRand();
void vmExprRandWithImmRange();
void vmExprImm16();
void vmExprImm32();
// flag/register/memory operations
void vmExprCflag();
void vmExprCflagPacked();
void vmExprEflag();
void vmExprEflagPacked();
void vmExprVar16();
void vmExprVar16Const16();
void vmExprVar16Expr();
void vmExprVar32();
void vmExprSysvar16Const16();
void vmExprSysvar16Expr();
void vmExprPtr16Get8();
void vmExprPtr16Get16();
void vmExprPtr32Get8();
void vmExprPtr32Get16();
void vmExprPtr32Get32();

void vmStmtTxtNoLog();
void vmStmtTxtNewLog();
void vmStmtTxtOldLog();
void vmStmtStrNoLog();
void vmStmtStrNewLog();
void vmStmtStrOldLog();
void vmUnprefixedTxtNewLog();
void vmUnprefixedStrNewLog();
void vmStmtSetFlagConst16();
void vmStmtSetFlagConst16Aiw();
void vmStmtSetFlagConst16_4bitWrap();
void vmStmtSetFlagConst16_4bitSaturate();
void vmStmtSetFlagExpr();
void vmStmtSetFlagExprAiw();
void vmStmtSetFlagExpr4bitWrap();
void vmStmtSetFlagExpr4bitSaturate();
void vmStmtSetVar16Const8();
void vmStmtSetVar16Const16Aiw();
void vmStmtSetVar16ExprAiw();
void vmStmtSetVar32Const8();
void vmStmtSetVar32Const8Aiw();
void vmStmtSetSysvar16Const16Aiw();
void vmStmtSetSysvar16ExprAiw();
void vmStmtPtr16Set8();
void vmStmtPtr16Set16();
void vmStmtPtr32Set32();
void vmStmtPtr32Set16();
void vmStmtPtr32Set8();
void vmStmtJz();
void vmStmtJmp();
void vmStmtSys();
void vmStmtSysOldLog();
void vmStmtMesjmp();
void vmStmtMesjmpAiw();
void vmStmtMescall();
void vmStmtMescallSaveProcedures();
void vmStmtMescallAiw();
void vmStmtDefmenu();
void vmStmtDefmenuAiw();
void vmStmtMenuexec();
void vmStmtDefproc();
void vmStmtCall();
void vmStmtCallOldLog();
void vmStmtUtil();
void vmStmtLine();
void vmStmtLineOldLog();

/** Fill the opcode tables with the default AI5 opcodes. */
void vmSetDefaultOps(Game &game);

inline bool vmFlagIsOn(GameFlag flag) {
	if (g_game->flags[flag] == kFlagAlwaysOn)
		return true;
	return memGetSysvar16(kSysVarFlags) & g_game->flags[flag];
}

inline void vmFlagOn(GameFlag flag) {
	if (g_game->flags[flag] == kFlagAlwaysOn)
		return;
	memSetSysvar16(kSysVarFlags, memGetSysvar16(kSysVarFlags) | g_game->flags[flag]);
}

inline void vmFlagOff(GameFlag flag) {
	memSetSysvar16(kSysVarFlags, memGetSysvar16(kSysVarFlags) & ~g_game->flags[flag]);
}

// input.cpp
void vmDelay(int ms);
uint32 vmGetTicks();

typedef uint32 VMTimer;

inline void vmTimerTick(VMTimer &timer, uint ms) {
	uint32 t = vmGetTicks();
	uint32 deltaT = t - timer;
	if (deltaT < ms) {
		vmDelay(ms - deltaT);
		timer = t + (ms - deltaT);
	} else {
		timer = t;
	}
}

inline bool vmTimerTickAsync(VMTimer &timer, uint ms) {
	uint32 t = vmGetTicks();
	if (t - timer < ms)
		return false;
	timer = t;
	return true;
}

inline VMTimer vmTimerCreate() { return vmGetTicks(); }

} // End of namespace AI5

#endif // AI5_VM_H
