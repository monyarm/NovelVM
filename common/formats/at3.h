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
 */

#ifndef FORMAT_AT3_H
#define FORMAT_AT3_H

#include "common/scummsys.h"
#include "common/str.h"

namespace Audio {
class SeekableAudioStream;
}

namespace Common {

/**
 * Opens a real Sony AT3 file: a RIFF/WAVE container (magic "RIFF"..."WAVE")
 * carrying either of two unrelated Sony codecs, both real and both found
 * this session:
 *
 * - Plain ATRAC3: "fmt " chunk uses Sony's registered format tag 0x0270,
 *   with a real 14-byte extension (unknown u16LE, samples-per-channel
 *   u32LE (unused), coding mode u16LE, a duplicate of the coding mode
 *   u16LE, frame factor u16LE, unknown u16LE). Byte offsets verified
 *   against a real PSP game's AT3 file (Shin Seiki Evangelion: Koutetsu
 *   no Girl Friend 2nd Portable's SND0.AT3) and cross-checked with
 *   ffprobe.
 * - ATRAC3+: "fmt " chunk uses WAVE_FORMAT_EXTENSIBLE (format tag 0xFFFE),
 *   found in Tiger & Bunny: Hero's Day's *.AT3, confirmed via ffprobe.
 *   Needs no extra extradata beyond the fixed WAVEFORMATEX header (channel
 *   layout is derived purely from the channel count).
 *
 * Returns nullptr if the file isn't a real AT3 file using one of these two
 * codecs, or on any other real structural problem.
 */
Audio::SeekableAudioStream *makeAT3Stream(const Common::String &filename);

} // namespace Common

#endif
