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
