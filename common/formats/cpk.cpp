#include "common/formats/cpk.h"

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/formats/utf_table.h"
#include "common/memstream.h"

namespace Common {

namespace {

const uint64 kNoOffset = 0xFFFFFFFFFFFFFFFFULL;

Common::Array<byte> decryptUTF(const Common::Array<byte> &input) {
	Common::Array<byte> result(input.size());
	uint32 m = 0x0000655f;
	const uint32 t = 0x00004115;
	for (uint32 i = 0; i < input.size(); i++) {
		result[i] = input[i] ^ (byte)(m & 0xff);
		m = m * t;
	}
	return result;
}

// Reads the little-endian "packet" framing CRI wraps every @UTF table in
// ("CPK "/"TOC "/etc. magic already consumed by the caller): a uint32
// sentinel (always 0xff for real, uncompressed CPKs seen so far), a
// uint64 packet size, then the packet bytes -- XOR-"decrypted" (see
// decryptUTF) if they don't already start with the "@UTF" magic.
bool readUTFPacket(Common::SeekableReadStream &s, UTFTable &table) {
	s.readSint32LE(); // sentinel, unused
	int64 utfSize = s.readSint64LE();
	if (utfSize <= 0 || utfSize > 0xffffffffLL || s.size() < s.pos() || utfSize > s.size() - s.pos())
		return false;

	Common::Array<byte> packet((uint32)utfSize);
	if (s.read(packet.data(), packet.size()) != packet.size())
		return false;

	if (packet.size() < 4 || memcmp(packet.data(), "@UTF", 4) != 0)
		packet = decryptUTF(packet);

	return table.parse(packet.data(), packet.size());
}

bool getNextBits(const byte *input, uint32 inputSize, int &inputOffset, byte &bitPool, int &bitsLeft, int bitCount, uint16 &outBits) {
	outBits = 0;
	int produced = 0;

	while (produced < bitCount) {
		if (bitsLeft == 0) {
			if (inputOffset < 0 || (uint32)inputOffset >= inputSize)
				return false;
			bitPool = input[inputOffset];
			bitsLeft = 8;
			inputOffset--;
		}

		int take = MIN(bitsLeft, bitCount - produced);
		outBits <<= take;
		outBits |= (uint16)((bitPool >> (bitsLeft - take)) & ((1 << take) - 1));
		bitsLeft -= take;
		produced += take;
	}

	return true;
}

// CRI's CRILAYLA compression: an LZ77-style scheme with the compressed
// stream read back-to-front. Ported from CriPakTools' LibCPK
// (DecompressCRILAYLA) and verified byte-length-exact against a real
// CRILAYLA-compressed entry from Persona 3 Portable's umd0.cpk.
//
// The real decompressed body size is self-declared in the compressed
// stream's own header (immediately after the "CRILAYLA" magic), not in the
// TOC's ExtractSize field -- real data showed ExtractSize is that body size
// plus the fixed 0x100-byte header block copied below, not the body size
// alone.
Common::Array<byte> decompressCRILAYLA(const Common::Array<byte> &input) {
	if (input.size() < 0x110)
		return Common::Array<byte>();

	// Header fields after the 8-byte "CRILAYLA" magic are little-endian.
	uint32 bodySize = (uint32)(input[8] | (input[9] << 8) | (input[10] << 16) | (input[11] << 24));
	int32 headerOffset = (int32)(input[12] | (input[13] << 8) | (input[14] << 16) | (input[15] << 24));

	if (headerOffset < 0 || (uint32)headerOffset > input.size() - 0x110)
		return Common::Array<byte>();
	if (bodySize > 0x7fffffffU - 0x100 || input.size() > 0x7fffffffU)
		return Common::Array<byte>();

	Common::Array<byte> result(bodySize + 0x100);
	memcpy(result.data(), input.data() + headerOffset + 0x10, 0x100);

	int inputOffset = (int)input.size() - 0x100 - 1;
	int outputEnd = 0x100 + (int)bodySize - 1;
	byte bitPool = 0;
	int bitsLeft = 0;
	int bytesOutput = 0;
	const int vleLens[4] = {2, 3, 5, 8};
	uint16 bits;

	while (bytesOutput < (int)bodySize) {
		if (!getNextBits(input.data(), input.size(), inputOffset, bitPool, bitsLeft, 1, bits))
			return Common::Array<byte>();
		if (bits > 0) {
			if (!getNextBits(input.data(), input.size(), inputOffset, bitPool, bitsLeft, 13, bits))
				return Common::Array<byte>();
			int backrefOffset = outputEnd - bytesOutput + bits + 3;
			uint32 backrefLen = 3;
			int vleLevel;

			for (vleLevel = 0; vleLevel < 4; vleLevel++) {
				if (!getNextBits(input.data(), input.size(), inputOffset, bitPool, bitsLeft, vleLens[vleLevel], bits))
					return Common::Array<byte>();
				uint32 thisLevel = bits;
				if (backrefLen > 0x7fffffffU - thisLevel)
					return Common::Array<byte>();
				backrefLen += thisLevel;
				if (thisLevel != (uint32)((1 << vleLens[vleLevel]) - 1))
					break;
			}
			if (vleLevel == 4) {
				uint32 thisLevel;
				do {
					if (!getNextBits(input.data(), input.size(), inputOffset, bitPool, bitsLeft, 8, bits))
						return Common::Array<byte>();
					thisLevel = bits;
					if (backrefLen > 0x7fffffffU - thisLevel)
						return Common::Array<byte>();
					backrefLen += thisLevel;
				} while (thisLevel == 255);
			}

			uint32 copyLength = MIN(backrefLen, bodySize - bytesOutput);
			if (backrefOffset < 0 || backrefOffset > outputEnd || backrefOffset < (int)copyLength - 1)
				return Common::Array<byte>();
			for (uint32 i = 0; i < copyLength; i++) {
				result[outputEnd - bytesOutput] = result[backrefOffset--];
				bytesOutput++;
			}
		} else {
			if (!getNextBits(input.data(), input.size(), inputOffset, bitPool, bitsLeft, 8, bits))
				return Common::Array<byte>();
			result[outputEnd - bytesOutput] = (byte)bits;
			bytesOutput++;
		}
	}

	return result;
}

// One ID/FileSize/ExtractSize row of a CpkItocL/CpkItocH sub-table. The
// ITOC records no explicit offset: files are packed back-to-back in ID order
// from ContentOffset, each aligned to the header's Align (see CPK::loadITOC).
struct ITOCSizeEntry {
	uint32 id = 0;
	uint32 size = 0;
	uint32 extractSize = 0;
};

bool itocSizeEntryLess(const ITOCSizeEntry &a, const ITOCSizeEntry &b) {
	return a.id < b.id;
}

// Parses a CpkItocL/CpkItocH @UTF blob. `data`/`size` come from another
// UTFTable's data segment, so the caller must keep that table alive.
bool parseITOCData(const byte *data, uint32 size, Common::Array<ITOCSizeEntry> &out) {
	UTFTable table;
	if (!table.parse(data, size))
		return false;

	for (int i = 0; i < table.numRows(); i++) {
		UTFValue id = table.get(i, "ID");
		UTFValue fileSize = table.get(i, "FileSize");
		if (!id.valid || !fileSize.valid)
			continue;

		UTFValue extractSize = table.get(i, "ExtractSize");
		ITOCSizeEntry entry;
		entry.id = (uint32)id.u;
		entry.size = (uint32)fileSize.u;
		entry.extractSize = extractSize.valid ? (uint32)extractSize.u : entry.size;
		out.push_back(entry);
	}
	return true;
}

} // namespace

CPK::CPK(const Common::String &filename) : _filename(filename) {
	load();
}

CPK::~CPK() {
}

bool CPK::load() {
	Common::File f;
	if (!f.open(Common::Path(_filename))) {
		warning("CPK::CPK(): Could not open %s", _filename.c_str());
		return false;
	}

	char magic[4];
	if (f.read(magic, 4) != 4 || memcmp(magic, "CPK ", 4) != 0) {
		warning("CPK::CPK(): '%s' is not a CPK archive", _filename.c_str());
		return false;
	}

	UTFTable header;
	if (!readUTFPacket(f, header)) {
		warning("CPK::CPK(): failed to parse header @UTF table in %s", _filename.c_str());
		return false;
	}

	uint64 tocOffset = header.get(0, "TocOffset").asUint(kNoOffset);
	uint64 itocOffset = header.get(0, "ItocOffset").asUint(kNoOffset);
	uint64 contentOffset = header.get(0, "ContentOffset").asUint(kNoOffset);
	uint16 align = (uint16)header.get(0, "Align").asUint(1);

	if (tocOffset == kNoOffset) {
		// No directory tree: fall back to the numeric-ID ITOC when present.
		if (itocOffset != kNoOffset)
			return loadITOC(f, itocOffset, contentOffset, align);

		warning("CPK::CPK(): %s has neither a TocOffset nor an ItocOffset", _filename.c_str());
		return false;
	}

	f.seek(tocOffset);
	char tocMagic[4];
	if (f.read(tocMagic, 4) != 4 || memcmp(tocMagic, "TOC ", 4) != 0) {
		warning("CPK::CPK(): bad TOC magic in %s", _filename.c_str());
		return false;
	}

	UTFTable toc;
	if (!readUTFPacket(f, toc)) {
		warning("CPK::CPK(): failed to parse TOC @UTF table in %s", _filename.c_str());
		return false;
	}

	// Real per-entry offsets in the TOC are relative to this, not to the
	// start of the file -- see CriPakTools' ReadTOC. Verified against real
	// umd0.cpk data: TocOffset=0x800, ContentOffset=0x39FC0, so
	// addOffset=min(ContentOffset, min(TocOffset,0x800))=0x800.
	uint64 fToc = tocOffset <= 0x800 ? tocOffset : 0x800;
	uint64 addOffset = contentOffset < fToc ? contentOffset : fToc;

	for (int i = 0; i < toc.numRows(); i++) {
		Common::String dirName = toc.get(i, "DirName").asString();
		Common::String fileName = toc.get(i, "FileName").asString();
		if (fileName.empty())
			continue;

		UTFValue fileSizeVal = toc.get(i, "FileSize");
		UTFValue extractSizeVal = toc.get(i, "ExtractSize");
		UTFValue offsetVal = toc.get(i, "FileOffset");

		CPKEntry entry;
		entry.name = dirName.empty() ? fileName : dirName + "/" + fileName;
		entry.offset = offsetVal.asUint(0) + addOffset;
		entry.size = (uint32)fileSizeVal.asUint(0);
		entry.extractSize = extractSizeVal.valid ? (uint32)extractSizeVal.u : entry.size;

		_entries[entry.name] = entry;
	}

	return true;
}

// TODO: verify the ITOC path below against a real ITOC-keyed CPK (P3P's CPKs are TOC-only).
// ITOC path: implemented from the documented CRI layout (CriPakTools'
// ReadITOC and vgmstream's cpk.c), but never verified against a real
// ITOC-keyed archive -- none was available in the test corpus. The layout is
// an "ITOC" magic followed by the same packet framing as TOC, wrapping a
// CpkItocInfo @UTF table: FilesL/FilesH plus DataL/DataH vldata columns, each
// of which is itself a CpkItocL/CpkItocH @UTF table of ID/FileSize/
// ExtractSize rows. Records carry no explicit offset, so files are laid out
// back-to-back in ascending ID order starting at ContentOffset, each rounded
// up to `align`.
bool CPK::loadITOC(Common::SeekableReadStream &f, uint64 itocOffset, uint64 contentOffset, uint16 align) {
	f.seek(itocOffset);
	char magic[4];
	if (f.read(magic, 4) != 4 || memcmp(magic, "ITOC", 4) != 0) {
		warning("CPK::CPK(): bad ITOC magic in %s", _filename.c_str());
		return false;
	}

	UTFTable itoc;
	if (!readUTFPacket(f, itoc)) {
		warning("CPK::CPK(): failed to parse ITOC @UTF table in %s", _filename.c_str());
		return false;
	}

	const byte *dataSegment = itoc.dataSegment();
	uint32 dataSegmentSize = itoc.dataSegmentSize();
	Common::Array<ITOCSizeEntry> sizes;
	static const char *const kSubTables[2] = { "DataL", "DataH" };
	for (int t = 0; t < 2; t++) {
		UTFValue sub = itoc.get(0, kSubTables[t]);
		if (!sub.valid)
			continue;
		if (sub.dataOffset > dataSegmentSize || sub.dataLength > dataSegmentSize - sub.dataOffset)
			continue;
		parseITOCData(dataSegment + sub.dataOffset, sub.dataLength, sizes);
	}

	if (sizes.empty()) {
		warning("CPK::CPK(): ITOC in %s has no usable entries", _filename.c_str());
		return false;
	}

	Common::sort(sizes.begin(), sizes.end(), itocSizeEntryLess);

	uint64 baseOffset = (contentOffset == kNoOffset) ? 0 : contentOffset;
	for (uint32 i = 0; i < sizes.size(); i++) {
		CPKEntry entry;
		entry.name = Common::String::format("%u", sizes[i].id);
		entry.offset = baseOffset;
		entry.size = sizes[i].size;
		entry.extractSize = sizes[i].extractSize;
		entry.id = sizes[i].id;
		entry.hasId = true;

		_entries[entry.name] = entry;
		_entriesById[entry.id] = entry;

		baseOffset += sizes[i].size;
		if (align > 1 && (baseOffset % align))
			baseOffset += align - (baseOffset % align);
	}

	return true;
}

bool CPK::getEntryById(uint32 id, CPKEntry &entry) const {
	if (!_entriesById.contains(id))
		return false;
	entry = _entriesById[id];
	return true;
}

bool CPK::hasFile(const Common::Path &name) const {
	return _entries.contains(name.toString());
}

int CPK::listMembers(Common::ArchiveMemberList &list) const {
	int matches = 0;
	for (CPKEntryMap::const_iterator it = _entries.begin(); it != _entries.end(); ++it) {
		list.push_back(Common::ArchiveMemberList::value_type(new Common::GenericArchiveMember(Common::Path(it->_value.name), *this)));
		matches++;
	}
	return matches;
}

const Common::ArchiveMemberPtr CPK::getMember(const Common::Path &name) const {
	if (!hasFile(name))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(name, *this));
}

Common::SeekableReadStream *CPK::createReadStreamForMember(const Common::Path &name) const {
	if (!_entries.contains(name.toString()))
		return nullptr;

	const CPKEntry &entry = _entries[name.toString()];

	Common::File f;
	if (!f.open(Common::Path(_filename)))
		return nullptr;
	f.seek(entry.offset);

	Common::Array<byte> raw(entry.size);
	if (f.read(raw.data(), entry.size) != entry.size)
		return nullptr;

	if (entry.extractSize != entry.size && entry.size >= 8 && memcmp(raw.data(), "CRILAYLA", 8) == 0) {
		Common::Array<byte> decompressed = decompressCRILAYLA(raw);
		if (decompressed.empty())
			return nullptr;
		byte *data = new byte[decompressed.size()];
		memcpy(data, decompressed.data(), decompressed.size());
		return new Common::MemoryReadStream(data, decompressed.size(), DisposeAfterUse::YES);
	}

	byte *data = new byte[raw.size()];
	memcpy(data, raw.data(), raw.size());
	return new Common::MemoryReadStream(data, raw.size(), DisposeAfterUse::YES);
}

CPK *makeCPKArchive(const Common::String &name) {
	return new CPK(name);
}

} // namespace Common
