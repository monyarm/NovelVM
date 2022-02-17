#ifndef ADX_H
#define ADX_H

#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"

#include "audio/audiostream.h"
#include "audio/decoders/adpcm_intern.h"
#include "common/str.h"
#include "common/system.h" 
#include "audio/mixer.h"

namespace Common {

struct AdxHeaderInfo {
	int ChannelCount;
	int SampleRate;
	int SampleCount;
	bool HasLoop;
	int LoopStart;
	int LoopEnd;

	int BitDepth;

	uint16 Highpass;
	uint8 FrameSize;
	int StreamDataOffset;

	int32 Hist1[2];
	int32 Hist2[2];

	int ReadPosition = 0;

	int SamplesPerBuffer = 0;
	int32 Coef1;
	int32 Coef2;

	int32 EncodedBytesPerBuffer;

	int BytesPerSample() const { return (BitDepth / 8) * ChannelCount; }
};

class ADX_ADPCMStream;

class ADX {

public:
	ADX(const char *path);
	ADX(Common::SeekableReadStream *stream);
	~ADX(){};

private:
	int16 coefficient[2];
	AdxHeaderInfo info;
	void readFile(Common::SeekableReadStream *stream);

	void readHeader(Common::SeekableReadStream *f);

	void SetCoefficients(double cutoff, double sample_rate);


};
} // namespace Common

#endif
