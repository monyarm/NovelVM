#include <cxxtest/TestSuite.h>

#include "common/formats/ahx.h"
#include "common/memstream.h"
#include "common/util.h"
#include "audio/audiostream.h"

/**
 * CRI AHX = CRI "voice": an MPEG-1/2 Audio Layer II stream wrapped in a CRI
 * header. The unit tests below use a synthetic, fully-silent AHX file built
 * by hand (no game data): a valid 0x14-byte CRI header, the `(c)CRI` banner
 * and one MPEG Layer II frame whose bit allocation is all zero -> decodes to
 * 1152 zero samples at 24000 Hz.
 */
class AHXTestSuite : public CxxTest::TestSuite {
public:
	void testHeaderParse() {
		// magic + startDelta (0x20 -> data at 0x24)
		// two silent frames + the CRI "AHXE(c)CRI" EOF footer
		const byte buf[] = {
			0x80, 0x00, 0x00, 0x20,
			// type, frameSize, bps, channels
			0x11, 0x00, 0x00, 0x01,
			// sample rate 24000
			0x00, 0x00, 0x5D, 0xC0,
			// numSamples 2304 (2 frames)
			0x00, 0x00, 0x09, 0x00,
			// reserved, version, encryption
			0x00, 0x00, 0x06, 0x00,
			// 10 filler bytes up to the banner
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			// banner "(c)CRI"
			0x28, 0x63, 0x29, 0x43, 0x52, 0x49,
			// MPEG Layer II frame: fixed header 0xFFF5E0C0 (MPEG-2/LSF,
			// 160kbps, mono) + all-zero bit allocation (silence)
			0xFF, 0xF5, 0xE0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00,
			// frame 2 (silence)
			0xFF, 0xF5, 0xE0, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00,
			// AHX EOF footer: 0x8001000C "AHXE(c)CRI\0\0"
			0x80, 0x01, 0x00, 0x0C, 0x41, 0x48, 0x58, 0x45, 0x28, 0x63, 0x29,
			0x43, 0x52, 0x49, 0x00, 0x00
		};

		Common::MemoryReadStream s(buf, sizeof(buf), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s1 = Common::makeAHXStream(&s, DisposeAfterUse::NO);
		TS_ASSERT(s1 != NULL);

		TS_ASSERT_EQUALS(s1->getRate(), 24000);
		TS_ASSERT_EQUALS(s1->isStereo(), false);
		TS_ASSERT_EQUALS(s1->getLength().totalNumberOfFrames(), 2304u);

		int16 bufOut[64];
		memset(bufOut, 0x55, sizeof(bufOut));
		TS_ASSERT_EQUALS(s1->readBuffer(bufOut, 64), 64);
		for (int i = 0; i < 64; i++) {
			TS_ASSERT_EQUALS(bufOut[i], 0); // silent frame
		}

		TS_ASSERT_EQUALS(s1->endOfData(), false);
		delete s1;
	}

	void testMalformed() {
		// not AHX magic -> reject
		const byte bad[0x14] = { 0xFF, 0xFF, 0, 0, 0x11, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0 };
		Common::MemoryReadStream s1(bad, sizeof(bad), DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(Common::makeAHXStream(&s1, DisposeAfterUse::NO), (Audio::SeekableAudioStream *)NULL);

		// encrypted stream -> reject
		const byte enc[0x14] = { 0x80, 0, 0, 0x20, 0x11, 0, 0, 1, 0, 0, 0x5D, 0xC0, 0, 0, 4, 0x80, 0, 0, 6, 0x08 };
		Common::MemoryReadStream s2(enc, sizeof(enc), DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(Common::makeAHXStream(&s2, DisposeAfterUse::NO), (Audio::SeekableAudioStream *)NULL);

		// stereo (channels != 1) -> reject
		const byte stereo[0x14] = { 0x80, 0, 0, 0x20, 0x11, 0, 0, 2, 0, 0, 0x5D, 0xC0, 0, 0, 4, 0x80, 0, 0, 6, 0x00 };
		Common::MemoryReadStream s3(stereo, sizeof(stereo), DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(Common::makeAHXStream(&s3, DisposeAfterUse::NO), (Audio::SeekableAudioStream *)NULL);
	}
};
