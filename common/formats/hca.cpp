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

#include "common/formats/hca.h"
#include "common/formats/hca_tables.h"

#include "audio/audiostream.h"
#include "common/array.h"
#include "common/memstream.h"
#include "common/textconsole.h"
#include "common/util.h"

// CRI HCA decoder. Algorithm structure and tables ported from vgmstream's
// src/coding/libs/clhca.c and clhca_data.h (see hca_tables.h):
//   Original decompilation by nyaga, C port by kode54, HCA v3 reverse
//   engineering by bnnm, VGAudio by Thealexbarney, CriCodecs by Youjose.
// Headers/CRC validated against real Persona 3 Reload .hca files; PCM
// output verified sample-exact against FFmpeg's HCA decoder.
//
// vgmstream (https://github.com/vgmstream/vgmstream) is distributed under
// the ISC license. For the ported portion in this file, its copyright
// notice and license follow:
//
//   Copyright (c) 2008-2025 Adam Gashlin, Fastelbja, Ronny Elfert, bnnm,
//                           Christopher Snowhill, NicknineTheEagle, bxaimc,
//                           Thealexbarney, CyberBotX, EdnessP, et al
//   Portions Copyright (c) 2004-2008, Marko Kreen
//   Portions Copyright 2001-2007  jagarl / Kazunori Ueno <jagarl@creator.club.ne.jp>
//   Portions Copyright (c) 1998, Justin Frankel/Nullsoft Inc.
//   Portions Copyright (c) 2005-2007 Paul Hsieh
//   Portions Copyright (C) 2000-2004 Leshade Entis, Entis-soft.
//   Portions Public Domain originating with Sun Microsystems
//
//   Permission to use, copy, modify, and distribute this software for any
//   purpose with or without fee is hereby granted, provided that the above
//   copyright notice and this permission notice appear in all copies.
//
//   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
//   WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
//   MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
//   ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
//   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
//   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
//   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

namespace Common {

namespace {

// The DSP tables in hca_tables.h store IEEE754 single-precision values as
// their raw uint32 bit patterns (avoiding compiler float-rounding on the
// exact hex constants); reinterpret here.
static inline float floatFromBits(uint32 bits) {
	float f;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

enum {
	kVersion101 = 0x0101,
	kVersion102 = 0x0102,
	kVersion103 = 0x0103,
	kVersion200 = 0x0200,
	kVersion300 = 0x0300,
};

enum {
	kMinChannels      = 1,
	kMaxChannels      = 16,
	kMinSampleRate    = 1,
	kMaxSampleRate    = 0x7FFFFF,
	kMinFrameSize     = 0x08,
	kMaxFrameSize     = 0xFFFF,
	kSubframes        = 8,
	kSamplesPerFrame  = 128,   // per subframe
	kSamplesPerSubframe = 128,
	kFrameSamples     = kSubframes * kSamplesPerFrame, // 1024 per channel per frame
	kMDCTBits         = 7,
	kDefaultRandom    = 1,
};

// Chunk-ID mask applied when the HCA is key-encrypted.
const uint32 kChunkMask = 0x7F7F7F7F;

enum ChannelType {
	kChannelDiscrete = 0,
	kChannelStereoPrimary = 1,
	kChannelStereoSecondary = 2,
};

struct HCAHeader {
	// hca chunk
	uint32 version = 0;
	uint32 headerSize = 0;
	// fmt chunk
	uint32 channels = 0;
	uint32 sampleRate = 0;
	uint32 frameCount = 0;
	uint32 encoderDelay = 0;
	uint32 encoderPadding = 0;
	// comp/dec chunk
	uint32 frameSize = 0;
	uint32 minResolution = 0;
	uint32 maxResolution = 0;
	uint32 trackCount = 0;
	uint32 channelConfig = 0;
	uint32 stereoType = 0;
	uint32 totalBandCount = 0;
	uint32 baseBandCount = 0;
	uint32 stereoBandCount = 0;
	uint32 bandsPerHFRGroup = 0;
	uint32 msStereo = 0;
	// vbr chunk
	uint32 vbrMaxFrameSize = 0;
	uint32 vbrNoiseLevel = 0;
	// ath chunk
	uint32 athType = 0;
	// loop chunk
	uint32 loopStartFrame = 0;
	uint32 loopEndFrame = 0;
	uint32 loopStartDelay = 0;
	uint32 loopEndPadding = 0;
	uint32 loopEnabled = 0;
	// ciph chunk
	uint32 ciphType = 0;
	uint64 keycode = 0;
	// rva chunk
	float rvaVolume = 1.0f;
	// comm chunk
	uint32 commentLen = 0;
	char comment[256] = {0};
	// derived
	uint32 hfrGroupCount = 0;
	uint32 valid = 0;
};

struct HCAChannel {
	ChannelType type = kChannelDiscrete;
	uint32 codedCount = 0;

	byte intensity[kSubframes];
	byte scalefactors[kSamplesPerFrame];
	byte resolution[kSamplesPerFrame];
	byte noises[kSamplesPerFrame];
	uint32 noiseCount = 0;
	uint32 validCount = 0;

	float gain[kSamplesPerFrame];
	float spectra[kSubframes][kSamplesPerFrame];
	float temp[kSamplesPerFrame];
	float imdctPrevious[kSamplesPerFrame];
	float wave[kSubframes][kSamplesPerFrame];
};

struct BitReader {
	const byte *data = nullptr;
	int64 sizeBits = 0;
	int64 bit = 0;
};

void bitInit(BitReader &br, const byte *data, int size) {
	br.data = data;
	br.sizeBits = (int64)size * 8;
	br.bit = 0;
}

uint32 bitPeek(const BitReader &br, int bitsRead) {
	const uint32 bitPos = (uint32)br.bit;
	const uint32 bitRem = bitPos & 7;
	const uint32 bitSize = (uint32)br.sizeBits;
	uint32 v = 0;

	if (bitPos + (uint32)bitsRead > bitSize)
		return v;
	if (bitsRead == 0)
		return v;

	const uint32 bitOffset = (uint32)bitsRead + bitRem;
	const uint32 bitsLeft = bitSize - bitPos;
	const byte *data = &br.data[bitPos >> 3];

	if (bitsLeft >= 32 && bitOffset >= 25) {
		v = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
		v &= 0xFFFFFFFF >> bitRem;
		v >>= 32 - bitRem - bitsRead;
	} else if (bitsLeft >= 24 && bitOffset >= 17) {
		v = (data[0] << 16) | (data[1] << 8) | data[2];
		v &= 0xFFFFFF >> bitRem;
		v >>= 24 - bitRem - bitsRead;
	} else if (bitsLeft >= 16 && bitOffset >= 9) {
		v = (data[0] << 8) | data[1];
		v &= 0xFFFF >> bitRem;
		v >>= 16 - bitRem - bitsRead;
	} else {
		v = data[0];
		v &= 0xFF >> bitRem;
		v >>= 8 - bitRem - bitsRead;
	}
	return v;
}

uint32 bitRead(BitReader &br, int bitsize) {
	uint32 v = bitPeek(br, bitsize);
	br.bit += bitsize;
	return v;
}

void bitSkip(BitReader &br, int bitsize) {
	br.bit += bitsize;
}

uint16 crc16(const byte *data, uint32 size) {
	uint16 sum = 0;
	for (uint32 i = 0; i < size; i++) {
		sum = (uint16)((sum << 8) ^ HCATables::kCRCMaskTable[(sum >> 8) ^ data[i]]);
	}
	return sum;
}

void cipherDecrypt(const byte cipherTable[256], byte *data, int size) {
	for (int i = 0; i < size; i++)
		data[i] = cipherTable[data[i]];
}

bool cipherInit0(byte cipherTable[256]) {
	for (int i = 0; i < 256; i++)
		cipherTable[i] = (byte)i;
	return true;
}

bool cipherInit1(byte cipherTable[256]) {
	const int mul = 13;
	const int add = 11;
	int v = 0;
	for (int i = 1; i < 256 - 1; i++) {
		v = (v * mul + add) & 0xFF;
		if (v == 0 || v == 0xFF)
			v = (v * mul + add) & 0xFF;
		cipherTable[i] = (byte)v;
	}
	cipherTable[0] = 0;
	cipherTable[0xFF] = 0xFF;
	return true;
}

void /*cipherInit56CreateTable*/ cipherTable56Create(byte r[16], byte key) {
	const int mul = ((key & 1) << 3) | 5;
	const int add = (key & 0xE) | 1;
	key >>= 4;
	for (int i = 0; i < 16; i++) {
		key = (byte)((key * mul + add) & 0xF);
		r[i] = key;
	}
}

bool cipherInit56(byte cipherTable[256], uint64 keycode) {
	byte kc[8] = {0};
	byte seed[16] = {0};
	byte base[256] = {0};
	byte baseR[16] = {0};
	byte baseC[16] = {0};

	// keycode = keycode - 1
	if (keycode != 0)
		keycode--;

	// init keycode table
	for (int r = 0; r < (8 - 1); r++) {
		kc[r] = (byte)(keycode & 0xFF);
		keycode >>= 8;
	}

	// init seed table
	seed[0x00] = kc[1];
	seed[0x01] = kc[1] ^ kc[6];
	seed[0x02] = kc[2] ^ kc[3];
	seed[0x03] = kc[2];
	seed[0x04] = kc[2] ^ kc[1];
	seed[0x05] = kc[3] ^ kc[4];
	seed[0x06] = kc[3];
	seed[0x07] = kc[3] ^ kc[2];
	seed[0x08] = kc[4] ^ kc[5];
	seed[0x09] = kc[4];
	seed[0x0A] = kc[4] ^ kc[3];
	seed[0x0B] = kc[5] ^ kc[6];
	seed[0x0C] = kc[5];
	seed[0x0D] = kc[5] ^ kc[4];
	seed[0x0E] = kc[6] ^ kc[1];
	seed[0x0F] = kc[6];

	// init base table
	cipherTable56Create(baseR, kc[0]);
	for (int r = 0; r < 16; r++) {
		byte nb;
		cipherTable56Create(baseC, seed[r]);
		nb = (byte)(baseR[r] << 4);
		for (int c = 0; c < 16; c++)
			base[r * 16 + c] = nb | baseC[c];
	}

	// final shuffle table
	{
		uint32 x = 0;
		uint32 pos = 1;
		for (int i = 0; i < 256; i++) {
			x = (x + 17) & 0xFF;
			if (base[x] != 0 && base[x] != 0xFF)
				cipherTable[pos++] = base[x];
		}
		cipherTable[0] = 0;
		cipherTable[0xFF] = 0xFF;
	}
	return true;
}

bool cipherInit(byte cipherTable[256], uint32 type, uint64 keycode) {
	if (type == 56 && keycode == 0)
		type = 0;

	switch (type) {
	case 0:
		return cipherInit0(cipherTable);
	case 1:
		return cipherInit1(cipherTable);
	case 56:
		return cipherInit56(cipherTable, keycode);
	default:
		return false;
	}
}

uint32 headerCeil2(uint32 a, uint32 b) {
	if (b < 1)
		return 0;
	return a / b + ((a % b) ? 1 : 0);
}

void setupChannelTypes(uint32 channels, uint32 trackCount, uint32 channelConfig, uint32 stereoBandCount, ChannelType *channelTypes) {
	uint32 channelsPerTrack = trackCount ? channels / trackCount : channels;

	if (stereoBandCount > 0 && channelsPerTrack > 1) {
		for (uint32 i = 0; i < trackCount; i++) {
			ChannelType *ct = &channelTypes[i * channelsPerTrack];
			switch (channelsPerTrack) {
			case 2:
				ct[0] = kChannelStereoPrimary;
				ct[1] = kChannelStereoSecondary;
				break;
			case 3:
				ct[0] = kChannelStereoPrimary;
				ct[1] = kChannelStereoSecondary;
				ct[2] = kChannelDiscrete;
				break;
			case 4:
				ct[0] = kChannelStereoPrimary;
				ct[1] = kChannelStereoSecondary;
				if (channelConfig == 0) {
					ct[2] = kChannelStereoPrimary;
					ct[3] = kChannelStereoSecondary;
				} else {
					ct[2] = kChannelDiscrete;
					ct[3] = kChannelDiscrete;
				}
				break;
			case 5:
				ct[0] = kChannelStereoPrimary;
				ct[1] = kChannelStereoSecondary;
				ct[2] = kChannelDiscrete;
				if (channelConfig <= 2) {
					ct[3] = kChannelStereoPrimary;
					ct[4] = kChannelStereoSecondary;
				} else {
					ct[3] = kChannelDiscrete;
					ct[4] = kChannelDiscrete;
				}
				break;
			case 6:
				ct[0] = kChannelStereoPrimary;
				ct[1] = kChannelStereoSecondary;
				ct[2] = kChannelDiscrete;
				ct[3] = kChannelDiscrete;
				ct[4] = kChannelStereoPrimary;
				ct[5] = kChannelStereoSecondary;
				break;
			case 7:
				ct[0] = kChannelStereoPrimary;
				ct[1] = kChannelStereoSecondary;
				ct[2] = kChannelDiscrete;
				ct[3] = kChannelDiscrete;
				ct[4] = kChannelStereoPrimary;
				ct[5] = kChannelStereoSecondary;
				ct[6] = kChannelDiscrete;
				break;
			case 8:
				ct[0] = kChannelStereoPrimary;
				ct[1] = kChannelStereoSecondary;
				ct[2] = kChannelDiscrete;
				ct[3] = kChannelDiscrete;
				ct[4] = kChannelStereoPrimary;
				ct[5] = kChannelStereoSecondary;
				ct[6] = kChannelStereoPrimary;
				ct[7] = kChannelStereoSecondary;
				break;
			default:
				for (uint32 ch = 0; ch < channelsPerTrack; ch++)
					ct[ch] = kChannelDiscrete;
				break;
			}
		}
	}
}

bool athInit(byte *athCurve, uint32 type, uint32 sampleRate) {
	if (type == 0) {
		memset(athCurve, 0, kSamplesPerFrame);
		return true;
	}
	if (type == 1) {
		uint32 acc = 0;
		for (int i = 0; i < kSamplesPerFrame; i++) {
			acc += sampleRate;
			uint32 index = acc >> 13;
			if (index >= 654) {
				memset(&athCurve[i], 0xFF, kSamplesPerFrame - i);
				break;
			}
			athCurve[i] = HCATables::kATHBaseCurve[index];
		}
		return true;
	}
	return false;
}

bool parseHCAHeader(Common::SeekableReadStream &s, HCAHeader &hca) {
	// Read the header into a buffer, then parse from memory (the header may
	// contain a masked/encrypted chunk layout but is itself checksummed).
	int64 fileSize = s.size();
	if (fileSize <= 0 || fileSize > 0x10000000)
		return false;

	Common::Array<byte> buf((uint32)fileSize);
	if (s.read(buf.data(), (uint32)fileSize) != (uint32)fileSize)
		return false;

	BitReader br;
	bitInit(br, buf.data(), (int)fileSize);

	// HCA base header
	uint32 u32 = bitRead(br, 32);
	if ((u32 & kChunkMask) != 0x48434100) // "HCA\0"
		return false;
	hca.version = bitRead(br, 16);
	hca.headerSize = bitRead(br, 16);

	if (hca.version != kVersion101 && hca.version != kVersion102 &&
	    hca.version != kVersion103 && hca.version != kVersion200 &&
	    hca.version != kVersion300)
		return false;
	if ((uint64)fileSize < hca.headerSize)
		return false;
	if (crc16(buf.data(), hca.headerSize))
		return false;

	// format info
	if ((bitRead(br, 32) & kChunkMask) != 0x666D7400) // "fmt\0"
		return false;
	hca.channels = bitRead(br, 8);
	hca.sampleRate = bitRead(br, 24);
	hca.frameCount = bitRead(br, 32);
	hca.encoderDelay = bitRead(br, 16);
	hca.encoderPadding = bitRead(br, 16);

	if (hca.channels < kMinChannels || hca.channels > kMaxChannels)
		return false;
	if (hca.frameCount == 0)
		return false;
	// frameCount * kFrameSamples is the stream's total sample count, used in
	// 32-bit arithmetic by endOfData()/seek()/getLength(). Reject a count that
	// would overflow that product (a wrapped total makes a malformed file look
	// empty); 0xFFFFFFFF / 1024 caps it.
	if (hca.frameCount > (0xFFFFFFFFu / kFrameSamples))
		return false;
	if (hca.sampleRate < kMinSampleRate || hca.sampleRate > kMaxSampleRate)
		return false;

	// compression (v2.0) or decode (v1.x) info
	uint32 chunkId = bitRead(br, 32) & kChunkMask;
	if (chunkId == 0x636F6D70) { // "comp"
		hca.frameSize = bitRead(br, 16);
		hca.minResolution = bitRead(br, 8);
		hca.maxResolution = bitRead(br, 8);
		hca.trackCount = bitRead(br, 8);
		hca.channelConfig = bitRead(br, 8);
		hca.totalBandCount = bitRead(br, 8);
		hca.baseBandCount = bitRead(br, 8);
		hca.stereoBandCount = bitRead(br, 8);
		hca.bandsPerHFRGroup = bitRead(br, 8);
		hca.msStereo = bitRead(br, 8);
		bitRead(br, 8); // reserved
	} else if (chunkId == 0x64656300) { // "dec\0"
		hca.frameSize = bitRead(br, 16);
		hca.minResolution = bitRead(br, 8);
		hca.maxResolution = bitRead(br, 8);
		hca.totalBandCount = bitRead(br, 8) + 1;
		hca.baseBandCount = bitRead(br, 8) + 1;
		hca.trackCount = bitRead(br, 4);
		hca.channelConfig = bitRead(br, 4);
		hca.stereoType = bitRead(br, 8);

		if (hca.stereoType == 0)
			hca.baseBandCount = hca.totalBandCount;
		hca.stereoBandCount = hca.totalBandCount - hca.baseBandCount;
		hca.bandsPerHFRGroup = 0;
	} else {
		return false;
	}

	// VBR info
	chunkId = bitRead(br, 32) & kChunkMask;
	if (chunkId == 0x76627200) { // "vbr\0"
		hca.vbrMaxFrameSize = bitRead(br, 16);
		hca.vbrNoiseLevel = bitRead(br, 16);
		if (!(hca.frameSize == 0 && hca.vbrMaxFrameSize > 8 && hca.vbrMaxFrameSize <= 0x1FF))
			return false;
	} else {
		bitSkip(br, -32);
		hca.vbrMaxFrameSize = 0;
		hca.vbrNoiseLevel = 0;
	}

	// ATH info
	chunkId = bitRead(br, 32) & kChunkMask;
	if (chunkId == 0x61746800) { // "ath\0"
		hca.athType = bitRead(br, 16);
	} else {
		bitSkip(br, -32);
		hca.athType = (hca.version < kVersion200) ? 1 : 0;
	}

	// loop info
	chunkId = bitRead(br, 32) & kChunkMask;
	if (chunkId == 0x6C6F6F70) { // "loop"
		hca.loopStartFrame = bitRead(br, 32);
		hca.loopEndFrame = bitRead(br, 32);
		hca.loopStartDelay = bitRead(br, 16);
		hca.loopEndPadding = bitRead(br, 16);
		hca.loopEnabled = 1;

		if (!(hca.loopStartFrame <= hca.loopEndFrame && hca.loopEndFrame < hca.frameCount))
			return false;
	} else {
		bitSkip(br, -32);
		hca.loopStartFrame = 0;
		hca.loopEndFrame = 0;
		hca.loopStartDelay = 0;
		hca.loopEndPadding = 0;
		hca.loopEnabled = 0;
	}

	// cipher info
	chunkId = bitRead(br, 32) & kChunkMask;
	if (chunkId == 0x63697068) { // "ciph"
		hca.ciphType = bitRead(br, 16);
		if (!(hca.ciphType == 0 || hca.ciphType == 1 || hca.ciphType == 56))
			return false;
	} else {
		bitSkip(br, -32);
		hca.ciphType = 0;
	}

	// RVA info
	chunkId = bitRead(br, 32) & kChunkMask;
	if (chunkId == 0x72766100) { // "rva\0"
		uint32 rvaCast = bitRead(br, 32);
		memcpy(&hca.rvaVolume, &rvaCast, sizeof(float));
	} else {
		bitSkip(br, -32);
		hca.rvaVolume = 1.0f;
	}

	// comment
	chunkId = bitRead(br, 32) & kChunkMask;
	if (chunkId == 0x636F6D6D) { // "comm"
		uint32 commentLen = bitRead(br, 8);
		hca.commentLen = commentLen;
		for (uint32 i = 0; i < commentLen; i++) {
			if (i < sizeof(hca.comment) - 1)
				hca.comment[i] = (char)bitRead(br, 8);
		}
		hca.comment[hca.commentLen < sizeof(hca.comment) ? hca.commentLen : sizeof(hca.comment) - 1] = '\0';
	} else {
		bitSkip(br, -32);
		hca.commentLen = 0;
	}

	// padding (fills up to headerSize sans checksum -- ignored)

	// extra validations
	// VBR frames are variable-sized, but this stream decodes fixed-size frames
	// and cannot safely resolve a zero frame size.
	if (hca.frameSize == 0 ||
	    !(hca.frameSize >= kMinFrameSize && hca.frameSize <= kMaxFrameSize))
		return false;
	if ((uint64)hca.encoderDelay + hca.encoderPadding > (uint64)hca.frameCount * kFrameSamples)
		return false;

	if (hca.version <= kVersion200) {
		if (hca.minResolution != 1 || hca.maxResolution != 15)
			return false;
	} else {
		if (hca.minResolution > hca.maxResolution || hca.maxResolution > 15)
			return false;
	}

	if (hca.trackCount == 0)
		hca.trackCount = 1;
	if (hca.trackCount > hca.channels)
		return false;

	if (hca.totalBandCount > kSamplesPerFrame || hca.totalBandCount == 0 ||
	    hca.baseBandCount + hca.stereoBandCount > hca.totalBandCount ||
	    hca.baseBandCount + hca.stereoBandCount == 0 ||
	    hca.stereoBandCount > hca.baseBandCount ||
	    hca.bandsPerHFRGroup > kSamplesPerFrame)
		return false;

	hca.hfrGroupCount = headerCeil2(
			hca.totalBandCount - hca.baseBandCount - hca.stereoBandCount,
			hca.bandsPerHFRGroup);

	hca.valid = 1;
	return true;
}

// Frame decode steps. All operate on raw float spectra.

int unpackScalefactors(HCAChannel *ch, BitReader *br, uint32 hfrGroupCount, uint32 version) {
	uint32 csCount = ch->codedCount;
	uint32 hsCount, sfCount;

	uint32 deltaBits = bitRead(*br, 3);

	if (ch->type == kChannelStereoSecondary || hfrGroupCount == 0 || version <= kVersion200)
		hsCount = 0;
	else
		hsCount = hfrGroupCount;

	sfCount = csCount + hsCount;

	if (deltaBits >= 6) {
		byte value = (byte)bitRead(*br, 6);
		ch->scalefactors[0] = value;
		for (uint32 i = 1; i < sfCount; i++)
			ch->scalefactors[i] = (byte)bitRead(*br, 6);
	} else if (deltaBits > 0) {
		const byte expectedDelta = (byte)((1 << deltaBits) - 1);
		byte value = (byte)bitRead(*br, 6);

		ch->scalefactors[0] = value;
		for (uint32 i = 1; i < sfCount; i++) {
			byte delta = (byte)bitRead(*br, deltaBits);
			if (delta == expectedDelta) {
				value = (byte)bitRead(*br, 6);
			} else {
				int scalefactorTest = (int)value + ((int)delta - (int)(expectedDelta >> 1));
				if (scalefactorTest < 0 || scalefactorTest >= 64)
					return 0;
				value = (byte)(value - (expectedDelta >> 1) + delta);
				value = (byte)(value & 0x3F);
			}
			ch->scalefactors[i] = value;
		}
	} else {
		for (uint32 i = 0; i < kSamplesPerFrame; i++)
			ch->scalefactors[i] = 0;
	}

	// copy v3.0 HFR scalefactors towards end
	for (uint32 i = hsCount; i > 0; i--)
		ch->scalefactors[kSamplesPerFrame - 1 - hsCount + i] = ch->scalefactors[csCount - 1 + i];

	return 1;
}

void unpackIntensity(HCAChannel *ch, BitReader *br, uint32 hfrGroupCount, uint32 version) {
	if (ch->type == kChannelStereoSecondary) {
		if (version <= kVersion200) {
			byte value = (byte)bitPeek(*br, 4);
			ch->intensity[0] = value;
			if (value < 15) {
				bitSkip(*br, 4);
				for (uint32 i = 1; i < kSubframes; i++)
					ch->intensity[i] = (byte)bitRead(*br, 4);
			}
		} else {
			byte value = (byte)bitPeek(*br, 4);
			uint32 deltaBits;

			if (value < 15) {
				bitSkip(*br, 4);
				deltaBits = bitRead(*br, 2);

				ch->intensity[0] = value;
				if (deltaBits == 3) {
					for (uint32 i = 1; i < kSubframes; i++)
						ch->intensity[i] = (byte)bitRead(*br, 4);
				} else {
					byte bmax = (byte)((2 << deltaBits) - 1);
					uint32 bits = deltaBits + 1;

					for (uint32 i = 1; i < kSubframes; i++) {
						byte delta = (byte)bitRead(*br, bits);
						if (delta == bmax) {
							value = (byte)bitRead(*br, 4);
						} else {
							value = (byte)(value - (bmax >> 1) + delta);
							if (value > 15)
								value = 0;
						}
						ch->intensity[i] = value;
					}
				}
			} else {
				bitSkip(*br, 4);
				for (uint32 i = 0; i < kSubframes; i++)
					ch->intensity[i] = 7;
			}
		}
	} else {
		if (version <= kVersion200) {
			byte *hfrScales = &ch->scalefactors[kSamplesPerFrame - hfrGroupCount];
			for (uint32 i = 0; i < hfrGroupCount; i++)
				hfrScales[i] = (byte)bitRead(*br, 6);
		}
	}
}

void calculateResolution(HCAChannel *ch, uint32 packedNoiseLevel, const byte *athCurve, uint32 minResolution, uint32 maxResolution) {
	uint32 crCount = ch->codedCount;
	uint32 noiseCount = 0;
	uint32 validCount = 0;

	for (uint32 i = 0; i < crCount; i++) {
		byte newResolution = 0;
		byte scalefactor = ch->scalefactors[i];

		if (scalefactor > 0) {
			int noiseLevel = athCurve[i] + ((packedNoiseLevel + i) >> 8);
			int curvePosition = noiseLevel + 1 - ((5 * scalefactor) >> 1);

			if (curvePosition < 0) {
				newResolution = 15;
			} else if (curvePosition <= 65) {
				newResolution = HCATables::kInvertTable[curvePosition];
			} else {
				newResolution = 0;
			}

			if (newResolution > maxResolution)
				newResolution = (byte)maxResolution;
			else if (newResolution < minResolution)
				newResolution = (byte)minResolution;

			if (newResolution < 1) {
				ch->noises[noiseCount] = i;
				noiseCount++;
			} else {
				ch->noises[kSamplesPerFrame - 1 - validCount] = i;
				validCount++;
			}
		}
		ch->resolution[i] = newResolution;
	}

	ch->noiseCount = noiseCount;
	ch->validCount = validCount;
	memset(&ch->resolution[crCount], 0, kSamplesPerFrame - crCount);
}

void calculateGain(HCAChannel *ch) {
	for (uint32 i = 0; i < ch->codedCount; i++) {
		float scalefactorScale = floatFromBits(HCATables::kQuantizerScalingTable[ch->scalefactors[i]]);
		float resolutionScale = floatFromBits(HCATables::kQuantizerRangeTable[ch->resolution[i]]);
		ch->gain[i] = scalefactorScale * resolutionScale;
	}
}

void dequantizeCoefficients(HCAChannel *ch, BitReader *br, int subframe) {
	for (uint32 i = 0; i < ch->codedCount; i++) {
		float qc;
		byte resolution = ch->resolution[i];
		uint32 bits = HCATables::kMaxBitTable[resolution];
		uint32 code = bitRead(*br, bits);

		if (resolution > 7) {
			// sign-magnitude form (lowest bit = sign)
			int signedCode = (1 - ((int)(code & 1) << 1)) * ((int)code >> 1);
			if (signedCode == 0)
				bitSkip(*br, -1);
			qc = (float)signedCode;
		} else {
			int index = (resolution << 4) + code;
			int skip = HCATables::kReadBitTable[index] - bits;
			bitSkip(*br, skip);
			qc = HCATables::kReadValTable[index];
		}

		ch->spectra[subframe][i] = ch->gain[i] * qc;
	}

	memset(&ch->spectra[subframe][ch->codedCount], 0, sizeof(float) * (kSamplesPerFrame - ch->codedCount));
}

void reconstructNoise(HCAChannel *ch, uint32 minResolution, uint32 msStereo, uint32 *randomP, int subframe) {
	if (minResolution > 0)
		return;
	if (ch->validCount == 0 || ch->noiseCount == 0)
		return;
	if (msStereo && ch->type != kChannelStereoPrimary)
		return;

	uint32 random = *randomP;
	for (uint32 i = 0; i < ch->noiseCount; i++) {
		random = 0x343FD * random + 0x269EC3;
		uint32 randomOut = (random >> 16) & 0x7FFF;

		uint32 randomIndex = kSamplesPerFrame - ch->validCount + ((randomOut * ch->validCount) >> 15);

		uint32 targetIndex = ch->noises[i];
		uint32 sourceIndex = ch->noises[randomIndex];

		byte sfTarget = ch->scalefactors[targetIndex];
		byte sfSource = ch->scalefactors[sourceIndex];

		int scIndex = (int)sfTarget - (int)sfSource + 62;
		if (scIndex < 0)
			scIndex = 0;

		ch->spectra[subframe][targetIndex] =
			floatFromBits(HCATables::kScaleConversionTable[scIndex]) * ch->spectra[subframe][sourceIndex];
	}
	*randomP = random;
}

void reconstructHighFrequency(HCAChannel *ch, uint32 hfrGroupCount, uint32 bandsPerHFRGroup,
		uint32 stereoBandCount, uint32 baseBandCount, uint32 totalBandCount, uint32 version, int subframe) {
	if (bandsPerHFRGroup == 0)
		return;
	if (ch->type == kChannelStereoSecondary)
		return;

	int groupLimit;
	int startBand = stereoBandCount + baseBandCount;
	int highband = startBand;
	int lowband = startBand - 1;
	byte *hfrScalefactors = &ch->scalefactors[128 - hfrGroupCount];

	if (version <= kVersion200) {
		groupLimit = (int)hfrGroupCount;
	} else {
		groupLimit = (int)hfrGroupCount;
		groupLimit >>= 1;
	}

	for (uint32 group = 0; group < hfrGroupCount; group++) {
		int scIndex;
		int lowbandAdjust = ((int)group < groupLimit) ? -1 : 1;

		if (highband >= (int)totalBandCount || lowband < 0)
			break;

		for (uint32 i = 0; i < bandsPerHFRGroup; i++) {
			if (highband >= (int)totalBandCount || lowband < 0)
				break;

			scIndex = (int)hfrScalefactors[group] - (int)ch->scalefactors[lowband] + 63;
			if (scIndex < 0)
				scIndex = 0;

			ch->spectra[subframe][highband] = floatFromBits(HCATables::kScaleConversionTable[scIndex]) * ch->spectra[subframe][lowband];

			highband += 1;
			lowband += lowbandAdjust;
		}
	}

	ch->spectra[subframe][highband - 1] = 0.0f;
}

void applyIntensityStereo(HCAChannel *chPair, int subframe, uint32 baseBandCount, uint32 totalBandCount) {
	if (chPair[0].type != kChannelStereoPrimary)
		return;

	int minBand = (int)baseBandCount;
	int maxBands = (int)totalBandCount;

	float ratioL = floatFromBits(HCATables::kIntensityRatioTable[chPair[1].intensity[subframe]]);
	float ratioR = 2.0f - ratioL;
	float *spL = &chPair[0].spectra[subframe][0];
	float *spR = &chPair[1].spectra[subframe][0];

	for (int band = minBand; band < maxBands; band++) {
		float coefL = spL[band] * ratioL;
		float coefR = spL[band] * ratioR;
		spL[band] = coefL;
		spR[band] = coefR;
	}
}

void applyMS_Stereo(HCAChannel *chPair, uint32 msStereo, uint32 baseBandCount, uint32 totalBandCount, int subframe) {
	if (!msStereo)
		return;
	if (chPair[0].type != kChannelStereoPrimary)
		return;

	int minBand = 0;
	int maxBands = (int)baseBandCount;

	const float ratio = 0.707106769084930419921875f; // 1 / sqrt(2)
	float *spL = &chPair[0].spectra[subframe][0];
	float *spR = &chPair[1].spectra[subframe][0];

	for (int band = minBand; band < maxBands; band++) {
		float coefL = (spL[band] + spR[band]) * ratio;
		float coefR = (spL[band] - spR[band]) * ratio;
		spL[band] = coefL;
		spR[band] = coefR;
	}
}

void imdctTransform(HCAChannel *ch, int subframe) {
	const uint32 size = kSamplesPerFrame;
	const uint32 half = kSamplesPerFrame / 2;
	const uint32 mdctBits = kMDCTBits;

	// pre-rotation butterflies
	{
		uint32 count1 = 1;
		uint32 count2 = half;
		float *temp1 = &ch->spectra[subframe][0];
		float *temp2 = &ch->temp[0];

		for (uint32 i = 0; i < mdctBits; i++) {
			float *swap;
			float *d1 = &temp2[0];
			float *d2 = &temp2[count2];

			for (uint32 j = 0; j < count1; j++) {
				for (uint32 k = 0; k < count2; k++) {
					float a = *(temp1++);
					float b = *(temp1++);
					*(d1++) = a + b;
					*(d2++) = a - b;
				}
				d1 += count2;
				d2 += count2;
			}
			swap = temp1 - kSamplesPerFrame;
			temp1 = temp2;
			temp2 = swap;

			count1 = count1 << 1;
			count2 = count2 >> 1;
		}
	}

	// main DCT-IV twiddles
	{
		uint32 count1 = half;
		uint32 count2 = 1;
		float *temp1 = &ch->temp[0];
		float *temp2 = &ch->spectra[subframe][0];

		for (uint32 i = 0; i < mdctBits; i++) {
			const float *sinTable = (const float *)HCATables::kSinTables[i];
			const float *cosTable = (const float *)HCATables::kCosTables[i];
			float *swap;
			float *d1 = &temp2[0];
			float *d2 = &temp2[count2 * 2 - 1];
			const float *s1 = &temp1[0];
			const float *s2 = &temp1[count2];

			for (uint32 j = 0; j < count1; j++) {
				for (uint32 k = 0; k < count2; k++) {
					float a = *(s1++);
					float b = *(s2++);
					float sin = *(sinTable++);
					float cos = *(cosTable++);
					*(d1++) = a * sin - b * cos;
					*(d2--) = a * cos + b * sin;
				}
				s1 += count2;
				s2 += count2;
				d1 += count2;
				d2 += count2 * 3;
			}
			swap = temp1;
			temp1 = temp2;
			temp2 = swap;

			count1 = count1 >> 1;
			count2 = count2 << 1;
		}
	}

	// update output with overlapped window
	{
		const float *dct = &ch->spectra[subframe][0];
		const float *prev = &ch->imdctPrevious[0];

		for (uint32 i = 0; i < half; i++) {
			ch->wave[subframe][i] = floatFromBits(HCATables::kWindowTable[i]) * dct[i + half] + prev[i];
			ch->wave[subframe][i + half] = floatFromBits(HCATables::kWindowTable[i + half]) * dct[size - 1 - i] - prev[i + half];
			ch->imdctPrevious[i] = floatFromBits(HCATables::kWindowTable[size - 1 - i]) * dct[half - i - 1];
			ch->imdctPrevious[i + half] = floatFromBits(HCATables::kWindowTable[half - i - 1]) * dct[i];
		}
	}
}

class HCAStream : public Audio::SeekableAudioStream {
public:
	HCAStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, uint64 keycode)
		: _stream(stream), _dispose(dispose), _keycode(keycode) {
		const int64 pos = _stream->pos();
		_ok = parseHCAHeader(*_stream, _header);
		_stream->seek(pos);
		if (!_ok) {
			return;
		}
		_header.keycode = keycode;

		// init channel types
		ChannelType channelTypes[kMaxChannels] = {kChannelDiscrete};
		setupChannelTypes(_header.channels, _header.trackCount, _header.channelConfig, _header.stereoBandCount, channelTypes);
		_channels.resize(_header.channels);
		for (uint32 i = 0; i < _header.channels; i++) {
			_channels[i].type = channelTypes[i];
			_channels[i].codedCount = _header.baseBandCount +
				(channelTypes[i] == kChannelStereoSecondary ? 0 : _header.stereoBandCount);
		}
		_random = kDefaultRandom;

		_ok = athInit(_athCurve, _header.athType, _header.sampleRate);
		if (!_ok)
			return;
		_ok = cipherInit(_cipherTable, _header.ciphType, keycode);
		if (!_ok)
			return;

		_header.valid = 1;
		_totalSamples = (uint32)((uint64)_header.frameCount * kFrameSamples -
			_header.encoderDelay - _header.encoderPadding);

		_frameBuf.resize(_header.frameSize);
		_decoded.resize(kFrameSamples * _header.channels);

		_stream->seek(_header.headerSize);
		rewind();
	}

	~HCAStream() override {
		if (_dispose == DisposeAfterUse::YES)
			delete _stream;
	}

	int readBuffer(int16 *buffer, const int numSamples) override {
		if (!_ok)
			return 0;

		uint32 channels = _header.channels;
		int samples = 0;
		while ((uint32)(numSamples - samples) >= channels && !endOfData()) {
			if (_bufPos >= kFrameSamples && !decodeFrame())
				break;

			uint32 framesAvail = kFrameSamples - _bufPos;
			if (_encoderDelayRemaining > 0) {
				uint32 skipped = MIN(_encoderDelayRemaining, framesAvail);
				_bufPos += skipped;
				_encoderDelayRemaining -= skipped;
				continue;
			}
			uint32 framesWanted = (uint32)(numSamples - samples) / channels;
			uint32 toCopy = MIN(framesAvail, MIN(framesWanted, _totalSamples - _samplesRead));
			if (toCopy == 0)
				break;

			for (uint32 ch = 0; ch < channels; ch++)
				for (uint32 f = 0; f < toCopy; f++)
					buffer[(samples + f * channels) + ch] = _decoded[f * channels + ch];
			samples += toCopy * channels;
			_bufPos += toCopy;
			_samplesRead += toCopy;
		}
		return samples;
	}

	bool isStereo() const override { return _header.channels == 2; }
	int getRate() const override { return (int)_header.sampleRate; }
	bool endOfData() const override { return _samplesRead >= _totalSamples; }

	bool rewind() override {
		_stream->seek(_header.headerSize);
		_random = kDefaultRandom;
		for (uint32 i = 0; i < _channels.size(); i++)
			memset(_channels[i].imdctPrevious, 0, sizeof(float) * kSamplesPerFrame);
		_samplesRead = 0;
		_samplesInBuf = 0;
		_bufPos = kFrameSamples;
		_encoderDelayRemaining = _header.encoderDelay;
		return true;
	}

	bool seek(const Audio::Timestamp &where) override {
		uint32 targetSample = (uint32)where.convertToFramerate(_header.sampleRate).totalNumberOfFrames();
		if (targetSample >= _totalSamples) {
			_samplesRead = _totalSamples;
			return true;
		}

		rewind();
		uint32 rawTargetSample = targetSample + _header.encoderDelay;
		uint32 frame = rawTargetSample / kFrameSamples;
		_stream->seek(_header.headerSize + (int64)frame * _header.frameSize);
		_samplesRead = targetSample;

		uint32 remainder = rawTargetSample - frame * kFrameSamples;
		if (remainder > 0) {
			if (!decodeFrame())
				return false;
			_bufPos = remainder;
		}
		_encoderDelayRemaining = 0;
		return true;
	}

	Audio::Timestamp getLength() const override {
		return Audio::Timestamp(0, _totalSamples, _header.sampleRate);
	}

private:
	Common::SeekableReadStream *_stream;
	DisposeAfterUse::Flag _dispose;
	HCAHeader _header;
	uint64 _keycode;
	bool _ok = false;
	// frameCount * kFrameSamples, cached after header validation (0 when the
	// header is invalid) so it is never recomputed with a wrapped value.
	uint32 _totalSamples = 0;

	Common::Array<HCAChannel> _channels;
	byte _athCurve[kSamplesPerFrame];
	byte _cipherTable[256];
	uint32 _random = kDefaultRandom;

	Common::Array<byte> _frameBuf;
	Common::Array<int16> _decoded;
	uint32 _samplesRead = 0;
	uint32 _samplesInBuf = 0;
	uint32 _bufPos = kFrameSamples;
	uint32 _encoderDelayRemaining = 0;
	bool _eofPending = false;

	bool decodeFrame() {
		if (_stream->read(_frameBuf.data(), _header.frameSize) != _header.frameSize) {
			_eofPending = true;
			return false;
		}

		unsigned short sync = (_frameBuf[0] << 8) | _frameBuf[1];
		if (sync != 0xFFFF) {
			_eofPending = true;
			return false;
		}

		if (crc16(_frameBuf.data(), _header.frameSize)) {
			_eofPending = true;
			return false;
		}

		cipherDecrypt(_cipherTable, _frameBuf.data(), _header.frameSize);

		BitReader br;
		bitInit(br, _frameBuf.data(), _header.frameSize);
		bitSkip(br, 16); // sync (0xFFFF), already validated

		uint32 frame_acceptable_noise_level = bitRead(br, 9);
		uint32 frame_evaluation_boundary = bitRead(br, 7);
		uint32 packed_noise_level = (frame_acceptable_noise_level << 8) - frame_evaluation_boundary;

		for (uint32 ch = 0; ch < _header.channels; ch++) {
			if (!unpackScalefactors(&_channels[ch], &br, _header.hfrGroupCount, _header.version)) {
				_eofPending = true;
				return false;
			}
			unpackIntensity(&_channels[ch], &br, _header.hfrGroupCount, _header.version);
			calculateResolution(&_channels[ch], packed_noise_level, _athCurve, _header.minResolution, _header.maxResolution);
			calculateGain(&_channels[ch]);
		}

		for (uint32 subframe = 0; subframe < kSubframes; subframe++) {
			for (uint32 ch = 0; ch < _header.channels; ch++)
				dequantizeCoefficients(&_channels[ch], &br, subframe);
		}

		// transform each subframe
		for (uint32 subframe = 0; subframe < kSubframes; subframe++) {
			uint32 hfrChannels = _header.channelConfig & 0x80 ? 1 : _header.channels;

			for (uint32 ch = 0; ch < hfrChannels; ch++) {
				reconstructNoise(&_channels[ch], _header.minResolution, _header.msStereo, &_random, subframe);
				reconstructHighFrequency(&_channels[ch], _header.hfrGroupCount, _header.bandsPerHFRGroup,
						_header.stereoBandCount, _header.baseBandCount, _header.totalBandCount, _header.version, subframe);
			}

			if (_header.stereoBandCount > 0) {
				for (uint32 ch = 0; ch < _header.channels - 1; ch++) {
					applyIntensityStereo(&_channels[ch], subframe, _header.baseBandCount, _header.totalBandCount);
					applyMS_Stereo(&_channels[ch], _header.msStereo, _header.baseBandCount, _header.totalBandCount, subframe);
				}
			}

			for (uint32 ch = 0; ch < _header.channels; ch++)
				imdctTransform(&_channels[ch], subframe);
		}

		// convert to int16
		const float scale = 32768.0f;
		for (uint32 sf = 0; sf < kSubframes; sf++) {
			for (uint32 s = 0; s < kSamplesPerFrame; s++) {
				for (uint32 ch = 0; ch < _header.channels; ch++) {
					float fsample = _channels[ch].wave[sf][s];
					int32 psample = (int32)(fsample * scale);
					if (psample > 32767) psample = 32767;
					else if (psample < -32768) psample = -32768;
					_decoded[(sf * kSamplesPerFrame + s) * _header.channels + ch] = (int16)psample;
				}
			}
		}

		_samplesInBuf = kFrameSamples;
		_bufPos = 0;
		return true;
	}
};

} // namespace

Audio::SeekableAudioStream *makeHCAStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse) {
	return makeHCAStream(stream, disposeAfterUse, 0);
}

Audio::SeekableAudioStream *makeHCAStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse, uint64 keycode) {
	return new HCAStream(stream, disposeAfterUse, keycode);
}

} // namespace Common
