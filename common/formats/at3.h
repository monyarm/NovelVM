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
