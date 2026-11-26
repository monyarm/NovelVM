#include "ikura/runtime/audio.h"

#include "audio/mixer_intern.h"
#include "common/archive.h"
#include "common/array.h"
#include "common/memstream.h"
#include "../../system/null_osystem.h"

#include <cxxtest/TestSuite.h>

namespace {

// Minimal single-member in-memory Archive double, same shape as
// test/engines/ikura/presentation.h and interpreter.h's copies.
class AudioOneFileArchive : public Common::Archive {
public:
	AudioOneFileArchive(const Common::String &name, Common::Array<byte> data) : _name(name), _data(data) {}

	bool hasFile(const Common::Path &path) const override { return path.toString() == _name; }
	int listMembers(Common::ArchiveMemberList &list) const override { return 0; }
	const Common::ArchiveMemberPtr getMember(const Common::Path &path) const override { return Common::ArchiveMemberPtr(); }
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override {
		if (!hasFile(path))
			return nullptr;
		return new Common::MemoryReadStream(_data.data(), _data.size(), DisposeAfterUse::NO);
	}

private:
	Common::String _name;
	Common::Array<byte> _data;
};

// Smallest valid PCM WAV: RIFF/WAVE, one "fmt " chunk (8-bit mono @
// 8000Hz), one 1-byte "data" chunk.
Common::Array<byte> buildWAVFixture(byte sample) {
	static const byte kTemplate[] = {
		'R', 'I', 'F', 'F', 37, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x40, 0x1F, 0x00, 0x00, 0x40, 0x1F, 0x00, 0x00, 1, 0, 8, 0,
		'd', 'a', 't', 'a', 1, 0, 0, 0, 0,
	};
	Common::Array<byte> data(kTemplate, sizeof(kTemplate));
	data.back() = sample;
	return data;
}

} // namespace

// Each test declares its Audio::MixerImpl before its Ikura::Runtime::Audio
// (both stack locals) so the mixer outlives the Audio that references it -
// C++ destroys stack locals in reverse declaration order. Never pumped (no
// mixCallback drives it), which is fine: playStream/stopHandle/
// isSoundHandleActive only touch channel bookkeeping, not actual output.
class IkuraAudioTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_play_music_starts_playing() {
		AudioOneFileArchive music("TK01.WAV", buildWAVFixture(200));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, &music, nullptr);

		TS_ASSERT(!audio.isMusicPlaying());
		audio.playMusic("TK01.WAV");
		TS_ASSERT(audio.isMusicPlaying());
		audio.stopMusic();
		TS_ASSERT(!audio.isMusicPlaying());
	}

	void test_play_music_extension_fallback_finds_real_file() {
		// Real scripts ask for e.g. "MP01.wav" but the shipped cabinet
		// only has "MP01.OGG" (see Audio::resolveStream in audio.cpp) -
		// mismatched extension, same base name, must still resolve and
		// decode using the *actual* file's format.
		AudioOneFileArchive music("MP01.WAV", buildWAVFixture(1));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, &music, nullptr);
		audio.playMusic("MP01.ogg"); // wrong extension, same base name
		TS_ASSERT(audio.isMusicPlaying());
	}

	void test_play_music_missing_file_is_noop() {
		AudioOneFileArchive music("TK01.WAV", buildWAVFixture(1));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, &music, nullptr);
		audio.playMusic("NOPE.WAV");
		TS_ASSERT(!audio.isMusicPlaying());
	}

	void test_play_music_replaces_previous_track() {
		AudioOneFileArchive music("TK01.WAV", buildWAVFixture(1));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, &music, nullptr);
		audio.playMusic("TK01.WAV");
		TS_ASSERT(audio.isMusicPlaying());
		audio.playMusic("TK01.WAV"); // must not assert/crash on a second start
		TS_ASSERT(audio.isMusicPlaying());
	}

	void test_play_sound_on_channel() {
		AudioOneFileArchive se("SE01.WAV", buildWAVFixture(1));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, nullptr, &se);

		TS_ASSERT(!audio.isSoundPlaying(3));
		audio.playSound("SE01.WAV", 3);
		TS_ASSERT(audio.isSoundPlaying(3));
		TS_ASSERT(!audio.isSoundPlaying(4)); // untouched channel
		audio.stopSound(3);
		TS_ASSERT(!audio.isSoundPlaying(3));
	}

	void test_play_sound_out_of_range_channel_is_noop() {
		AudioOneFileArchive se("SE01.WAV", buildWAVFixture(1));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, nullptr, &se);
		audio.playSound("SE01.WAV", -1);
		audio.playSound("SE01.WAV", Ikura::Runtime::Audio::kChannelCount);
		TS_ASSERT(!audio.isSoundPlaying(-1));
	}

	void test_stop_sound_negative_channel_stops_all() {
		AudioOneFileArchive se("SE01.WAV", buildWAVFixture(1));
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, nullptr, &se);
		audio.playSound("SE01.WAV", 0);
		audio.playSound("SE01.WAV", 5);
		TS_ASSERT(audio.isSoundPlaying(0));
		TS_ASSERT(audio.isSoundPlaying(5));
		audio.stopSound(-1);
		TS_ASSERT(!audio.isSoundPlaying(0));
		TS_ASSERT(!audio.isSoundPlaying(5));
	}

	void test_without_cabinets_everything_is_noop() {
		Audio::MixerImpl mixer(22050);
		mixer.setReady(true);
		Ikura::Runtime::Audio audio(&mixer, nullptr, nullptr);
		audio.playMusic("anything.WAV");
		audio.playSound("anything.WAV", 0);
		TS_ASSERT(!audio.isMusicPlaying());
		TS_ASSERT(!audio.isSoundPlaying(0));
	}
};
