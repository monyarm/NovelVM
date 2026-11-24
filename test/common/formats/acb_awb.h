#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/array.h"
#include "common/file.h"
#include "common/formats/acb.h"
#include "common/formats/awb.h"
#include "common/memstream.h"
#include "common/path.h"
#include "common/str.h"

#include "../../system/null_osystem.h"

// Synthetic, game-data-free fixtures for the CRI AWB (AFS2) archive and the
// ACB cue-sheet that embeds one. The byte layouts below follow the parsers in
// common/formats/awb.cpp and common/formats/utf_table.cpp exactly: a
// little-endian AFS2 header/ID table/offset table plus 32-byte-aligned
// subfiles, and a big-endian @UTF table whose vldata columns point into the
// data segment.
namespace {

const byte kUTFUint8 = 0x00;
const byte kUTFUint16 = 0x02;
const byte kUTFUint32 = 0x04;
const byte kUTFString = 0x0A;
const byte kUTFData = 0x0B;
const byte kPerRow = 0x50;

void w16le(byte *p, uint16 v) {
	p[0] = (byte)(v & 0xFF);
	p[1] = (byte)((v >> 8) & 0xFF);
}

void w32le(byte *p, uint32 v) {
	p[0] = (byte)(v & 0xFF);
	p[1] = (byte)((v >> 8) & 0xFF);
	p[2] = (byte)((v >> 16) & 0xFF);
	p[3] = (byte)(v >> 24);
}

void w16be(byte *p, uint16 v) {
	p[0] = (byte)(v >> 8);
	p[1] = (byte)(v & 0xFF);
}

void w32be(byte *p, uint32 v) {
	p[0] = (byte)(v >> 24);
	p[1] = (byte)((v >> 16) & 0xFF);
	p[2] = (byte)((v >> 8) & 0xFF);
	p[3] = (byte)(v & 0xFF);
}

uint32 alignUp(uint32 value, uint32 align) {
	return (value + align - 1) / align * align;
}

uint32 utfTypeSize(byte type) {
	switch (type) {
	case kUTFUint8:
		return 1;
	case kUTFUint16:
		return 2;
	case kUTFUint32:
		return 4;
	case kUTFString:
		return 4;
	case kUTFData:
		return 8;
	default:
		return 0;
	}
}

// One per-row column of a synthetic @UTF table.
struct UTFColumn {
	Common::String name;
	byte type = kUTFUint8;
	Common::Array<uint64> uints;
	Common::Array<Common::String> strings;
	Common::Array<Common::Array<byte>> data;
};

UTFColumn utfUint(const Common::String &name, byte type, uint64 value) {
	UTFColumn c;
	c.name = name;
	c.type = type;
	c.uints.push_back(value);
	return c;
}

UTFColumn utfStr(const Common::String &name, const Common::String &value) {
	UTFColumn c;
	c.name = name;
	c.type = kUTFString;
	c.strings.push_back(value);
	return c;
}

UTFColumn utfData(const Common::String &name, const Common::Array<byte> &value) {
	UTFColumn c;
	c.name = name;
	c.type = kUTFData;
	c.data.push_back(value);
	return c;
}

// Build a minimal single-row @UTF table from per-row columns.
Common::Array<byte> buildUTF(const Common::Array<UTFColumn> &cols) {
	const uint32 numCols = cols.size();
	uint32 numRows = 0;
	for (uint32 i = 0; i < numCols; i++) {
		uint32 n;
		if (cols[i].type == kUTFString)
			n = cols[i].strings.size();
		else if (cols[i].type == kUTFData)
			n = cols[i].data.size();
		else
			n = cols[i].uints.size();
		if (n > numRows)
			numRows = n;
	}

	Common::Array<byte> strings;
	Common::Array<uint32> nameOff(numCols);
	for (uint32 i = 0; i < numCols; i++) {
		nameOff[i] = strings.size();
		const Common::String &s = cols[i].name;
		for (uint32 j = 0; j < s.size(); j++)
			strings.push_back((byte)s[j]);
		strings.push_back(0);
	}

	Common::Array<Common::Array<uint32>> strOff(numCols);
	for (uint32 i = 0; i < numCols; i++) {
		if (cols[i].type != kUTFString)
			continue;
		strOff[i].resize(numRows);
		for (uint32 r = 0; r < numRows; r++) {
			strOff[i][r] = strings.size();
			const Common::String &s = cols[i].strings[r];
			for (uint32 j = 0; j < s.size(); j++)
				strings.push_back((byte)s[j]);
			strings.push_back(0);
		}
	}

	Common::Array<byte> data;
	Common::Array<Common::Array<uint32>> dataOff(numCols), dataLen(numCols);
	for (uint32 i = 0; i < numCols; i++) {
		if (cols[i].type != kUTFData)
			continue;
		dataOff[i].resize(numRows);
		dataLen[i].resize(numRows);
		for (uint32 r = 0; r < numRows; r++) {
			dataOff[i][r] = data.size();
			const Common::Array<byte> &b = cols[i].data[r];
			dataLen[i][r] = b.size();
			data.push_back(b);
		}
	}

	uint32 rowLength = 0;
	for (uint32 i = 0; i < numCols; i++)
		rowLength += utfTypeSize(cols[i].type);

	Common::Array<byte> rows(rowLength * numRows, (byte)0);
	for (uint32 r = 0; r < numRows; r++) {
		uint32 pos = r * rowLength;
		for (uint32 i = 0; i < numCols; i++) {
			byte *p = rows.data() + pos;
			switch (cols[i].type) {
			case kUTFUint8:
				*p = (byte)cols[i].uints[r];
				pos += 1;
				break;
			case kUTFUint16:
				w16be(p, (uint16)cols[i].uints[r]);
				pos += 2;
				break;
			case kUTFUint32:
				w32be(p, (uint32)cols[i].uints[r]);
				pos += 4;
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
		schema[5 * i] = (byte)(kPerRow | cols[i].type);
		w32be(schema.data() + 5 * i + 1, nameOff[i]);
	}

	const uint32 rowsAbs = 32 + schemaSize;
	const uint32 stringsAbs = rowsAbs + rows.size();
	const uint32 dataAbs = stringsAbs + strings.size();
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
	if (!strings.empty())
		memcpy(out.data() + stringsAbs, strings.data(), strings.size());
	if (!data.empty())
		memcpy(out.data() + dataAbs, data.data(), data.size());
	return out;
}

// Build an AFS2 archive (4-byte offsets, 32-byte alignment). Subfiles are
// placed at 32-byte boundaries; the final subfile ends exactly at EOF so the
// offset-table's last value is the file size.
Common::Array<byte> buildAWB(const Common::Array<uint16> &ids,
                             const Common::Array<Common::Array<byte>> &subs) {
	const uint32 n = ids.size();
	const uint32 idTableOff = 16;
	const uint32 offTableOff = idTableOff + 2 * n;
	const uint32 dataStart = offTableOff + 4 * (n + 1);

	Common::Array<uint32> offsets(n + 1);
	uint32 cur = alignUp(dataStart, 32);
	for (uint32 i = 0; i < n; i++) {
		offsets[i] = cur;
		cur += subs[i].size();
		if (i + 1 < n)
			cur = alignUp(cur, 32);
	}
	offsets[n] = cur;

	Common::Array<byte> out(cur, (byte)0);
	memcpy(out.data(), "AFS2", 4);
	out[4] = 1; // version
	out[5] = 4; // offset size
	w16le(out.data() + 6, 1);
	w32le(out.data() + 8, n);
	w16le(out.data() + 12, 32);
	w16le(out.data() + 14, 0);
	for (uint32 i = 0; i < n; i++)
		w16le(out.data() + idTableOff + 2 * i, ids[i]);
	for (uint32 i = 0; i <= n; i++)
		w32le(out.data() + offTableOff + 4 * i, offsets[i]);
	for (uint32 i = 0; i < n; i++)
		memcpy(out.data() + offsets[i], subs[i].data(), subs[i].size());
	return out;
}

// 32-byte HCA-looking subfile whose tail bytes are a known pattern.
Common::Array<byte> makeSubfile(byte seed) {
	Common::Array<byte> sub(32, (byte)0);
	sub[0] = 'H';
	sub[1] = 'C';
	sub[2] = 'A';
	sub[3] = 0;
	for (uint32 i = 4; i < sub.size(); i++)
		sub[i] = (byte)(seed + i);
	return sub;
}

// An ACB "Header" table with one cue ("bgm_battle") referencing waveform id 5
// and the embedded AWB containing that id.
Common::Array<byte> buildACB(byte seed) {
	Common::Array<uint16> ids;
	ids.push_back(5);
	Common::Array<Common::Array<byte>> subs;
	subs.push_back(makeSubfile(seed));
	Common::Array<byte> awb = buildAWB(ids, subs);

	Common::Array<UTFColumn> wave;
	wave.push_back(utfUint("Id", kUTFUint16, 5));
	wave.push_back(utfUint("EncodeType", kUTFUint8, 0));
	wave.push_back(utfUint("NumChannels", kUTFUint8, 1));
	wave.push_back(utfUint("NumSamples", kUTFUint32, 1000));
	wave.push_back(utfUint("LoopFlag", kUTFUint8, 0));
	wave.push_back(utfUint("ExtensionData", kUTFUint16, 0xFFFF));

	Common::Array<UTFColumn> cue;
	cue.push_back(utfUint("ReferenceIndex", kUTFUint16, 0));

	Common::Array<UTFColumn> cueNames;
	cueNames.push_back(utfUint("CueIndex", kUTFUint16, 0));
	cueNames.push_back(utfStr("CueName", "bgm_battle"));

	Common::Array<UTFColumn> header;
	header.push_back(utfData("AwbFile", awb));
	header.push_back(utfData("WaveformTable", buildUTF(wave)));
	header.push_back(utfData("CueTable", buildUTF(cue)));
	header.push_back(utfData("CueNameTable", buildUTF(cueNames)));
	return buildUTF(header);
}

} // namespace

class AWBFormatTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// BUG 2 regression: id-based lookup returns the right subfile, and the
	// synthetic "id_NNNN.ext" member naming still resolves.
	void testCreateReadStreamForIdAndName() {
		Common::Array<uint16> ids;
		ids.push_back(7);
		Common::Array<Common::Array<byte>> subs;
		subs.push_back(makeSubfile(0x40));
		Common::Array<byte> bytes = buildAWB(ids, subs);

		Common::MemoryReadStream s(bytes.data(), bytes.size(), DisposeAfterUse::NO);
		Common::AWB awb(&s, DisposeAfterUse::NO);

		Common::SeekableReadStream *byId = awb.createReadStreamForId(7);
		TS_ASSERT(byId != nullptr);
		if (!byId)
			return;
		byte buf[32];
		TS_ASSERT_EQUALS(byId->size(), 32);
		TS_ASSERT_EQUALS(byId->read(buf, 32), 32u);
		TS_ASSERT_EQUALS(buf[0], 'H');
		TS_ASSERT_EQUALS(buf[3], (byte)0);
		TS_ASSERT_EQUALS(buf[31], (byte)(0x40 + 31));
		delete byId;

		TS_ASSERT(awb.hasFile(Common::Path("id_0007.hca")));
		Common::SeekableReadStream *byName = awb.createReadStreamForMember(Common::Path("ID_0007.HCA"));
		TS_ASSERT(byName != nullptr);
		delete byName;

		TS_ASSERT(awb.createReadStreamForId(999) == nullptr);
	}

	// BUG 3 regression: a file-backed AWB whose last subfile ends exactly at
	// EOF must size that entry to the real end, not realign past the file.
	void testFileBackedLastEntryEndsAtEof() {
		Common::Array<uint16> ids;
		ids.push_back(6);
		Common::Array<byte> sub(10, (byte)0xA5);
		sub[0] = 'H';
		sub[1] = 'C';
		sub[2] = 'A';
		sub[3] = 0;
		Common::Array<Common::Array<byte>> subs;
		subs.push_back(sub);
		Common::Array<byte> bytes = buildAWB(ids, subs);

		// dataStart=26 -> aligned 32, subfile 10 bytes -> EOF 42 (unaligned).
		TS_ASSERT_EQUALS(bytes.size(), 42u);

		Common::DumpFile out;
		TS_ASSERT(out.open(Common::Path("/tmp/opencode/novelvm_awb_eof_test.awb")));
		if (out.isOpen()) {
			TS_ASSERT_EQUALS(out.write(bytes.data(), bytes.size()), bytes.size());
			out.close();
		}

		if (!SearchMan.hasArchive("novelvm_awb_tmp"))
			SearchMan.addDirectory("novelvm_awb_tmp", Common::Path("/tmp/opencode"), 1, 1);

		Common::AWB awb("novelvm_awb_eof_test.awb");
		Common::SeekableReadStream *rs = awb.createReadStreamForId(6);
		TS_ASSERT(rs != nullptr);
		if (rs) {
			TS_ASSERT_EQUALS(rs->size(), 10);
			byte buf[10];
			TS_ASSERT_EQUALS(rs->read(buf, 10), 10u);
			TS_ASSERT_EQUALS(buf[0], 'H');
			TS_ASSERT_EQUALS(buf[9], (byte)0xA5);
			delete rs;
		}
	}
};

class ACBFormatTestSuite : public CxxTest::TestSuite {
public:
	void testCueResolvesToEmbeddedAwbSubfile() {
		Common::Array<byte> bytes = buildACB(0x40);
		Common::MemoryReadStream s(bytes.data(), bytes.size(), DisposeAfterUse::NO);
		Common::ACB acb(&s, DisposeAfterUse::NO);

		TS_ASSERT_EQUALS(acb.numCues(), 1);
		TS_ASSERT_EQUALS(acb.numWaveforms(), 1);
		TS_ASSERT(acb.hasAWB());
		TS_ASSERT(acb.hasFile(Common::Path("bgm_battle")));

		// loadFromStream() has returned, so its local @UTF header (and the
		// embedded AWB bytes it pointed at) are gone: this read is the BUG 2
		// id-resolution check and the BUG 1 use-after-free regression.
		Common::SeekableReadStream *rs = acb.createReadStreamForMember(Common::Path("bgm_battle"));
		TS_ASSERT(rs != nullptr);
		if (!rs)
			return;
		TS_ASSERT_EQUALS(rs->size(), 32);
		byte buf[32];
		TS_ASSERT_EQUALS(rs->read(buf, 32), 32u);
		TS_ASSERT_EQUALS(buf[0], 'H');
		TS_ASSERT_EQUALS(buf[3], (byte)0);
		TS_ASSERT_EQUALS(buf[31], (byte)(0x40 + 31));
		delete rs;

		TS_ASSERT(acb.getLoopInfo("bgm_battle").valid == false);
	}
};
