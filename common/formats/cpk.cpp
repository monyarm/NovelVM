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
 */

#include "common/formats/cpk.h"

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/formats/utf_table.h"
#include "common/memstream.h"
#include "common/textconsole.h"

namespace Common {

namespace {

const uint32 kChunkHeaderSize = 0x10;
const uint32 kCrilaylaHeaderSize = 0x10;
const uint32 kCrilaylaPrefixSize = 0x100;
// Bounds the output allocation; real compressed members stay below 4 MiB.
const uint64 kMaxExtractSize = 512 * 1024 * 1024;

bool hasUTFMagic(const byte *buf, uint64 size) {
	return size >= 4 && memcmp(buf, "@UTF", 4) == 0;
}

/**
 * Reads the chunk at chunkOffset (16-byte header + @UTF packet) and parses
 * its table. Every failure is reported as "table unreadable".
 */
bool readChunkTable(Common::File &f, uint64 fileSize, uint64 chunkOffset, const char *tag,
		bool requireTag, const Common::String &archive, UTFTable &table) {
	if (chunkOffset > fileSize || fileSize - chunkOffset < kChunkHeaderSize) {
		warning("CPK: '%s' chunk of %s lies outside the file", tag, archive.c_str());
		return false;
	}

	byte hdr[kChunkHeaderSize];
	if (!f.seek((int64)chunkOffset) || f.read(hdr, kChunkHeaderSize) != kChunkHeaderSize) {
		warning("CPK: cannot read '%s' chunk header of %s", tag, archive.c_str());
		return false;
	}

	// Only the archive header's tag is authoritative; other chunk tags vary
	// (e.g. TOC/ETOC) and a mismatch alone is not worth losing the table over.
	if (memcmp(hdr, tag, 4) != 0) {
		if (requireTag) {
			warning("CPK: %s is not a CPK archive", archive.c_str());
			return false;
		}
		warning("CPK: unexpected tag on '%s' chunk of %s", tag, archive.c_str());
	}

	const uint64 length = READ_LE_UINT64(hdr + 8);
	const uint64 avail = fileSize - chunkOffset - kChunkHeaderSize;
	if (length == 0 || length > avail || length > 0xFFFFFFFFULL) {
		warning("CPK: bad '%s' packet length in %s", tag, archive.c_str());
		return false;
	}

	Common::Array<byte> packet((uint32)length);
	if (f.read(packet.data(), (uint32)length) != (uint32)length) {
		warning("CPK: short read of '%s' packet in %s", tag, archive.c_str());
		return false;
	}

	// TODO: The mask scheme comes from another tool's description; no real
	// masked archive has been seen to verify it.
	if (UTFTable::isMasked(packet.data(), (uint32)length))
		UTFTable::toggleMask(packet.data(), (uint32)length);

	if (!hasUTFMagic(packet.data(), length)) {
		warning("CPK: '%s' table of %s is not an @UTF table", tag, archive.c_str());
		return false;
	}

	if (!table.parse(packet.data(), (uint32)length)) {
		warning("CPK: cannot parse '%s' table of %s", tag, archive.c_str());
		return false;
	}
	return true;
}

/**
 * CRILAYLA bit stream reader. The stream is consumed from its last byte
 * towards its first, and each byte from its most significant bit down.
 */
class CrilaylaBitReader {
public:
	CrilaylaBitReader(const byte *data, uint32 end, uint32 floor) :
		_data(data), _next(end), _floor(floor), _cur(0), _bitsLeft(0), _failed(false) {}

	uint32 read(int width) {
		uint32 value = 0;
		for (int i = 0; i < width; i++) {
			if (_bitsLeft == 0) {
				if (_next <= _floor) {
					_failed = true;
					return 0;
				}
				_cur = _data[--_next];
				_bitsLeft = 8;
			}
			_bitsLeft--;
			value = (value << 1) | ((_cur >> _bitsLeft) & 1);
		}
		return value;
	}

	bool failed() const { return _failed; }

private:
	const byte *_data;
	uint32 _next;   // index of the byte after the next one to consume
	uint32 _floor;  // lowest index the stream may consume
	byte _cur;
	int _bitsLeft;
	bool _failed;
};

/**
 * Decompresses a CRILAYLA member of srcSize stored bytes into dst, which
 * must hold extractSize bytes. Returns false on any malformed input.
 */
bool decompressCrilayla(const byte *src, uint32 srcSize, byte *dst, uint32 extractSize) {
	if (srcSize < kCrilaylaHeaderSize || memcmp(src, "CRILAYLA", 8) != 0)
		return false;

	const uint32 u = READ_LE_UINT32(src + 8);
	const uint32 c = READ_LE_UINT32(src + 12);
	// Trailing bytes after the raw prefix are tolerated.
	if ((uint64)kCrilaylaHeaderSize + c + kCrilaylaPrefixSize > srcSize)
		return false;
	// The header must agree with the table so the allocation stays bounded by it.
	if ((uint64)kCrilaylaPrefixSize + u != extractSize)
		return false;

	// The first 0x100 output bytes are stored raw after the bit stream.
	memcpy(dst, src + kCrilaylaHeaderSize + c, kCrilaylaPrefixSize);

	// The rest of the output is produced back to front.
	CrilaylaBitReader bits(src, kCrilaylaHeaderSize + c, kCrilaylaHeaderSize);
	const int64 end = (int64)extractSize;
	int64 pos = end - 1;
	while (pos >= (int64)kCrilaylaPrefixSize) {
		const uint32 flag = bits.read(1);
		if (bits.failed())
			return false;

		if (flag == 0) {
			const uint32 literal = bits.read(8);
			if (bits.failed())
				return false;
			dst[pos--] = (byte)literal;
			continue;
		}

		const int64 distance = (int64)bits.read(13) + 3;
		if (bits.failed())
			return false;

		// Length grows through 2-, 3- and 5-bit fields, then 8-bit fields, each
		// only continuing while the previous one was all ones. Stop as soon as
		// the run cannot fit, so a hostile chain of 0xFF fields ends early.
		const int64 remaining = pos - (int64)kCrilaylaPrefixSize + 1;
		int64 length = 3;
		static const int kWidths[] = { 2, 3, 5 };
		bool more = true;
		for (int i = 0; i < 3 && more; i++) {
			const uint32 v = bits.read(kWidths[i]);
			if (bits.failed())
				return false;
			length += v;
			more = (v == (1u << kWidths[i]) - 1);
		}
		while (more && length <= remaining) {
			const uint32 v = bits.read(8);
			if (bits.failed())
				return false;
			length += v;
			more = (v == 0xFF);
		}
		if (length > remaining)
			return false;

		int64 from = pos + distance;
		if (from >= end)
			return false;

		// Byte by byte on purpose: runs may overlap their own output.
		for (int64 i = 0; i < length; i++)
			dst[pos--] = dst[from--];
	}
	return true;
}

struct ItocRow {
	uint64 id;
	uint64 fileSize;
	uint64 extractSize;
	uint32 seq; // collection order, so the sort keeps the first duplicate first
};

bool itocRowLess(const ItocRow &a, const ItocRow &b) {
	if (a.id != b.id)
		return a.id < b.id;
	return a.seq < b.seq;
}

void collectItocRows(const UTFTable &info, const char *column, const Common::String &archive, Common::Array<ItocRow> &rows) {
	const UTFValue v = info.get(0, column);
	if (!v.valid || v.dataLength == 0)
		return;

	if ((uint64)v.dataOffset + v.dataLength > info.dataSegmentSize()) {
		warning("CPK: ITOC %s table of %s lies outside its data segment", column, archive.c_str());
		return;
	}

	const byte *buf = info.dataSegment() + v.dataOffset;
	UTFTable sub;
	if (!hasUTFMagic(buf, v.dataLength) || !sub.parse(buf, v.dataLength)) {
		warning("CPK: cannot parse ITOC %s table of %s", column, archive.c_str());
		return;
	}

	for (int r = 0; r < sub.numRows(); r++) {
		ItocRow row;
		row.id = sub.get(r, "ID").asUint();
		row.fileSize = sub.get(r, "FileSize").asUint();
		row.extractSize = sub.get(r, "ExtractSize").asUint();
		row.seq = rows.size();
		rows.push_back(row);
	}
}

} // namespace

CPK::CPK(const Common::String &filename) : _filename(filename), _fileSize(0) {
	load();
}

CPK::~CPK() {
}

void CPK::load() {
	Common::File f;
	if (!f.open(Common::Path(_filename))) {
		warning("CPK: could not open %s", _filename.c_str());
		return;
	}

	const int64 size = f.size();
	if (size < (int64)kChunkHeaderSize) {
		warning("CPK: %s is too small to be a CPK archive", _filename.c_str());
		return;
	}
	_fileSize = (uint64)size;

	UTFTable header;
	if (!readChunkTable(f, _fileSize, 0, "CPK ", true, _filename, header))
		return;
	if (header.numRows() < 1) {
		warning("CPK: empty header table in %s", _filename.c_str());
		return;
	}

	// Files is only the declared count; the table row counts are what matter.
	const uint64 tocOffset = header.get(0, "TocOffset").asUint();
	const uint64 itocOffset = header.get(0, "ItocOffset").asUint();
	const uint64 contentOffset = header.get(0, "ContentOffset").asUint();
	const uint64 align = header.get(0, "Align").asUint();

	if (tocOffset != 0)
		loadToc(f, tocOffset);
	// TODO: Persona 2 PSP pairs its TOC with a CpkExtendId ITOC (ID -> TOC row) that
	// loadItoc() does not read, so its duplicate-named TOC rows past the first (two
	// card.bin) cannot be reached; map those IDs for getEntryById().
	if (itocOffset != 0)
		loadItoc(f, itocOffset, contentOffset, align);

	debug(1, "CPK: %s: %u members", _filename.c_str(), _entries.size());
}

void CPK::loadToc(Common::File &f, uint64 tocOffset) {
	UTFTable toc;
	if (!readChunkTable(f, _fileSize, tocOffset, "TOC ", false, _filename, toc))
		return;

	for (int r = 0; r < toc.numRows(); r++) {
		const Common::String dir = toc.get(r, "DirName").asString();
		const Common::String file = toc.get(r, "FileName").asString();
		Common::String joined = dir.empty() ? file : dir + "/" + file;

		Common::String path;
		for (uint i = 0; i < joined.size(); i++) {
			const char ch = (joined[i] == '\\') ? '/' : joined[i];
			if (ch == '/' && path.empty())
				continue; // strip leading separators
			path += ch;
		}
		if (path.empty()) {
			warning("CPK: TOC row %d of %s has no name, skipping", r, _filename.c_str());
			continue;
		}

		const uint64 relOffset = toc.get(r, "FileOffset").asUint();
		const uint64 fileSize = toc.get(r, "FileSize").asUint();
		const uint64 extractSize = toc.get(r, "ExtractSize").asUint();
		// FileOffset counts from the TOC chunk, not from ContentOffset.
		if (relOffset > ~(uint64)0 - tocOffset || fileSize > 0xFFFFFFFFULL || extractSize > 0xFFFFFFFFULL) {
			warning("CPK: TOC entry '%s' of %s has an invalid offset or size, skipping", path.c_str(), _filename.c_str());
			continue;
		}

		CPKEntry entry;
		entry.name = path;
		entry.offset = tocOffset + relOffset;
		entry.size = (uint32)fileSize;
		entry.extractSize = (uint32)extractSize;
		addEntry(entry, false);
	}
}

void CPK::loadItoc(Common::File &f, uint64 itocOffset, uint64 contentOffset, uint64 align) {
	// TODO: The ITOC layout follows other readers and a synthetic test only;
	// it has not been checked against a real ITOC archive.
	UTFTable info;
	if (!readChunkTable(f, _fileSize, itocOffset, "ITOC", false, _filename, info))
		return;
	if (info.numRows() < 1) {
		warning("CPK: empty ITOC table in %s", _filename.c_str());
		return;
	}

	// Small and large members are interleaved by ID, so pool both before sorting.
	Common::Array<ItocRow> rows;
	collectItocRows(info, "DataL", _filename, rows);
	collectItocRows(info, "DataH", _filename, rows);
	Common::sort(rows.begin(), rows.end(), itocRowLess);

	// Members sit back to back from ContentOffset in ascending-ID order, each
	// start rounded up to Align.
	uint64 offset = contentOffset;
	for (uint i = 0; i < rows.size(); i++) {
		const ItocRow &row = rows[i];
		// TODO: Unclear what real archives do with duplicate IDs; keep the first.
		if (i > 0 && row.id == rows[i - 1].id) {
			warning("CPK: duplicate ITOC ID %llu in %s, skipping", (unsigned long long)row.id, _filename.c_str());
			continue;
		}
		if (offset > _fileSize || row.fileSize > 0xFFFFFFFFULL) {
			warning("CPK: ITOC of %s runs past the file, dropping %u entries", _filename.c_str(), rows.size() - i);
			break;
		}

		if (row.id > 0xFFFFFFFFULL || row.extractSize > 0xFFFFFFFFULL) {
			warning("CPK: ITOC entry %llu of %s has an invalid ID or size, skipping", (unsigned long long)row.id, _filename.c_str());
		} else {
			CPKEntry entry;
			entry.name = Common::String::format("%u", (uint32)row.id);
			entry.offset = offset;
			entry.size = (uint32)row.fileSize;
			entry.extractSize = (uint32)row.extractSize;
			entry.id = (uint32)row.id;
			entry.hasId = true;
			addEntry(entry, true);
		}

		// offset <= _fileSize and fileSize < 2^32, so neither step can wrap
		// unless Align itself is absurd; that case ends the loop next round.
		offset += row.fileSize;
		if (align > 0 && offset % align != 0) {
			const uint64 pad = align - offset % align;
			if (pad > ~(uint64)0 - offset)
				offset = ~(uint64)0;
			else
				offset += pad;
		}
	}
}

bool CPK::addEntry(const CPKEntry &entry, bool isItoc) {
	if (entry.offset > _fileSize || entry.size > _fileSize - entry.offset) {
		warning("CPK: entry '%s' of %s lies outside the file, skipping", entry.name.c_str(), _filename.c_str());
		return false;
	}

	const bool nameTaken = _byName.contains(entry.name);
	if (nameTaken && !isItoc) {
		warning("CPK: duplicate TOC path '%s' in %s, keeping the first", entry.name.c_str(), _filename.c_str());
		return false;
	}

	const uint32 idx = _entries.size();
	_entries.push_back(entry);
	// A TOC member keeps its name over an ITOC ID name; the ITOC entry is
	// then only reachable by ID.
	if (!nameTaken) {
		_byName[entry.name] = idx;
		_named.push_back(idx);
	}
	if (isItoc)
		_byId[entry.id] = idx;
	return true;
}

bool CPK::hasFile(const Common::Path &name) const {
	return _byName.contains(name.toString('/'));
}

int CPK::listMembers(Common::ArchiveMemberList &list) const {
	for (uint i = 0; i < _named.size(); i++)
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(Common::Path(_entries[_named[i]].name), *this)));
	return _named.size();
}

const Common::ArchiveMemberPtr CPK::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *CPK::createReadStreamForMember(const Common::Path &name) const {
	NameMap::const_iterator it = _byName.find(name.toString('/'));
	if (it == _byName.end())
		return nullptr;
	const CPKEntry &entry = _entries[it->_value];

	const bool compressed = entry.extractSize != entry.size;
	if (compressed && (entry.extractSize > kMaxExtractSize || entry.extractSize < kCrilaylaPrefixSize)) {
		warning("CPK: implausible extracted size for '%s' in %s", entry.name.c_str(), _filename.c_str());
		return nullptr;
	}

	Common::File f;
	if (!f.open(Common::Path(_filename)) || !f.seek((int64)entry.offset)) {
		warning("CPK: cannot reopen %s", _filename.c_str());
		return nullptr;
	}

	// malloc because MemoryReadStream releases its buffer with free(); keep
	// it non-null for empty members.
	byte *stored = (byte *)malloc(entry.size ? entry.size : 1);
	if (!stored)
		return nullptr;
	if (f.read(stored, entry.size) != entry.size) {
		warning("CPK: short read of '%s' in %s", entry.name.c_str(), _filename.c_str());
		free(stored);
		return nullptr;
	}

	// Equal sizes mean stored raw; do not sniff for CRILAYLA in that case.
	if (!compressed)
		return new Common::MemoryReadStream(stored, entry.size, DisposeAfterUse::YES);

	byte *out = (byte *)malloc(entry.extractSize);
	if (!out) {
		free(stored);
		return nullptr;
	}
	const bool ok = decompressCrilayla(stored, entry.size, out, entry.extractSize);
	free(stored);
	if (!ok) {
		warning("CPK: cannot decompress '%s' in %s", entry.name.c_str(), _filename.c_str());
		free(out);
		return nullptr;
	}
	return new Common::MemoryReadStream(out, entry.extractSize, DisposeAfterUse::YES);
}

bool CPK::getEntryById(uint32 id, CPKEntry &entry) const {
	IdMap::const_iterator it = _byId.find(id);
	if (it == _byId.end())
		return false;
	entry = _entries[it->_value];
	return true;
}

CPK *makeCPKArchive(const Common::String &name) {
	return new CPK(name);
}

} // namespace Common
