#ifndef FORMAT_HCA_H
#define FORMAT_HCA_H

#include "common/scummsys.h"
#include "common/stream.h"
#include "common/str.h"

namespace Audio {
class SeekableAudioStream;
}

namespace Common {

/**
 * Decodes a CRI HCA stream. Returns nullptr on a malformed/unsupported
 * file, or an encrypted file with no key. Decoder facts/structure and
 * tables are ported from vgmstream's clhca.c/clhca_data.h, which is
 * distributed under the ISC license -- its copyright notice and license
 * text are reproduced in full in hca.cpp and hca_tables.h:
 *   Original decompilation by nyaga, C port by kode54, HCA v3 reverse
 *   engineering by bnnm, VGAudio by Thealexbarney, CriCodecs by Youjose.
 *
 * The on-disk layout is: a masked 8-byte preamble ("HCA\0" magic under
 * 0x7F7F7F7F + version + headerSize), then a series of fixed-order chunks
 * ("fmt\0" / "comp" or "dec\0" / "vbr\0" / "ath\0" / "loop" / "ciph" /
 * "rva\0" / "comm" / "pad\0", chunk IDs also masked in encrypted files),
 * then frameCount fixed-size frames. Each frame is 1024 samples
 * (8 subframes x 128), ends with a 16-bit CRC16 (polynomial 0x8005), and
 * may be XOR-"decrypted" with a keycode (ciph type 56) or keyless
 * substituion cipher (type 1).
 */
Audio::SeekableAudioStream *makeHCAStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse);

// As above, decrypting with a 56-bit keycode (ciph type 56, the scheme
// used by Persona 3 Reload etc.). Keys come from the game's own data (e.g.
// the .hcakey sidecar files) -- never hardcode one here.
Audio::SeekableAudioStream *makeHCAStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse, uint64 keycode);

} // namespace Common

#endif
