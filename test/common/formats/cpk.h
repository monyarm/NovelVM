#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/array.h"
#include "common/file.h"
#include "common/formats/cpk.h"
#include "common/formats/utf_table.h"
#include "common/path.h"
#include "common/str.h"

#include "../../system/null_osystem.h"

/**
 * Synthetic fixture for the CPK ITOC path (no real archive available to
 * verify against). The file is built exactly as common/formats/cpk.cpp's
 * ITOC reader documents: a "CPK " header @UTF table with ItocOffset/
 * ContentOffset/Align, an "ITOC" @UTF CpkItocInfo table whose DataL/DataH
 * vldata columns are CpkItocL/CpkItocH tables of ID/FileSize/ExtractSize
 * rows, and back-to-back content from ContentOffset.
 */
class CPKFormatTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	void testITOCEntriesAndOffsets() {
		const uint32 kItocOffset = 0x200;
		const uint32 kContentOffset = 0x400;

		// DataL lists IDs 2 then 0 (deliberately unsorted); DataH lists ID 1.
		Common::Array<byte> dataL = buildSizeTable(2, 0x08, 0, 0x10);
		Common::Array<byte> dataH = buildSizeTable(1, 0x20, 0x20, 0x20);

		Common::Array<CpkCol> itocInfo;
		itocInfo.push_back(colData("DataL", dataL));
		itocInfo.push_back(colData("DataH", dataH));
		Common::Array<byte> itocPacket = buildUTF(itocInfo);

		Common::Array<CpkCol> header;
		header.push_back(colUint("ItocOffset", kUTFUint64, kItocOffset));
		header.push_back(colUint("ContentOffset", kUTFUint64, kContentOffset));
		header.push_back(colUint("Align", kUTFUint16, 0x10));
		header.push_back(colUint("Files", kUTFUint32, 3));
		Common::Array<byte> headerPacket = buildUTF(header);

		Common::Array<byte> file = assemble(headerPacket, itocPacket, kItocOffset, kContentOffset);

		Common::DumpFile out;
		TS_ASSERT(out.open(Common::Path("/tmp/novelvm_cpk_itoc_test.cpk")));
		if (out.isOpen()) {
			TS_ASSERT_EQUALS(out.write(file.data(), file.size()), file.size());
			out.close();
		}

		if (!SearchMan.hasArchive("novelvm_cpk_tmp"))
			SearchMan.addDirectory("novelvm_cpk_tmp", Common::Path("/tmp"), 1, 1);

		Common::CPK cpk("novelvm_cpk_itoc_test.cpk");

		Common::CPKEntry e;
		// Ascending-ID packing from ContentOffset with 16-byte alignment.
		TS_ASSERT(cpk.getEntryById(0, e));
		TS_ASSERT_EQUALS(e.offset, (uint64)kContentOffset);
		TS_ASSERT_EQUALS(e.size, 0x10u);
		TS_ASSERT_EQUALS(e.extractSize, 0x10u);
		TS_ASSERT(e.hasId);

		TS_ASSERT(cpk.getEntryById(1, e));
		TS_ASSERT_EQUALS(e.offset, (uint64)(kContentOffset + 0x10));
		TS_ASSERT_EQUALS(e.size, 0x20u);

		TS_ASSERT(cpk.getEntryById(2, e));
		TS_ASSERT_EQUALS(e.offset, (uint64)(kContentOffset + 0x30));
		TS_ASSERT_EQUALS(e.size, 0x08u);

		TS_ASSERT(!cpk.getEntryById(99, e));

		// Numeric-ID names are exposed through the normal archive API.
		TS_ASSERT(cpk.hasFile(Common::Path("0")));
		Common::SeekableReadStream *rs = cpk.createReadStreamForMember(Common::Path("0"));
		TS_ASSERT(rs != nullptr);
		if (rs) {
			TS_ASSERT_EQUALS(rs->size(), 0x10);
			byte buf[0x10];
			TS_ASSERT_EQUALS(rs->read(buf, sizeof(buf)), sizeof(buf));
			TS_ASSERT_EQUALS(buf[0], (byte)'A');
			TS_ASSERT_EQUALS(buf[0x0F], (byte)'A');
			delete rs;
		}
	}

	void testTOCNamedEntries() {
		const uint32 kTocOffset = 0x100;
		const uint32 kContentOffset = 0x800;
		const uint32 rel = kContentOffset - kTocOffset; // FileOffset counts from the TOC chunk

		Common::Array<CpkCol> toc;
		toc.push_back(colStrs("DirName", "data/a", "\\win", "DATA/A", "data", "", ""));
		toc.push_back(colStrs("FileName", "X.BIN", "b.bin", "x.bin", "far.bin", "", "top.bin"));
		toc.push_back(colUints("FileSize", kUTFUint32, 4, 2, 2, 4, 2, 2));
		toc.push_back(colUints("ExtractSize", kUTFUint32, 4, 2, 2, 4, 2, 2));
		toc.push_back(colUints("FileOffset", kUTFUint64, rel, rel + 4, rel + 6, 0x10000, rel, rel + 6));
		Common::Array<byte> tocPacket = buildUTF(toc);

		Common::Array<CpkCol> header;
		header.push_back(colUint("TocOffset", kUTFUint64, kTocOffset));
		header.push_back(colUint("ContentOffset", kUTFUint64, kContentOffset));
		header.push_back(colUint("Align", kUTFUint16, 0));
		Common::Array<byte> headerPacket = buildUTF(header);

		Common::Array<byte> content;
		appendStr(content, "abcdefgh");
		TS_ASSERT(kTocOffset + 0x10 + tocPacket.size() <= kContentOffset);
		Common::Array<byte> file = assembleWith(headerPacket, "TOC ", kTocOffset, tocPacket, kContentOffset, content);
		Common::CPK cpk(writeTemp("novelvm_cpk_toc_test.cpk", file));

		// Duplicate (by case), out-of-range and nameless rows are dropped.
		Common::ArchiveMemberList list;
		TS_ASSERT_EQUALS(cpk.listMembers(list), 3);
		Common::ArchiveMemberList::const_iterator it = list.begin();
		if (list.size() == 3) {
			TS_ASSERT_EQUALS((*it++)->getPathInArchive().toString('/'), Common::String("data/a/X.BIN"));
			TS_ASSERT_EQUALS((*it++)->getPathInArchive().toString('/'), Common::String("win/b.bin"));
			TS_ASSERT_EQUALS((*it++)->getPathInArchive().toString('/'), Common::String("top.bin"));
		}

		TS_ASSERT(cpk.hasFile(Common::Path("DATA/A/x.bin")));
		TS_ASSERT(!cpk.hasFile(Common::Path("data/far.bin")));
		TS_ASSERT(checkMember(cpk, "data/a/x.bin", "abcd", 4));
		TS_ASSERT(checkMember(cpk, "win/b.bin", "ef", 2));
		TS_ASSERT(checkMember(cpk, "top.bin", "gh", 2));

		// TOC members are not reachable by ID.
		Common::CPKEntry e;
		TS_ASSERT(!cpk.getEntryById(0, e));
	}

	void testMaskedTables() {
		const uint32 kTocOffset = 0x100;
		const uint32 kContentOffset = 0x400;
		const uint32 rel = kContentOffset - kTocOffset;

		Common::Array<CpkCol> toc;
		toc.push_back(colStrs("DirName", "d", "d", "", "", "", ""));
		toc.push_back(colStrs("FileName", "a.bin", "b.bin", "", "", "", ""));
		toc.push_back(colUints("FileSize", kUTFUint32, 2, 2, 0, 0, 0, 0));
		toc.push_back(colUints("ExtractSize", kUTFUint32, 2, 2, 0, 0, 0, 0));
		toc.push_back(colUints("FileOffset", kUTFUint64, rel, rel + 2, 0, 0, 0, 0));
		Common::Array<byte> tocPacket = buildUTF(toc);

		Common::Array<CpkCol> header;
		header.push_back(colUint("TocOffset", kUTFUint64, kTocOffset));
		header.push_back(colUint("ContentOffset", kUTFUint64, kContentOffset));
		header.push_back(colUint("Align", kUTFUint16, 0));
		Common::Array<byte> headerPacket = buildUTF(header);

		// Both chunk packets are stored masked; the reader must unmask them.
		Common::UTFTable::toggleMask(headerPacket.data(), headerPacket.size());
		Common::UTFTable::toggleMask(tocPacket.data(), tocPacket.size());
		TS_ASSERT(Common::UTFTable::isMasked(headerPacket.data(), headerPacket.size()));

		Common::Array<byte> content;
		appendStr(content, "abcd");
		Common::Array<byte> file = assembleWith(headerPacket, "TOC ", kTocOffset, tocPacket, kContentOffset, content);
		Common::CPK cpk(writeTemp("novelvm_cpk_masked_test.cpk", file));

		Common::ArchiveMemberList list;
		TS_ASSERT_EQUALS(cpk.listMembers(list), 2);
		TS_ASSERT(checkMember(cpk, "d/a.bin", "ab", 2));
		TS_ASSERT(checkMember(cpk, "d/b.bin", "cd", 2));
	}

	void testCRILAYLADecompression() {
		const uint32 kItocOffset = 0x100;
		const uint32 kContentOffset = 0x400;

		// Three literals, then a back-reference (distance 3, length 5) that
		// overlaps its own output: the 8 decoded bytes are "YXZYXZYX".
		Common::Array<byte> bits;
		pushBits(bits, 0, 1); pushBits(bits, 'X', 8);
		pushBits(bits, 0, 1); pushBits(bits, 'Y', 8);
		pushBits(bits, 0, 1); pushBits(bits, 'Z', 8);
		pushBits(bits, 1, 1); pushBits(bits, 0, 13); pushBits(bits, 2, 2);
		Common::Array<byte> good = buildCrilayla(8, packBits(bits));

		// Same header, but the stream ends after the literals.
		Common::Array<byte> shortBits;
		for (uint32 i = 0; i < 27; i++)
			shortBits.push_back(bits[i]);
		Common::Array<byte> bad = buildCrilayla(8, packBits(shortBits));

		Common::Array<byte> content;
		appendBytes(content, good);
		appendBytes(content, bad);

		Common::Array<CpkCol> sub;
		sub.push_back(colUints("ID", kUTFUint16, 0, 1));
		sub.push_back(colUints("FileSize", kUTFUint32, good.size(), bad.size()));
		sub.push_back(colUints("ExtractSize", kUTFUint32, 0x108, 0x108));
		Common::Array<CpkCol> itocInfo;
		itocInfo.push_back(colData("DataH", buildUTF(sub)));
		Common::Array<byte> itocPacket = buildUTF(itocInfo);

		Common::Array<CpkCol> header;
		header.push_back(colUint("ItocOffset", kUTFUint64, kItocOffset));
		header.push_back(colUint("ContentOffset", kUTFUint64, kContentOffset));
		Common::Array<byte> headerPacket = buildUTF(header);

		TS_ASSERT(kItocOffset + 0x10 + itocPacket.size() <= kContentOffset);
		Common::Array<byte> file = assembleWith(headerPacket, "ITOC", kItocOffset, itocPacket, kContentOffset, content);
		Common::CPK cpk(writeTemp("novelvm_cpk_crilayla_test.cpk", file));

		Common::SeekableReadStream *rs = cpk.createReadStreamForMember(Common::Path("0"));
		TS_ASSERT(rs != nullptr);
		if (rs) {
			TS_ASSERT_EQUALS(rs->size(), 0x108);
			byte buf[0x108];
			TS_ASSERT_EQUALS(rs->read(buf, sizeof(buf)), sizeof(buf));
			bool prefixOk = true;
			for (uint32 i = 0; i < 0x100; i++)
				prefixOk = prefixOk && buf[i] == (byte)(i ^ 0x5A);
			TS_ASSERT(prefixOk);
			TS_ASSERT_EQUALS(memcmp(buf + 0x100, "YXZYXZYX", 8), 0);
			delete rs;
		}

		TS_ASSERT(cpk.createReadStreamForMember(Common::Path("1")) == nullptr);
	}

private:
	static const byte kUTFUint16 = 0x02;
	static const byte kUTFUint32 = 0x04;
	static const byte kUTFUint64 = 0x06;
	static const byte kUTFString = 0x0A;
	static const byte kUTFData = 0x0B;

	struct CpkCol {
		Common::String name;
		byte type;
		Common::Array<uint64> uints;
		Common::Array<Common::String> strs;
		Common::Array<Common::Array<byte> > data;
	};

	static void w16le(byte *p, uint16 v) {
		p[0] = (byte)(v & 0xFF);
		p[1] = (byte)((v >> 8) & 0xFF);
	}

	static void w32le(byte *p, uint32 v) {
		p[0] = (byte)(v & 0xFF);
		p[1] = (byte)((v >> 8) & 0xFF);
		p[2] = (byte)((v >> 16) & 0xFF);
		p[3] = (byte)(v >> 24);
	}

	static void w16be(byte *p, uint16 v) {
		p[0] = (byte)(v >> 8);
		p[1] = (byte)(v & 0xFF);
	}

	static void w32be(byte *p, uint32 v) {
		p[0] = (byte)(v >> 24);
		p[1] = (byte)((v >> 16) & 0xFF);
		p[2] = (byte)((v >> 8) & 0xFF);
		p[3] = (byte)(v & 0xFF);
	}

	static void w64be(byte *p, uint64 v) {
		for (int i = 0; i < 8; i++)
			p[i] = (byte)((v >> (56 - i * 8)) & 0xFF);
	}

	static uint32 typeSize(byte type) {
		switch (type) {
		case kUTFUint16: return 2;
		case kUTFUint32: return 4;
		case kUTFUint64: return 8;
		case kUTFString: return 4;
		case kUTFData: return 8;
		default: return 0;
		}
	}

	static CpkCol colUint(const char *name, byte type, uint64 value) {
		CpkCol c;
		c.name = name;
		c.type = type;
		c.uints.push_back(value);
		return c;
	}

	static CpkCol colUints(const char *name, byte type, uint64 a, uint64 b) {
		CpkCol c;
		c.name = name;
		c.type = type;
		c.uints.push_back(a);
		c.uints.push_back(b);
		return c;
	}

	static CpkCol colUints(const char *name, byte type, uint64 a, uint64 b, uint64 c2, uint64 d, uint64 e, uint64 f) {
		CpkCol c = colUints(name, type, a, b);
		c.uints.push_back(c2);
		c.uints.push_back(d);
		c.uints.push_back(e);
		c.uints.push_back(f);
		return c;
	}

	static CpkCol colStrs(const char *name, const char *a, const char *b, const char *c2, const char *d, const char *e, const char *f) {
		CpkCol c;
		c.name = name;
		c.type = kUTFString;
		c.strs.push_back(a);
		c.strs.push_back(b);
		c.strs.push_back(c2);
		c.strs.push_back(d);
		c.strs.push_back(e);
		c.strs.push_back(f);
		return c;
	}

	static void appendStr(Common::Array<byte> &out, const char *s) {
		for (; *s; s++)
			out.push_back((byte)*s);
	}

	static void appendBytes(Common::Array<byte> &out, const Common::Array<byte> &in) {
		for (uint32 i = 0; i < in.size(); i++)
			out.push_back(in[i]);
	}

	static void pushBits(Common::Array<byte> &bits, uint32 value, int width) {
		for (int i = width - 1; i >= 0; i--)
			bits.push_back((byte)((value >> i) & 1));
	}

	// CRILAYLA consumes its stream from the last byte backwards, MSB first:
	// pack in consumption order, then reverse the byte order.
	static Common::Array<byte> packBits(const Common::Array<byte> &bits) {
		const uint32 n = (bits.size() + 7) / 8;
		Common::Array<byte> fwd(n, (byte)0);
		for (uint32 i = 0; i < bits.size(); i++)
			if (bits[i])
				fwd[i / 8] |= (byte)(0x80 >> (i % 8));
		Common::Array<byte> out(n, (byte)0);
		for (uint32 i = 0; i < n; i++)
			out[n - 1 - i] = fwd[i];
		return out;
	}

	// "CRILAYLA", U, C, the stream, then the 0x100-byte raw prefix.
	static Common::Array<byte> buildCrilayla(uint32 u, const Common::Array<byte> &stream) {
		Common::Array<byte> out(0x10, (byte)0);
		memcpy(out.data(), "CRILAYLA", 8);
		w32le(out.data() + 8, u);
		w32le(out.data() + 12, stream.size());
		appendBytes(out, stream);
		for (uint32 i = 0; i < 0x100; i++)
			out.push_back((byte)(i ^ 0x5A));
		return out;
	}

	static Common::Array<byte> assembleWith(const Common::Array<byte> &headerPacket, const char *tag,
			uint32 tableOffset, const Common::Array<byte> &tablePacket, uint32 contentOffset,
			const Common::Array<byte> &content) {
		Common::Array<byte> out(contentOffset + content.size(), (byte)0);
		memcpy(out.data(), "CPK ", 4);
		writePacket(out, 0x00, headerPacket);
		memcpy(out.data() + tableOffset, tag, 4);
		writePacket(out, tableOffset, tablePacket);
		if (!content.empty())
			memcpy(out.data() + contentOffset, content.data(), content.size());
		return out;
	}

	static Common::String writeTemp(const char *name, const Common::Array<byte> &file) {
		Common::DumpFile out;
		if (out.open(Common::Path("/tmp").appendComponent(name))) {
			out.write(file.data(), file.size());
			out.close();
		}
		// Re-add the directory so its cached listing includes the file just written.
		SearchMan.remove("novelvm_cpk_tmp");
		SearchMan.addDirectory("novelvm_cpk_tmp", Common::Path("/tmp"), 1, 1);
		return name;
	}

	static bool checkMember(const Common::CPK &cpk, const char *path, const char *expect, uint32 len) {
		Common::SeekableReadStream *rs = cpk.createReadStreamForMember(Common::Path(path));
		if (!rs)
			return false;
		byte buf[16];
		bool ok = rs->size() == (int64)len && len <= sizeof(buf) && rs->read(buf, len) == len && memcmp(buf, expect, len) == 0;
		delete rs;
		return ok;
	}

	static CpkCol colData(const char *name, const Common::Array<byte> &value) {
		CpkCol c;
		c.name = name;
		c.type = kUTFData;
		c.data.push_back(value);
		return c;
	}

	// Builds a one-row @UTF table (rows, strings and data segments), the
	// same big-endian layout common/formats/utf_table.cpp parses.
	static Common::Array<byte> buildUTF(const Common::Array<CpkCol> &cols) {
		const uint32 numCols = cols.size();
		uint32 numRows = 0;
		for (uint32 i = 0; i < numCols; i++) {
			uint32 n = (cols[i].type == kUTFData) ? cols[i].data.size()
				: (cols[i].type == kUTFString) ? cols[i].strs.size() : cols[i].uints.size();
			if (n > numRows)
				numRows = n;
		}

		Common::Array<byte> stringData;
		Common::Array<uint32> nameOff(numCols);
		for (uint32 i = 0; i < numCols; i++) {
			nameOff[i] = stringData.size();
			const Common::String &s = cols[i].name;
			for (uint32 j = 0; j < s.size(); j++)
				stringData.push_back((byte)s[j]);
			stringData.push_back(0);
		}

		Common::Array<Common::Array<uint32> > strOff(numCols);
		for (uint32 i = 0; i < numCols; i++) {
			if (cols[i].type != kUTFString)
				continue;
			for (uint32 r = 0; r < cols[i].strs.size(); r++) {
				strOff[i].push_back(stringData.size());
				const Common::String &s = cols[i].strs[r];
				for (uint32 j = 0; j < s.size(); j++)
					stringData.push_back((byte)s[j]);
				stringData.push_back(0);
			}
		}

		Common::Array<byte> data;
		Common::Array<Common::Array<uint32> > dataOff(numCols), dataLen(numCols);
		for (uint32 i = 0; i < numCols; i++) {
			if (cols[i].type != kUTFData)
				continue;
			dataOff[i].resize(numRows);
			dataLen[i].resize(numRows);
			for (uint32 r = 0; r < numRows; r++) {
				dataOff[i][r] = data.size();
				const Common::Array<byte> &b = cols[i].data[r];
				dataLen[i][r] = b.size();
				for (uint32 k = 0; k < b.size(); k++)
					data.push_back(b[k]);
			}
		}

		uint32 rowLength = 0;
		for (uint32 i = 0; i < numCols; i++)
			rowLength += typeSize(cols[i].type);

		Common::Array<byte> rows(rowLength * numRows, (byte)0);
		for (uint32 r = 0; r < numRows; r++) {
			uint32 pos = r * rowLength;
			for (uint32 i = 0; i < numCols; i++) {
				byte *p = rows.data() + pos;
				switch (cols[i].type) {
				case kUTFUint16:
					w16be(p, (uint16)cols[i].uints[r]);
					pos += 2;
					break;
				case kUTFUint32:
					w32be(p, (uint32)cols[i].uints[r]);
					pos += 4;
					break;
				case kUTFUint64:
					w64be(p, cols[i].uints[r]);
					pos += 8;
					break;
				case kUTFString:
					w32be(p, strOff[i][r]);
					pos += 4;
					break;
				case kUTFData:
					w32be(p, dataOff[i][r]);
					w32be(p + 4, dataLen[i][r]);
					pos += 8;
					break;
				default:
					break;
				}
			}
		}

		const uint32 schemaSize = 5 * numCols;
		Common::Array<byte> schema(schemaSize, (byte)0);
		for (uint32 i = 0; i < numCols; i++) {
			schema[5 * i] = (byte)(0x50 | cols[i].type);
			w32be(schema.data() + 5 * i + 1, nameOff[i]);
		}

		const uint32 rowsAbs = 32 + schemaSize;
		const uint32 stringsAbs = rowsAbs + rows.size();
		const uint32 dataAbs = stringsAbs + stringData.size();
		const uint32 total = dataAbs + data.size();

		Common::Array<byte> out(total, (byte)0);
		memcpy(out.data(), "@UTF", 4);
		w32be(out.data() + 4, total);
		w32be(out.data() + 8, rowsAbs - 8);
		w32be(out.data() + 12, stringsAbs - 8);
		w32be(out.data() + 16, dataAbs - 8);
		w32be(out.data() + 20, 0);
		w16be(out.data() + 24, (uint16)numCols);
		w16be(out.data() + 26, (uint16)rowLength);
		w32be(out.data() + 28, numRows);
		memcpy(out.data() + 32, schema.data(), schema.size());
		memcpy(out.data() + rowsAbs, rows.data(), rows.size());
		if (!stringData.empty())
			memcpy(out.data() + stringsAbs, stringData.data(), stringData.size());
		if (!data.empty())
			memcpy(out.data() + dataAbs, data.data(), data.size());
		return out;
	}

	// CpkItocL/CpkItocH: two ID/FileSize/ExtractSize rows.
	static Common::Array<byte> buildSizeTable(uint16 id0, uint32 size0, uint16 id1, uint32 size1) {
		CpkCol ids;
		ids.name = "ID";
		ids.type = kUTFUint16;
		ids.uints.push_back(id0);
		ids.uints.push_back(id1);

		CpkCol sizes;
		sizes.name = "FileSize";
		sizes.type = kUTFUint32;
		sizes.uints.push_back(size0);
		sizes.uints.push_back(size1);

		CpkCol extract;
		extract.name = "ExtractSize";
		extract.type = kUTFUint32;
		extract.uints.push_back(size0);
		extract.uints.push_back(size1);

		Common::Array<CpkCol> cols;
		cols.push_back(ids);
		cols.push_back(sizes);
		cols.push_back(extract);
		return buildUTF(cols);
	}

	static void writePacket(Common::Array<byte> &out, uint32 at, const Common::Array<byte> &packet) {
		w32le(out.data() + at + 4, 0xFF); // sentinel
		w32le(out.data() + at + 8, (uint32)packet.size());
		w32le(out.data() + at + 12, 0);
		memcpy(out.data() + at + 16, packet.data(), packet.size());
	}

	static Common::Array<byte> assemble(const Common::Array<byte> &headerPacket,
			const Common::Array<byte> &itocPacket, uint32 itocOffset, uint32 contentOffset) {
		Common::Array<byte> out(contentOffset + 0x40, (byte)0);
		memcpy(out.data(), "CPK ", 4);
		writePacket(out, 0x00, headerPacket);       // @UTF starts at 0x10
		memcpy(out.data() + itocOffset, "ITOC", 4);
		writePacket(out, itocOffset, itocPacket);

		for (uint32 i = 0; i < 0x10; i++)
			out[contentOffset + i] = 'A';
		for (uint32 i = 0; i < 0x20; i++)
			out[contentOffset + 0x10 + i] = 'B';
		for (uint32 i = 0; i < 0x08; i++)
			out[contentOffset + 0x30 + i] = 'C';
		return out;
	}
};
