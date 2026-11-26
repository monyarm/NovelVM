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

#ifndef AI5_AUDIO_H
#define AI5_AUDIO_H

#include "common/scummsys.h"

namespace Common {
class SeekableReadStream;
}

namespace AI5 {

enum AudioChannel {
	kAudioChBgm = 0,
	kAudioChSe0 = 1,
	kAudioChSe1 = 2,
	kAudioChSe2 = 3,
	kAudioChVoice0 = 4,
	kAudioChVoice1 = 5,
	kAudioNrChannels
};

enum AudioMixer {
	kMixerMusic,
	kMixerEffect,
	kMixerVoice,
	kNrMixers
};

enum {
	kAudioVolumeMin = -5000,
	kAudioVolumeMax = 0
};

inline bool audioSeChannelValid(uint ch) { return ch < 3; }
inline bool audioVoiceChannelValid(uint ch) { return ch < 2; }

void audioInit();
void audioFini();
void audioUpdate();
/** Play a file; the stream is owned by the channel. */
void audioPlay(AudioChannel ch, Common::SeekableReadStream *file, const char *name, bool checkPlaying);
void audioStop(AudioChannel ch);
/** Set a channel's mixer volume, in hundredths of decibels. */
void audioSetVolume(AudioChannel ch, int vol);
void audioFade(AudioChannel ch, int vol, int t, bool stop, bool sync);
void audioMixerFade(AudioChannel ch, int vol, int t, bool stop, bool sync);
bool audioIsPlaying(AudioChannel ch);
bool audioIsFading(AudioChannel ch);

// convenience functions
void audioBgmPlay(const char *name, bool checkPlaying);
void audioSePlay(const char *name, uint ch);
void audioSyssePlay(const char *name, uint ch);
void audioSeStop(uint ch);
void audioSeFade(int vol, uint t, bool stop, bool sync, uint ch);
void audioVoicePlay(const char *name, uint ch);
void audioVoiceStop(uint ch);
void audioVoicesubPlay(const char *name);
inline void audioVoicesubStop() { audioStop(kAudioChVoice1); }
inline bool audioVoicesubIsPlaying() { return audioIsPlaying(kAudioChVoice1); }

} // End of namespace AI5

#endif // AI5_AUDIO_H
