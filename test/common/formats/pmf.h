#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/file.h"
#include "common/formats/pmf.h"
#include "common/path.h"

#include "../../system/null_osystem.h"

#include <stdlib.h>

// forbidden.h was already included by the runner before this file.
#undef getenv

/**
 * Synthetic PSMF/PMF fixture (no game data): a single video stream with a
 * non-zero EP map, built to the offsets common/formats/pmf.cpp reads. The
 * test checks the EP map is parsed and reachable through the public
 * videoEntryPoints() accessor.
 */
class PMFFormatTestSuite : public CxxTest::TestSuite {
public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	void testVideoEntryPointsParsed() {
		const uint32 kEpMapOffset = 0x200;
		const uint32 kMpegOffset = 0x300;
		const uint32 kNumStreams = 1;

		Common::Array<byte> file(0x400, (byte)0);
		memcpy(file.data(), "PSMF", 4);
		put32be(file.data() + 0x08, kMpegOffset);
		put32be(file.data() + 0x0C, 0x80); // mpeg size
		put48be(file.data() + 0x54, 90000); // first timestamp
		put16be(file.data() + 0x80, (uint16)kNumStreams);

		// Stream table entry 0 (16 bytes) for a video stream.
		byte *st = file.data() + 0x82;
		st[0] = 0xE0; // stream id (video)
		st[1] = 0;
		st[2] = 0x20; // type flag
		st[3] = 0x75; // codec flag (AVC)
		put32be(st + 4, kEpMapOffset);
		put32be(st + 8, 2); // epMapCount
		st[12] = 30; // width = 30 * 16 = 480
		st[13] = 17; // height = 17 * 16 = 272

		// Two 10-byte EP entries: index, pic offset, PTS, stream offset.
		byte *ep = file.data() + kEpMapOffset;
		ep[0] = 0; ep[1] = 0;
		put32be(ep + 2, 1000);
		put32be(ep + 6, 0x00000008);
		ep[10] = 1; ep[11] = 1;
		put32be(ep + 12, 5000);
		put32be(ep + 16, 0x00001234);

		Common::DumpFile out;
		TS_ASSERT(out.open(Common::Path("/tmp/opencode/novelvm_pmf_ep_test.pmf")));
		if (out.isOpen()) {
			TS_ASSERT_EQUALS(out.write(file.data(), file.size()), file.size());
			out.close();
		}

		if (!SearchMan.hasArchive("novelvm_pmf_tmp"))
			SearchMan.addDirectory("novelvm_pmf_tmp", Common::Path("/tmp/opencode"), 1, 1);

		Common::PMF pmf("novelvm_pmf_ep_test.pmf");
		TS_ASSERT(pmf.isValid());
		if (!pmf.isValid())
			return;

		TS_ASSERT_EQUALS(pmf.getWidth(), 480);
		TS_ASSERT_EQUALS(pmf.getHeight(), 272);
		TS_ASSERT_EQUALS(pmf.getStreams().size(), 1u);
		TS_ASSERT(pmf.getStreams()[0].isVideo);

		const Common::Array<Common::PMFEntryPoint> &eps = pmf.videoEntryPoints();
		TS_ASSERT_EQUALS(eps.size(), 2u);
		if (eps.size() != 2)
			return;

		TS_ASSERT_EQUALS(eps[0].index, 0);
		TS_ASSERT_EQUALS(eps[0].picOffset, 0);
		TS_ASSERT_EQUALS(eps[0].pts, 1000u);
		TS_ASSERT_EQUALS(eps[0].offset, 0x00000008u);

		TS_ASSERT_EQUALS(eps[1].index, 1);
		TS_ASSERT_EQUALS(eps[1].picOffset, 1);
		TS_ASSERT_EQUALS(eps[1].pts, 5000u);
		TS_ASSERT_EQUALS(eps[1].offset, 0x00001234u);
	}

	// Real-corpus probe (dev-only, skipped unless SMT_GMO_CORPUS is set):
	// demuxes the real P3P opening with our PSMF code and writes the inner
	// MPEG-PS plus a stats report to /tmp/opencode, so the demuxed stream can
	// be decoded by ffmpeg and compared frame-for-frame against the same
	// conversion done directly by ffmpeg on the original .pmsf.
	void testRealP3PDump() {
		const char *corpus = getenv("SMT_GMO_CORPUS");
		if (!corpus || !*corpus)
			return;

		SearchMan.addDirectory(Common::Path(corpus), 0, 10);
		Common::PMF pmf("sound/pmsf/P3OPMV_P3P.pmsf");
		TS_ASSERT(pmf.isValid());
		if (!pmf.isValid())
			return;
		TS_ASSERT_EQUALS(pmf.getWidth(), 480);
		TS_ASSERT_EQUALS(pmf.getHeight(), 272);

		Common::PMF::ATRAC3Audio a = pmf.extractATRAC3Audio();
		TS_ASSERT(a.valid());
		if (!a.valid())
			return;

		Common::SeekableReadStream *mpeg = pmf.createMpegStream();
		TS_ASSERT(mpeg);
		if (!mpeg)
			return;

		Common::DumpFile out;
		TS_ASSERT(out.open(Common::Path("/tmp/opencode/ours_p3p_inner.mpg"), true));
		uint32 mpegBytes = (uint32)mpeg->size();
		byte buf[65536];
		uint32 dumped = 0;
		while (!mpeg->eos()) {
			uint32 r = mpeg->read(buf, sizeof(buf));
			if (!r)
				break;
			out.write(buf, r);
			dumped += r;
		}
		out.close();
		delete mpeg;

		uint32 frameCount = a.blockAlign ? (uint32)(a.frames.size() / a.blockAlign) : 0;
		Common::String msg = Common::String::format(
			"audio channels=%u sampleRate=%u blockAlign=%u numSamples=%u frames=%u audioSeconds=%.3f\n"
			"video width=%u height=%u mpegSize=%u mpegDumped=%u\n",
			a.channels, a.sampleRate, a.blockAlign, a.numSamples, frameCount,
			a.sampleRate ? (double)a.numSamples / (double)a.sampleRate : 0.0,
			pmf.getWidth(), pmf.getHeight(), mpegBytes, dumped);

		Common::DumpFile rep;
		if (rep.open(Common::Path("/tmp/opencode/ours_p3p_report.txt"), true)) {
			rep.write(msg.c_str(), msg.size());
			rep.close();
		}
	}

private:
	static void put16be(byte *p, uint16 v) {
		p[0] = (byte)(v >> 8);
		p[1] = (byte)(v & 0xFF);
	}

	static void put32be(byte *p, uint32 v) {
		p[0] = (byte)(v >> 24);
		p[1] = (byte)((v >> 16) & 0xFF);
		p[2] = (byte)((v >> 8) & 0xFF);
		p[3] = (byte)(v & 0xFF);
	}

	static void put48be(byte *p, uint64 v) {
		for (int i = 0; i < 6; i++)
			p[i] = (byte)((v >> (40 - i * 8)) & 0xFF);
	}
};
