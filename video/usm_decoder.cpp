#include "video/usm_decoder.h"

#ifdef USE_VPX

#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "common/formats/utf_table.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "graphics/yuv_to_rgb.h"
#include "image/codecs/codec.h"

#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>

namespace Video {

namespace {

// Same real chunk sub-header layout as common/formats/usm.cpp (kept as a
// separate small copy here since this decoder needs to walk chunks
// incrementally/lazily rather than eagerly load them).
struct ChunkHeader {
	char tag[5] = {0};
	uint32 size = 0;
	byte payloadOffset = 0;
	uint16 paddingSize = 0;
	byte streamIndex = 0;
	byte payloadType = 0;
};

bool readChunkHeader(Common::SeekableReadStream &f, ChunkHeader &out) {
	if (f.read(out.tag, 4) != 4)
		return false;
	out.tag[4] = '\0';
	out.size = f.readUint32BE();
	if (f.eos() || out.size == 0)
		return false;

	int64 dataStart = f.pos();
	out.payloadOffset = 0;
	out.paddingSize = 0;
	out.streamIndex = 0;
	out.payloadType = 0;

	if (!strcmp(out.tag, "@SFV") || !strcmp(out.tag, "@SFA") ||
	    !strcmp(out.tag, "@ALP") || !strcmp(out.tag, "@SBT") ||
	    !strcmp(out.tag, "CRID")) {
		f.readByte(); // unknown
		out.payloadOffset = f.readByte();
		out.paddingSize = f.readUint16BE();
		out.streamIndex = f.readByte();
		f.skip(2); // unknown
		out.payloadType = f.readByte();
	}

	return f.seek(dataStart + out.size);
}

Common::Array<byte> readChunkPayload(Common::SeekableReadStream &f, int64 chunkDataStart, const ChunkHeader &header) {
	uint32 payloadLen = header.size >= (uint32)header.payloadOffset + header.paddingSize
	                         ? header.size - header.payloadOffset - header.paddingSize
	                         : 0;
	Common::Array<byte> result(payloadLen);
	if (payloadLen == 0)
		return result;
	f.seek(chunkDataStart + header.payloadOffset);
	if (f.read(result.data(), payloadLen) != payloadLen)
		result.clear();
	return result;
}

} // namespace

USMDecoder::USMVideoTrack::USMVideoTrack(uint16 width, uint16 height, const Common::Rational &frameRate) :
		_endOfTrack(false), _curFrame(-1), _width(width), _height(height), _frameRate(frameRate) {
	// The decoder always converts I420 to RGB, so the default output format
	// must be a real YUV-convertible RGB format (2 or 4 bytes/pixel), never
	// CLUT8. A fixed 32-bit BGRA is used rather than
	// Image::Codec::getDefaultYUVFormat(): that helper queries the active
	// screen and crashes when there is no graphics manager (e.g. headless
	// tests). Engines still call setOutputPixelFormat() to get the screen's
	// exact format.
	_pixelFormat = Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24);
}

bool USMDecoder::USMVideoTrack::setOutputPixelFormat(const Graphics::PixelFormat &format) {
	if (format.bytesPerPixel != 2 && format.bytesPerPixel != 4)
		return false;
	_pixelFormat = format;
	return true;
}

USMDecoder::VP9VideoTrack::VP9VideoTrack(uint16 width, uint16 height, const Common::Rational &frameRate) :
		USMVideoTrack(width, height, frameRate) {
	_codec = new vpx_codec_ctx_t();
	if (vpx_codec_dec_init(_codec, vpx_codec_vp9_dx(), nullptr, 0))
		error("USMDecoder: failed to initialize VP9 decoder");
}

USMDecoder::VP9VideoTrack::~VP9VideoTrack() {
	_surface.free();
	vpx_codec_destroy(_codec);
	delete _codec;
}

const Graphics::Surface *USMDecoder::VP9VideoTrack::decodeNextFrame() {
	if (_displayQueue.size()) {
		if (_surface.getPixels())
			_surface.free();
		_surface = _displayQueue.pop();
		_curFrame++;
	}
	return &_surface;
}

bool USMDecoder::VP9VideoTrack::decodeFrame(const byte *data, uint32 size) {
	// Real @SFV data chunks aren't raw VP9 -- CRI wraps them in an IVF
	// framing: the very first chunk starts with a 32-byte IVF file header
	// ("DKIF", "VP90" fourcc, width/height/etc), and every chunk (first
	// included) then carries a 12-byte IVF per-frame record (4-byte LE
	// frame size + 8-byte LE timestamp) ahead of the real VP9 bytes.
	// Confirmed against real mov050.usm bytes: chunk 2 begins with LE
	// u32 103538, and 103538 + 12 == that chunk's real total size.
	if (size >= 32 && !memcmp(data, "DKIF", 4)) {
		uint16 hdrLen = data[6] | (data[7] << 8);
		if (hdrLen > size)
			return false;
		data += hdrLen;
		size -= hdrLen;
	}

	while (size >= 12) {
		uint32 frameSize = data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32)data[3] << 24);
		data += 12;
		size -= 12;
		if (frameSize > size)
			break;

		vpx_codec_err_t err = vpx_codec_decode(_codec, data, frameSize, nullptr, 0);
		if (err) {
			warning("USMDecoder: VP9 frame decode failed: %s", vpx_codec_err_to_string(err));
			return false;
		}

		vpx_codec_iter_t iter = nullptr;
		vpx_image_t *img;
		while ((img = vpx_codec_get_frame(_codec, &iter))) {
			if (img->fmt != VPX_IMG_FMT_I420) {
				warning("USMDecoder: VP9 frame isn't I420, skipping");
				continue;
			}

			Graphics::Surface tmp;
			tmp.create(_width, _height, _pixelFormat);
			YUVToRGBMan.convert420(&tmp, Graphics::YUVToRGBManager::kScaleITU,
			                       img->planes[0], img->planes[1], img->planes[2],
			                       img->d_w, img->d_h, img->stride[0], img->stride[1]);
			_displayQueue.push(tmp);
		}

		data += frameSize;
		size -= frameSize;
	}
	return true;
}

#ifdef USE_OPENH264

USMDecoder::H264VideoTrack::H264VideoTrack(uint16 width, uint16 height, const Common::Rational &frameRate) :
		USMVideoTrack(width, height, frameRate) {
}

USMDecoder::H264VideoTrack::~H264VideoTrack() {
}

const Graphics::Surface *USMDecoder::H264VideoTrack::decodeNextFrame() {
	return _decoder.getSurface();
}

bool USMDecoder::H264VideoTrack::decodeFrame(const byte *data, uint32 size) {
	// Implemented from FFmpeg's libavformat/usmdec.c plus the existing
	// MPEG-PS H.264 decode core (video/mpeg_video_track.h). TO VERIFY LATER
	// with a real H.264 USM sample: this path is NOT yet verified against
	// real @SFV data; VP9 remains the only real-data-verified USM video
	// codec.
	//
	// Real @SFV payloads for VP9 are wrapped in IVF; assume the same outer
	// framing and strip the IVF file header when present. Unlike VP9, the
	// 12-byte IVF per-frame records are deliberately NOT parsed/stripped:
	// H.264 Annex-B start codes may occur at any byte offset, so the record
	// bytes are left in place. The Annex-B scanner drops everything that
	// precedes the first start code, so leading junk is harmless.
	if (size >= 32 && !memcmp(data, "DKIF", 4)) {
		uint16 hdrLen = data[6] | (data[7] << 8);
		if (hdrLen > size)
			return false;
		data += hdrLen;
		size -= hdrLen;
	}

	uint16 width = _width, height = _height;
	if (!_decoder.decode(data, size, width, height, _pixelFormat))
		return false;

	if (width && height) {
		_width = width;
		_height = height;
	}

	_curFrame++;
	return true;
}

#endif // USE_OPENH264

#ifdef USE_MPEG2

USMDecoder::MPEGVideoTrack::MPEGVideoTrack(uint16 width, uint16 height, const Common::Rational &frameRate) :
		USMVideoTrack(width, height, frameRate), _surfaceValid(false) {
}

USMDecoder::MPEGVideoTrack::~MPEGVideoTrack() {
	if (_surfaceValid)
		_surface.free();
}

const Graphics::Surface *USMDecoder::MPEGVideoTrack::decodeNextFrame() {
	return _surfaceValid ? &_surface : nullptr;
}

bool USMDecoder::MPEGVideoTrack::decodeFrame(const byte *data, uint32 size) {
	// Implemented from FFmpeg's libavformat/usmdec.c plus the existing
	// MPEG-PS decode core (video/mpeg_video_track.h). TO VERIFY LATER with
	// a real MPEG-1 USM sample: this path is NOT yet verified against real
	// @SFV data; VP9 remains the only real-data-verified USM video codec.
	//
	// Same IVF outer-framing caveat as the H.264 path above: strip only the
	// IVF file header (if present), not the per-frame records, so MPEG start
	// codes are not corrupted; libmpeg2 skips any leading junk.
	if (size >= 32 && !memcmp(data, "DKIF", 4)) {
		uint16 hdrLen = data[6] | (data[7] << 8);
		if (hdrLen > size)
			return false;
		data += hdrLen;
		size -= hdrLen;
	}

	if (!_surfaceValid) {
		_surface.create(_width, _height, _pixelFormat);
		_surfaceValid = true;
	}

	Common::MemoryReadStream packet(data, size);
	uint32 framePeriod = 0;
	if (!_decoder.decode(packet, framePeriod, &_surface))
		return false;

	_curFrame++;
	return true;
}

#endif // USE_MPEG2

USMDecoder::ADXAudioTrack::ADXAudioTrack(uint32 sampleRate, byte channels, Audio::Mixer::SoundType soundType) :
		AudioTrack(soundType), _haveHeader(false), _frameBufPos(0) {
	_info.sampleRate = sampleRate;
	_info.channelCount = channels;

	byte flags = Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN;
	if (channels == 2)
		flags |= Audio::FLAG_STEREO;
	_audStream = Audio::makePacketizedRawStream(sampleRate, flags);

	_hist1[0] = _hist1[1] = _hist2[0] = _hist2[1] = 0;
}

USMDecoder::ADXAudioTrack::~ADXAudioTrack() {
	delete _audStream;
}

Audio::AudioStream *USMDecoder::ADXAudioTrack::getAudioStream() const {
	return _audStream;
}

bool USMDecoder::ADXAudioTrack::queueChunk(const byte *data, uint32 size) {
	Common::MemoryReadStream chunkStream(data, size);

	if (!_haveHeader) {
		if (!Common::parseADXFrameInfo(chunkStream, _info)) {
			warning("USMDecoder: failed to parse embedded ADX header");
			return false;
		}
		chunkStream.skip(_info.streamDataOffset);
		_frameBuffer.resize(_info.frameSize());
		_haveHeader = true;
	}

	uint32 frameSize = _info.frameSize();
	uint32 sampleBytesPerFrame = _info.samplesPerFrame() * _info.channelCount * sizeof(int16);

	while (!chunkStream.eos()) {
		uint32 avail = (uint32)(chunkStream.size() - chunkStream.pos());
		if (avail == 0)
			break;
		uint32 take = MIN(frameSize - _frameBufPos, avail);
		chunkStream.read(_frameBuffer.data() + _frameBufPos, take);
		_frameBufPos += take;
		if (_frameBufPos < frameSize)
			break;

		int16 *pcm = (int16 *)malloc(sampleBytesPerFrame);
		Common::decodeADXFrame(_frameBuffer.data(), _info, _hist1, _hist2, pcm);
		_audStream->queuePacket(new Common::MemoryReadStream((byte *)pcm, sampleBytesPerFrame, DisposeAfterUse::YES));
		_frameBufPos = 0;
	}

	return true;
}

USMDecoder::USMDecoder() : _stream(nullptr), _videoTrack(nullptr), _audioTrack(nullptr), _hasKey(false) {
}

// TODO: no real USM corpus is available (P5/P5R/P4G are PC/PS4/Vita): only stream index 0 is
// used, @ALP/@SBT streams are detected but not decoded, the MPEG1/H264 codec paths are
// untested on real data, and there is no mechanism to source per-title keys.
void USMDecoder::setDecryptionKey(uint32 key1, uint32 key2) {
	Common::initCRIMask(key1, key2, _mask);
	_hasKey = true;
}

USMDecoder::~USMDecoder() {
	close();
}

void USMDecoder::close() {
	VideoDecoder::close();
	delete _stream;
	_stream = nullptr;
	_videoTrack = nullptr;
	_audioTrack = nullptr;
}

bool USMDecoder::loadStream(Common::SeekableReadStream *stream) {
	close();
	if (!stream)
		return false;

	char magic[4];
	if (stream->read(magic, 4) != 4 || memcmp(magic, "CRID", 4) != 0) {
		warning("USMDecoder: not a real USM file");
		delete stream;
		return false;
	}
	stream->seek(0);
	_stream = stream;

	bool haveVideoMeta = false, haveAudioMeta = false;
	uint16 vidWidth = 0, vidHeight = 0;
	uint32 vidCodec = 0, frNum = 0, frDen = 1;
	uint32 audSampleRate = 0;
	byte audChannels = 0, audCodec = 0;

	// All @UTF metadata chunks for a stream precede that stream's first
	// data chunk (the same leading-header layout FFmpeg's
	// libavformat/usmdec.c walks), so scan the leading metadata and stop at
	// the first payload chunk: @SFV/@SFA give the video/audio info, and
	// @ALP/@SBT merely record that alpha/subtitle streams exist (plus the
	// @UTF width/height/framerate fields when present). Rewind so
	// readNextPacket() resumes exactly at that first data chunk -- nothing
	// is consumed here.
	while (!_stream->eos()) {
		int64 chunkStart = _stream->pos();
		ChunkHeader header;
		if (!readChunkHeader(*_stream, header))
			break;
		int64 chunkDataStart = chunkStart + 8;

		bool isVideo = !strcmp(header.tag, "@SFV");
		bool isAudio = !strcmp(header.tag, "@SFA");
		bool isAlpha = !strcmp(header.tag, "@ALP");
		bool isSubtitle = !strcmp(header.tag, "@SBT");
		bool isMedia = isVideo || isAudio || isAlpha || isSubtitle;

		if (isMedia && header.payloadType == 0) {
			// First data chunk: all leading metadata has been seen.
			_stream->seek(chunkStart);
			break;
		}

		// Only stream index 0 of the video/audio streams is implemented;
		// @ALP/@SBT chunks may use any index, so accept those as-is.
		bool wantedStream = isAlpha || isSubtitle || header.streamIndex == 0;
		if (isMedia && header.payloadType == 1 && wantedStream) {
			Common::Array<byte> payload = readChunkPayload(*_stream, chunkDataStart, header);
			Common::UTFTable table;
			bool parsed = payload.size() >= 4 && table.parse(payload.data(), payload.size());

			if (isVideo && !haveVideoMeta && parsed) {
				vidWidth = (uint16)table.get(0, "width").asUint(0);
				vidHeight = (uint16)table.get(0, "height").asUint(0);
				vidCodec = (uint32)table.get(0, "mpeg_codec").asUint(0);
				frNum = (uint32)table.get(0, "framerate_n").asUint(0);
				frDen = (uint32)table.get(0, "framerate_d").asUint(1);
				haveVideoMeta = true;
			} else if (isAudio && !haveAudioMeta && parsed) {
				audSampleRate = (uint32)table.get(0, "sampling_rate").asUint(0);
				audChannels = (byte)table.get(0, "num_channels").asUint(0);
				audCodec = (byte)table.get(0, "audio_codec").asUint(0);
				haveAudioMeta = true;
			} else if (isAlpha && !_alphaInfo.present) {
				// Presence is recorded even if the @UTF payload can't be
				// parsed, matching FFmpeg marking the stream used before
				// parsing its metadata. Decoding the alpha payload itself is
				// deferred (no real sample to verify against).
				_alphaInfo.present = true;
				if (parsed) {
					_alphaInfo.width = (uint16)table.get(0, "width").asUint(0);
					_alphaInfo.height = (uint16)table.get(0, "height").asUint(0);
					_alphaInfo.framerateNum = (uint32)table.get(0, "framerate_n").asUint(0);
					_alphaInfo.framerateDen = (uint32)table.get(0, "framerate_d").asUint(1);
				}
			} else if (isSubtitle && !_subtitleInfo.present) {
				// Subtitle payload decode is likewise deferred.
				_subtitleInfo.present = true;
				if (parsed) {
					_subtitleInfo.width = (uint16)table.get(0, "width").asUint(0);
					_subtitleInfo.height = (uint16)table.get(0, "height").asUint(0);
					_subtitleInfo.framerateNum = (uint32)table.get(0, "framerate_n").asUint(0);
					_subtitleInfo.framerateDen = (uint32)table.get(0, "framerate_d").asUint(1);
				}
			}
		}

		_stream->seek(chunkDataStart + header.size);
	}

	if (!haveVideoMeta || vidWidth == 0 || vidHeight == 0) {
		warning("USMDecoder: no real video stream found");
		return false;
	}
	// 9 == VP9, 5 == H264, 1 == MPEG1 (see common/formats/usm.h). VP9 is
	// the only real-data-verified USM video codec; the H264/MPEG1 paths
	// reuse the MPEG-PS cores and are TO VERIFY LATER against real samples.
	Common::Rational frameRate(frNum, frDen ? frDen : 1);
	if (vidCodec == 9) {
		_videoTrack = new VP9VideoTrack(vidWidth, vidHeight, frameRate);
	} else if (vidCodec == 5) {
#ifdef USE_OPENH264
		_videoTrack = new H264VideoTrack(vidWidth, vidHeight, frameRate);
#else
		warning("USMDecoder: H.264 video requested, but no OpenH264 support is compiled in");
		return false;
#endif
	} else if (vidCodec == 1) {
#ifdef USE_MPEG2
		_videoTrack = new MPEGVideoTrack(vidWidth, vidHeight, frameRate);
#else
		warning("USMDecoder: MPEG-1 video requested, but no MPEG-2 support is compiled in");
		return false;
#endif
	} else {
		warning("USMDecoder: real video codec %u isn't supported here", vidCodec);
		return false;
	}

	addTrack(_videoTrack);

	if (haveAudioMeta && audCodec == 2 && audSampleRate > 0 && audChannels > 0) { // 2 == ADX
		_audioTrack = new ADXAudioTrack(audSampleRate, audChannels, getSoundType());
		addTrack(_audioTrack);
	} else if (haveAudioMeta) {
		warning("USMDecoder: real audio codec %u isn't ADX (the only one implemented here) -- video only", audCodec);
	}

	return true;
}

void USMDecoder::readNextPacket() {
	if (!_stream)
		return;

	while (!_stream->eos()) {
		int64 chunkStart = _stream->pos();
		ChunkHeader header;
		if (!readChunkHeader(*_stream, header)) {
			_videoTrack->setEndOfTrack();
			return;
		}
		int64 chunkDataStart = chunkStart + 8;

		bool isVideo = !strcmp(header.tag, "@SFV");
		bool isAudio = !strcmp(header.tag, "@SFA");

		if (isVideo && header.streamIndex == 0 && header.payloadType == 0) {
			Common::Array<byte> payload = readChunkPayload(*_stream, chunkDataStart, header);
			_stream->seek(chunkDataStart + header.size);
			if (!payload.empty()) {
				if (_hasKey)
					Common::maskVideoChunk(payload.data(), payload.size(), _mask);
				_videoTrack->decodeFrame(payload.data(), payload.size());
			}
			return;
		} else if (isAudio && header.streamIndex == 0 && header.payloadType == 0 && _audioTrack) {
			Common::Array<byte> payload = readChunkPayload(*_stream, chunkDataStart, header);
			_stream->seek(chunkDataStart + header.size);
			if (!payload.empty()) {
				if (_hasKey)
					Common::maskAudioChunk(payload.data(), payload.size(), _mask);
				_audioTrack->queueChunk(payload.data(), payload.size());
			}
			continue;
		}

		_stream->seek(chunkDataStart + header.size);
	}

	_videoTrack->setEndOfTrack();
}

} // namespace Video

#endif // USE_VPX
