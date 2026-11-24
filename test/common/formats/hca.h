#include <cxxtest/TestSuite.h>

#include "audio/audiostream.h"
#include "common/formats/hca.h"
#include "common/formats/hca_tables.h"
#include "common/memstream.h"

/**
 * Synthetic HCA header tests (no game data). Fixtures are hand-built
 * "HCA\0" headers with a correctly checksummed header block; no frames are
 * decoded. Layout follows common/formats/hca.cpp's parser.
 */
class HCAFormatTestSuite : public CxxTest::TestSuite {
public:
	// frameCount * 1024 is the stream's total sample count. A count that
	// would overflow that 32-bit product must be rejected: previously the
	// product wrapped (e.g. to 2048) and getLength()/endOfData() lied.
	void testRejectsFrameCountOverflow() {
		byte buf[44];
		buildHeader(buf, 0x00400002); // 4194306 frames -> product wraps to 2048

		Common::MemoryReadStream stream(buf, sizeof(buf), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s = Common::makeHCAStream(&stream, DisposeAfterUse::NO);
		TS_ASSERT(s != nullptr);
		if (s) {
			// Rejected header: no wrapped nonzero length, and immediately
			// at end of data.
			TS_ASSERT(s->endOfData());
			TS_ASSERT_EQUALS(s->getLength().totalNumberOfFrames(), 0);
		}
		delete s;
	}

	// Positive control: a plausible frame count yields the exact sample
	// count (no wrap) from getLength().
	void testValidFrameCountLength() {
		byte buf[44];
		buildHeader(buf, 2);

		Common::MemoryReadStream stream(buf, sizeof(buf), DisposeAfterUse::NO);
		Audio::SeekableAudioStream *s = Common::makeHCAStream(&stream, DisposeAfterUse::NO);
		TS_ASSERT(s != nullptr);
		if (s) {
			TS_ASSERT_EQUALS(s->getLength().totalNumberOfFrames(), 2048);
			TS_ASSERT(!s->endOfData());
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

	// Builds a 44-byte v1.03 HCA header: 2 channels, 44100 Hz, the given
	// frameCount, an 8-bit "dec" chunk, and a valid trailing CRC16.
	static void buildHeader(byte *buf, uint32 frameCount) {
		memset(buf, 0, 44);
		buf[0] = 'H'; buf[1] = 'C'; buf[2] = 'A'; buf[3] = 0;
		put16be(buf + 4, 0x0103); // version
		put16be(buf + 6, 44);     // header size

		// fmt chunk
		put32be(buf + 8, 0x666D7400); // "fmt\0"
		buf[12] = 2;                  // channels
		buf[13] = 0x00;               // sampleRate = 44100 (24-bit BE)
		buf[14] = 0xAC;
		buf[15] = 0x44;
		put32be(buf + 16, frameCount);
		put16be(buf + 20, 0); // encoderDelay
		put16be(buf + 22, 0); // encoderPadding

		// dec chunk (v1.x)
		put32be(buf + 24, 0x64656300); // "dec\0"
		put16be(buf + 28, 0x0100);     // frameSize
		buf[30] = 1;                   // minResolution
		buf[31] = 15;                  // maxResolution
		buf[32] = 3;                   // totalBandCount - 1
		buf[33] = 3;                   // baseBandCount - 1
		buf[34] = 0x12;                // trackCount=1, channelConfig=2
		buf[35] = 0;                   // stereoType

		// bytes 36..41 are padding; the CRC occupies the final two bytes.
		uint16 crc = 0;
		for (int i = 0; i < 42; i++)
			crc = (uint16)((crc << 8) ^ Common::HCATables::kCRCMaskTable[((crc >> 8) ^ buf[i]) & 0xFF]);
		put16be(buf + 42, crc);
	}
};
