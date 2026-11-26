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

#ifndef AI5_MOVIE_H
#define AI5_MOVIE_H

#include "common/ptr.h"
#include "common/stream.h"

#include "audio/mixer.h"

namespace Video {
class VideoDecoder;
}

namespace AI5 {

struct GfxSurface;

/**
 * An AVI movie with an optional separate audio file, timed by its own clock
 * so that the video can be seeked while the audio keeps playing.
 */
class Movie {
public:
	Movie();
	~Movie();

	bool load(Common::SeekableReadStream *video, Common::SeekableReadStream *audio);
	void play(int volume);
	/**
	 * Draw the frame for the current time into an RGB24 surface.
	 * Returns 1 if a new frame was drawn, 0 if not, -1 on error.
	 */
	int draw(GfxSurface *dst);
	bool isEnd() const;
	/** Continue the video from the given frame without changing the time. */
	bool seekVideo(uint frame);
	/** Milliseconds since play(). */
	int getPosition() const;

private:
	Common::ScopedPtr<Video::VideoDecoder> _video;
	Audio::SoundHandle _audioHandle;
	bool _hasAudio;
	bool _videoFinished;
	uint32 _startTime;
	// the video plays frame _baseFrame at time _baseTime
	uint _baseFrame;
	int _baseTime;
};

} // End of namespace AI5

#endif // AI5_MOVIE_H
