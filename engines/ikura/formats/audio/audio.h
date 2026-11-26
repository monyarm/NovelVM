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
 * Based on VileVN, Copyright (C) ViLE Team (GPL-3.0).
 *
 */

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
