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
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage (GPL-2.0-or-later).
 *
 */

#ifndef COMMON_LZSS_H
#define COMMON_LZSS_H

#include "common/array.h"

namespace Common {

class SeekableReadStream;

/**
 * @defgroup common_lzss LZSS decompressor
 * @ingroup common_compression
 *
 * @brief Decompressors for the LZSS variants with a 4 KiB window.
 * @{
 */

/**
 * Decompress byte-aligned LZSS, as in Haruhiko Okumura's LZSS.C: a flag byte
 * (LSB first) precedes each group of eight items; a set bit is a literal
 * byte, a clear bit a two-byte reference with a 12-bit window offset and a
 * 4-bit length (plus 3). Reads until the end of the stream.
 *
 * @param in           Compressed data.
 * @param out          Decompressed data.
 * @param limit        Stop after this many output bytes (0 for no limit).
 * @param windowStart  Initial write position in the window.
 */
void decompressLZSS(SeekableReadStream &in, Array<byte> &out, uint32 limit = 0, uint windowStart = 0xfee);

/**
 * Decompress bit-packed LZSS (MSB first): a 1 bit is followed by an 8-bit
 * literal, a 0 bit by a 12-bit window offset and a 4-bit length (plus 2).
 * An offset of 0 ends the data. The window write position starts at 1.
 */
void decompressLZSSBitwise(SeekableReadStream &in, Array<byte> &out);

/** @} */

} // End of namespace Common

#endif
