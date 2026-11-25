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

#ifndef VIDEO_USM_DECODER_H
#define VIDEO_USM_DECODER_H

#include "common/scummsys.h"	// for USE_VPX

#ifdef USE_VPX

#include "common/formats/adx.h"
#include "common/formats/cri_mask.h"
#include "common/queue.h"
#include "common/rational.h"
#include "graphics/surface.h"
#include "video/mpeg_video_track.h"
#include "video/video_decoder.h"

typedef struct vpx_codec_ctx vpx_codec_ctx_t;

namespace Audio {
class PacketizedAudioStream;
}

namespace Common {
class SeekableReadStream;
}

namespace Video {

/**
 * Decoder for CRI USM (Sofdec2) video: a chunked container (`CRID` file
 * marker, then `@SFV`/`@SFA` per-stream chunks, each either an `@UTF`
 * metadata packet or raw payload data -- see common/formats/usm.h for the
 * real structure facts, cross-checked against FFmpeg's LGPL
 * libavformat/usmdec.c).
 *
 * Video: VP9 via libvpx is the only real-data-verified codec (Persona 5
 * Royal's mov050.usm). H264 (`mpeg_codec` 5) and MPEG1 (`mpeg_codec` 1)
 * are also accepted, decoding through the same shared cores the MPEG-PS
 * decoder uses (video/mpeg_video_track.h). Those two paths are implemented
 * from FFmpeg's libavformat/usmdec.c plus the existing MPEGPS logic and are
 * NOT yet verified against a real H264/MPEG1 USM sample -- TO VERIFY LATER.
 * Audio: CRI ADX, decoded incrementally the same way SFD's ADXAudioTrack
 * (video/mpegps_decoder.h) does -- USM's real audio chunking is the same
 * "first chunk is the ADX header, later chunks are raw frame data" layout.
 */
class USMDecoder : public VideoDecoder {
public:
	USMDecoder();
	~USMDecoder() override;

	bool loadStream(Common::SeekableReadStream *stream) override;
	void close() override;

	// Some titles (e.g. Persona 5 Royal) encrypt @SFV/@SFA data chunks
	// with CRI's per-title Sofdec2 XOR scheme (see common/formats/cri_mask.h).
	// Call before loadStream() if the real key is known; the real key
	// value itself is never hardcoded/committed in this repository, it
	// must come from the caller (e.g. extracted from the game's exe, or
	// supplied via a local, non-committed key file/config).
	void setDecryptionKey(uint32 key1, uint32 key2);

	// @ALP (alpha) and @SBT (subtitle) chunks are recognized by the chunk
	// header reader but their payloads aren't decoded here: no real sample
	// was available to verify a decoder against, so decode is deferred.
	// loadStream()'s metadata scan does record whether such streams exist,
	// plus the alpha stream's @UTF width/height/framerate fields (the same
	// fields FFmpeg's libavformat/usmdec.c reads for its ALPHAI channel), so
	// callers can distinguish a video-only file from one with alpha or
	// subtitle streams.
	bool hasAlphaStream() const { return _alphaInfo.present; }
	bool hasSubtitleStream() const { return _subtitleInfo.present; }
	uint16 getAlphaWidth() const { return _alphaInfo.width; }
	uint16 getAlphaHeight() const { return _alphaInfo.height; }
	Common::Rational getAlphaFrameRate() const {
		return Common::Rational(_alphaInfo.framerateNum, _alphaInfo.framerateDen ? _alphaInfo.framerateDen : 1);
	}

protected:
	void readNextPacket() override;

private:
	// Shared base for the codec-specific @SFV video tracks. Frame pacing
	// comes from the @UTF framerate_n/d fields, exactly as VP9VideoTrack
	// first did; each concrete track only supplies decodeFrame().
	class USMVideoTrack : public FixedRateVideoTrack {
	public:
		USMVideoTrack(uint16 width, uint16 height, const Common::Rational &frameRate);
		~USMVideoTrack() override {}

		bool endOfTrack() const override { return _endOfTrack; }
		uint16 getWidth() const override { return _width; }
		uint16 getHeight() const override { return _height; }
		Graphics::PixelFormat getPixelFormat() const override { return _pixelFormat; }
		bool setOutputPixelFormat(const Graphics::PixelFormat &format) override;
		int getCurFrame() const override { return _curFrame; }

		virtual bool decodeFrame(const byte *data, uint32 size) = 0;
		void setEndOfTrack() { _endOfTrack = true; }

	protected:
		Common::Rational getFrameRate() const override { return _frameRate; }

		bool _endOfTrack;
		int _curFrame;
		uint16 _width, _height;
		Graphics::PixelFormat _pixelFormat;
		Common::Rational _frameRate;
	};

	// VP9 via libvpx -- the only real-data-verified USM video codec (real
	// Persona 5 Royal mov050.usm).
	class VP9VideoTrack : public USMVideoTrack {
	public:
		VP9VideoTrack(uint16 width, uint16 height, const Common::Rational &frameRate);
		~VP9VideoTrack() override;

		const Graphics::Surface *decodeNextFrame() override;
		bool decodeFrame(const byte *data, uint32 size) override;

	private:
		Common::Queue<Graphics::Surface> _displayQueue;
		Graphics::Surface _surface;

		vpx_codec_ctx_t *_codec;
	};

	// H.264/AVC via OpenH264, reusing the shared Annex-B decode core
	// (video/mpeg_video_track.h) that also backs the MPEG-PS H.264 track.
	//
	// TO VERIFY LATER: implemented from FFmpeg's libavformat/usmdec.c and
	// the existing MPEGPS decode logic; NOT yet verified against a real
	// H.264 USM sample. VP9 remains the only real-data-verified USM video
	// codec.
#ifdef USE_OPENH264
	class H264VideoTrack : public USMVideoTrack {
	public:
		H264VideoTrack(uint16 width, uint16 height, const Common::Rational &frameRate);
		~H264VideoTrack() override;

		const Graphics::Surface *decodeNextFrame() override;
		bool decodeFrame(const byte *data, uint32 size) override;

	private:
		H264FrameDecoder _decoder;
	};
#endif

	// MPEG-1 via libmpeg2, reusing the shared decode core
	// (video/mpeg_video_track.h) that also backs the MPEG-PS MPEG track.
	//
	// TO VERIFY LATER: implemented from FFmpeg's libavformat/usmdec.c and
	// the existing MPEGPS decode logic; NOT yet verified against a real
	// MPEG-1 USM sample. VP9 remains the only real-data-verified USM video
	// codec.
#ifdef USE_MPEG2
	class MPEGVideoTrack : public USMVideoTrack {
	public:
		MPEGVideoTrack(uint16 width, uint16 height, const Common::Rational &frameRate);
		~MPEGVideoTrack() override;

		const Graphics::Surface *decodeNextFrame() override;
		bool decodeFrame(const byte *data, uint32 size) override;

	private:
		MPEGFrameDecoder _decoder;
		Graphics::Surface _surface;
		bool _surfaceValid;
	};
#endif

	// Mirrors video/mpegps_decoder.h's ADXAudioTrack (same real chunked
	// ADX layout); kept as a separate small copy rather than a shared
	// class since the two containers are otherwise unrelated.
	class ADXAudioTrack : public AudioTrack {
	public:
		ADXAudioTrack(uint32 sampleRate, byte channels, Audio::Mixer::SoundType soundType);
		~ADXAudioTrack() override;

		bool queueChunk(const byte *data, uint32 size);

	protected:
		Audio::AudioStream *getAudioStream() const override;

	private:
		Common::ADXFrameInfo _info;
		Audio::PacketizedAudioStream *_audStream;
		bool _haveHeader;

		Common::Array<byte> _frameBuffer;
		uint32 _frameBufPos;
		int32 _hist1[2];
		int32 _hist2[2];
	};

	bool readChunk();

	// Minimal per-stream metadata captured during loadStream()'s leading
	// @UTF scan. Only presence (and, when the table exposes them, these
	// fields) is stored; no alpha/subtitle payload is decoded.
	struct StreamMetadata {
		bool present = false;
		uint16 width = 0;
		uint16 height = 0;
		uint32 framerateNum = 0;
		uint32 framerateDen = 1;
	};

	Common::SeekableReadStream *_stream;
	USMVideoTrack *_videoTrack;
	ADXAudioTrack *_audioTrack;

	bool _hasKey;
	Common::CRIMask _mask;

	StreamMetadata _alphaInfo;
	StreamMetadata _subtitleInfo;
};

} // namespace Video

#endif // USE_VPX

#endif
