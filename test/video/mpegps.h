#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "video/mpegps_decoder.h"

class MPEGPSDetectionTestSuite : public CxxTest::TestSuite {
public:
	void testMpegSequenceHeaderIsNotH264() {
		const byte packet[] = {0x00, 0x00, 0x01, 0xB3, 0x02, 0x80, 0x01, 0xE0};
		TS_ASSERT(!Video::isH264AnnexBPacket(packet, sizeof(packet)));
	}

	void testH264SpsPpsIsDetected() {
		const byte packet[] = {
			0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E,
			0x00, 0x00, 0x01, 0x68, 0xCE, 0x06, 0xE2
		};
		TS_ASSERT(Video::isH264AnnexBPacket(packet, sizeof(packet)));
	}

	// PSMF ATRAC3+ parameters come from the frame header itself. 0x285C is the real
	// P3P opening's header (rate code 1 = 44100 Hz, 2 channels, 744 data bytes).
	void testDetectsATRAC3PlusFromFrameHeader() {
		const byte packet[] = { 0x00, 0x00, 0x00, 0x00, 0x0F, 0xD0, 0x28, 0x5C, 0xAA };
		Common::MemoryReadStream stream(packet, sizeof(packet));
		uint16 channels = 0, blockAlign = 0;
		uint32 sampleRate = 0;
		TS_ASSERT(Video::MPEGPSDecoder::detectATRAC3PlusInfo(stream, channels, sampleRate, blockAlign));
		TS_ASSERT_EQUALS(channels, 2);
		TS_ASSERT_EQUALS(sampleRate, 44100u);
		TS_ASSERT_EQUALS(blockAlign, 744);
		TS_ASSERT_EQUALS(stream.pos(), 0);

		const byte other[] = { 0x00, 0x00, 0x00, 0x00, 0x12, 0x34, 0x28, 0x5C };
		Common::MemoryReadStream notAtrac(other, sizeof(other));
		TS_ASSERT(!Video::MPEGPSDecoder::detectATRAC3PlusInfo(notAtrac, channels, sampleRate, blockAlign));
	}
};
