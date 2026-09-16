#include "ikura/runtime/audio.h"

#include "ikura/formats/audio/audio.h"

namespace Ikura::Runtime {

namespace {

Common::String withoutExtension(const Common::String &name) {
	int dot = (int)name.size() - 1;
	while (dot >= 0 && name[dot] != '.')
		dot--;
	return dot >= 0 ? name.substr(0, dot) : name;
}

// Real scripts name tracks by an extension the shipped cabinet doesn't
// actually use - e.g. Snow Sakura's scripts ask for "MP01.wav" but its
// wmsc cabinet only has "MP01.OGG" (publishers re-encode music without
// recompiling scripts). The reference's resource manager resolves this
// by extension fallback, so this does too: try the literal name first,
// then the same base name under every format decodeAudioStream knows.
// Sets actualName to whichever name actually matched (decodeAudioStream
// dispatches on that, not the originally requested name).
Common::SeekableReadStream *resolveStream(Common::Archive *cabinet, const Common::String &name, Common::String &actualName) {
	static const char *kKnownExtensions[] = {".OGG", ".WAV"};

	if (cabinet->hasFile(Common::Path(name))) {
		actualName = name;
		return cabinet->createReadStreamForMember(Common::Path(name));
	}

	Common::String base = withoutExtension(name);
	for (const char *ext : kKnownExtensions) {
		Common::String candidate = base + ext;
		if (cabinet->hasFile(Common::Path(candidate))) {
			actualName = candidate;
			return cabinet->createReadStreamForMember(Common::Path(candidate));
		}
	}
	return nullptr;
}

} // namespace

Audio::Audio(::Audio::Mixer *mixer, Common::Archive *musicCabinet, Common::Archive *seCabinet, Common::Archive *voiceCabinet)
	: _mixer(mixer), _musicCabinet(musicCabinet), _seCabinet(seCabinet), _voiceCabinet(voiceCabinet) {
	for (int i = 0; i < kChannelCount; i++)
		_soundActive[i] = false;
}

Audio::~Audio() {
	_mixer->stopHandle(_musicHandle);
	_mixer->stopHandle(_voiceHandle);
	for (int i = 0; i < kChannelCount; i++)
		if (_soundActive[i])
			_mixer->stopHandle(_soundHandles[i]);
}

void Audio::playMusic(const Common::String &name) {
	_mixer->stopHandle(_musicHandle);
	if (!_musicCabinet)
		return;

	Common::String actualName;
	Common::SeekableReadStream *stream = resolveStream(_musicCabinet, name, actualName);
	if (!stream)
		return;

	::Audio::SeekableAudioStream *decoded = Format::Audio::decodeAudioStream(actualName, stream);
	if (!decoded)
		return;

	::Audio::AudioStream *looped = ::Audio::makeLoopingAudioStream(decoded, 0);
	_mixer->playStream(::Audio::Mixer::kMusicSoundType, &_musicHandle, looped);
}

void Audio::stopMusic() {
	_mixer->stopHandle(_musicHandle);
}

void Audio::playSound(const Common::String &name, int channel) {
	if (channel < 0 || channel >= kChannelCount || !_seCabinet)
		return;

	if (_soundActive[channel])
		_mixer->stopHandle(_soundHandles[channel]);
	_soundActive[channel] = false;

	Common::String actualName;
	Common::SeekableReadStream *stream = resolveStream(_seCabinet, name, actualName);
	if (!stream)
		return;

	::Audio::SeekableAudioStream *decoded = Format::Audio::decodeAudioStream(actualName, stream);
	if (!decoded)
		return;

	_mixer->playStream(::Audio::Mixer::kSFXSoundType, &_soundHandles[channel], decoded);
	_soundActive[channel] = true;
}

void Audio::stopSound(int channel) {
	if (channel < 0) {
		for (int i = 0; i < kChannelCount; i++)
			stopSound(i);
		return;
	}
	if (channel >= kChannelCount)
		return;
	if (_soundActive[channel]) {
		_mixer->stopHandle(_soundHandles[channel]);
		_soundActive[channel] = false;
	}
}

void Audio::playVoice(const Common::String &name) {
	_mixer->stopHandle(_voiceHandle);
	if (!_voiceCabinet)
		return;

	Common::String actualName;
	Common::SeekableReadStream *stream = resolveStream(_voiceCabinet, name, actualName);
	if (!stream)
		return;

	::Audio::SeekableAudioStream *decoded = Format::Audio::decodeAudioStream(actualName, stream);
	if (!decoded)
		return;

	_mixer->playStream(::Audio::Mixer::kSpeechSoundType, &_voiceHandle, decoded);
}

void Audio::stopVoice() {
	_mixer->stopHandle(_voiceHandle);
}

bool Audio::isVoicePlaying() const {
	return _mixer->isSoundHandleActive(_voiceHandle);
}

bool Audio::isMusicPlaying() const {
	return _mixer->isSoundHandleActive(_musicHandle);
}

bool Audio::isSoundPlaying(int channel) const {
	if (channel < 0 || channel >= kChannelCount)
		return false;
	return _soundActive[channel] && _mixer->isSoundHandleActive(_soundHandles[channel]);
}

} // End of namespace Ikura::Runtime
