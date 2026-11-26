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

#include "audio/audiostream.h"
#include "audio/decoders/mp3.h"
#include "audio/decoders/raw.h"
#include "audio/decoders/vorbis.h"
#include "audio/decoders/wave.h"
#include "audio/mixer.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "ai5/ai5.h"
#include "ai5/asset.h"
#include "ai5/audio.h"
#include "ai5/vm.h"

namespace AI5 {

struct Fade {
	bool fading;
	bool stop;
	uint32 startT;
	uint32 duration;
	float startVolume;
	float endVolume;
};

struct Channel {
	AudioMixer mixer;
	Audio::SoundHandle handle;
	Common::String fileName;
	bool active;
	float volume; // 0..1
	Fade fade;
};

struct MixerGroup {
	float volume; // 0..1
	Fade fade;
};

static Channel g_channels[kAudioNrChannels];
static MixerGroup g_mixers[kNrMixers];

static Audio::Mixer::SoundType soundType(AudioMixer m) {
	switch (m) {
	case kMixerMusic:
		return Audio::Mixer::kMusicSoundType;
	case kMixerVoice:
		return Audio::Mixer::kSpeechSoundType;
	default:
		return Audio::Mixer::kSFXSoundType;
	}
}

void audioInit() {
	static const AudioMixer mixers[kAudioNrChannels] = {
		kMixerMusic, kMixerEffect, kMixerEffect, kMixerEffect, kMixerVoice, kMixerVoice
	};
	for (int i = 0; i < kAudioNrChannels; i++) {
		g_channels[i].mixer = mixers[i];
		g_channels[i].active = false;
		g_channels[i].volume = 1.0f;
		g_channels[i].fade.fading = false;
	}
	for (int i = 0; i < kNrMixers; i++) {
		g_mixers[i].volume = 1.0f;
		g_mixers[i].fade.fading = false;
	}
}

void audioFini() {
	g_system->getMixer()->stopAll();
}

static void applyVolume(Channel &ch) {
	if (!ch.active)
		return;
	float v = ch.volume * g_mixers[ch.mixer].volume;
	g_system->getMixer()->setChannelVolume(ch.handle, CLIP<int>(v * Audio::Mixer::kMaxChannelVolume, 0, 255));
}

static void channelStop(Channel &ch) {
	if (ch.active)
		g_system->getMixer()->stopHandle(ch.handle);
	ch.active = false;
	ch.fileName.clear();
	ch.fade.fading = false;
}

// Volume is given in hundredths of decibels, from -5000 to 0; convert to a
// linear 0..100 scale.
static int getLinearVolume(int vol) {
	vol = CLIP(vol, -5000, 0);
	return (int)floorf(powf(10.f, (float)vol / 2000.f) * 100 + 0.5f);
}

// Parse the loop of a WAV file's smpl chunk, in sample frames.
static bool wavLoop(Common::SeekableReadStream &s, uint32 &start, uint32 &end, uint32 &count) {
	s.seek(12);
	while (s.pos() + 8 <= s.size()) {
		uint32 id = s.readUint32BE();
		uint32 size = s.readUint32LE();
		uint32 next = s.pos() + size + (size & 1);
		if (id == MKTAG('s', 'm', 'p', 'l') && size >= 36 + 24) {
			s.skip(28);
			uint32 nrLoops = s.readUint32LE();
			s.skip(4);
			if (!nrLoops)
				return false;
			s.skip(8); // id, type
			start = s.readUint32LE();
			end = s.readUint32LE() + 1;
			s.skip(4); // fraction
			count = s.readUint32LE();
			return true;
		}
		s.seek(next);
	}
	return false;
}

static Audio::AudioStream *openStream(Common::SeekableReadStream *file, const char *name) {
	byte magic[4] = {};
	file->read(magic, 4);
	file->seek(0);

	uint32 loopStart = 0, loopEnd = 0, loopCount = 1;
	Audio::SeekableAudioStream *stream = nullptr;

	ArcArchive::Type arcType;
	const ArcArchive::Entry *awd = assetVoiceEntry(name, arcType);
	if (awd && (arcType == ArcArchive::kTypeAwd || arcType == ArcArchive::kTypeAwf) && awd->awdType == ArcArchive::kAwdPcm) {
		// raw 16-bit mono PCM; loop points are in the archive
		stream = Audio::makeRawStream(file, 44100, Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN, DisposeAfterUse::YES);
		if (arcType == ArcArchive::kTypeAwd && awd->loopStart != 0xffffffff) {
			loopStart = awd->loopStart;
			loopEnd = awd->loopEnd;
			loopCount = 0;
		}
	} else if (!memcmp(magic, "RIFF", 4)) {
		wavLoop(*file, loopStart, loopEnd, loopCount);
		file->seek(0);
		stream = Audio::makeWAVStream(file, DisposeAfterUse::YES);
	} else if (!memcmp(magic, "OggS", 4)) {
#ifdef USE_VORBIS
		stream = Audio::makeVorbisStream(file, DisposeAfterUse::YES);
#else
		warning("Ogg Vorbis support not compiled in: %s", name);
#endif
	} else {
#ifdef USE_MAD
		stream = Audio::makeMP3Stream(file, DisposeAfterUse::YES);
#else
		warning("MP3 support not compiled in: %s", name);
#endif
	}
	if (!stream) {
		warning("Failed to open audio file: %s", name);
		return nullptr;
	}

	if (loopStart != loopEnd) {
		uint rate = stream->getRate();
		return Audio::makeLoopingAudioStream(stream, Audio::Timestamp(0, loopStart, rate),
				Audio::Timestamp(0, loopEnd, rate), loopCount);
	}
	return stream;
}

void audioPlay(AudioChannel chNo, Common::SeekableReadStream *file, const char *name, bool checkPlaying) {
	Channel &ch = g_channels[chNo];
	if (checkPlaying && ch.fileName.equals(name) && audioIsPlaying(chNo)) {
		delete file;
		return;
	}

	channelStop(ch);
	Audio::AudioStream *stream = openStream(file, name);
	if (!stream)
		return;

	g_system->getMixer()->playStream(soundType(ch.mixer), &ch.handle, stream);
	ch.active = true;
	ch.volume = 1.0f;
	ch.fileName = name;
	applyVolume(ch);
}

void audioStop(AudioChannel ch) {
	channelStop(g_channels[ch]);
}

void audioSetVolume(AudioChannel ch, int vol) {
	MixerGroup &m = g_mixers[g_channels[ch].mixer];
	m.volume = getLinearVolume(vol) / 100.0f;
	m.fade.fading = false;
	for (int i = 0; i < kAudioNrChannels; i++)
		applyVolume(g_channels[i]);
}

static void startFade(Fade &f, float from, int vol, int t, bool stop) {
	f.fading = true;
	f.stop = stop;
	f.startT = g_system->getMillis();
	f.duration = t;
	f.startVolume = from;
	f.endVolume = CLIP(getLinearVolume(vol) / 100.0f, 0.0f, 1.0f);
}

void audioFade(AudioChannel chNo, int vol, int t, bool stop, bool sync) {
	Channel &ch = g_channels[chNo];
	if (!ch.active)
		return;
	if ((int)(ch.volume * 100 + 0.5f) == getLinearVolume(vol))
		return;

	startFade(ch.fade, ch.volume, vol, t, stop);
	if (sync) {
		while (audioIsFading(chNo) && !g_engine->shouldQuit()) {
			vmPeek();
			vmDelay(16);
		}
	}
}

void audioMixerFade(AudioChannel chNo, int vol, int t, bool stop, bool sync) {
	MixerGroup &m = g_mixers[g_channels[chNo].mixer];
	if ((int)(m.volume * 100 + 0.5f) == getLinearVolume(vol))
		return;

	startFade(m.fade, m.volume, vol, t, stop);
	if (sync) {
		while (m.fade.fading && !g_engine->shouldQuit()) {
			vmPeek();
			vmDelay(16);
		}
	}
}

// returns the faded volume; clears the fade when done
static float stepFade(Fade &f, bool &done) {
	uint32 elapsed = g_system->getMillis() - f.startT;
	done = elapsed >= f.duration;
	if (done) {
		f.fading = false;
		return f.stop ? 0.0f : f.endVolume;
	}
	float progress = (float)elapsed / (float)f.duration;
	return f.startVolume + (f.endVolume - f.startVolume) * progress;
}

void audioUpdate() {
	for (int i = 0; i < kNrMixers; i++) {
		MixerGroup &m = g_mixers[i];
		if (!m.fade.fading)
			continue;
		bool done;
		bool stop = m.fade.stop;
		m.volume = stepFade(m.fade, done);
		if (done && stop) {
			m.volume = m.fade.endVolume;
			for (int c = 0; c < kAudioNrChannels; c++) {
				if (g_channels[c].mixer == (AudioMixer)i)
					channelStop(g_channels[c]);
			}
		}
	}
	for (int i = 0; i < kAudioNrChannels; i++) {
		Channel &ch = g_channels[i];
		if (ch.active && ch.fade.fading) {
			bool done;
			bool stop = ch.fade.stop;
			ch.volume = stepFade(ch.fade, done);
			if (done && stop) {
				channelStop(ch);
				continue;
			}
		}
		applyVolume(ch);
	}
}

bool audioIsPlaying(AudioChannel ch) {
	return g_channels[ch].active && g_system->getMixer()->isSoundHandleActive(g_channels[ch].handle);
}

bool audioIsFading(AudioChannel ch) {
	return g_channels[ch].active && g_channels[ch].fade.fading;
}

void audioBgmPlay(const char *name, bool checkPlaying) {
	Common::SeekableReadStream *file = assetBgmLoad(name);
	if (!file) {
		warning("Failed to load BGM file: %s", name);
		return;
	}
	audioPlay(kAudioChBgm, file, name, checkPlaying);
}

void audioSePlay(const char *name, uint ch) {
	if (!audioSeChannelValid(ch)) {
		warning("Invalid SE channel: %u", ch);
		return;
	}
	Common::SeekableReadStream *file = assetEffectLoad(name);
	if (!file) {
		warning("Failed to load SE file: %s", name);
		return;
	}
	audioPlay((AudioChannel)(kAudioChSe0 + ch), file, name, false);
}

void audioSyssePlay(const char *name, uint ch) {
	if (!audioSeChannelValid(ch)) {
		warning("Invalid SE channel: %u", ch);
		return;
	}
	Common::SeekableReadStream *file = assetLoad(kAssetSysse, name);
	if (!file) {
		warning("Failed to load SYSSE file: %s", name);
		return;
	}
	audioPlay((AudioChannel)(kAudioChSe0 + ch), file, name, false);
}

void audioVoicePlay(const char *name, uint ch) {
	if (!audioVoiceChannelValid(ch)) {
		warning("Invalid voice channel: %u", ch);
		return;
	}
	Common::SeekableReadStream *file = assetVoiceLoad(name);
	if (!file) {
		warning("Failed to load voice file: %s", name);
		return;
	}
	audioPlay((AudioChannel)(kAudioChVoice0 + ch), file, name, false);
}

void audioVoiceStop(uint ch) {
	if (!audioVoiceChannelValid(ch)) {
		warning("Invalid voice channel: %u", ch);
		return;
	}
	audioStop((AudioChannel)(kAudioChVoice0 + ch));
}

void audioVoicesubPlay(const char *name) {
	Common::SeekableReadStream *file = assetVoicesubLoad(name);
	if (!file) {
		warning("Failed to load voicesub file: %s", name);
		return;
	}
	audioPlay(kAudioChVoice1, file, name, false);
}

void audioSeStop(uint ch) {
	if (!audioSeChannelValid(ch)) {
		warning("Invalid SE channel: %u", ch);
		return;
	}
	audioStop((AudioChannel)(kAudioChSe0 + ch));
}

void audioSeFade(int vol, uint t, bool stop, bool sync, uint ch) {
	if (!audioSeChannelValid(ch)) {
		warning("Invalid SE channel: %u", ch);
		return;
	}
	audioFade((AudioChannel)(kAudioChSe0 + ch), vol, t, stop, sync);
}

} // End of namespace AI5
