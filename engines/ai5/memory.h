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

#ifndef AI5_MEMORY_H
#define AI5_MEMORY_H

#include "common/endian.h"

#include "ai5/mes.h"

namespace AI5 {

enum {
	kMemFileDataSize = 0x630d40,
	kMemMem16MaxSize = 0x2000,
	kMemVar4MaxSize = 0x1000,
	kMemMenuEntryMax = 200,
	kMemBacklogDataSize = 2048,
	kMemBacklogNrEntries = 64
};

/**
 * The VM's 32-bit address space. The layout of the 16-bit area at the start
 * varies by game; MemoryPtr locates the variables in it.
 */
struct Memory {
	byte mem16[kMemMem16MaxSize];
	byte fileData[kMemFileDataSize];
	byte palette[0x400];
	byte savedPalette[0x400];
	uint32 menuEntryAddresses[kMemMenuEntryMax];
	uint32 menuEntryNumbers[kMemMenuEntryMax];
	byte backlog[kMemBacklogDataSize * kMemBacklogNrEntries];
	byte mapData[52];
};

struct MemoryPtr {
	byte *mesName;
	byte *var4;
	byte *systemVar16Ptr;
	byte *var16;
	byte *systemVar16;
	byte *var32;
	byte *systemVar32;
};

extern Memory *g_mem;
extern MemoryPtr g_memPtr;

inline byte *memRaw() { return (byte *)g_mem; }
inline byte *memEnd() { return memRaw() + sizeof(Memory); }

inline bool memPtrValid(const byte *p, int size) {
	return p >= memRaw() && p + size <= memEnd();
}

inline void memSetSysvar16Ptr(uint32 ptr) { WRITE_LE_UINT32(g_memPtr.systemVar16Ptr, ptr); }
inline char *memMesName() { return (char *)memRaw(); }

inline byte memGetVar4(uint i) { return g_memPtr.var4[i]; }
inline void memSetVar4(uint i, byte v) { g_memPtr.var4[i] = v; }

inline byte memGetVar4Packed(uint no) {
	byte flag = memGetVar4(no / 2);
	return (no % 2) ? flag & 0xf : flag >> 4;
}

inline void memSetVar4Packed(uint no, byte val) {
	uint i = no / 2;
	byte b = memGetVar4(i);
	if (no % 2)
		memSetVar4(i, (b & 0xf0) | val);
	else
		memSetVar4(i, (b & 0x0f) | (val << 4));
}

inline uint16 memGetVar16(uint i) { return READ_LE_UINT16(g_memPtr.var16 + i * 2); }
inline void memSetVar16(uint i, uint16 v) { WRITE_LE_UINT16(g_memPtr.var16 + i * 2, v); }
inline uint16 memGetSysvar16(uint i) { return READ_LE_UINT16(g_memPtr.systemVar16 + i * 2); }
inline void memSetSysvar16(uint i, uint16 v) { WRITE_LE_UINT16(g_memPtr.systemVar16 + i * 2, v); }
inline uint32 memGetVar32(uint i) { return READ_LE_UINT32(g_memPtr.var32 + i * 4); }
inline void memSetVar32(uint i, uint32 v) { WRITE_LE_UINT32(g_memPtr.var32 + i * 4, v); }
inline uint32 memGetSysvar32(uint i) { return READ_LE_UINT32(g_memPtr.systemVar32 + i * 4); }
inline void memSetSysvar32(uint i, uint32 v) { WRITE_LE_UINT32(g_memPtr.systemVar32 + i * 4, v); }

inline uint16 memGetSysvar16(MesSysVar16 v) { return memGetSysvar16(sysvar16(v)); }
inline void memSetSysvar16(MesSysVar16 v, uint16 val) { memSetSysvar16(sysvar16(v), val); }
inline uint32 memGetSysvar32(MesSysVar32 v) { return memGetSysvar32(sysvar32(v)); }
inline void memSetSysvar32(MesSysVar32 v, uint32 val) { memSetSysvar32(sysvar32(v), val); }

/** Returns the null-terminated string at ptr, or nullptr if it runs past the end of memory. */
inline char *memGetCString(uint32 ptr) {
	byte *p = memRaw() + ptr;
	for (int i = 0; true; i++) {
		if (p + i >= memEnd())
			return nullptr;
		if (!p[i])
			break;
	}
	return (char *)p;
}

} // End of namespace AI5

#endif // AI5_MEMORY_H
