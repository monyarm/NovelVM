/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage
 * (GPL-2.0-or-later).
 *
 */

#include "common/array.h"
#include "common/config-manager.h"
#include "common/file.h"
#include "common/ptr.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "graphics/thumbnail.h"

#include "ai5/ai5.h"
#include "ai5/detection.h"
#include "ai5/game.h"
#include "ai5/savedata.h"
#include "ai5/vm.h"

namespace AI5 {

// The games keep their saves in FLAG## files shipped with the game. They are
// stored through the savefile manager as <target>.FLAG##; the shipped file is
// the initial contents. Files written by NovelVM are wrapped in a header with
// a description, a thumbnail, the creation date and the play time so they show
// up in the launcher and the global main menu; headerless files (those shipped
// with the games) are loaded as plain memory images.
static const uint32 kSaveMagic = MKTAG('A', 'I', '5', 'V');
static const uint32 kSaveVersion = 1;

struct SaveFile {
	Common::Array<byte> payload;
	SaveHeader hdr;
	bool wrapped;
};

static Common::String saveFileName(const char *saveName) {
	return ConfMan.getActiveDomainName() + "." + saveName;
}

static Common::String defaultDesc(const char *saveName) {
	if (!strncmp(saveName, "FLAG", 4)) {
		int slot = atoi(saveName + 4);
		return Common::String::format("Save %d", slot);
	}
	return Common::String(saveName);
}

// Parse the header of a wrapped save file. The stream must be positioned at
// the start; on success it is positioned at the payload and true is returned.
// Without the magic, the file is a plain memory image and the stream is rewound.
// When wantThumbnail is set, hdr.thumbnail is allocated and owned by the caller.
static bool readHeader(Common::SeekableReadStream *s, SaveHeader &hdr, bool wantThumbnail) {
	if (s->readUint32BE() != kSaveMagic) {
		s->seek(0, SEEK_SET);
		return false;
	}
	uint32 version = s->readUint32BE();
	if (version > kSaveVersion) {
		warning("Save file version %u is too new", version);
		s->seek(0, SEEK_SET);
		return false;
	}
	uint32 descLen = s->readUint32BE();
	hdr.desc = s->readString(0, descLen);
	if (!Graphics::loadThumbnail(*s, hdr.thumbnail, !wantThumbnail)) {
		warning("Failed to read save file thumbnail");
		s->seek(0, SEEK_SET);
		return false;
	}
	hdr.td.tm_sec  = s->readUint32BE();
	hdr.td.tm_min  = s->readUint32BE();
	hdr.td.tm_hour = s->readUint32BE();
	hdr.td.tm_mday = s->readUint32BE();
	hdr.td.tm_mon  = s->readUint32BE();
	hdr.td.tm_wday = s->readUint32BE();
	hdr.td.tm_year = s->readUint32BE();
	hdr.playTimeSec = s->readUint32BE();
	return true;
}

static void loadSaveFile(const char *saveName, SaveFile &sf) {
	sf.payload.clear();
	sf.hdr.desc.clear();
	sf.hdr.playTimeSec = 0;
	sf.wrapped = false;
	Common::ScopedPtr<Common::InSaveFile> in(g_system->getSavefileManager()->openForLoading(saveFileName(saveName)));
	Common::SeekableReadStream *s = in.get();
	Common::File f;
	if (!s && !f.open(Common::Path(saveName))) {
		// Shipped files can use lowercase names (e.g. "flag0"); the
		// filesystem on case-sensitive platforms needs the exact name.
		Common::String lower = saveName;
		lower.toLowercase();
		f.open(Common::Path(lower));
	}
	if (!s && f.isOpen())
		s = &f;
	if (s) {
		if (readHeader(s, sf.hdr, false)) {
			sf.wrapped = true;
			sf.payload.resize(s->size() - s->pos());
		} else {
			sf.payload.resize(s->size());
		}
		s->read(sf.payload.data(), sf.payload.size());
	}
	if (sf.payload.size() < g_game->mem16Size)
		sf.payload.resize(g_game->mem16Size);
}

static void storeSaveFile(const char *saveName, const SaveFile &sf) {
	Common::ScopedPtr<Common::OutSaveFile> out(g_system->getSavefileManager()->openForSaving(saveFileName(saveName), false));
	if (!out) {
		warning("Failed to write save file \"%s\"", saveName);
		return;
	}
	out->writeUint32BE(kSaveMagic);
	out->writeUint32BE(kSaveVersion);
	Common::String desc = sf.hdr.desc;
	if (desc.empty())
		desc = defaultDesc(saveName);
	out->writeUint32BE(desc.size());
	out->write(desc.c_str(), desc.size());
	if (!Graphics::saveThumbnail(*out))
		warning("Failed to write save file thumbnail");
	TimeDate td;
	g_system->getTimeAndDate(td);
	out->writeUint32BE(td.tm_sec);
	out->writeUint32BE(td.tm_min);
	out->writeUint32BE(td.tm_hour);
	out->writeUint32BE(td.tm_mday);
	out->writeUint32BE(td.tm_mon);
	out->writeUint32BE(td.tm_wday);
	out->writeUint32BE(td.tm_year);
	out->writeUint32BE(g_ai5->getTotalPlayTime() / 1000);
	out->write(sf.payload.data(), sf.payload.size());
	out->finalize();
	if (out->err())
		warning("Failed to write save file \"%s\"", saveName);
}

bool savedataReadHeader(const char *saveName, SaveHeader &hdr) {
	Common::ScopedPtr<Common::InSaveFile> in(g_system->getSavefileManager()->openForLoading(saveFileName(saveName)));
	if (!in)
		return false;
	if (!readHeader(in.get(), hdr, true)) {
		hdr.desc = defaultDesc(saveName);
		hdr.td = TimeDate();
		hdr.playTimeSec = 0;
	}
	return true;
}

const char *savedataSlotName(uint slot) {
	static char buf[8];
	if (g_game && g_game->id == GID_SHUUSAKU)
		snprintf(buf, sizeof(buf), "FLAG%u", slot);
	else
		snprintf(buf, sizeof(buf), "FLAG%02u", slot);
	return buf;
}

void savedataSaveFull(const char *saveName, const Common::String &desc) {
	SaveFile sf;
	loadSaveFile(saveName, sf);
	sf.payload.resize(g_game->mem16Size);
	memcpy(sf.payload.data(), memRaw(), g_game->mem16Size);
	sf.hdr.desc = desc;
	storeSaveFile(saveName, sf);
}

void savedataLoadFull(const char *saveName) {
	savedataResumeLoad(saveName);
}

void savedataRead(const char *saveName, byte *buf, uint32 off, uint32 size) {
	SaveFile sf;
	loadSaveFile(saveName, sf);
	if (off + size > sf.payload.size()) {
		warning("Save file \"%s\" is too short", saveName);
		return;
	}
	memcpy(buf + off, sf.payload.data() + off, size);
}

void savedataWrite(const char *saveName, const byte *buf, uint32 off, uint32 size) {
	SaveFile sf;
	loadSaveFile(saveName, sf);
	if (off + size > sf.payload.size())
		sf.payload.resize(off + size);
	memcpy(sf.payload.data() + off, buf + off, size);
	storeSaveFile(saveName, sf);
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
	SaveFile sf;
	loadSaveFile(saveName, sf);
	const uint32 off = var4Off();
	if (off + g_game->var4Size > sf.payload.size())
		sf.payload.resize(off + g_game->var4Size);
	for (uint i = 0; i < g_game->var4Size; i++)
		sf.payload[off + i] |= g_memPtr.var4[i];
	storeSaveFile(saveName, sf);
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
