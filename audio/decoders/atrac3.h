/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
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

// The decode algorithm (spectral/tonal component decoding, gain
// compensation, QMF synthesis filterbank, joint-stereo matrixing) is
// ported from FFmpeg's libavcodec/atrac3.c and atrac.c, real facts/offsets
// only, no code copied verbatim. Those files are:
//   Copyright (c) 2006-2008 Maxim Poliakovski
//   Copyright (c) 2006-2008 Benjamin Larsson
// and licensed under the GNU Lesser General Public License, version 2.1 or
// later -- see LICENSES/COPYING.LGPL in this repository for the full text.

#ifndef AUDIO_DECODERS_ATRAC3_H
#define AUDIO_DECODERS_ATRAC3_H

#include "common/scummsys.h"
#include "common/types.h"

#include "audio/decoders/atrac3_common.h"

namespace Common {
class SeekableReadStream;
}

namespace Audio {

class SeekableAudioStream;

// Sony's two real ATRAC3 channel coding modes, as stored in a real AT3
// WAVE-format fmt chunk's extradata (see common/formats/at3.h).
enum ATRAC3CodingMode {
	kATRAC3CodingSingle = 0x2,
	kATRAC3CodingJointStereo = 0x12
};

/**
 * Stateful frame-level decoder for plain ATRAC3 (not ATRAC3+ -- a different,
 * unrelated codec). Shares the ATRACFrameDecoder interface with ATRAC3+ so
 * containers and the shared stream wrapper can drive either codec.
 */
class ATRAC3FrameDecoder : public ATRACFrameDecoder {
public:
	explicit ATRAC3FrameDecoder(ATRAC3CodingMode codingMode);
	~ATRAC3FrameDecoder() override;

	bool init(uint16 channels, uint16 blockAlign) override;
	bool decodeFrame(const byte *frame, uint32 blockAlign, int16 *out) override;
	uint16 channels() const override { return _channels; }
	int frameSamples() const override;

private:
	struct Impl;
	Impl *_impl;
	uint16 _channels;
};

/**
 * Decodes a real ATRAC3 (not ATRAC3+ -- a different, unrelated codec)
 * elementary stream, given the real per-file parameters an AT3/WAV
 * container's fmt chunk provides. `blockAlign` is the real per-frame byte
 * size (one block decodes to 1024 samples per channel). `numSamples` is
 * the real total per-channel sample count, as derived by the container
 * (e.g. from a WAVE "fact" chunk or the data chunk size).
 */
SeekableAudioStream *makeATRAC3Stream(
	Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse,
	uint16 channels, uint32 sampleRate, uint16 blockAlign, ATRAC3CodingMode codingMode,
	uint32 numSamples);

} // namespace Audio

#endif
