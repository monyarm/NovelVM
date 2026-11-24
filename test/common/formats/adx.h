#include <cxxtest/TestSuite.h>

#include "audio/audiostream.h"
#include "common/formats/adx.h"
#include "common/memstream.h"

/**
 * Synthetic ADX header tests (no game data). The fixtures are hand-built
 * 0x8000-magic headers with a 1-channel 4-bit stream; only the header is
 * parsed here, no frames are decoded.
 */
class ADXFormatTestSuite : public CxxTest::TestSuite {
public:
	// sampleRate is the divisor of the coefficient math and the basis of
	// every timestamp: 0 once produced NaN coefficients and an undefined
	// float->int conversion. It must now be rejected cleanly.
	void testRejectsZeroSampleRate() {
		byte buf[80];
		buildHeader(buf, sizeof(buf), 0);

		Common::MemoryReadStream stream(buf, sizeof(buf), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s = Common::makeADXStream(&stream, DisposeAfterUse::NO);
		TS_ASSERT(s == nullptr);
		delete s;
	}

	// An implausible (too large) sample rate must likewise be rejected.
	void testRejectsImplausibleSampleRate() {
		byte buf[80];
		buildHeader(buf, sizeof(buf), 0xFFFFFFFFu);

		Common::MemoryReadStream stream(buf, sizeof(buf), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s = Common::makeADXStream(&stream, DisposeAfterUse::NO);
		TS_ASSERT(s == nullptr);
		delete s;
	}

	// Positive control: a plausible rate still parses.
	void testAcceptsPlausibleSampleRate() {
		byte buf[80];
		buildHeader(buf, sizeof(buf), 44100);

		Common::MemoryReadStream stream(buf, sizeof(buf), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s = Common::makeADXStream(&stream, DisposeAfterUse::NO);
		TS_ASSERT(s != nullptr);
		if (s) {
			TS_ASSERT_EQUALS(s->getRate(), 44100);
			TS_ASSERT(!s->isStereo());
		}
		delete s;
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

	// Writes a minimal unencrypted ADX header; `streamDataOffset` is placed
	// past the optional 24-byte loop descriptor so the loop fields stay zero.
	static void buildHeader(byte *buf, int size, uint32 sampleRate) {
		memset(buf, 0, size);
		put16be(buf + 0, 0x8000);
		put16be(buf + 2, 0x40); // copyright offset -> stream data at 0x44
		buf[4] = 3;             // format: ADX
		buf[5] = 18;            // block size
		buf[6] = 4;             // bits per sample
		buf[7] = 1;             // channel count
		put32be(buf + 8, sampleRate);
		put32be(buf + 12, 1000); // sample count
		put16be(buf + 16, 500);  // highpass
		buf[18] = 3;             // version
		buf[19] = 0;             // encryption flag
	}
};
