#ifndef IKURA_FORMATS_AUDIO_AUDIO_H
#define IKURA_FORMATS_AUDIO_AUDIO_H

#include "audio/audiostream.h"
#include "common/str.h"
#include "common/stream.h"

namespace Ikura::Format::Audio {

// Picks the right decoder for a resource by its extension. Real cabinets
// mix formats: Kana Okaeri's music/SE/voice cabinets are all Ogg Vorbis
// (.OGG), Kana Imouto's are all PCM WAV (.WAV) - see
// engines/ikura/README.md's real-data validation table. Takes ownership
// of stream either way. Returns nullptr if the extension is unrecognized
// or the decoder rejects the stream (stream is still consumed/deleted).
::Audio::SeekableAudioStream *decodeAudioStream(const Common::String &name, Common::SeekableReadStream *stream);

} // End of namespace Ikura::Format::Audio

#endif
