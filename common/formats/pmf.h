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

#ifndef FORMAT_PMF_H
#define FORMAT_PMF_H

#include "common/array.h"
#include "common/stream.h"

namespace Common {

// Sony's PSMF ("PMF") container: a fixed 2048-byte header, then a
// standard MPEG-PS stream (video on PES id 0xE0, audio -- ATRAC3 or
// PCM -- on private_stream_1/0xBD). Real byte layout cross-checked
// against two independent, actively-maintained PSP emulators (JPCSP,
// PPSSPP), which agree exactly on every offset, then verified against
// real Code Geass: Lost Colors .pmf files.
struct PMFStream {
	byte streamId = 0;       // 0xE0-0xEF = video (AVC); 0xBD = audio
	byte subStreamId = 0;    // video: sub-index; audio: type/channel bits
	bool isVideo = false;
	bool isAudio = false;

	// Video only.
	uint32 epMapOffset = 0;
	uint32 epMapCount = 0;
	uint16 width = 0;
	uint16 height = 0;
	// Per-type flag bytes right after the stream id pair: video is
	// 0x20/0x21 plus a codec byte -- 0x44 = MPEG-video (SFD-style), other
	// values (0x75/0xFB/0xEF seen in real PSP files) = H.264/AVC.
	byte codecFlag = 0;

	// Audio only.
	byte channelConfig = 0;  // 1 = mono, 2 = stereo
	byte sampleFreqCode = 0; // 2 = 44100Hz in every real sample seen
};

struct PMFEntryPoint {
	byte index = 0;
	byte picOffset = 0;
	uint32 pts = 0;
	uint32 offset = 0; // relative to the start of the MPEG-PS data
};

class PMF {
public:
	PMF(const Common::String &filename);

	bool isValid() const { return _valid; }
	uint16 getWidth() const { return _width; }
	uint16 getHeight() const { return _height; }
	uint32 getMpegOffset() const { return _mpegOffset; }
	uint32 getMpegSize() const { return _mpegSize; }
	const Common::Array<PMFStream> &getStreams() const { return _streams; }

	// Real EP map entries for the first video stream, if it has one
	// (empty in every real Code Geass sample checked -- optional per
	// JPCSP/PPSSPP, not every file uses it).
	const Common::Array<PMFEntryPoint> &getVideoEntryPoints() const { return _videoEntryPoints; }
	const Common::Array<PMFEntryPoint> &videoEntryPoints() const { return _videoEntryPoints; }

	// A stream positioned at the start of the raw MPEG-PS data, ready to
	// hand directly to Video::MPEGPSDecoder::loadStream() -- PMF's inner
	// stream is a normal MPEG-PS stream, same as SFD's.
	Common::SeekableReadStream *createMpegStream() const;

	/**
	 * Extract the PSMF audio elementary stream. Real PSMF audio framing
	 * (verified against JPCSP/PPSSPP's demuxer and the real P3P opening):
	 * private_stream_1 (0xBD) PES payloads each begin with a 4-byte PSP
	 * audio sub-header, followed by ATRAC3+ frames tagged `0F D0`, whose
	 * size is `((c1 & 3) << 8) | (c2 * 8) + 0x10` (c1/c2 are frame bytes
	 * 2/3). The 8 bytes after the tag are a container header; the
	 * remaining `frameSize - 8` bytes are the raw ATRAC3+ frame (matching
	 * the 744-byte blockAlign of a real ATRAC3+ .AT3). Frames are
	 * concatenated across PES packets.
	 *
	 * The codec stream itself is built by the caller (the engine owns the
	 * audio decoder dependency); this only demuxes the container.
	 */
	struct ATRAC3Audio {
		uint16 channels = 0;
		uint32 sampleRate = 0;
		uint16 blockAlign = 0;  // raw bytes per ATRAC3+ frame
		uint32 numSamples = 0;  // per channel
		Common::Array<byte> frames;
		bool valid() const { return !frames.empty() && blockAlign != 0; }
	};
	ATRAC3Audio extractATRAC3Audio() const;

private:
	Common::String _filename;
	bool _valid = false;
	uint16 _width = 0;
	uint16 _height = 0;
	uint32 _mpegOffset = 0;
	uint32 _mpegSize = 0;
	Common::Array<PMFStream> _streams;
	Common::Array<PMFEntryPoint> _videoEntryPoints;

	bool load();
};

} // namespace Common

#endif
