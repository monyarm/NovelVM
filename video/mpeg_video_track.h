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

#ifndef VIDEO_MPEG_VIDEO_TRACK_H
#define VIDEO_MPEG_VIDEO_TRACK_H

#include "common/array.h"
#include "common/scummsys.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

namespace Common {
class SeekableReadStream;
}

namespace Image {
class MPEGDecoder;
}

namespace Video {

// Shared decode cores used by both the MPEG-PS and USM video tracks, so the
// libmpeg2 and OpenH264 decode logic lives in exactly one place instead of
// being duplicated per container.

#ifdef USE_MPEG2
// Thin wrapper around Image::MPEGDecoder; it only feeds a packet and reports
// whether a frame came out. Extracted from MPEGPSDecoder::MPEGVideoTrack.
class MPEGFrameDecoder {
public:
	MPEGFrameDecoder();
	~MPEGFrameDecoder();

	bool decode(Common::SeekableReadStream &packet, uint32 &framePeriod, Graphics::Surface *dst);

private:
	Image::MPEGDecoder *_mpegDecoder;
};
#endif // USE_MPEG2

#ifdef USE_OPENH264
// Annex-B H.264/AVC decode core: accumulates NAL units across calls (a NAL
// frequently spans packets), decodes them with OpenH264, and converts the
// YUV420 output to RGB. Extracted from MPEGPSDecoder::H264VideoTrack, which
// was originally written for PSP PSMF openings.
class H264FrameDecoder {
public:
	H264FrameDecoder();
	~H264FrameDecoder();

	// Feeds Annex-B bytes. Returns true if at least one frame was produced;
	// outWidth/outHeight then hold that frame's dimensions. The decoded
	// surface is owned by this object and returned by getSurface().
	bool decode(const byte *data, uint32 size, uint16 &outWidth, uint16 &outHeight,
	            const Graphics::PixelFormat &format);

	const Graphics::Surface *getSurface() const { return _surface; }

private:
	// Annex-B bytes retained between calls: only complete NALs (bounded by a
	// following start code) are decoded; the trailing one is kept.
	Common::Array<byte> _pending;
	Graphics::Surface *_surface;
	bool _haveDimensions;
	uint16 _width, _height;

	struct DecoderContext {
		void *pDecoder;
		int iLevelSetting;
		DecoderContext();
		~DecoderContext();
	} _ctx;
};
#endif // USE_OPENH264

} // namespace Video

#endif
