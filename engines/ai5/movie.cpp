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
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

#include "common/system.h"
#include "common/textconsole.h"

#include "audio/audiostream.h"
#include "audio/decoders/wave.h"
#include "graphics/surface.h"
#include "video/avi_decoder.h"

#include "ai5/gfx.h"
#include "ai5/movie.h"

namespace AI5 {

Movie::Movie() : _hasAudio(false), _videoFinished(false), _startTime(0), _baseFrame(0), _baseTime(0) {
}

Movie::~Movie() {
	g_system->getMixer()->stopHandle(_audioHandle);
}

bool Movie::load(Common::SeekableReadStream *video, Common::SeekableReadStream *audio) {
	_video.reset(new Video::AVIDecoder());
	if (!_video->loadStream(video)) {
		delete audio;
		return false;
	}

	if (audio) {
		Audio::SeekableAudioStream *stream = Audio::makeWAVStream(audio, DisposeAfterUse::YES);
		if (!stream) {
			warning("Failed to decode movie audio");
		} else {
			// started paused, so play() can start audio and video together
			g_system->getMixer()->playStream(Audio::Mixer::kSFXSoundType, &_audioHandle, stream, -1,
					Audio::Mixer::kMaxChannelVolume, 0, DisposeAfterUse::YES, false, false);
			g_system->getMixer()->pauseHandle(_audioHandle, true);
			_hasAudio = true;
		}
	}
	return true;
}

void Movie::play(int volume) {
	// volume is a percentage
	if (_hasAudio) {
		g_system->getMixer()->setChannelVolume(_audioHandle, CLIP(volume, 0, 100) * Audio::Mixer::kMaxChannelVolume / 100);
		g_system->getMixer()->pauseHandle(_audioHandle, false);
	}
	_video->start();
	_startTime = g_system->getMillis();
}

int Movie::getPosition() const {
	return _startTime ? (int)(g_system->getMillis() - _startTime) : 0;
}

bool Movie::seekVideo(uint frame) {
	if (!_video->seekToFrame(frame)) {
		warning("Movie: failed to seek to frame %u", frame);
		return false;
	}
	_baseFrame = frame;
	_baseTime = getPosition();
	_videoFinished = false;
	return true;
}

bool Movie::isEnd() const {
	bool audioPlaying = _hasAudio && g_system->getMixer()->isSoundHandleActive(_audioHandle);
	return _videoFinished && !audioPlaying;
}

int Movie::draw(GfxSurface *dst) {
	if (_videoFinished)
		return 0;

	// the frame that should be on screen now
	uint32 frames = _video->getFrameCount();
	uint32 duration = _video->getDuration().msecs();
	if (!frames || !duration)
		return -1;
	uint target = _baseFrame + (uint)((uint64)(getPosition() - _baseTime) * frames / duration);
	if (_video->getCurFrame() >= (int)target)
		return 0;

	const Graphics::Surface *frame = nullptr;
	while (_video->getCurFrame() < (int)target) {
		if (_video->endOfVideo()) {
			_videoFinished = true;
			break;
		}
		int prev = _video->getCurFrame();
		// empty chunks repeat the previous frame and decode to nullptr
		const Graphics::Surface *f = _video->decodeNextFrame();
		if (f)
			frame = f;
		if (_video->getCurFrame() == prev)
			return -1;
	}
	if (!frame)
		return 0;

	const Graphics::PixelFormat &fmt = frame->format;
	int w = MIN<int>(frame->w, dst->w);
	int h = MIN<int>(frame->h, dst->h);
	for (int y = 0; y < h; y++) {
		byte *out = gfxPixel(dst, 0, y);
		for (int x = 0; x < w; x++, out += 3) {
			uint32 c = frame->getPixel(x, y);
			if (fmt.isCLUT8()) {
				const byte *pal = _video->getPalette();
				out[0] = pal[c * 3];
				out[1] = pal[c * 3 + 1];
				out[2] = pal[c * 3 + 2];
			} else {
				fmt.colorToRGB(c, out[0], out[1], out[2]);
			}
		}
	}
	return 1;
}

} // End of namespace AI5
