#include "ikura/formats/audio/audio.h"

#include "audio/decoders/vorbis.h"
#include "audio/decoders/wave.h"

namespace Ikura::Format::Audio {

namespace {

Common::String extensionOf(const Common::String &name) {
	int dot = (int)name.size() - 1;
	while (dot >= 0 && name[dot] != '.')
		dot--;
	return dot >= 0 ? name.substr(dot) : Common::String();
}

} // namespace

::Audio::SeekableAudioStream *decodeAudioStream(const Common::String &name, Common::SeekableReadStream *stream) {
	Common::String ext = extensionOf(name);
#ifdef USE_VORBIS
	if (ext.equalsIgnoreCase(".OGG"))
		return ::Audio::makeVorbisStream(stream, DisposeAfterUse::YES);
#endif
	if (ext.equalsIgnoreCase(".WAV"))
		return ::Audio::makeWAVStream(stream, DisposeAfterUse::YES);
	delete stream;
	return nullptr;
}

} // End of namespace Ikura::Format::Audio
