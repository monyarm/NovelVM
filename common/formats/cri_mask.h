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

#ifndef FORMAT_CRI_MASK_H
#define FORMAT_CRI_MASK_H

#include "common/scummsys.h"

namespace Common {

// CRI Sofdec2's per-title USM chunk encryption (seen on some titles, e.g.
// Persona 5 Royal's movies). A 64-bit key (two u32 halves) expands into
// three 32-byte XOR tables; @SFV/@SFA *data* chunks (payloadType 0) get
// masked, metadata chunks (payloadType 1) don't. Algorithm facts only,
// independently reimplemented from public reverse-engineering documentation
// (no license on the source, so no code copied). The real key for a given
// title is meant to be supplied at runtime (never hardcoded/committed here).
struct CRIMask {
	byte audioMask[0x20];
	byte videoMask1[0x20];
	byte videoMask2[0x20];
};

void initCRIMask(uint32 key1, uint32 key2, CRIMask &out);

// In-place. No-op if size <= 0x140 (matches real behavior: chunks too
// short to reach the masked region -- e.g. the ADX header chunk -- are
// left untouched).
void maskAudioChunk(byte *data, uint32 size, const CRIMask &mask);

// In-place. No-op if size <= 0x40 or the region beyond 0x40 is < 0x200
// bytes (matches real behavior -- short video chunks aren't masked).
void maskVideoChunk(byte *data, uint32 size, const CRIMask &mask);

} // namespace Common

#endif
