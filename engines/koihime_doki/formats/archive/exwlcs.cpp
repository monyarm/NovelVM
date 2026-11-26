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
 */

#include "common/file.h"
#include "common/memstream.h"
#include "common/path.h"
#include "common/textconsole.h"

#include "koihime_doki/formats/archive/exwlcs.h"

namespace KoihimeDoki {
namespace Format {
namespace Archive {

namespace {

const uint32 kIndexHeaderSize = 4;
const uint32 kEntrySize = 76;
const uint32 kNameFieldSize = 64;
const uint32 kSniffSize = 10;

uint32 readUint32LE(const byte *data) {
	return (uint32)data[0] | ((uint32)data[1] << 8) | ((uint32)data[2] << 16) | ((uint32)data[3] << 24);
}

// The top byte of the stored count is both the key and the mask byte.
uint32 decodeCount(uint32 stored) {
	const byte key = (stored >> 24) & 0xFF;
	const uint32 mask = key * 0x01010101u;

	return stored ^ mask;
}

// Signature checks run on the stored (undecoded) payload bytes and must not
// look past the bytes actually read.
const char *detectExtension(const byte *data, uint32 size) {
	if (size >= 2 && data[0] == 0x42 && data[1] == 0x4D)
		return ".bmp";

	if (size >= 4 && data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47)
		return ".png";

	if (size >= 10 && data[6] == 0x4A && data[7] == 0x46 && data[8] == 0x49 && data[9] == 0x46)
		return ".jpg";

	return ".bin";
}

} // namespace

EXWLCSArchive::EXWLCSArchive(const Common::String &archiveBaseName) : _baseName(archiveBaseName) {
	readIndex();
}

EXWLCSArchive::~EXWLCSArchive() {
}

void EXWLCSArchive::readIndex() {
	if (_baseName.empty()) {
		warning("EXWLCSArchive: empty archive base name");
		return;
	}

	Common::File body;
	if (!body.open(Common::Path(_baseName))) {
		warning("EXWLCSArchive: could not open body file '%s'", _baseName.c_str());
		return;
	}

	Common::File index;
	if (!index.open(Common::Path(_baseName + ".lst"))) {
		warning("EXWLCSArchive: could not open index file '%s.lst'", _baseName.c_str());
		return;
	}

	if (index.size() < (int64)kIndexHeaderSize) {
		warning("EXWLCSArchive: index file '%s.lst' is too short", _baseName.c_str());
		return;
	}

	const uint32 storedCount = index.readUint32LE();
	if (index.err()) {
		warning("EXWLCSArchive: could not read the header of '%s.lst'", _baseName.c_str());
		return;
	}

	const uint32 count = decodeCount(storedCount);
	if ((count >> 24) != 0) {
		warning("EXWLCSArchive: '%s.lst' has an invalid member count", _baseName.c_str());
		return;
	}

	// Check the declared count against the file size before allocating
	// anything for it.
	const uint64 required = (uint64)kIndexHeaderSize + (uint64)count * kEntrySize;
	if ((uint64)index.size() < required) {
		warning("EXWLCSArchive: index file '%s.lst' is truncated (%u entries declared)", _baseName.c_str(), count);
		return;
	}

	const byte key = (storedCount >> 24) & 0xFF;
	const uint32 mask = key * 0x01010101u;
	const uint64 bodySize = (uint64)body.size();

	byte record[kEntrySize];
	for (uint32 i = 0; i < count; i++) {
		if (index.read(record, kEntrySize) != kEntrySize) {
			warning("EXWLCSArchive: could not read entry %u of '%s.lst'", i, _baseName.c_str());
			_entries.clear();
			_entryMap.clear();
			return;
		}

		const uint32 offset = readUint32LE(record) ^ mask;
		const uint32 size = readUint32LE(record + 4) ^ mask;

		// Decode the whole fixed field, then stop at the first decoded NUL.
		// This keeps a name containing the key byte intact.
		byte nameField[kNameFieldSize];
		for (uint32 j = 0; j < kNameFieldSize; j++)
			nameField[j] = record[8 + j] ^ key;

		uint32 nameLength = 0;
		while (nameLength < kNameFieldSize && nameField[nameLength] != 0)
			nameLength++;

		if (nameLength == kNameFieldSize)
			warning("EXWLCSArchive: entry %u has an unterminated name", i);

		Common::String name((const char *)nameField, nameLength);

		// Reject wrapping and out-of-range ranges instead of streaming a
		// short buffer later.
		if ((uint64)offset + (uint64)size > bodySize) {
			warning("EXWLCSArchive: entry %u ('%s') lies outside the body; skipping", i, name.c_str());
			continue;
		}

		byte signature[kSniffSize] = {};
		const uint32 sniffSize = size < kSniffSize ? size : kSniffSize;
		if (sniffSize > 0) {
			if (!body.seek(offset) || body.read(signature, sniffSize) != sniffSize) {
				warning("EXWLCSArchive: could not read entry %u ('%s') from the body; skipping", i, name.c_str());
				continue;
			}
		}

		const char *extension = detectExtension(signature, sniffSize);
		name += extension;
		name.trim();

		Entry entry;
		entry.name = name;
		entry.offset = offset;
		entry.size = size;
		// Sniffing is the only discriminator: every member the sniff does not
		// recognise is XOR-decoded, which would corrupt a plaintext member of an
		// unrecognised type. This matches the reference reader and the engine
		// only exposes images from this archive today; telling encoded and plain
		// members apart would need a real archive to confirm.
		entry.encoded = Common::String(extension) == ".bin";

		// A member shorter than four bytes has no fourth byte to take the key
		// from; hand it back as stored instead of reading past the buffer.
		if (entry.encoded && size > 0 && size < 4) {
			warning("EXWLCSArchive: member '%s' is too short to decode; returning it as stored", name.c_str());
			entry.encoded = false;
		}

		if (_entryMap.contains(name)) {
			warning("EXWLCSArchive: duplicate member '%s'; using the last entry", name.c_str());
			_entries[_entryMap[name]] = entry;
		} else {
			_entryMap[name] = _entries.size();
			_entries.push_back(entry);
		}
	}
}

bool EXWLCSArchive::hasFile(const Common::Path &name) const {
	if (name.empty())
		return false;

	return _entryMap.contains(name.toString());
}

int EXWLCSArchive::listMembers(Common::ArchiveMemberList &list) const {
	int matches = 0;

	for (Common::Array<Entry>::const_iterator it = _entries.begin(); it != _entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberPtr(new Common::GenericArchiveMember(it->name, *this)));
		matches++;
	}

	return matches;
}

const Common::ArchiveMemberPtr EXWLCSArchive::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();

	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *EXWLCSArchive::createReadStreamForMember(const Common::Path &name) const {
	if (!hasFile(name))
		return nullptr;

	const Entry &entry = _entries[_entryMap[name.toString()]];

	Common::File body;
	if (!body.open(Common::Path(_baseName))) {
		warning("EXWLCSArchive: could not reopen body file '%s'", _baseName.c_str());
		return nullptr;
	}

	// Allocate at least one byte so a zero-length member still yields a valid
	// (empty) stream.
	byte *data = (byte *)malloc(entry.size > 0 ? entry.size : 1);
	if (!data) {
		warning("EXWLCSArchive: out of memory for member '%s'", entry.name.c_str());
		return nullptr;
	}

	if (entry.size > 0) {
		if (!body.seek(entry.offset) || body.read(data, entry.size) != entry.size) {
			warning("EXWLCSArchive: could not read member '%s' from the body", entry.name.c_str());
			free(data);
			return nullptr;
		}
	}

	if (entry.encoded && entry.size >= 4) {
		const byte key = data[3];
		if (key != 0) {
			for (uint32 i = 0; i < entry.size; i++)
				data[i] ^= key;
		}
	}

	return new Common::MemoryReadStream(data, entry.size, DisposeAfterUse::YES);
}

EXWLCSArchive *EXWLCSFactory(const Common::String &name) {
	return new EXWLCSArchive(name);
}

} // namespace Archive
} // namespace Format
} // namespace KoihimeDoki
