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

	// Loops end exactly at the loop end sample (mid-frame here) and resume from the decoder state
	// saved at the loop start, so a loop from 0 repeats the first samples bit for bit.
	void testLoopEndsAtLoopEndSampleAndRestoresState() {
		Common::Array<byte> d = buildV4Stream(4, 0, 0, true, 0, 40);
		Common::MemoryReadStream stream(d.data(), d.size(), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s = Common::makeADXStream(&stream, DisposeAfterUse::NO);
		TS_ASSERT(s != nullptr);
		if (!s)
			return;
		int16 out[80];
		TS_ASSERT_EQUALS(s->readBuffer(out, 80), 80);
		for (int i = 0; i < 40; ++i)
			TS_ASSERT_EQUALS(out[40 + i], out[i]);
		delete s;
	}

	// A frame whose scale word has bit 15 set ends the stream (CRI's footer).
	void testEndMarkerStopsDecoding() {
		Common::Array<byte> d = buildV4Stream(4, 0, 0, false, 0, 0);
		put16be(&d[0x44 + 2 * 18], 0x8001);
		Common::MemoryReadStream stream(d.data(), d.size(), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s = Common::makeADXStream(&stream, DisposeAfterUse::NO);
		TS_ASSERT(s != nullptr);
		if (!s)
			return;
		int16 out[128];
		TS_ASSERT_EQUALS(s->readBuffer(out, 128), 64);
		TS_ASSERT(s->endOfData());
		delete s;
	}

	// Version 4 headers seed the prediction from the stored history; with silent frames the
	// output decays from it instead of starting at 0.
	void testInitialHistorySeedsPrediction() {
		Common::Array<byte> d = buildV4Stream(1, 1000, 1000, false, 0, 0);
		for (uint i = 0x44; i < d.size(); ++i)
			d[i] = 0; // scale 0 (+1), all nibbles 0
		Common::MemoryReadStream stream(d.data(), d.size(), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s = Common::makeADXStream(&stream, DisposeAfterUse::NO);
		TS_ASSERT(s != nullptr);
		if (!s)
			return;
		int16 out[1];
		TS_ASSERT_EQUALS(s->readBuffer(out, 1), 1);
		TS_ASSERT_DIFFERS(out[0], 0);
		delete s;
	}

private:
	// Version 4 mono stream: `frames` 18-byte frames (32 samples each) of a fixed pattern, history
	// (h1, h2) for channel 0, and an optional loop [loopStart, loopEnd).
	static Common::Array<byte> buildV4Stream(int frames, int16 h1, int16 h2, bool loop, uint32 loopStart, uint32 loopEnd) {
		Common::Array<byte> d(0x44 + frames * 18, 0);
		buildHeader(d.data(), 0x44, 24000);
		put32be(&d[12], frames * 32);
		d[18] = 4;
		put16be(&d[0x18], (uint16)h1);
		put16be(&d[0x1A], (uint16)h2);
		if (loop) {
			put16be(&d[0x22], 1);
			put32be(&d[0x28], loopStart);
			put32be(&d[0x30], loopEnd);
		}
		for (int f = 0; f < frames; ++f) {
			byte *frame = &d[0x44 + f * 18];
			put16be(frame, 0x40);
			for (int i = 2; i < 18; ++i)
				frame[i] = (byte)(0x17 * (i + f) + 0x31);
		}
		return d;
	}

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
