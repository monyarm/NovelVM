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

#include "common/compression/lzss.h"
#include "common/endian.h"
#include "common/memstream.h"
#include "common/textconsole.h"
#include "common/util.h"

#include "ai5/archive.h"

namespace AI5 {

enum {
	kMaxSaneFiles = 100000
};

bool ArcArchive::readSizeAndCount() {
	_meta.arcSize = _file.size();
	if (!_meta.arcSize)
		return false;
	_file.seek(0);
	_meta.nrFiles = _file.readUint32LE();
	if (_meta.nrFiles > kMaxSaneFiles) {
		warning("ArcArchive: file count is not sane: %u", _meta.nrFiles);
		return false;
	}
	return !_file.err();
}

static bool getOffsetAndSizeKey(uint32 offEnc, uint32 offNextEnc, uint32 offNext2Enc,
		uint32 sizeEnc, uint32 sizeNextEnc, uint32 sizeNext2Enc,
		uint32 offGuess, uint32 arcSize, uint32 &offKey, uint32 &sizeKey) {
	uint32 key = offEnc ^ offGuess;
	uint32 offNext = offNextEnc ^ key;
	if (offNext <= offGuess)
		return false;

	uint32 sizeGuess = offNext - offGuess;
	uint32 skey = sizeEnc ^ sizeGuess;
	uint32 sizeNext = sizeNextEnc ^ skey;
	if (offGuess + sizeGuess >= arcSize || offNext + sizeNext >= arcSize)
		return false;

	uint32 offNext2 = offNext2Enc ^ key;
	uint32 sizeNext2 = sizeNext2Enc ^ skey;
	if (offNext + sizeNext != offNext2)
		return false;
	if (offNext2 + sizeNext2 >= arcSize)
		return false;

	offKey = key;
	sizeKey = skey;
	return true;
}

bool ArcArchive::readArcMeta(GameId game) {
	_meta.indexOff = 4;
	if (!readSizeAndCount())
		return false;

	if (game == GID_KAKYUUSEI) {
		_meta.entrySize = 20;
		_meta.nameLength = 12;
		return true;
	}

	// read (at least) 3 entries
	byte entry[0x318];
	if (_file.read(entry, sizeof(entry)) != sizeof(entry))
		return false;

	// Names are XOR'd with an 8-bit key and offset/size with 32-bit keys.
	// The name key is the last byte of the name field (names are
	// null-terminated). The first offset is known (end of the index), which
	// gives the offset key; the first two offsets then give the size key.
	// Some games swap the offset and size fields, so both orders are tried
	// and checked against the third entry.
	static const uint nameLengths[] = { 0xc, 0x10, 0x14, 0x1e, 0x20, 0x100 };
	for (uint i = 0; i < ARRAYSIZE(nameLengths); i++) {
		const uint len = nameLengths[i];
		byte nameKey = entry[len - 1];

		bool valid = true;
		for (uint j = 0; j < len && entry[j] != nameKey; j++) {
			if (!Common::isPrint(entry[j] ^ nameKey)) {
				valid = false;
				break;
			}
		}
		if (!valid)
			continue;

		const byte *entry2 = entry + len + 8;
		const byte *entry3 = entry + (len + 8) * 2;

		uint32 fstA = READ_LE_UINT32(entry + len);
		uint32 fstB = READ_LE_UINT32(entry + len + 4);
		uint32 sndA = READ_LE_UINT32(entry2 + len);
		uint32 sndB = READ_LE_UINT32(entry2 + len + 4);
		uint32 thdA = READ_LE_UINT32(entry3 + len);
		uint32 thdB = READ_LE_UINT32(entry3 + len + 4);
		uint32 offGuess = 4 + _meta.nrFiles * (len + 8);

		// name / size / offset
		if (getOffsetAndSizeKey(fstB, sndB, thdB, fstA, sndA, thdA, offGuess, _meta.arcSize, _meta.offsetKey, _meta.sizeKey)) {
			_meta.nameLength = len;
			_meta.nameKey = nameKey;
			_meta.offsetOff = len + 4;
			_meta.sizeOff = len;
			_meta.nameOff = 0;
			break;
		}
		// name / offset / size
		if (getOffsetAndSizeKey(fstA, sndA, thdA, fstB, sndB, thdB, offGuess, _meta.arcSize, _meta.offsetKey, _meta.sizeKey)) {
			_meta.nameLength = len;
			_meta.nameKey = nameKey;
			_meta.offsetOff = len;
			_meta.sizeOff = len + 4;
			_meta.nameOff = 0;
			break;
		}
	}
	if (!_meta.nameLength)
		return false;

	_meta.entrySize = _meta.nameLength + 8;
	return true;
}

bool ArcArchive::readDatMeta() {
	_meta.indexOff = 8;
	_meta.entrySize = 28;
	_meta.nameLength = 20;
	_meta.offsetOff = 4;
	_meta.sizeOff = 0;
	_meta.nameOff = 8;

	_meta.arcSize = _file.size();
	byte buf[0x24];
	if (!_meta.arcSize || _file.read(buf, sizeof(buf)) != sizeof(buf))
		return false;

	uint32 key = READ_LE_UINT32(buf + 4);
	_meta.nrFiles = READ_LE_UINT32(buf) ^ key;
	if (_meta.nrFiles > kMaxSaneFiles) {
		warning("ArcArchive: file count is not sane: %u", _meta.nrFiles);
		return false;
	}
	_meta.offsetKey = key;
	_meta.sizeKey = key;
	_meta.nameKey = buf[0x23];
	return true;
}

bool ArcArchive::readAwdMeta() {
	_meta.indexOff = 4;
	_meta.entrySize = 38;
	_meta.nameLength = 16;
	_meta.nameOff = 0;
	_meta.offsetOff = 18;
	_meta.sizeOff = 22;
	_meta.awdTypeOff = 16;
	_meta.loopStartOff = 26;
	_meta.loopEndOff = 30;

	// older awd format (Shuusaku CD version)
	_file.seek(22);
	if (_file.readUint16LE() == 0) {
		_meta.offsetOff += 2;
		_meta.sizeOff += 2;
		_meta.loopStartOff += 2;
		_meta.loopEndOff += 2;
		_meta.entrySize += 2;
	}
	return readSizeAndCount();
}

bool ArcArchive::readAwfMeta() {
	_meta.indexOff = 4;
	_meta.entrySize = 52;
	_meta.nameLength = 32;
	_meta.nameOff = 0;
	_meta.offsetOff = 32;
	_meta.sizeOff = 36;
	_meta.loopStartOff = 40;
	_meta.loopEndOff = 44;
	return readSizeAndCount();
}

bool ArcArchive::readTypicalEntry(byte *buf, Entry &e) {
	byte *name = buf + _meta.nameOff;
	uint nameLen;
	for (nameLen = 0; nameLen < _meta.nameLength; nameLen++) {
		name[nameLen] ^= _meta.nameKey;
		if (name[nameLen] == 0)
			break;
	}

	e.offset = READ_LE_UINT32(buf + _meta.offsetOff) ^ _meta.offsetKey;
	e.size = READ_LE_UINT32(buf + _meta.sizeOff) ^ _meta.sizeKey;
	// name is not necessarily null-terminated (happens in Kawa95)
	e.name = Common::String((const char *)name, nameLen);

	if (e.offset + e.size > _meta.arcSize) {
		warning("ArcArchive: %s @ %x + %x extends beyond eof (%x)", e.name.c_str(), e.offset, e.size, _meta.arcSize);
		return false;
	}

	if (_type == kTypeAwd) {
		e.awdType = READ_LE_UINT16(buf + _meta.awdTypeOff);
		e.loopStart = READ_LE_UINT32(buf + _meta.loopStartOff);
		e.loopEnd = READ_LE_UINT32(buf + _meta.loopEndOff);
	} else if (_type == kTypeAwf) {
		e.awdType = kAwdPcm;
		e.loopStart = READ_LE_UINT32(buf + _meta.loopStartOff);
		e.loopEnd = READ_LE_UINT32(buf + _meta.loopEndOff);
	}
	return true;
}

bool ArcArchive::readKakyuuseiIndex() {
	// encrypted bytes of index are out of order
	static const byte shuffle[20] = {
		17, 2, 8, 19, 0, 5, 10, 13, 1, 15, 6, 4, 11, 16, 3, 9, 18, 12, 7, 14
	};

	byte key = _meta.nrFiles;
	byte buf[20], dec[20];
	_entries.resize(_meta.nrFiles);
	for (uint i = 0; i < _meta.nrFiles; i++) {
		if (_file.read(buf, 20) != 20)
			return false;
		for (int j = 0; j < 20; j++) {
			dec[shuffle[j]] = buf[j] ^ key;
			key = (key * 3 + 1) & 0xff;
		}
		Entry &e = _entries[i];
		e = Entry();
		e.offset = READ_LE_UINT32(dec + 16);
		e.size = READ_LE_UINT32(dec + 12);
		dec[12] = 0;
		e.name = Common::String((const char *)dec);
	}
	return true;
}

bool ArcArchive::readIndex(GameId game) {
	_file.seek(_meta.indexOff);

	if (_type == kTypeArc && game == GID_KAKYUUSEI)
		return readKakyuuseiIndex();

	Common::Array<byte> buf(_meta.nrFiles * _meta.entrySize);
	if (_file.read(buf.data(), buf.size()) != buf.size())
		return false;

	// The original rejects the whole archive on a bad entry; only that
	// entry is skipped here.
	for (uint i = 0; i < _meta.nrFiles; i++) {
		Entry e = Entry();
		if (readTypicalEntry(buf.data() + i * _meta.entrySize, e))
			_entries.push_back(e);
		else
			warning("ArcArchive: skipping bad entry %u", i);
	}
	return true;
}

void ArcArchive::createIndex() {
	for (uint i = 0; i < _entries.size(); i++) {
		if (_index.contains(_entries[i].name)) {
			warning("ArcArchive: skipping duplicate file name %s", _entries[i].name.c_str());
			continue;
		}
		_index[_entries[i].name] = i;
	}
}

ArcArchive *ArcArchive::open(const Common::Path &path, GameId game, uint flags) {
	Common::ScopedPtr<ArcArchive> arc(new ArcArchive());
	if (!arc->_file.open(path))
		return nullptr;
	arc->_flags = flags;

	Common::String name = path.baseName();
	bool ok;
	if (name.hasSuffixIgnoreCase(".dat")) {
		arc->_type = kTypeDat;
		ok = arc->readDatMeta();
	} else if (name.hasSuffixIgnoreCase(".awd")) {
		arc->_type = kTypeAwd;
		ok = arc->readAwdMeta();
	} else if (name.hasSuffixIgnoreCase(".awf")) {
		arc->_type = kTypeAwf;
		ok = arc->readAwfMeta();
	} else {
		arc->_type = kTypeArc;
		ok = arc->readArcMeta(game);
	}
	if (!ok) {
		warning("ArcArchive: failed to read metadata of %s", name.c_str());
		return nullptr;
	}
	if (!arc->readIndex(game))
		return nullptr;
	arc->createIndex();
	return arc.release();
}

const ArcArchive::Entry *ArcArchive::findEntry(const Common::Path &path) const {
	Common::HashMap<Common::String, uint, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo>::const_iterator it = _index.find(path.toString());
	return it == _index.end() ? nullptr : &_entries[it->_value];
}

bool ArcArchive::hasFile(const Common::Path &path) const {
	return findEntry(path) != nullptr;
}

int ArcArchive::listMembers(Common::ArchiveMemberList &list) const {
	for (uint i = 0; i < _entries.size(); i++)
		list.push_back(Common::ArchiveMemberPtr(new Common::GenericArchiveMember(Common::Path(_entries[i].name), *this)));
	return _entries.size();
}

const Common::ArchiveMemberPtr ArcArchive::getMember(const Common::Path &path) const {
	if (!hasFile(path))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(path, *this));
}

Common::SeekableReadStream *ArcArchive::createReadStreamForMember(const Common::Path &path) const {
	const Entry *e = findEntry(path);
	if (!e)
		return nullptr;

	byte *raw = (byte *)malloc(e->size);
	if (!raw)
		return nullptr;
	_file.seek(e->offset);
	if (_file.read(raw, e->size) != e->size) {
		free(raw);
		return nullptr;
	}

	// AWD/AWF members are raw PCM or MP3; see Entry::awdType.
	if (!(_flags & kArcCompressed) || _type == kTypeAwd || _type == kTypeAwf)
		return new Common::MemoryReadStream(raw, e->size, DisposeAfterUse::YES);

	Common::Array<byte> out;
	Common::MemoryReadStream in(raw, e->size, DisposeAfterUse::YES);
	if (_flags & kArcBitwise)
		Common::decompressLZSSBitwise(in, out);
	else
		Common::decompressLZSS(in, out);

	byte *data = (byte *)malloc(MAX<uint32>(out.size(), 1));
	if (!data)
		return nullptr;
	memcpy(data, out.data(), out.size());
	return new Common::MemoryReadStream(data, out.size(), DisposeAfterUse::YES);
}

} // End of namespace AI5
