#include "common/formats/adx.h"

#include <cmath>

#include "audio/audiostream.h"
#include "common/array.h"
#include "common/formats/adx_key8_primes.h"
#include "common/textconsole.h"
#include "common/util.h"

// TODO: loops are only verified on a file whose loop end is also its last sample; a loop that
// ends earlier has no real sample to check against.

namespace Common {

namespace {

// Header layout verified against real Code Geass and Persona 3 (FES/
// Portable) .adx files. https://rewiki.miraheze.org/wiki/CRI_ADX_Audio
struct ADXHeader {
	uint32 streamDataOffset = 0;
	byte channelCount = 0;
	uint32 sampleRate = 0;
	uint32 sampleCount = 0;
	uint16 highpass = 0;
	byte blockSize = 0;
	int32 coef1 = 0;
	int32 coef2 = 0;

	bool hasLoop = false;
	uint32 loopStart = 0;
	uint32 loopEnd = 0;

	bool encrypted = false;
	uint16 xorStart = 0;
	uint16 xorMult = 0;
	uint16 xorAdd = 0;
};

// CRI key derivation, ported from vgmstream's cri_keys.c. Public,
// game-agnostic algorithms; the actual per-game key isn't shipped here.
// Unverified against real encrypted data (none in this session's corpus).

// TODO: Type 8/9 ADX decryption is only self-consistency tested; no encrypted ADX exists in
// the available corpus (P3F/P4 BGM.CVM ADX are unencrypted). Verify against a real sample.
void deriveKey9(uint64 key9, uint16 subkey, uint16 &xorStart, uint16 &xorMult, uint16 &xorAdd) {
	xorStart = xorMult = xorAdd = 0;
	if (key9 == 0)
		return;
	if (subkey != 0)
		key9 = key9 * (((uint64)subkey << 16) | (uint16)(~subkey + 2));
	key9--;
	xorStart = (uint16)((key9 >> 27) & 0x7fff);
	xorMult = (uint16)(((key9 >> 12) & 0x7ffc) | 1);
	xorAdd = (uint16)(((key9 << 1) & 0x7fff) | 1);
}

void deriveKey8(const Common::String &key8, uint16 &xorStart, uint16 &xorMult, uint16 &xorAdd) {
	xorStart = xorMult = xorAdd = 0;
	if (key8.empty())
		return;
	xorStart = kADXKey8Primes[0x100];
	xorMult = kADXKey8Primes[0x200];
	xorAdd = kADXKey8Primes[0x300];
	for (uint32 i = 0; i < key8.size(); i++) {
		int idx = (int)(int8)key8[i] + 0x80;
		xorStart = kADXKey8Primes[xorStart * kADXKey8Primes[idx] % 0x400];
		xorMult = kADXKey8Primes[xorMult * kADXKey8Primes[idx] % 0x400];
		xorAdd = kADXKey8Primes[xorAdd * kADXKey8Primes[idx] % 0x400];
	}
}

bool parseADXHeader(Common::SeekableReadStream &s, ADXHeader &h) {
	if (s.readUint16BE() != 0x8000) {
		warning("ADX: bad magic (expected 0x8000)");
		return false;
	}

	uint16 copyrightOffset = s.readUint16BE();
	h.streamDataOffset = (uint32)copyrightOffset + 4;

	byte format = s.readByte();
	h.blockSize = s.readByte();
	byte bitsPerSample = s.readByte();
	h.channelCount = s.readByte();
	h.sampleRate = s.readUint32BE();
	h.sampleCount = s.readUint32BE();
	h.highpass = s.readUint16BE();
	byte version = s.readByte();
	byte encryptionFlag = s.readByte();

	if (s.err() || s.eos()) {
		warning("ADX: truncated header");
		return false;
	}
	if (format != 3) {
		warning("ADX: unsupported encoding format %d", format);
		return false;
	}
	if (bitsPerSample != 4) {
		warning("ADX: unsupported bit depth %d", bitsPerSample);
		return false;
	}
	if (h.channelCount != 1 && h.channelCount != 2) {
		warning("ADX: unsupported channel count %d", h.channelCount);
		return false;
	}
	// sampleRate is the divisor in the coefficient math below and the basis
	// for every timestamp; 0 (or an absurd value) would yield a NaN/Inf
	// coefficient and an undefined float->int conversion.
	if (h.sampleRate == 0 || h.sampleRate > 384000) {
		warning("ADX: implausible sample rate %u", h.sampleRate);
		return false;
	}
	if (h.blockSize < 3) {
		warning("ADX: implausible block size %d", h.blockSize);
		return false;
	}
	if (version != 3 && version != 4) {
		warning("ADX: unsupported header version %d", version);
		return false;
	}
	h.encrypted = (encryptionFlag != 0);

	// Hist1/Hist2 (unused, decode always starts from zero), then an
	// optional 24-byte loop descriptor.
	s.skip(4);
	s.skip(8);
	int64 loopOffset = s.pos();
	if (loopOffset + 24 <= (int64)h.streamDataOffset) {
		s.skip(8);
		h.loopStart = s.readUint32BE();
		s.skip(4);
		h.loopEnd = s.readUint32BE();
		s.skip(4);
		h.hasLoop = (h.loopEnd > h.loopStart) && (h.loopEnd <= h.sampleCount);
	}

	// https://wiki.multimedia.cx/index.php/CRI_ADX_ADPCM#Coefficients
	double z = cos(2.0 * M_PI * h.highpass / h.sampleRate);
	double a = M_SQRT2 - z;
	double b = M_SQRT2 - 1.0;
	double c = (a - sqrt((a + b) * (a - b))) / b;
	h.coef1 = (int32)floor(c * 8192.0);
	h.coef2 = (int32)floor(c * c * -4096.0);

	s.seek(h.streamDataOffset);
	return !s.err();
}

const int8 kNibbleToInt[16] = {0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1};

int16 clamp16(int32 v) {
	if (v > 32767)
		return 32767;
	if (v < -32768)
		return -32768;
	return (int16)v;
}

class ADXStream : public Audio::SeekableAudioStream {
public:
	ADXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const ADXHeader &header)
		: _stream(stream), _dispose(dispose), _header(header) {
		_samplesPerFrame = ((uint32)header.blockSize - 2) * 2;
		_frameBuf.resize((uint32)header.blockSize * header.channelCount);
		_decoded.resize(_samplesPerFrame * header.channelCount);
		rewind();
	}

	~ADXStream() override {
		if (_dispose == DisposeAfterUse::YES)
			delete _stream;
	}

	// numSamples/return value count interleaved int16 slots, not frames.
	int readBuffer(int16 *buffer, const int numSamples) override {
		uint32 channels = _header.channelCount;
		int samples = 0;
		while ((uint32)(numSamples - samples) >= channels && !endOfData()) {
			if (_bufPos >= _samplesInBuf && !decodeFrame())
				break;

			uint32 framesAvail = _samplesInBuf - _bufPos;
			uint32 framesWanted = (uint32)(numSamples - samples) / channels;
			uint32 toCopy = MIN(framesAvail, framesWanted);
			if (toCopy == 0)
				break;

			memcpy(buffer, &_decoded[_bufPos * channels], toCopy * channels * sizeof(int16));
			buffer += toCopy * channels;
			samples += toCopy * channels;
			_bufPos += toCopy;
			_samplesRead += toCopy;
		}
		return samples;
	}

	bool isStereo() const override { return _header.channelCount == 2; }
	int getRate() const override { return _header.sampleRate; }
	bool endOfData() const override { return !_header.hasLoop && _samplesRead >= _header.sampleCount; }

	bool rewind() override {
		_stream->seek(_header.streamDataOffset);
		_hist1[0] = _hist1[1] = _hist2[0] = _hist2[1] = 0;
		_xorState[0] = _xorState[1] = _header.xorStart;
		_samplesRead = 0;
		_samplesInBuf = 0;
		_bufPos = 0;
		return true;
	}

	bool seek(const Audio::Timestamp &where) override {
		uint32 targetSample = (uint32)where.convertToFramerate(_header.sampleRate).totalNumberOfFrames();
		if (!_header.hasLoop && targetSample >= _header.sampleCount) {
			_samplesRead = _header.sampleCount;
			return true;
		}

		rewind();
		uint32 frame = targetSample / _samplesPerFrame;
		_stream->seek(_header.streamDataOffset + (int64)frame * _frameBuf.size());
		_samplesRead = frame * _samplesPerFrame;

		uint32 remainder = targetSample - _samplesRead;
		if (remainder > 0) {
			if (!decodeFrame())
				return false;
			_bufPos = remainder;
			_samplesRead += remainder;
		}
		return true;
	}

	Audio::Timestamp getLength() const override {
		return Audio::Timestamp(0, _header.sampleCount, _header.sampleRate);
	}

private:
	Common::SeekableReadStream *_stream;
	DisposeAfterUse::Flag _dispose;
	ADXHeader _header;

	uint32 _samplesPerFrame = 0;
	Common::Array<byte> _frameBuf;
	Common::Array<int16> _decoded;
	int32 _hist1[2] = {0, 0};
	int32 _hist2[2] = {0, 0};
	uint16 _xorState[2] = {0, 0};
	uint32 _samplesRead = 0;
	uint32 _samplesInBuf = 0;
	uint32 _bufPos = 0;

	bool decodeFrame() {
		if (_header.hasLoop && _samplesRead >= _header.loopEnd) {
			uint32 frame = _header.loopStart / _samplesPerFrame;
			_stream->seek(_header.streamDataOffset + (int64)frame * _frameBuf.size());
			_samplesRead = frame * _samplesPerFrame;
		}

		if (_stream->read(_frameBuf.data(), _frameBuf.size()) != _frameBuf.size())
			return false;

		for (byte c = 0; c < _header.channelCount; c++) {
			const byte *chan = &_frameBuf[(uint32)c * _header.blockSize];
			int32 rawScale = ((int32)chan[0] << 8) | chan[1];
			int32 scale;
			if (_header.encrypted) {
				scale = (((int16)rawScale ^ (int16)_xorState[c]) & 0x1fff) + 1;
				_xorState[c] = (_xorState[c] * _header.xorMult + _header.xorAdd) & 0x7fff;
			} else {
				scale = rawScale + 1; // +1 matters on quiet streams (scale 0)
			}
			const byte *nibbles = chan + 2;

			for (uint32 i = 0; i < _samplesPerFrame; i++) {
				byte b = nibbles[i / 2];
				int sample = kNibbleToInt[(i & 1) ? (b & 0xf) : (b >> 4)];
				int32 predicted = (_header.coef1 * _hist1[c] >> 12) + (_header.coef2 * _hist2[c] >> 12);
				int16 out = clamp16(sample * scale + predicted);
				_decoded[i * _header.channelCount + c] = out;
				_hist2[c] = _hist1[c];
				_hist1[c] = out;
			}
		}

		_samplesInBuf = _samplesPerFrame;
		_bufPos = 0;
		return true;
	}
};

} // namespace

Audio::SeekableAudioStream *makeADXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse) {
	ADXHeader header;
	if (!parseADXHeader(*stream, header) || header.encrypted) {
		if (header.encrypted)
			warning("ADX: file is encrypted, no key supplied");
		if (disposeAfterUse == DisposeAfterUse::YES)
			delete stream;
		return nullptr;
	}
	return new ADXStream(stream, disposeAfterUse, header);
}

Audio::SeekableAudioStream *makeADXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse, uint64 key9, uint16 subkey) {
	ADXHeader header;
	if (!parseADXHeader(*stream, header)) {
		if (disposeAfterUse == DisposeAfterUse::YES)
			delete stream;
		return nullptr;
	}
	if (header.encrypted)
		deriveKey9(key9, subkey, header.xorStart, header.xorMult, header.xorAdd);
	return new ADXStream(stream, disposeAfterUse, header);
}

Audio::SeekableAudioStream *makeADXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse, const Common::String &key8) {
	ADXHeader header;
	if (!parseADXHeader(*stream, header)) {
		if (disposeAfterUse == DisposeAfterUse::YES)
			delete stream;
		return nullptr;
	}
	if (header.encrypted)
		deriveKey8(key8, header.xorStart, header.xorMult, header.xorAdd);
	return new ADXStream(stream, disposeAfterUse, header);
}

bool parseADXFrameInfo(Common::SeekableReadStream &stream, ADXFrameInfo &info) {
	int64 startPos = stream.pos();
	ADXHeader h;
	bool ok = parseADXHeader(stream, h);
	stream.seek(startPos);
	if (!ok)
		return false;

	info.channelCount = h.channelCount;
	info.sampleRate = h.sampleRate;
	info.blockSize = h.blockSize;
	info.streamDataOffset = h.streamDataOffset;
	info.coef1 = h.coef1;
	info.coef2 = h.coef2;
	return true;
}

void decodeADXFrame(const byte *frameData, const ADXFrameInfo &info, int32 hist1[2], int32 hist2[2], int16 *out) {
	uint32 samplesPerFrame = info.samplesPerFrame();
	for (byte c = 0; c < info.channelCount; c++) {
		const byte *chan = frameData + (uint32)c * info.blockSize;
		int32 scale = (((int32)chan[0] << 8) | chan[1]) + 1; // +1 matters on quiet streams (scale 0)
		const byte *nibbles = chan + 2;

		for (uint32 i = 0; i < samplesPerFrame; i++) {
			byte b = nibbles[i / 2];
			int sample = kNibbleToInt[(i & 1) ? (b & 0xf) : (b >> 4)];
			int32 predicted = (info.coef1 * hist1[c] >> 12) + (info.coef2 * hist2[c] >> 12);
			int16 o = clamp16(sample * scale + predicted);
			out[i * info.channelCount + c] = o;
			hist2[c] = hist1[c];
			hist1[c] = o;
		}
	}
}

} // namespace Common
