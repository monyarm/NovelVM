#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/array.h"
#include "common/file.h"
#include "common/formats/cpk.h"
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

private:
	static const byte kUTFUint16 = 0x02;
	static const byte kUTFUint32 = 0x04;
	static const byte kUTFUint64 = 0x06;
	static const byte kUTFData = 0x0B;

	struct CpkCol {
		Common::String name;
		byte type;
		Common::Array<uint64> uints;
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
			uint32 n = (cols[i].type == kUTFData) ? cols[i].data.size() : cols[i].uints.size();
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
