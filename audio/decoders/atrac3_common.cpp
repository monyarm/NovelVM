#include "audio/decoders/atrac3_common.h"

#include "common/stream.h"
#include "common/util.h"

#include <string.h>

namespace Audio {

ATRACFrameAudioStream::ATRACFrameAudioStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
                                             ATRACFrameDecoder *decoder, uint16 channels, uint32 sampleRate,
                                             uint16 blockAlign, uint32 numSamples)
	: _stream(stream), _dispose(dispose), _decoder(decoder), _channels(channels),
	  _sampleRate(sampleRate), _blockAlign(blockAlign), _numSamples(numSamples),
	  _frameSamples(decoder->frameSamples()), _dataStart(0),
	  _samplesRead(0), _samplesInBuf(0), _bufPos(0) {
	_dataStart = _stream->pos();
	_frameBuf.resize(_blockAlign);
	_decoded.resize((uint32)_channels * _frameSamples);
	_decoder->init(_channels, _blockAlign);
	rewind();
}

ATRACFrameAudioStream::~ATRACFrameAudioStream() {
	delete _decoder;
	if (_dispose == DisposeAfterUse::YES)
		delete _stream;
}

bool ATRACFrameAudioStream::decodeFrame() {
	if (_stream->read(_frameBuf.data(), _blockAlign) != _blockAlign)
		return false;
	if (!_decoder->decodeFrame(_frameBuf.data(), _blockAlign, _decoded.data()))
		return false;
	_samplesInBuf = _frameSamples;
	_bufPos = 0;
	return true;
}

int ATRACFrameAudioStream::readBuffer(int16 *buffer, const int numSamples) {
	int samples = 0;
	while ((numSamples - samples) >= _channels && !endOfData()) {
		if (_bufPos >= _samplesInBuf && !decodeFrame())
			break;

		uint32 framesAvail = _samplesInBuf - _bufPos;
		uint32 framesWanted = (uint32)(numSamples - samples) / _channels;
		uint32 toCopy = MIN(framesAvail, framesWanted);
		if (toCopy == 0)
			break;

		memcpy(buffer, &_decoded[_bufPos * _channels], toCopy * _channels * sizeof(int16));
		buffer += toCopy * _channels;
		samples += toCopy * _channels;
		_bufPos += toCopy;
		_samplesRead += toCopy;
	}
	return samples;
}

bool ATRACFrameAudioStream::rewind() {
	_stream->seek(_dataStart);
	_samplesRead = 0;
	_samplesInBuf = 0;
	_bufPos = 0;
	_decoder->init(_channels, _blockAlign);
	return true;
}

bool ATRACFrameAudioStream::seek(const Audio::Timestamp &where) {
	uint32 targetSample = (uint32)where.convertToFramerate(_sampleRate).totalNumberOfFrames();
	if (targetSample >= _numSamples) {
		rewind();
		_samplesRead = _numSamples;
		return true;
	}

	rewind();
	uint32 frame = targetSample / _frameSamples;
	_stream->seek(_dataStart + (int64)frame * _blockAlign);
	_samplesRead = frame * _frameSamples;

	uint32 remainder = targetSample - _samplesRead;
	if (remainder > 0) {
		if (!decodeFrame())
			return false;
		_bufPos = remainder;
		_samplesRead += remainder;
	}
	return true;
}

Audio::Timestamp ATRACFrameAudioStream::getLength() const {
	return Audio::Timestamp(0, _numSamples, _sampleRate);
}

} // namespace Audio
