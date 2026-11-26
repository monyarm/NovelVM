#include "common/formats/adx.h"


#include "audio/audiostream.h"
#include "common/array.h"
#include "common/formats/adx_key8_primes.h"
#include "common/textconsole.h"
#include "common/util.h"

// Behaviour cross-checked against the CRI ADX library in the Persona 4 decompilation: prediction
// rounding, coefficients, initial history, loop end/state, key stream and end marker.
//
// TODO: CRI treats encoding types 2 and 4 as ADX too; only type 3 (the one every real file uses)
// is decoded. Loops are only verified on real files whose loop start is frame-aligned.

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

	int32 history[2][2] = {{0, 0}, {0, 0}};

	bool hasLoop = false;
	uint32 loopStart = 0;
	uint32 loopEnd = 0;

	bool encrypted = false;
	bool autoKey = false; ///< the key comes from the sample count, no key needs supplying
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
	// CRI: no decryption below version 4; flag 0x10 and up derives the key from the sample
	// count, 0x08-0x0F uses the application's key.
	h.encrypted = version >= 4 && encryptionFlag >= 0x08;
	if (h.encrypted && encryptionFlag >= 0x10) {
		h.autoKey = true;
		deriveKey8(Common::String::format("%08X", h.sampleCount), h.xorStart, h.xorMult, h.xorAdd);
	}

	// Version 4: initial history at 0x18 (per channel: previous, second previous sample), then
	// the loop block at 0x20. Version 3: the loop block is at 0x14.
	if (version == 4 && h.streamDataOffset >= 0x20) {
		s.seek(0x18);
		for (int c = 0; c < 2; ++c)
			for (int k = 0; k < 2; ++k)
				h.history[c][k] = s.readSint16BE();
	}
	const int64 loopOffset = version == 4 ? 0x20 : 0x14;
	if (loopOffset + 24 <= (int64)h.streamDataOffset) {
		s.seek(loopOffset + 2);
		const uint16 loopCount = s.readUint16BE();
		s.skip(4);
		h.loopStart = s.readUint32BE();
		s.skip(4);
		h.loopEnd = s.readUint32BE();
		h.hasLoop = loopCount == 1 && (h.loopEnd > h.loopStart) && (h.loopEnd <= h.sampleCount);
	}

	// https://wiki.multimedia.cx/index.php/CRI_ADX_ADPCM#Coefficients, in single precision
	// and truncated like CRI's library.
	const float z = cosf(2.0f * (float)M_PI * h.highpass / h.sampleRate);
	const float a = (float)M_SQRT2 - z;
	const float b = (float)M_SQRT2 - 1.0f;
	const float c = (a - sqrtf((a + b) * (a - b))) / b;
	h.coef1 = (int16)(c * 8192.0f);
	h.coef2 = (int16)(c * c * -4096.0f);

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

/**
 * Decode one channel block: 13-bit scale (bit 15 of the raw word marks the end of the stream),
 * prediction from the two previous samples summed before one shift, 16-bit clamp. Returns
 * false for an end marker.
 */
bool decodeBlock(const byte *block, uint32 samples, int32 coef1, int32 coef2, int32 &hist1, int32 &hist2,
		uint16 key, bool encrypted, int16 *out, uint32 stride) {
	const uint16 rawScale = (uint16)((block[0] << 8) | block[1]);
	if (rawScale & 0x8000)
		return false;
	const int32 scale = ((encrypted ? rawScale ^ key : rawScale) & 0x1fff) + 1; // +1 matters on quiet streams
	const byte *nibbles = block + 2;
	for (uint32 i = 0; i < samples; i++) {
		const byte b = nibbles[i / 2];
		const int sample = kNibbleToInt[(i & 1) ? (b & 0xf) : (b >> 4)];
		const int32 predicted = (coef1 * hist1 + coef2 * hist2) >> 12;
		const int16 o = clamp16(sample * scale + predicted);
		out[i * stride] = o;
		hist2 = hist1;
		hist1 = o;
	}
	return true;
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
			// Loops end exactly at the loop end sample and resume from the state saved at the
			// loop start.
			if (_header.hasLoop && _samplesRead >= _header.loopEnd && !loopBack())
				break;
			if (_bufPos >= _samplesInBuf && !decodeFrame())
				break;

			uint32 framesAvail = _samplesInBuf - _bufPos;
			if (_header.hasLoop)
				framesAvail = MIN(framesAvail, _header.loopEnd - _samplesRead);
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
	bool endOfData() const override { return _ended || (!_header.hasLoop && _samplesRead >= _header.sampleCount); }

	bool rewind() override {
		_stream->seek(_header.streamDataOffset);
		for (int c = 0; c < 2; ++c) {
			_hist1[c] = _header.history[c][0];
			_hist2[c] = _header.history[c][1];
		}
		_key = _header.xorStart;
		_samplesRead = 0;
		_samplesInBuf = 0;
		_bufPos = 0;
		_ended = false;
		_haveSnapshot = false;
		return true;
	}

	bool seek(const Audio::Timestamp &where) override {
		uint32 targetSample = (uint32)where.convertToFramerate(_header.sampleRate).totalNumberOfFrames();
		if (!_header.hasLoop && targetSample >= _header.sampleCount) {
			_samplesRead = _header.sampleCount;
			return true;
		}

		// Decode up to the target so the prediction history and key stream are right.
		rewind();
		while (_samplesRead + _samplesPerFrame <= targetSample) {
			if (!decodeFrame())
				return false;
			_samplesRead += _samplesPerFrame;
			_bufPos = _samplesInBuf;
		}
		const uint32 remainder = targetSample - _samplesRead;
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
	struct Snapshot {
		int64 position = 0;  ///< stream position of the frame holding the loop start
		uint32 sample = 0;   ///< first sample of that frame
		int32 hist1[2] = {0, 0}, hist2[2] = {0, 0};
		uint16 key = 0;
	};

	Common::SeekableReadStream *_stream;
	DisposeAfterUse::Flag _dispose;
	ADXHeader _header;

	uint32 _samplesPerFrame = 0;
	Common::Array<byte> _frameBuf;
	Common::Array<int16> _decoded;
	int32 _hist1[2] = {0, 0};
	int32 _hist2[2] = {0, 0};
	uint16 _key = 0; ///< one key stream, stepped per channel block
	uint32 _samplesRead = 0;
	uint32 _samplesInBuf = 0;
	uint32 _bufPos = 0;
	bool _ended = false;
	bool _haveSnapshot = false;
	Snapshot _snapshot;

	bool loopBack() {
		if (!_haveSnapshot) // a loop start past the loop end; play through instead
			return false;
		_stream->seek(_snapshot.position);
		for (int c = 0; c < 2; ++c) {
			_hist1[c] = _snapshot.hist1[c];
			_hist2[c] = _snapshot.hist2[c];
		}
		_key = _snapshot.key;
		_samplesRead = _snapshot.sample;
		if (!decodeFrame())
			return false;
		_bufPos = _header.loopStart - _snapshot.sample;
		_samplesRead = _header.loopStart;
		return true;
	}

	bool decodeFrame() {
		// The state at the start of the frame holding the loop start is what a loop returns to.
		const uint32 frameStart = _samplesRead; // callers decode once the previous frame is used up
		if (_header.hasLoop && !_haveSnapshot && frameStart <= _header.loopStart && _header.loopStart < frameStart + _samplesPerFrame) {
			_snapshot.position = _stream->pos();
			_snapshot.sample = frameStart;
			for (int c = 0; c < 2; ++c) {
				_snapshot.hist1[c] = _hist1[c];
				_snapshot.hist2[c] = _hist2[c];
			}
			_snapshot.key = _key;
			_haveSnapshot = true;
		}

		if (_stream->read(_frameBuf.data(), _frameBuf.size()) != _frameBuf.size())
			return false;

		for (byte c = 0; c < _header.channelCount; c++) {
			if (!decodeBlock(&_frameBuf[(uint32)c * _header.blockSize], _samplesPerFrame, _header.coef1, _header.coef2,
					_hist1[c], _hist2[c], _key, _header.encrypted, &_decoded[c], _header.channelCount)) {
				_ended = true;
				return false;
			}
			if (_header.encrypted)
				_key = (_key * _header.xorMult + _header.xorAdd) & 0x7fff;
		}

		_samplesInBuf = _samplesPerFrame;
		_bufPos = 0;
		return true;
	}
};

} // namespace

Audio::SeekableAudioStream *makeADXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse) {
	ADXHeader header;
	if (!parseADXHeader(*stream, header) || (header.encrypted && !header.autoKey)) {
		if (header.encrypted && !header.autoKey)
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
	if (header.encrypted && !header.autoKey)
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
	if (header.encrypted && !header.autoKey)
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
	info.sampleCount = h.sampleCount;
	for (int c = 0; c < 2; ++c)
		for (int k = 0; k < 2; ++k)
			info.history[c][k] = h.history[c][k];
	return true;
}

bool decodeADXFrame(const byte *frameData, const ADXFrameInfo &info, int32 hist1[2], int32 hist2[2], int16 *out) {
	for (byte c = 0; c < info.channelCount; c++)
		if (!decodeBlock(frameData + (uint32)c * info.blockSize, info.samplesPerFrame(), info.coef1, info.coef2,
				hist1[c], hist2[c], 0, false, out + c, info.channelCount))
			return false;
	return true;
}

} // namespace Common
