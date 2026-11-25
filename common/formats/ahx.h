/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
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

#ifndef FORMAT_AHX_H
#define FORMAT_AHX_H

#include "common/scummsys.h"
#include "common/stream.h"
#include "common/str.h"

namespace Audio {
class SeekableAudioStream;
}

namespace Common {

/**
 * Decodes a CRI AHX stream. Returns nullptr on a malformed/unsupported
 * file. Decoder structure, bitstream parsing and tables are ported from
 * FFmpeg's MPEG-1/2 Audio Layer II decoder (libavcodec/mpegaudiodec.c,
 * mpegaudiodsp.c, mpegaudiodata.c), which is distributed under the GNU
 * Lesser General Public License v2.1+ -- its copyright notice and license
 * text are reproduced in full in ahx.cpp:
 *   Copyright (c) 2001, 2002 Fabrice Bellard, and FFmpeg contributors.
 * CRITICAL container facts (header layout, VBR frame split by header
 * resync, zero-fill to the fixed frame size) follow vgmstream's
 * src/meta/ahx.c + src/coding/mpeg_custom_utils_ahx.c (ISC).
 *
 * AHX is CRI's low-bitrate "voice" format. The on-disk layout is a fixed
 * CRI header ("\x80\x00" magic + start offset + type/version/encryption/
 * sample info), then a `(c)CRI` banner, then consecutive MPEG-1/2 Audio
 * Layer II frames that all share one fixed frame header (for Persona 4:
 * 0xFFF5E0C0 = MPEG-2/LSF Layer II, 160 kbps, mono). Frames are variable
 * length (VBR, padding omitted) and must be resynced by scanning for the
 * next copy of that header; each frame is zero-filled to the frame size
 * implied by the MPEG header before decode. The effective sample rate is
 * the AHX header's own rate (e.g. 24000 Hz), not the MPEG header's.
 * Some games encrypt the scale-factor-selection bits of type-0x08 AHX
 * with a per-game key; only unencrypted streams are supported here
 * (every Persona 4 file is), matching the "never ship a game key" rule
 * used elsewhere in this codebase.
 */
Audio::SeekableAudioStream *makeAHXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse);

} // namespace Common

#endif
