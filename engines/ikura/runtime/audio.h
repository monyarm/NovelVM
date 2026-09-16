#ifndef IKURA_RUNTIME_AUDIO_H
#define IKURA_RUNTIME_AUDIO_H

#include "audio/mixer.h"
#include "common/archive.h"

namespace Ikura::Runtime {

// Drives Audio::Mixer from the interpreter's music/SE opcode handlers
// (VileVN reference: EngineMixer, src/engine/emixer.cpp). Matches its
// channel model: one music slot that loops indefinitely once started
// (SetReplays(-1) there), and a fixed 32-slot SE channel array where each
// channel holds at most one sound, played once (no loop).
class Audio {
public:
	// Matches the reference's AUDIO_CHANNELS.
	static const int kChannelCount = 32;

	// mixer/musicCabinet/seCabinet/voiceCabinet are not owned; must outlive
	// this Audio. Any cabinet may be nullptr if that category isn't
	// available - the matching opcode just no-ops then.
	Audio(::Audio::Mixer *mixer, Common::Archive *musicCabinet, Common::Archive *seCabinet, Common::Archive *voiceCabinet = nullptr);
	~Audio();

	// IOP_ML: stops whatever's currently playing and loops the named
	// track indefinitely. No-op if name isn't in musicCabinet or fails
	// to decode.
	void playMusic(const Common::String &name);

	// IOP_MS
	void stopMusic();

	// IOP_SER: plays the named sound once on channel, replacing whatever
	// was already playing there. No-op if channel is out of range, name
	// isn't in seCabinet, or it fails to decode.
	void playSound(const Common::String &name, int channel);

	// IOP_SET. channel<0 stops every channel (matches the reference).
	void stopSound(int channel);

	bool isMusicPlaying() const;
	bool isSoundPlaying(int channel) const;

	// IOP_PCML: plays the named voice clip once, replacing whatever voice
	// clip was already playing (single slot - VileVN reference always
	// plays voices on the one VA_VOICES channel, never a pool like SE's).
	// No-op if name isn't in voiceCabinet or fails to decode.
	void playVoice(const Common::String &name);

	// IOP_PCMS
	void stopVoice();

	bool isVoicePlaying() const;

private:
	::Audio::Mixer *_mixer;
	Common::Archive *_musicCabinet;
	Common::Archive *_seCabinet;
	Common::Archive *_voiceCabinet;
	::Audio::SoundHandle _musicHandle;
	::Audio::SoundHandle _voiceHandle;
	::Audio::SoundHandle _soundHandles[kChannelCount];
	bool _soundActive[kChannelCount];
};

} // End of namespace Ikura::Runtime

#endif
