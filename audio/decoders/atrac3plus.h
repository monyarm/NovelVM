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

// The decode algorithm (word-length/scale-factor/code-table bitstream
// decode, spectral Huffman decode, gain compensation, GHA sine-wave tone
// synthesis, noise/power compensation, and the 16-band polyphase (PQF)
// synthesis filter) is ported from FFmpeg's libavcodec/atrac3plus.c,
// atrac3plusdec.c and atrac3plusdsp.c -- real facts/formulas only, no code
// copied verbatim. Those files are:
//   Copyright (c) 2010-2013 Maxim Poliakovski
// and licensed under the GNU Lesser General Public License, version 2.1 or
// later -- see LICENSES/COPYING.LGPL in this repository for the full text.

#ifndef AUDIO_DECODERS_ATRAC3PLUS_H
#define AUDIO_DECODERS_ATRAC3PLUS_H

#include "common/scummsys.h"
#include "common/types.h"

#include "audio/decoders/atrac3_common.h"

namespace Common {
class SeekableReadStream;
}

namespace Audio {

class SeekableAudioStream;

/**
 * Stateful frame-level ATRAC3+ decoder.
 *
 * The ATRAC3+ pipeline carries synthesis state (per-channel-unit window gain,
 * GHA tone history, IPQF polyphase filter memory, previous-frame buffers)
 * across frames, so frames must be fed in order. This is shared by the
 * whole-stream `makeATRAC3PlusStream()` and by streaming containers that need
 * to decode one frame at a time (e.g. PSP PSMF video audio tracks).
 */
class ATRAC3PlusFrameDecoder : public ATRACFrameDecoder {
public:
	ATRAC3PlusFrameDecoder();
	~ATRAC3PlusFrameDecoder() override;

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
 * Decodes a real ATRAC3+ elementary stream (a different, unrelated codec
 * from plain ATRAC3, despite the similar name and shared container).
 * `channels` must be 1-4, 6, 7 or 8 (5 is not a real supported channel
 * count -- matches the reference decoder). `numSamples` is the real total
 * per-channel sample count, as derived by the container.
 */
SeekableAudioStream *makeATRAC3PlusStream(
	Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse,
	uint16 channels, uint32 sampleRate, uint16 blockAlign, uint32 numSamples);

} // namespace Audio

#endif
