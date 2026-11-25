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

#include "common/formats/cri_mask.h"

namespace Common {

void initCRIMask(uint32 key1, uint32 key2, CRIMask &out) {
	byte k1[4] = {
		(byte)(key1 & 0xFF), (byte)((key1 >> 8) & 0xFF),
		(byte)((key1 >> 16) & 0xFF), (byte)((key1 >> 24) & 0xFF)
	};
	byte k2[4] = {
		(byte)(key2 & 0xFF), (byte)((key2 >> 8) & 0xFF),
		(byte)((key2 >> 16) & 0xFF), (byte)((key2 >> 24) & 0xFF)
	};

	// Real key-schedule facts (independently reverse engineered): a fixed sequence of byte-wise add/xor
	// operations expanding the two key halves into a 32-byte table.
	byte t[0x20];
	t[0x00] = k1[0];
	t[0x01] = k1[1];
	t[0x02] = k1[2];
	t[0x03] = (byte)(k1[3] - 0x34);
	t[0x04] = (byte)(k2[0] + 0xF9);
	t[0x05] = (byte)(k2[1] ^ 0x13);
	t[0x06] = (byte)(k2[2] + 0x61);
	t[0x07] = (byte)(t[0x00] ^ 0xFF);
	t[0x08] = (byte)(t[0x02] + t[0x01]);
	t[0x09] = (byte)(t[0x01] - t[0x07]);
	t[0x0A] = (byte)(t[0x02] ^ 0xFF);
	t[0x0B] = (byte)(t[0x01] ^ 0xFF);
	t[0x0C] = (byte)(t[0x0B] + t[0x09]);
	t[0x0D] = (byte)(t[0x08] - t[0x03]);
	t[0x0E] = (byte)(t[0x0D] ^ 0xFF);
	t[0x0F] = (byte)(t[0x0A] - t[0x0B]);
	t[0x10] = (byte)(t[0x08] - t[0x0F]);
	t[0x11] = (byte)(t[0x10] ^ t[0x07]);
	t[0x12] = (byte)(t[0x0F] ^ 0xFF);
	t[0x13] = (byte)(t[0x03] ^ 0x10);
	t[0x14] = (byte)(t[0x04] - 0x32);
	t[0x15] = (byte)(t[0x05] + 0xED);
	t[0x16] = (byte)(t[0x06] ^ 0xF3);
	t[0x17] = (byte)(t[0x13] - t[0x0F]);
	t[0x18] = (byte)(t[0x15] + t[0x07]);
	t[0x19] = (byte)(0x21 - t[0x13]);
	t[0x1A] = (byte)(t[0x14] ^ t[0x17]);
	t[0x1B] = (byte)(t[0x16] + t[0x16]);
	t[0x1C] = (byte)(t[0x17] + 0x44);
	t[0x1D] = (byte)(t[0x03] + t[0x04]);
	t[0x1E] = (byte)(t[0x05] - t[0x16]);
	t[0x1F] = (byte)(t[0x1D] ^ t[0x13]);

	static const byte kAudioLiteral[4] = {'U', 'R', 'U', 'C'};
	for (int i = 0; i < 0x20; i++) {
		out.videoMask1[i] = t[i];
		out.videoMask2[i] = (byte)(t[i] ^ 0xFF);
		out.audioMask[i] = (i & 1) ? kAudioLiteral[(i >> 1) & 3] : (byte)(t[i] ^ 0xFF);
	}
}

void maskAudioChunk(byte *data, uint32 size, const CRIMask &mask) {
	const uint32 kOffset = 0x140;
	if (size <= kOffset)
		return;
	data += kOffset;
	size -= kOffset;
	for (uint32 i = 0; i < size; i++)
		data[i] ^= mask.audioMask[i & 0x1F];
}

void maskVideoChunk(byte *data, uint32 size, const CRIMask &mask) {
	const uint32 kOffset = 0x40;
	if (size <= kOffset)
		return;
	data += kOffset;
	size -= kOffset;
	if (size < 0x200)
		return;

	// Real facts: a two-pass, self-feeding (CFB-like) XOR -- the working
	// mask mutates as it consumes plaintext, it isn't a static repeating
	// XOR. Pass 1 covers the tail of the region (from 0x100 on), folding
	// each decrypted byte back into the mask; pass 2 covers the first
	// 0x100 bytes, seeded fresh and folding in the (now-decrypted) pass-1
	// output at the matching offset.
	byte rolling[0x20];
	memcpy(rolling, mask.videoMask2, sizeof(rolling));
	for (uint32 i = 0x100; i < size; i++) {
		data[i] ^= rolling[i & 0x1F];
		rolling[i & 0x1F] = (byte)(data[i] ^ mask.videoMask2[i & 0x1F]);
	}

	memcpy(rolling, mask.videoMask1, sizeof(rolling));
	for (uint32 i = 0; i < 0x100; i++) {
		rolling[i & 0x1F] ^= data[0x100 + i];
		data[i] ^= rolling[i & 0x1F];
	}
}

} // namespace Common
