#include <cxxtest/TestSuite.h>

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
};
