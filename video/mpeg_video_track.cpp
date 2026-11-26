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

#include "video/mpeg_video_track.h"

#include "common/stream.h"
#include "common/textconsole.h"

#ifdef USE_MPEG2
#include "image/codecs/mpeg.h"
#endif

#ifdef USE_OPENH264
#include "graphics/yuv_to_rgb.h"
#include "wels/codec_api.h"
#endif

namespace Video {

#ifdef USE_MPEG2

MPEGFrameDecoder::MPEGFrameDecoder() : _mpegDecoder(nullptr) {
}

MPEGFrameDecoder::~MPEGFrameDecoder() {
	delete _mpegDecoder;
}

bool MPEGFrameDecoder::decode(Common::SeekableReadStream &packet, uint32 &framePeriod, Graphics::Surface *dst) {
	// Created lazily: Image::MPEGDecoder's constructor queries the active
	// screen (via getDefaultYUVFormat), which is unavailable in headless
	// contexts that only load/inspect a stream. Nothing is decoded until a
	// real packet arrives, where a graphics system exists.
	if (!_mpegDecoder)
		_mpegDecoder = new Image::MPEGDecoder();
	return _mpegDecoder->decodePacket(packet, framePeriod, dst);
}

#endif // USE_MPEG2

#ifdef USE_OPENH264

H264FrameDecoder::H264FrameDecoder() : _surface(nullptr), _haveDimensions(false), _width(0), _height(0) {
	ISVCDecoder *dec = nullptr;
	if (WelsCreateDecoder(&dec) != 0)
		error("H264FrameDecoder: failed to create the OpenH264 decoder");
	_ctx.pDecoder = dec;

	SDecodingParam param;
	memset(&param, 0, sizeof(param));
	param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
	param.eEcActiveIdc = ERROR_CON_SLICE_MV_COPY_CROSS_IDR;
	param.uiTargetDqLayer = UCHAR_MAX;
	if (dec->Initialize(&param) != 0)
		error("H264FrameDecoder: OpenH264 init failed");
}

H264FrameDecoder::~H264FrameDecoder() {
	if (_surface) {
		_surface->free();
		delete _surface;
	}
}

H264FrameDecoder::DecoderContext::DecoderContext() {
	pDecoder = nullptr;
	iLevelSetting = 0;
}

H264FrameDecoder::DecoderContext::~DecoderContext() {
	if (pDecoder)
		WelsDestroyDecoder((ISVCDecoder *)pDecoder);
}

bool H264FrameDecoder::decode(const byte *data, uint32 size, uint16 &outWidth, uint16 &outHeight,
                              const Graphics::PixelFormat &format) {
	if (size == 0)
		return false;

	uint32 oldSize = _pending.size();
	_pending.resize(oldSize + size);
	memcpy(_pending.data() + oldSize, data, size);

	bool gotFrame = false;
	uint32 n = _pending.size();

	// Locate every start code in the buffer.
	Common::Array<uint32> starts;
	for (uint32 p = 0; p + 3 <= n;) {
		if (_pending[p] == 0 && _pending[p + 1] == 0 && _pending[p + 2] == 1) {
			starts.push_back(p);
			p += 3;
		} else if (p + 4 <= n && _pending[p] == 0 && _pending[p + 1] == 0 &&
		           _pending[p + 2] == 0 && _pending[p + 3] == 1) {
			starts.push_back(p);
			p += 4;
		} else {
			p++;
		}
	}

	if (starts.empty()) {
		// No start code yet; keep only a possible partial one (3 bytes).
		if (n > 3) {
			memmove(_pending.data(), _pending.data() + n - 3, 3);
			_pending.resize(3);
		}
		return false;
	}

	// Decode every NAL that is bounded by a following start code. The last
	// NAL is retained because it may continue in the next packet.
	for (uint32 i = 0; i + 1 < starts.size(); i++) {
		uint32 nalStart = starts[i]; // keep the Annex-B start code prefix:
		uint32 nalEnd = starts[i + 1]; // OpenH264's AVC input expects it
		if (nalEnd <= nalStart)
			continue;

		uint8 *outBuf[4] = { nullptr, nullptr, nullptr, nullptr };
		SBufferInfo dstInfo;
		memset(&dstInfo, 0, sizeof(dstInfo));
		DECODING_STATE st = ((ISVCDecoder *)_ctx.pDecoder)->DecodeFrameNoDelay(
			_pending.data() + nalStart, (int)(nalEnd - nalStart), outBuf, &dstInfo);

		if (st != dsFramePending && dstInfo.iBufferStatus == 1 && outBuf[0]) {
			if (!_haveDimensions) {
				_width = (uint16)dstInfo.UsrData.sSystemBuffer.iWidth;
				_height = (uint16)dstInfo.UsrData.sSystemBuffer.iHeight;
				if (_width == 0 || _height == 0)
					_width = 480, _height = 272; // PSP PSMF sanity fallback
				_haveDimensions = true;
			}

			if (!_surface) {
				_surface = new Graphics::Surface();
				_surface->create(_width, _height, format);
			}

			Graphics::YUVToRGBManager &yuv = Graphics::YUVToRGBManager::instance();
			yuv.convert420(_surface, Graphics::YUVToRGBManager::kScaleITU,
			               outBuf[0], outBuf[1], outBuf[2],
			               _width, _height,
			               dstInfo.UsrData.sSystemBuffer.iStride[0],
			               dstInfo.UsrData.sSystemBuffer.iStride[1]);

			gotFrame = true;
		}
	}

	// Retain the trailing (possibly incomplete) NAL.
	uint32 keepFrom = starts.back();
	if (keepFrom > 0) {
		uint32 keep = n - keepFrom;
		memmove(_pending.data(), _pending.data() + keepFrom, keep);
		_pending.resize(keep);
	}

	if (gotFrame) {
		outWidth = _width;
		outHeight = _height;
	}

	return gotFrame;
}

#endif // USE_OPENH264

} // namespace Video
