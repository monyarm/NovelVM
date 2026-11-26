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

#ifndef FORMAT_ADX_H
#define FORMAT_ADX_H

#include "common/scummsys.h"
#include "common/stream.h"
#include "common/str.h"

namespace Audio {
class SeekableAudioStream;
}

namespace Common {

// Decodes a CRI ADX stream. Returns nullptr on a malformed/unsupported
// file, or an encrypted file with no key. Loops indefinitely if the file
// declares loop points (never signals end-of-data); getLength() still
// reports the one-pass length.
Audio::SeekableAudioStream *makeADXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse);

// As above, decrypting with CRI's "type 9" (keycode) scheme.
Audio::SeekableAudioStream *makeADXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse, uint64 key9, uint16 subkey = 0);

// As above, decrypting with CRI's "type 8" (keystring) scheme.
Audio::SeekableAudioStream *makeADXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse, const Common::String &key8);

// For decoding ADX arriving as a series of chunks rather than one
// seekable file (e.g. ADX embedded in an MPEG-PS video's audio stream --
// see video/mpegps_decoder.cpp). Unencrypted, non-looping only; nothing
// using this needs either.
struct ADXFrameInfo {
	byte channelCount = 0;
	uint32 sampleRate = 0;
	byte blockSize = 0;
	uint32 streamDataOffset = 0;
	int32 coef1 = 0;
	int32 coef2 = 0;
	uint32 sampleCount = 0;
	int32 history[2][2] = {{0, 0}, {0, 0}}; ///< per channel: previous, second previous sample (v4 header)

	uint32 samplesPerFrame() const { return ((uint32)blockSize - 2) * 2; }
	uint32 frameSize() const { return (uint32)blockSize * channelCount; }
};

// Parses a real ADX header from the start of `stream` without consuming
// it (the stream position is restored before returning).
bool parseADXFrameInfo(Common::SeekableReadStream &stream, ADXFrameInfo &info);

// Decodes one frame (info.frameSize() bytes at `frameData`) into `out`
// (info.channelCount * info.samplesPerFrame() interleaved int16 samples).
// hist1/hist2 carry decode state across calls -- seed them from
// info.history before the first frame. Returns false (and decodes nothing)
// for the end-of-stream marker frame.
bool decodeADXFrame(const byte *frameData, const ADXFrameInfo &info, int32 hist1[2], int32 hist2[2], int16 *out);

} // namespace Common

#endif
