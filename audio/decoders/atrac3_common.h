/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef AUDIO_DECODERS_ATRAC3_COMMON_H
#define AUDIO_DECODERS_ATRAC3_COMMON_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/types.h"

#include "audio/audiostream.h"

namespace Common {
class SeekableReadStream;
}

namespace Audio {

/**
 * Shared frame-level interface for the two unrelated Sony ATRAC codecs
 * (plain ATRAC3 and ATRAC3+). Both keep synthesis state across frames, so
 * frames must be fed in order. The shared SeekableAudioStream wrapper below
 * (and streaming containers, e.g. PSMF video audio) drive either codec
 * through this interface without duplicating plumbing.
 */
class ATRACFrameDecoder {
public:
	virtual ~ATRACFrameDecoder() {}

	/** (Re)initialise / reset for `channels` and per-frame `blockAlign`. */
	virtual bool init(uint16 channels, uint16 blockAlign) = 0;

	/**
	 * Decode one raw frame (`blockAlign` bytes, no container header) into
	 * `out`, which must hold channels() * frameSamples() interleaved int16
	 * samples. State persists across calls.
	 */
	virtual bool decodeFrame(const byte *frame, uint32 blockAlign, int16 *out) = 0;

	virtual uint16 channels() const = 0;

	/** Decoded samples per channel per frame (1024 for ATRAC3, 2048 for ATRAC3+). */
	virtual int frameSamples() const = 0;
};

/**
 * SeekableAudioStream over a raw ATRAC elementary stream made of fixed
 * `blockAlign`-byte frames. Shared read/seek/rewind/getLength bookkeeping for
 * both codecs; codec specifics live in the supplied decoder. Takes ownership
 * of both `stream` and `decoder`.
 */
class ATRACFrameAudioStream : public SeekableAudioStream {
public:
	ATRACFrameAudioStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
	                      ATRACFrameDecoder *decoder, uint16 channels, uint32 sampleRate,
	                      uint16 blockAlign, uint32 numSamples);
	~ATRACFrameAudioStream() override;

	int readBuffer(int16 *buffer, const int numSamples) override;
	bool isStereo() const override { return _channels == 2; }
	int getRate() const override { return _sampleRate; }
	bool endOfData() const override { return _samplesRead >= _numSamples; }
	bool rewind() override;
	bool seek(const Audio::Timestamp &where) override;
	Audio::Timestamp getLength() const override;

private:
	bool decodeFrame();

	Common::SeekableReadStream *_stream;
	DisposeAfterUse::Flag _dispose;
	ATRACFrameDecoder *_decoder;
	uint16 _channels;
	uint32 _sampleRate;
	uint16 _blockAlign;
	uint32 _numSamples;
	int _frameSamples;
	int64 _dataStart;

	Common::Array<byte> _frameBuf;
	Common::Array<int16> _decoded;

	uint32 _samplesRead;
	uint32 _samplesInBuf;
	uint32 _bufPos;
};

} // namespace Audio

#endif
