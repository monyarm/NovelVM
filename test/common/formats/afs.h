#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/file.h"
#include "common/formats/afs.h"

#include "../../system/null_osystem.h"

/**
 * AFS archive tests. Fixtures are SYNTHETIC, built in-test.
 */
class AFSTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// Regression: a TOC entry whose offset + size runs past the file must
	// not push the filename-table position (which follows the furthest data end) off the
	// file, losing every member name. The bad entry itself gets no data.
	void testEntryOutsideFileKeepsNames() {
		// header (8) + 2 TOC entries (16); data at 0x800; name table at 0x1000 (2 x 48 bytes)
		Common::Array<byte> bytes(0x1000 + 2 * 48, (byte)0);
		memcpy(bytes.data(), "AFS\0", 4);
		put32(bytes.data() + 4, 2);
		put32(bytes.data() + 8, 0x800);       // entry 0: valid, 16 bytes
		put32(bytes.data() + 12, 0x10);
		put32(bytes.data() + 16, 0x10000000); // entry 1: far past EOF
		put32(bytes.data() + 20, 0x10);
		for (int i = 0; i < 16; i++)
			bytes[0x800 + i] = (byte)(0xA0 + i);
		memcpy(bytes.data() + 0x1000, "first.bin", 9);
		memcpy(bytes.data() + 0x1000 + 48, "second.bin", 10);

		Common::DumpFile out;
		TS_ASSERT(out.open(Common::Path("/tmp/novelvm_afs_range_test.afs")));
		if (out.isOpen()) {
			out.write(bytes.data(), bytes.size());
			out.close();
		}
		if (!SearchMan.hasArchive("novelvm_afs_tmp"))
			SearchMan.addDirectory("novelvm_afs_tmp", Common::Path("/tmp"), 1, 1);

		Common::AFS afs("novelvm_afs_range_test.afs");
		TS_ASSERT(afs.hasFile(Common::Path("first.bin")));
		Common::SeekableReadStream *good = afs.createReadStreamForMember(Common::Path("first.bin"));
		TS_ASSERT(good != nullptr);
		if (good) {
			TS_ASSERT_EQUALS(good->size(), 16);
			TS_ASSERT_EQUALS(good->readByte(), (byte)0xA0);
			delete good;
		}
	}

private:
	static void put32(byte *p, uint32 v) {
		p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
	}
};
