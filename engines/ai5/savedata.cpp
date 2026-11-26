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

#include "common/array.h"
#include "common/config-manager.h"
#include "common/file.h"
#include "common/ptr.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "ai5/ai5.h"
#include "ai5/savedata.h"
#include "ai5/vm.h"

namespace AI5 {

// The games keep their saves in FLAG## files shipped with the game. They are
// stored through the savefile manager as <target>.FLAG##; the shipped file is
// the initial contents.
static Common::String saveFileName(const char *saveName) {
	return ConfMan.getActiveDomainName() + "." + saveName;
}

static void loadSaveFile(const char *saveName, Common::Array<byte> &data) {
	data.clear();
	Common::ScopedPtr<Common::InSaveFile> in(g_system->getSavefileManager()->openForLoading(saveFileName(saveName)));
	Common::SeekableReadStream *s = in.get();
	Common::File f;
	if (!s && f.open(Common::Path(saveName)))
		s = &f;
	if (s) {
		data.resize(s->size());
		s->read(data.data(), data.size());
	}
	if (data.size() < g_game->mem16Size)
		data.resize(g_game->mem16Size);
}

static void storeSaveFile(const char *saveName, const Common::Array<byte> &data) {
	Common::ScopedPtr<Common::OutSaveFile> out(g_system->getSavefileManager()->openForSaving(saveFileName(saveName), false));
	if (out) {
		out->write(data.data(), data.size());
		out->finalize();
	}
	if (!out || out->err())
		warning("Failed to write save file \"%s\"", saveName);
}

void savedataRead(const char *saveName, byte *buf, uint32 off, uint32 size) {
	Common::Array<byte> data;
	loadSaveFile(saveName, data);
	if (off + size > data.size()) {
		warning("Save file \"%s\" is too short", saveName);
		return;
	}
	memcpy(buf + off, data.data() + off, size);
}

void savedataWrite(const char *saveName, const byte *buf, uint32 off, uint32 size) {
	Common::Array<byte> data;
	loadSaveFile(saveName, data);
	if (off + size > data.size())
		data.resize(off + size);
	memcpy(data.data() + off, buf + off, size);
	storeSaveFile(saveName, data);
}

void savedataResumeLoad(const char *saveName) {
	savedataRead(saveName, memRaw(), 0, g_game->mem16Size);
	g_game->memRestore();
	vmLoadMes(memMesName());
	vmFlagOn(kFlagReturn);
}

void savedataResumeSave(const char *saveName) {
	savedataWrite(saveName, memRaw(), 0, g_game->mem16Size);
}

// load/save everything except the mes name
void savedataLoad(const char *saveName, uint start) {
	savedataRead(saveName, memRaw(), start, g_game->mem16Size - start);
	g_game->memRestore();
}

void savedataSave(const char *saveName, uint start) {
	savedataWrite(saveName, memRaw(), start, g_game->mem16Size - start);
}

static uint32 var4Off() {
	return g_memPtr.var4 - memRaw();
}

void savedataLoadVar4(const char *saveName) {
	savedataRead(saveName, memRaw(), var4Off(), g_game->var4Size);
}

void savedataLoadVar4Restore(const char *saveName) {
	savedataRead(saveName, memRaw(), var4Off(), g_game->var4Size);
	g_game->memRestore();
}

void savedataSaveVar4(const char *saveName) {
	savedataWrite(saveName, memRaw(), var4Off(), g_game->var4Size);
}

void savedataSaveUnionVar4(const char *saveName) {
	Common::Array<byte> data;
	loadSaveFile(saveName, data);
	const uint32 off = var4Off();
	if (off + g_game->var4Size > data.size())
		data.resize(off + g_game->var4Size);
	for (uint i = 0; i < g_game->var4Size; i++)
		data[off + i] |= g_memPtr.var4[i];
	storeSaveFile(saveName, data);
}

void savedataLoadVar4Slice(const char *saveName, uint from, uint to) {
	if (from > to)
		return;
	savedataRead(saveName, memRaw(), var4Off() + from, to + 1 - from);
}

void savedataSaveVar4Slice(const char *saveName, uint from, uint to) {
	if (from > to)
		return;
	savedataWrite(saveName, memRaw(), var4Off() + from, to + 1 - from);
}

void savedataCopy(const char *srcSave, const char *dstSave) {
	byte buf[kMemMem16MaxSize];
	savedataRead(srcSave, buf, 0, g_game->mem16Size);
	savedataWrite(dstSave, buf, 0, g_game->mem16Size);
}

void savedataSetMesName(const char *saveName, const char *mesName) {
	savedataWrite(saveName, (const byte *)mesName, 0, strlen(mesName) + 1);
}

void savedataLoadVariables(const char *saveName, const char *vars) {
	const uint32 var16Off = g_memPtr.var16 - memRaw();
	byte save[kMemMem16MaxSize];
	savedataRead(saveName, save, var16Off, 26 * 2);

	for (const char *str = vars; *str; str++) {
		if (*str < 'A' || *str > 'Z') {
			warning("Invalid variable name: %c", *str);
			return;
		}
		uint varno = *str - 'A';
		memSetVar16(varno, READ_LE_UINT16(save + var16Off + varno * 2));
	}
}

} // End of namespace AI5
