#include <algorithm>

#include "common/formats/adx.h"
#include "video/mpegps_decoder.h"
#include "audio/mixer.h"
#include "common/endian.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/types.h"
#include "common/util.h"

namespace {

const int nibble_to_int[16] = {0, 1, 2, 3, 4, 5, 6, 7,
                               -8, -7, -6, -5, -4, -3, -2, -1};

} // anonymous namespace

namespace Common {

class ADX_ADPCMStream : public ::Audio::ADPCMStream {
public:
	ADX_ADPCMStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse, uint32 size, uint32 blockSize, AdxHeaderInfo info) : ::Audio::ADPCMStream(stream, disposeAfterUse, size, 44100, 1, blockSize), _looped(info.HasLoop) {}

	bool endOfData() const override {
		return (!_looped && ::Audio::ADPCMStream::endOfData());
	}

	void seekToBlock(uint32 block) {
		reset();
		_stream->seek(_startpos + _blockAlign * block);
	}

	int readBuffer(int16 *buffer, const int numSamples) override {
		int samples = 0;
		int read = 0;

		while (samples < numSamples) {
			if (::Audio::ADPCMStream::endOfData()) {
				if (!_looped)
					break;
				rewind();
			}

			if (DecodedSamplesAvailable) {
				int samplesWrittenNow = std::min(samples, DecodedSamplesAvailable);

				if (buffer) {
					// null pointer => discard-seek
					memcpy(buffer,
					       DecodedBuffer +
					           DecodedSamplesConsumed * _info.ChannelCount,
					       samplesWrittenNow * _info.ChannelCount * sizeof(int16));
					buffer += samplesWrittenNow * _info.ChannelCount;
				}
				samples += samplesWrittenNow;
				read += samplesWrittenNow;
				_info.ReadPosition += samplesWrittenNow;
				DecodedSamplesAvailable -= samplesWrittenNow;
				DecodedSamplesConsumed += samplesWrittenNow;
			}
			else {

				int samplesLeft = _info.SampleCount - _info.ReadPosition;
				if (samplesLeft == 0)
					return read;

				DecodedSamplesAvailable = _info.SamplesPerBuffer;
				DecodedSamplesConsumed = 0;

				_stream->read(EncodedBuffer,_info.EncodedBytesPerBuffer);

				bool decodeSuccess = DecodeBuffer();

				DecodedSamplesAvailable =
				    MIN(DecodedSamplesAvailable, samplesLeft);
				if (!decodeSuccess)
					return read;
			}
		}

			return samples;
		}

private:
	bool _looped;
	AdxHeaderInfo _info;
	int DecodedSamplesAvailable = 0;
	int DecodedSamplesConsumed = 0;
	int16 DecodedBuffer[127] = {0};
	uint8 EncodedBuffer[256] = {0};

	static inline int get_high_nibble_signed(uint8 n) {
		return nibble_to_int[n >> 4];
	}

	static inline int get_low_nibble_signed(uint8 n) {
		return nibble_to_int[n & 0xf];
	}

	static inline int clamp16(int32_t val) {
		if (val > 32767)
			return 32767;
		if (val < -32768)
			return -32768;
		return val;
	}

	bool DecodeBuffer() {

		int16 *output = DecodedBuffer;
		uint8 *input = EncodedBuffer;

		for (int c = 0; c < _info.ChannelCount; c++) {
			/* the +1 becomes important on quiet ADXs */
			int scale = SWAP_BYTES_16(*(uint16 *)input) + 1;
			input += 2;

			for (int i = 0; i < _info.SamplesPerBuffer; i++) {
				/* this byte contains nibbles for two samples */
				int sample_byte = input[i / 2];

				output[i * _info.ChannelCount + c] =
				    clamp16((i & 1 ? get_low_nibble_signed(sample_byte)
				                   : get_high_nibble_signed(sample_byte)) *
				                scale +
				            (_info.Coef1 * _info.Hist1[c] >> 12) + (_info.Coef2 * _info.Hist2[c] >> 12));

				_info.Hist2[c] = _info.Hist1[c];
				_info.Hist1[c] = output[i * _info.ChannelCount + c];
			}
			input += _info.SamplesPerBuffer / 2;
		}

		return true;
	}
};

void ADX::SetCoefficients(double cutoff, double sample_rate) {
	// https://wiki.multimedia.cx/index.php/CRI_ADX_ADPCM#Coefficients

	/* temps to keep the calculation simple */
	double z, a, b, c;

	z = cos(2.0 * M_PI * cutoff / sample_rate);

	a = M_SQRT2 - z;
	b = M_SQRT2 - 1.0;
	c = (a - sqrt((a + b) * (a - b))) / b;

	/* compute the coefficients as fixed point values, with 12 fractional bits */
	coefficient[0] = (int16)floor(c * 8192);
	coefficient[1] = (int16)floor(c * c * -4096);
}

ADX::ADX(const char *path) {

	Common::File f;
	if (f.open(path)) {
		readFile(&f);
	}
}

ADX::ADX(Common::SeekableReadStream *stream) {
	readFile(stream);
}

void ADX::readFile(Common::SeekableReadStream *stream) {
	readHeader(stream);
	::Audio::SoundHandle* _handle = new ::Audio::SoundHandle();
	auto _astream = stream->readStream(stream->size() - info.StreamDataOffset);
	::Audio::AudioStream *_as = new ADX_ADPCMStream(_astream, DisposeAfterUse::NO, _astream->size(),info.EncodedBytesPerBuffer,info);
	g_system->getMixer()->playStream(::Audio::Mixer::kMusicSoundType, _handle, _as, -1, ::Audio::Mixer::kMaxChannelVolume,0);
}

void ADX::readHeader(Common::SeekableReadStream *stream) {

	uint8 header[0x34];
	stream->read(header, 0x34);
	if (SWAP_BYTES_16((*(uint16 *)header)) != 0x8000) {
		warning("Corrupt ADX Header");
		return;
	}

	info.StreamDataOffset = SWAP_BYTES_16(*(uint16 *)(header + 2)) + 4;

	stream->seek(info.StreamDataOffset - 6, SEEK_SET);
	char const magic[] = "(c)CRI";
	char fileMagic[6];
	stream->read(fileMagic, 6);
	if (memcmp(magic, fileMagic, 6) != 0) {
		warning("Corrupt 2nd ADX Header");
		return;
	}

	if (header[4] != 3) {
		warning(
		    "Encountered ADX file with unsupported encoding type format %d\n",
		    header[4]);
		return;
	}

	info.FrameSize = header[5];

	if (header[6] != 4) {
		warning(
		    "Encountered ADX file with unknown bits per sample %d\n", header[6]);
		return;
	}

	info.ChannelCount = header[7];
	if (info.ChannelCount != 1 && info.ChannelCount != 2) {
		warning(
		    "Encountered ADX file with unsupported channel count %d\n",
		    info.ChannelCount);
		return;
	}

	info.SampleRate = SWAP_BYTES_16(*(uint32 *)(header + 8));

	info.SampleCount = SWAP_BYTES_16(*(uint32 *)(header + 12));

	info.Highpass = SWAP_BYTES_16(*(uint16 *)(header + 16));

	if (header[18] != 4) {
		warning(
		       "Encountered ADX file with unsupported header version %d\n",
		       header[18]);
		return;
	}

	if (header[19] != 0) {
		warning("Encountered encrypted ADX file, type %d\n",
		       header[19]);
		return;
	}

	int HistOffset = 0x18;
	int HistSize = MAX(8, 4 * info.ChannelCount);

	info.Hist1[0] = SWAP_BYTES_16(*(uint16 *)(header + HistOffset));
	info.Hist2[0] = SWAP_BYTES_16(*(uint16 *)(header + HistOffset + 2));
	info.Hist1[1] = SWAP_BYTES_16(*(uint16 *)(header + HistOffset + 4));
	info.Hist2[1] = SWAP_BYTES_16(*(uint16 *)(header + HistOffset + 6));

	int LoopOffset = HistOffset + HistSize;
	int LoopSize = 0x18;
	if (LoopOffset + LoopSize <= info.StreamDataOffset) {
		info.HasLoop = true;
		info.LoopStart = SWAP_BYTES_32(*(uint32 *)(header + LoopOffset + 8));
		info.LoopEnd = SWAP_BYTES_32(*(uint32 *)(header + LoopOffset + 16));
	} else {
		info.HasLoop = false;
	}

	info.EncodedBytesPerBuffer = info.FrameSize * info.ChannelCount;
	info.SamplesPerBuffer = (info.FrameSize - 2) * 2;
	info.LoopStart = info.HasLoop ? info.LoopStart : 0;
	info.LoopEnd = info.HasLoop ? info.LoopEnd : info.SampleCount;
	SetCoefficients(info.Highpass, info.SampleRate);

	info.BitDepth = 16;

	stream->seek(info.StreamDataOffset, SEEK_SET);
}

} // namespace Common
