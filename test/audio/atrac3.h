#include <cxxtest/TestSuite.h>

#include "audio/decoders/atrac3.h"
#include "audio/decoders/atrac3_common.h"
#include "audio/decoders/atrac3plus.h"
#include "common/archive.h"
#include "common/array.h"
#include "common/file.h"
#include "common/formats/at3.h"
#include "common/formats/pmf.h"
#include "common/memstream.h"
#include "common/path.h"

#include "../system/null_osystem.h"

#include <stdlib.h>
#include <string.h>

// forbidden.h is already included by the generated runner before this file.
#undef getenv

/**
 * Real-data checks for the shared ATRAC frame decoder / stream refactor.
 *
 * The whole-stream SeekableAudioStream and the raw frame-at-a-time decoder
 * must produce bit-identical PCM for the same real frames. These are
 * developer/real-corpus checks (skipped unless the relevant env var points
 * at real data) so the normal `make test` run stays corpus-free.
 *
 *   SMT_GMO_CORPUS=<.../P3P_extracted/PSP_GAME/USRDIR>  P3P opening PSMF
 *   SMT_AT3_FILE=<path to a real .AT3>                  plain ATRAC3 / ATRAC3+
 */
class Atrac3RefTestSuite : public CxxTest::TestSuite {
	static int16 *decodeWholeStream(Audio::SeekableAudioStream *stream, uint32 total, uint32 &got) {
		int16 *out = (int16 *)malloc(total * sizeof(int16));
		got = 0;
		while (got < total) {
			int n = stream->readBuffer(out + got, (int)(total - got));
			if (n <= 0)
				break;
			got += n;
		}
		return out;
	}

	static bool anyNonZero(const int16 *pcm, uint32 count) {
		for (uint32 i = 0; i < count; i++)
			if (pcm[i])
				return true;
		return false;
	}

public:
	void setUp() { Common::install_null_g_system(); }
	void tearDown() { Common::uninstall_null_g_system(); }

	// P3P opening PSMF: decode every real ATRAC3+ frame both ways and require
	// the frame decoder to match the whole-stream decode sample for sample.
	void testPmfAtrac3PlusFrameDecoderMatchesStream() {
		const char *corpus = getenv("SMT_GMO_CORPUS");
		if (!corpus || !*corpus)
			return; // dev-only real-corpus check
		SearchMan.addDirectory(Common::Path(corpus), 0, 10);

		Common::PMF pmf("sound/pmsf/P3OPMV_P3P.pmsf");
		TS_ASSERT(pmf.isValid());
		if (!pmf.isValid())
			return;
		Common::PMF::ATRAC3Audio audio = pmf.extractATRAC3Audio();
		TS_ASSERT(audio.valid());
		if (!audio.valid())
			return;

		byte *es = (byte *)malloc(audio.frames.size());
		memcpy(es, audio.frames.data(), audio.frames.size());
		Audio::SeekableAudioStream *stream = Audio::makeATRAC3PlusStream(
			new Common::MemoryReadStream(es, audio.frames.size(), DisposeAfterUse::YES),
			DisposeAfterUse::YES, audio.channels, audio.sampleRate, audio.blockAlign, audio.numSamples);
		TS_ASSERT(stream);
		if (!stream)
			return;

		uint32 total = audio.numSamples * audio.channels;
		uint32 got = 0;
		int16 *whole = decodeWholeStream(stream, total, got);
		TS_ASSERT_EQUALS(got, total);

		Audio::ATRAC3PlusFrameDecoder decoder;
		TS_ASSERT(decoder.init(audio.channels, audio.blockAlign));
		TS_ASSERT_EQUALS(decoder.frameSamples(), 2048);

		uint32 channelSamples = decoder.frameSamples();
		Common::Array<int16> one((uint32)audio.channels * channelSamples);
		uint32 frames = audio.blockAlign ? audio.frames.size() / audio.blockAlign : 0;
		uint32 mismatch = 0;
		for (uint32 f = 0; f < frames; f++) {
			TS_ASSERT(decoder.decodeFrame(audio.frames.data() + f * audio.blockAlign, audio.blockAlign, one.data()));
			uint32 base = f * channelSamples * audio.channels;
			for (uint32 i = 0; i < (uint32)audio.channels * channelSamples && base + i < total; i++)
				if (one[i] != whole[base + i])
					mismatch++;
		}
		TS_ASSERT_EQUALS(mismatch, 0u);
		TS_ASSERT(anyNonZero(whole, total));

		free(whole);
		delete stream;
	}

	// Real plain-ATRAC3/ATRAC3+ .AT3 file: the shared stream wrapper (backed
	// by the refactored frame decoder) must decode to real, non-silent PCM.
	void testRealAt3Decodes() {
		const char *at3 = getenv("SMT_AT3_FILE");
		if (!at3 || !*at3)
			return; // dev-only real-corpus check
		Common::Path path(at3);
		SearchMan.addDirectory(path.getParent());

		Audio::SeekableAudioStream *stream = Common::makeAT3Stream(path.getLastComponent().toString());
		TS_ASSERT(stream);
		if (!stream)
			return;

		Common::Array<int16> buf(4096);
		uint32 got = 0, total = 0;
		bool nonZero = false;
		// Bound by total sample count, not a fixed chunk count, so a leading
		// fade/silence does not make a real decode look empty.
		while (!stream->endOfData() && total < (1u << 24)) {
			int n = stream->readBuffer(buf.data(), buf.size());
			if (n <= 0)
				break;
			got += n;
			total += n;
			if (anyNonZero(buf.data(), n))
				nonZero = true;
		}
		TS_ASSERT(got > 0);
		TS_ASSERT(nonZero);
		delete stream;
	}
};
