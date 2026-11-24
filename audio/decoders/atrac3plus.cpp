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

// The decode algorithm here (word-length/scale-factor/code-table
// bitstream decode, spectral Huffman decode, gain compensation, GHA
// sine-wave tone synthesis, noise/power compensation, and the 16-band
// polyphase (PQF) synthesis filter) is ported from FFmpeg's
// libavcodec/atrac3plus.c, atrac3plusdec.c and atrac3plusdsp.c -- real
// facts/formulas only, re-expressed in ScummVM idiom, no code copied
// verbatim. Those files are:
//   Copyright (c) 2010-2013 Maxim Poliakovski
// and licensed under the GNU Lesser General Public License, version 2.1 or
// later -- see LICENSES/COPYING.LGPL in this repository for the full text.

#include "audio/decoders/atrac3plus.h"

#include "audio/audiostream.h"
#include "common/array.h"
#include "common/bitstream.h"
#include "common/formats/atrac3plus_dsp_tables.h"
#include "common/formats/atrac3plus_tables.h"
#include "common/intrinsics.h"
#include "common/util.h"
#include "math/mdct.h"

#include <math.h>

namespace Audio {

namespace {

// FFmpeg's av_log2() returns 0 for an input of 0, whereas
// avLog2() returns -1. ATRAC3+ derives sub-field widths from these
// values (nbits = log2(v) + 1), so a zero must still consume one bit. Using
// intLog2 directly desynchronised the frame on real PSMF/P3P opening audio
// (the decoder stalled after a few frames); matched to av_log2 semantics here.
static inline int avLog2(uint32 v) { return v ? Common::intLog2(v) : 0; }


using namespace Common::ATRAC3PlusTables;

const int kFrameSamples = 2048;    // ATRAC3P_FRAME_SAMPLES
const int kSubbands = 16;          // ATRAC3P_SUBBANDS
const int kSubbandSamples = 128;   // ATRAC3P_SUBBAND_SAMPLES
const int kMDCTSize = 256;         // kSubbandSamples * 2
const int kMDCTBits = 8;           // Math::MDCT: N = 2^bits = 256
const int kPQFFIRLen = 12;         // ATRAC3P_PQF_FIR_LEN
const int kPowerCompOff = 15;      // ATRAC3P_POWER_COMP_OFF

const int kGainIdToExpOffset = 6;
const int kGainLocScaleBits = 2;
const int kGainLocSize = 1 << kGainLocScaleBits;

enum ChannelUnitType {
	kChUnitMono = 0,
	kChUnitStereo = 1,
	kChUnitExtension = 2,
	kChUnitTerminator = 3
};

// Real per-channel-count layout (channel_blocks[]) and output channel
// reorder (channel_map[]) from atrac3plusdec.c's set_channel_params().
struct ChannelLayout {
	int numBlocks;
	byte blocks[5];
	byte map[8];
};

const ChannelLayout kChannelLayouts[9] = {
	/* 0 (unused) */ {0, {0}, {0}},
	/* 1 */ {1, {kChUnitMono}, {0}},
	/* 2 */ {1, {kChUnitStereo}, {0, 1}},
	/* 3 */ {2, {kChUnitStereo, kChUnitMono}, {0, 1, 2}},
	/* 4 */ {3, {kChUnitStereo, kChUnitMono, kChUnitMono}, {0, 1, 2, 3}},
	/* 5 (unsupported) */ {0, {0}, {0}},
	/* 6 */ {4, {kChUnitStereo, kChUnitMono, kChUnitStereo, kChUnitMono}, {0, 1, 2, 4, 5, 3}},
	/* 7 */ {5, {kChUnitStereo, kChUnitMono, kChUnitStereo, kChUnitMono, kChUnitMono}, {0, 1, 2, 4, 5, 6, 3}},
	/* 8 */ {5, {kChUnitStereo, kChUnitMono, kChUnitStereo, kChUnitStereo, kChUnitMono}, {0, 1, 2, 4, 5, 6, 7, 3}},
};

typedef Common::BitStreamMemory8MSB BitReader;

int32 signExtend(int32 val, int bits) {
	if (bits <= 0)
		return 0;
	int32 shift = 32 - bits;
	return (val << shift) >> shift;
}

uint32 zeroExtend(uint32 val, int bits) {
	if (bits <= 0 || bits >= 32)
		return val;
	return val & ((1u << bits) - 1);
}

// FFmpeg's get_bitsz(): like get_bits(), but 0 bits is legal and yields 0.
uint32 getBitsZ(BitReader &bits, int n) {
	return n > 0 ? bits.getBits(n) : 0;
}

/* ---- canonical-Huffman VLC subsystem (variable code length, up to 12 bits) ---- */

struct VLCTable {
	Common::Array<byte> value;
	Common::Array<byte> length;
	int maxLen = 0;
};

void buildCanonicalHuff(const int8 *cb, const byte *&xlat, VLCTable &out) {
	byte lens[256];
	int index = 0;
	for (int b = 1; b <= 12; b++) {
		// The real reference always reads these counts through a
		// uint8_t*-typed parameter, even for atrac3p_spectra_cbs's real
		// int8_t-declared array (which needs signed values for its
		// separate "reuse" marker in element 0) -- reinterpret unsigned
		// here to match, recovering 172 from a stored -84 for one real
		// table (see atrac3plus_tables.h).
		int count = (byte)cb[b - 1];
		for (int i = 0; i < count; i++)
			lens[index++] = (byte)b;
	}
	if (index == 0) {
		out.maxLen = 0;
		return;
	}

	int maxLen = lens[index - 1];
	out.maxLen = maxLen;
	int tableSize = 1 << maxLen;
	out.value.resize(tableSize, 0);
	out.length.resize(tableSize, 0);

	int blCount[13] = {0};
	for (int i = 0; i < index; i++)
		blCount[lens[i]]++;
	int nextCode[13] = {0};
	int code = 0;
	for (int len = 1; len <= 12; len++) {
		code = (code + blCount[len - 1]) << 1;
		nextCode[len] = code;
	}

	for (int i = 0; i < index; i++) {
		int len = lens[i];
		int c = nextCode[len]++;
		int shift = maxLen - len;
		int base = c << shift;
		int span = 1 << shift;
		byte symbol = xlat[i];
		for (int k = 0; k < span; k++) {
			out.value[base + k] = symbol;
			out.length[base + k] = (byte)len;
		}
	}
	xlat += index;
}

byte readVLC(BitReader &bits, const VLCTable &tab) {
	if (tab.maxLen == 0)
		return 0;
	uint32 peeked = bits.peekBits(tab.maxLen);
	byte len = tab.length[peeked];
	byte val = tab.value[peeked];
	bits.skip(len);
	return val;
}

struct VLCSet {
	VLCTable wl[4];
	VLCTable ct[4];
	VLCTable sf[8];
	VLCTable spec[112];
	VLCTable gain[11];
	VLCTable tone[7];

	VLCSet() {
		const byte *xlat = atrac3p_wl_ct_xlats;
		for (int i = 0; i < 4; i++) {
			buildCanonicalHuff((const int8 *)atrac3p_wl_cbs[i], xlat, wl[i]);
			buildCanonicalHuff((const int8 *)atrac3p_ct_cbs[i], xlat, ct[i]);
		}

		xlat = atrac3p_sf_xlats;
		for (int i = 0; i < 8; i++)
			buildCanonicalHuff((const int8 *)atrac3p_sf_cbs[i], xlat, sf[i]);

		xlat = atrac3p_spectra_xlats;
		for (int i = 0; i < 112; i++) {
			if (atrac3p_spectra_cbs[i][0] >= 0)
				buildCanonicalHuff(atrac3p_spectra_cbs[i], xlat, spec[i]);
			else
				spec[i] = spec[-atrac3p_spectra_cbs[i][0]];
		}

		xlat = atrac3p_gain_xlats;
		for (int i = 0; i < 11; i++)
			buildCanonicalHuff((const int8 *)atrac3p_gain_cbs[i], xlat, gain[i]);

		xlat = atrac3p_tone_xlats;
		for (int i = 0; i < 7; i++)
			buildCanonicalHuff((const int8 *)atrac3p_tone_cbs[i], xlat, tone[i]);
	}
};

const VLCSet &vlcs() {
	static const VLCSet v;
	return v;
}

/* ---- per-unit data structures (facts: real field layout from atrac3plus.h) ---- */

struct GainInfo {
	int numPoints = 0;
	int levCode[8] = {0};
	int locCode[8] = {0};
};

struct WaveEnvelope {
	int hasStartPoint = 0;
	int hasStopPoint = 0;
	int startPos = 0;
	int stopPos = 0;
};

struct WavesData {
	WaveEnvelope pendEnv;
	WaveEnvelope currEnv;
	int numWavs = 0;
	int startIndex = 0;
};

struct WaveParam {
	int freqIndex = 0;
	int ampSf = 0;
	int ampIndex = 0;
	int phaseIndex = 0;
};

struct WaveSynthParams {
	int tonesPresent = 0;
	int amplitudeMode = 0;
	int numToneBands = 0;
	byte toneSharing[kSubbands] = {0};
	byte toneMaster[kSubbands] = {0};
	byte invertPhase[kSubbands] = {0};
	int tonesIndex = 0;
	WaveParam waves[48];
};

struct ChanParams {
	int chNum = 0;
	int numCodedVals = 0;
	int fillMode = 0;
	int splitPoint = 0;
	int tableType = 0;
	int quWordlen[32] = {0};
	int quSfIdx[32] = {0};
	int quTabIdx[32] = {0};
	int16 spectrum[kFrameSamples] = {0};
	byte powerLevs[5] = {0};

	byte wndShapeHist[2][kSubbands] = {{0}};
	byte *wndShape = nullptr;
	byte *wndShapePrev = nullptr;

	GainInfo gainDataHist[2][kSubbands];
	GainInfo *gainData = nullptr;
	GainInfo *gainDataPrev = nullptr;
	int numGainSubbands = 0;

	WavesData tonesInfoHist[2][kSubbands];
	WavesData *tonesInfo = nullptr;
	WavesData *tonesInfoPrev = nullptr;

	void init(int ch) {
		chNum = ch;
		wndShape = &wndShapeHist[0][0];
		wndShapePrev = &wndShapeHist[1][0];
		gainData = &gainDataHist[0][0];
		gainDataPrev = &gainDataHist[1][0];
		tonesInfo = &tonesInfoHist[0][0];
		tonesInfoPrev = &tonesInfoHist[1][0];
	}
};

struct IPQFChannelCtx {
	float buf1[kPQFFIRLen * 2][8] = {{0}};
	float buf2[kPQFFIRLen * 2][8] = {{0}};
	int pos = 0;
};

struct ChanUnitCtx {
	int unitType = 0;
	int numQuantUnits = 0;
	int numSubbands = 0;
	int usedQuantUnits = 0;
	int numCodedSubbands = 0;
	int muteFlag = 0;
	int useFullTable = 0;
	int noisePresent = 0;
	int noiseLevelIndex = 0;
	int noiseTableIndex = 0;
	byte swapChannels[kSubbands] = {0};
	byte negateCoeffs[kSubbands] = {0};
	ChanParams channels[2];

	WaveSynthParams waveSynthHist[2];
	WaveSynthParams *wavesInfo = nullptr;
	WaveSynthParams *wavesInfoPrev = nullptr;

	IPQFChannelCtx ipqfCtx[2];
	float prevBuf[2][kFrameSamples] = {{0}};

	void init() {
		channels[0].init(0);
		channels[1].init(1);
		wavesInfo = &waveSynthHist[0];
		wavesInfoPrev = &waveSynthHist[1];
	}
};

/* ---- bitstream decode: word length ---- */

// Real result codes: true = OK, false = malformed bitstream.
bool numCodedUnits(BitReader &gb, ChanParams &chan, ChanUnitCtx &ctx) {
	chan.fillMode = (int)gb.getBits(2);
	if (!chan.fillMode) {
		chan.numCodedVals = ctx.numQuantUnits;
	} else {
		chan.numCodedVals = (int)gb.getBits(5);
		if (chan.numCodedVals > ctx.numQuantUnits)
			return false;
		if (chan.fillMode == 3)
			chan.splitPoint = (int)gb.getBits(2) + (chan.chNum << 1) + 1;
	}
	return true;
}

bool addWordlenWeights(ChanUnitCtx &ctx, ChanParams &chan, int wtabIdx) {
	const int8 *weights = &atrac3p_wl_weights[chan.chNum * 3 + wtabIdx - 1][0];
	for (int i = 0; i < ctx.numQuantUnits; i++) {
		chan.quWordlen[i] += weights[i];
		if (chan.quWordlen[i] < 0 || chan.quWordlen[i] > 7)
			return false;
	}
	return true;
}

bool subtractSfWeights(ChanUnitCtx &ctx, ChanParams &chan, int wtabIdx) {
	const int8 *weights = &atrac3p_sf_weights[wtabIdx - 1][0];
	for (int i = 0; i < ctx.usedQuantUnits; i++) {
		chan.quSfIdx[i] -= weights[i];
		if (chan.quSfIdx[i] < 0 || chan.quSfIdx[i] > 63)
			return false;
	}
	return true;
}

void unpackVqShape(int startVal, const int8 *shapeVec, int *dst, int numValues) {
	if (numValues) {
		dst[0] = dst[1] = dst[2] = startVal;
		for (int i = 3; i < numValues; i++)
			dst[i] = startVal - shapeVec[atrac3p_qu_num_to_seg[i] - 1];
	}
}

bool decodeChannelWordlen(BitReader &gb, ChanUnitCtx &ctx, int chNum) {
	int weightIdx = 0;
	ChanParams &chan = ctx.channels[chNum];
	ChanParams &refChan = ctx.channels[0];

	chan.fillMode = 0;

	switch (gb.getBits(2)) {
	case 0:
		for (int i = 0; i < ctx.numQuantUnits; i++)
			chan.quWordlen[i] = (int)gb.getBits(3);
		break;
	case 1:
		if (chNum) {
			if (!numCodedUnits(gb, chan, ctx))
				return false;
			if (chan.numCodedVals) {
				const VLCTable &vlc = vlcs().wl[gb.getBits(2)];
				for (int i = 0; i < chan.numCodedVals; i++) {
					int delta = readVLC(gb, vlc);
					chan.quWordlen[i] = (refChan.quWordlen[i] + delta) & 7;
				}
			}
		} else {
			weightIdx = (int)gb.getBits(2);
			if (!numCodedUnits(gb, chan, ctx))
				return false;
			if (chan.numCodedVals) {
				int pos = (int)gb.getBits(5);
				if (pos > chan.numCodedVals)
					return false;
				int deltaBits = (int)gb.getBits(2);
				int minVal = (int)gb.getBits(3);
				for (int i = 0; i < pos; i++)
					chan.quWordlen[i] = (int)gb.getBits(3);
				for (int i = pos; i < chan.numCodedVals; i++)
					chan.quWordlen[i] = (minVal + (int)getBitsZ(gb, deltaBits)) & 7;
			}
		}
		break;
	case 2:
		if (!numCodedUnits(gb, chan, ctx))
			return false;
		if (chNum && chan.numCodedVals) {
			const VLCTable &vlc = vlcs().wl[gb.getBits(2)];
			int delta = readVLC(gb, vlc);
			chan.quWordlen[0] = (refChan.quWordlen[0] + delta) & 7;
			for (int i = 1; i < chan.numCodedVals; i++) {
				int diff = refChan.quWordlen[i] - refChan.quWordlen[i - 1];
				delta = readVLC(gb, vlc);
				chan.quWordlen[i] = (chan.quWordlen[i - 1] + diff + delta) & 7;
			}
		} else if (chan.numCodedVals) {
			int flag = (int)gb.getBits(1);
			const VLCTable &vlc = vlcs().wl[gb.getBits(1)];
			int startVal = (int)gb.getBits(3);
			unpackVqShape(startVal, &atrac3p_wl_shapes[startVal][gb.getBits(4)][0],
			              chan.quWordlen, chan.numCodedVals);
			if (!flag) {
				for (int i = 0; i < chan.numCodedVals; i++) {
					int delta = readVLC(gb, vlc);
					chan.quWordlen[i] = (chan.quWordlen[i] + delta) & 7;
				}
			} else {
				int i = 0;
				for (; i < (chan.numCodedVals & ~1); i += 2) {
					if (!gb.getBits(1)) {
						chan.quWordlen[i] = (chan.quWordlen[i] + readVLC(gb, vlc)) & 7;
						chan.quWordlen[i + 1] = (chan.quWordlen[i + 1] + readVLC(gb, vlc)) & 7;
					}
				}
				if (chan.numCodedVals & 1)
					chan.quWordlen[i] = (chan.quWordlen[i] + readVLC(gb, vlc)) & 7;
			}
		}
		break;
	case 3:
		weightIdx = (int)gb.getBits(2);
		if (!numCodedUnits(gb, chan, ctx))
			return false;
		if (chan.numCodedVals) {
			const VLCTable &vlc = vlcs().wl[gb.getBits(2)];
			chan.quWordlen[0] = (int)gb.getBits(3);
			for (int i = 1; i < chan.numCodedVals; i++) {
				int delta = readVLC(gb, vlc);
				chan.quWordlen[i] = (chan.quWordlen[i - 1] + delta) & 7;
			}
		}
		break;
	}

	if (chan.fillMode == 2) {
		for (int i = chan.numCodedVals; i < ctx.numQuantUnits; i++)
			chan.quWordlen[i] = chNum ? (int)gb.getBits(1) : 1;
	} else if (chan.fillMode == 3) {
		int pos = chNum ? chan.numCodedVals + chan.splitPoint
		                : ctx.numQuantUnits - chan.splitPoint;
		if (pos > 32)
			pos = 32;
		for (int i = chan.numCodedVals; i < pos; i++)
			chan.quWordlen[i] = 1;
	}

	if (weightIdx)
		return addWordlenWeights(ctx, chan, weightIdx);
	return true;
}

/* ---- bitstream decode: scale factors ---- */

bool decodeChannelSfIdx(BitReader &gb, ChanUnitCtx &ctx, int chNum) {
	int weightIdx = 0;
	ChanParams &chan = ctx.channels[chNum];
	ChanParams &refChan = ctx.channels[0];

	switch (gb.getBits(2)) {
	case 0:
		for (int i = 0; i < ctx.usedQuantUnits; i++)
			chan.quSfIdx[i] = (int)gb.getBits(6);
		break;
	case 1:
		if (chNum) {
			const VLCTable &vlc = vlcs().sf[gb.getBits(2)];
			for (int i = 0; i < ctx.usedQuantUnits; i++) {
				int delta = readVLC(gb, vlc);
				chan.quSfIdx[i] = (refChan.quSfIdx[i] + delta) & 0x3F;
			}
		} else {
			weightIdx = (int)gb.getBits(2);
			if (weightIdx == 3) {
				int startVal = (int)gb.getBits(6);
				unpackVqShape(startVal, &atrac3p_sf_shapes[gb.getBits(6)][0],
				              chan.quSfIdx, ctx.usedQuantUnits);

				int numLongVals = (int)gb.getBits(5);
				int deltaBits = (int)gb.getBits(2);
				int minVal = (int)gb.getBits(4) - 7;
				for (int i = 0; i < numLongVals; i++)
					chan.quSfIdx[i] = (chan.quSfIdx[i] + (int)gb.getBits(4) - 7) & 0x3F;
				for (int i = numLongVals; i < ctx.usedQuantUnits; i++)
					chan.quSfIdx[i] = (chan.quSfIdx[i] + minVal + (int)getBitsZ(gb, deltaBits)) & 0x3F;
			} else {
				int numLongVals = (int)gb.getBits(5);
				int deltaBits = (int)gb.getBits(3);
				int minVal = (int)gb.getBits(6);
				if (numLongVals > ctx.usedQuantUnits || deltaBits == 7)
					return false;
				for (int i = 0; i < numLongVals; i++)
					chan.quSfIdx[i] = (int)gb.getBits(6);
				for (int i = numLongVals; i < ctx.usedQuantUnits; i++)
					chan.quSfIdx[i] = (minVal + (int)getBitsZ(gb, deltaBits)) & 0x3F;
			}
		}
		break;
	case 2:
		if (chNum) {
			const VLCTable &vlc = vlcs().sf[gb.getBits(2)];
			int delta = readVLC(gb, vlc);
			chan.quSfIdx[0] = (refChan.quSfIdx[0] + delta) & 0x3F;
			for (int i = 1; i < ctx.usedQuantUnits; i++) {
				int diff = refChan.quSfIdx[i] - refChan.quSfIdx[i - 1];
				delta = readVLC(gb, vlc);
				chan.quSfIdx[i] = (chan.quSfIdx[i - 1] + diff + delta) & 0x3F;
			}
		} else {
			const VLCTable &vlc = vlcs().sf[gb.getBits(2) + 4];
			int startVal = (int)gb.getBits(6);
			unpackVqShape(startVal, &atrac3p_sf_shapes[gb.getBits(6)][0],
			              chan.quSfIdx, ctx.usedQuantUnits);
			for (int i = 0; i < ctx.usedQuantUnits; i++) {
				int delta = readVLC(gb, vlc);
				chan.quSfIdx[i] = (chan.quSfIdx[i] + signExtend(delta, 4)) & 0x3F;
			}
		}
		break;
	case 3:
		if (chNum) {
			for (int i = 0; i < ctx.usedQuantUnits; i++)
				chan.quSfIdx[i] = refChan.quSfIdx[i];
		} else {
			weightIdx = (int)gb.getBits(2);
			int vlcSel = (int)gb.getBits(2);
			if (weightIdx == 3) {
				const VLCTable &vlc = vlcs().sf[vlcSel + 4];
				int startVal = (int)gb.getBits(6);
				unpackVqShape(startVal, &atrac3p_sf_shapes[gb.getBits(6)][0],
				              chan.quSfIdx, ctx.usedQuantUnits);

				int diff = ((int)gb.getBits(4) + 56) & 0x3F;
				chan.quSfIdx[0] = (chan.quSfIdx[0] + diff) & 0x3F;
				for (int i = 1; i < ctx.usedQuantUnits; i++) {
					int delta = readVLC(gb, vlc);
					diff = (diff + signExtend(delta, 4)) & 0x3F;
					chan.quSfIdx[i] = (diff + chan.quSfIdx[i]) & 0x3F;
				}
			} else {
				const VLCTable &vlc = vlcs().sf[vlcSel];
				chan.quSfIdx[0] = (int)gb.getBits(6);
				for (int i = 1; i < ctx.usedQuantUnits; i++) {
					int delta = readVLC(gb, vlc);
					chan.quSfIdx[i] = (chan.quSfIdx[i - 1] + delta) & 0x3F;
				}
			}
		}
		break;
	}

	if (weightIdx && weightIdx < 3)
		return subtractSfWeights(ctx, chan, weightIdx);
	return true;
}

bool decodeQuantWordlen(BitReader &gb, ChanUnitCtx &ctx, int numChannels) {
	for (int ch = 0; ch < numChannels; ch++) {
		memset(ctx.channels[ch].quWordlen, 0, sizeof(ctx.channels[ch].quWordlen));
		if (!decodeChannelWordlen(gb, ctx, ch))
			return false;
	}

	int i;
	for (i = ctx.numQuantUnits - 1; i >= 0; i--)
		if (ctx.channels[0].quWordlen[i] || (numChannels == 2 && ctx.channels[1].quWordlen[i]))
			break;
	ctx.usedQuantUnits = i + 1;
	return true;
}

bool decodeScaleFactors(BitReader &gb, ChanUnitCtx &ctx, int numChannels) {
	if (!ctx.usedQuantUnits)
		return true;
	for (int ch = 0; ch < numChannels; ch++) {
		memset(ctx.channels[ch].quSfIdx, 0, sizeof(ctx.channels[ch].quSfIdx));
		if (!decodeChannelSfIdx(gb, ctx, ch))
			return false;
	}
	return true;
}

/* ---- bitstream decode: code table indexes ---- */

int getNumCtValues(BitReader &gb, ChanUnitCtx &ctx, bool &ok) {
	ok = true;
	if (gb.getBits(1)) {
		int n = (int)gb.getBits(5);
		if (n > ctx.usedQuantUnits) {
			ok = false;
			return 0;
		}
		return n;
	}
	return ctx.usedQuantUnits;
}

bool decodeChannelCodeTab(BitReader &gb, ChanUnitCtx &ctx, int chNum) {
	ChanParams &chan = ctx.channels[chNum];
	ChanParams &refChan = ctx.channels[0];
	int mask = ctx.useFullTable ? 7 : 3;

	chan.tableType = (int)gb.getBits(1);

	bool ok;
	switch (gb.getBits(2)) {
	case 0: { // directly coded
		int numBits = ctx.useFullTable + 2;
		int numVals = getNumCtValues(gb, ctx, ok);
		if (!ok)
			return false;
		for (int i = 0; i < numVals; i++) {
			if (chan.quWordlen[i])
				chan.quTabIdx[i] = (int)gb.getBits(numBits);
			else if (chNum && refChan.quWordlen[i])
				chan.quTabIdx[i] = (int)gb.getBits(1);
		}
		break;
	}
	case 1: { // entropy-coded
		const VLCTable &vlc = ctx.useFullTable ? vlcs().ct[1] : vlcs().ct[0];
		int numVals = getNumCtValues(gb, ctx, ok);
		if (!ok)
			return false;
		for (int i = 0; i < numVals; i++) {
			if (chan.quWordlen[i])
				chan.quTabIdx[i] = readVLC(gb, vlc);
			else if (chNum && refChan.quWordlen[i])
				chan.quTabIdx[i] = (int)gb.getBits(1);
		}
		break;
	}
	case 2: { // entropy-coded delta
		const VLCTable &vlc = ctx.useFullTable ? vlcs().ct[1] : vlcs().ct[0];
		const VLCTable &deltaVlc = ctx.useFullTable ? vlcs().ct[2] : vlcs().ct[0];
		int numVals = getNumCtValues(gb, ctx, ok);
		if (!ok)
			return false;
		int pred = 0;
		for (int i = 0; i < numVals; i++) {
			if (chan.quWordlen[i]) {
				int val = !i ? readVLC(gb, vlc) : (pred + readVLC(gb, deltaVlc)) & mask;
				chan.quTabIdx[i] = val;
				pred = chan.quTabIdx[i];
			} else if (chNum && refChan.quWordlen[i]) {
				chan.quTabIdx[i] = (int)gb.getBits(1);
			}
		}
		break;
	}
	case 3: { // entropy-coded difference to master
		if (chNum) {
			const VLCTable &vlc = ctx.useFullTable ? vlcs().ct[3] : vlcs().ct[0];
			int numVals = getNumCtValues(gb, ctx, ok);
			if (!ok)
				return false;
			for (int i = 0; i < numVals; i++) {
				if (chan.quWordlen[i])
					chan.quTabIdx[i] = (refChan.quTabIdx[i] + readVLC(gb, vlc)) & mask;
				else if (refChan.quWordlen[i])
					chan.quTabIdx[i] = (int)gb.getBits(1);
			}
		}
		break;
	}
	}

	return true;
}

bool decodeCodeTableIndexes(BitReader &gb, ChanUnitCtx &ctx, int numChannels) {
	if (!ctx.usedQuantUnits)
		return true;
	ctx.useFullTable = (int)gb.getBits(1);
	for (int ch = 0; ch < numChannels; ch++) {
		memset(ctx.channels[ch].quTabIdx, 0, sizeof(ctx.channels[ch].quTabIdx));
		if (!decodeChannelCodeTab(gb, ctx, ch))
			return false;
	}
	return true;
}

/* ---- bitstream decode: spectrum ---- */

void decodeQuSpectra(BitReader &gb, const Atrac3pSpecCodeTab &tab, const VLCTable &vlc,
                     int16 *out, int numSpecs) {
	int groupSize = tab.group_size;
	int numCoeffs = tab.num_coeffs;
	int bits = tab.bits;
	bool isSigned = tab.is_signed != 0;

	int pos = 0;
	while (pos < numSpecs) {
		if (groupSize == 1 || gb.getBits(1)) {
			for (int j = 0; j < groupSize; j++) {
				uint32 val = readVLC(gb, vlc);
				for (int i = 0; i < numCoeffs; i++) {
					int32 cf = (int32)zeroExtend(val, bits);
					if (isSigned)
						cf = signExtend(cf, bits);
					else if (cf && gb.getBits(1))
						cf = -cf;
					out[pos++] = (int16)cf;
					val >>= bits;
				}
			}
		} else {
			pos += groupSize * numCoeffs;
		}
	}
}

void decodeSpectrum(BitReader &gb, ChanUnitCtx &ctx, int numChannels) {
	for (int ch = 0; ch < numChannels; ch++) {
		ChanParams &chan = ctx.channels[ch];
		memset(chan.spectrum, 0, sizeof(chan.spectrum));
		memset(chan.powerLevs, kPowerCompOff, sizeof(chan.powerLevs));

		for (int qu = 0; qu < ctx.usedQuantUnits; qu++) {
			int numSpecs = ff_atrac3p_qu_to_spec_pos[qu + 1] - ff_atrac3p_qu_to_spec_pos[qu];
			int wordlen = chan.quWordlen[qu];
			int codetab = chan.quTabIdx[qu];
			if (wordlen) {
				if (!ctx.useFullTable)
					codetab = atrac3p_ct_restricted_to_full[chan.tableType][wordlen - 1][codetab];

				int tabIndex = (chan.tableType * 8 + codetab) * 7 + wordlen - 1;
				const Atrac3pSpecCodeTab &tab = atrac3p_spectra_tabs[tabIndex];

				decodeQuSpectra(gb, tab, vlcs().spec[tabIndex],
				                &chan.spectrum[ff_atrac3p_qu_to_spec_pos[qu]], numSpecs);
			} else if (ch && ctx.channels[0].quWordlen[qu] && !codetab) {
				memcpy(&chan.spectrum[ff_atrac3p_qu_to_spec_pos[qu]],
				       &ctx.channels[0].spectrum[ff_atrac3p_qu_to_spec_pos[qu]],
				       numSpecs * sizeof(chan.spectrum[0]));
				chan.quWordlen[qu] = ctx.channels[0].quWordlen[qu];
			}
		}

		if (ctx.usedQuantUnits > 2) {
			int numSpecs = atrac3p_subband_to_num_powgrps[ctx.numCodedSubbands - 1];
			for (int i = 0; i < numSpecs; i++)
				chan.powerLevs[i] = (byte)gb.getBits(4);
		}
	}
}

/* ---- shared subband-flags helper ---- */

int getSubbandFlags(BitReader &gb, byte *out, int numFlags) {
	memset(out, 0, numFlags);
	int result = (int)gb.getBits(1);
	if (result) {
		if (gb.getBits(1)) {
			for (int i = 0; i < numFlags; i++)
				out[i] = (byte)gb.getBits(1);
		} else {
			memset(out, 1, numFlags);
		}
	}
	return result;
}

void decodeWindowShape(BitReader &gb, ChanUnitCtx &ctx, int numChannels) {
	for (int ch = 0; ch < numChannels; ch++)
		getSubbandFlags(gb, ctx.channels[ch].wndShape, ctx.numSubbands);
}

/* ---- bitstream decode: gain control ---- */

bool decodeGainCNpoints(BitReader &gb, ChanUnitCtx &ctx, int chNum, int codedSubbands) {
	ChanParams &chan = ctx.channels[chNum];
	ChanParams &refChan = ctx.channels[0];

	switch (gb.getBits(2)) {
	case 0:
		for (int i = 0; i < codedSubbands; i++)
			chan.gainData[i].numPoints = (int)gb.getBits(3);
		break;
	case 1:
		for (int i = 0; i < codedSubbands; i++)
			chan.gainData[i].numPoints = readVLC(gb, vlcs().gain[0]);
		break;
	case 2:
		if (chNum) {
			for (int i = 0; i < codedSubbands; i++) {
				int delta = readVLC(gb, vlcs().gain[1]);
				chan.gainData[i].numPoints = (refChan.gainData[i].numPoints + delta) & 7;
			}
		} else {
			chan.gainData[0].numPoints = readVLC(gb, vlcs().gain[0]);
			for (int i = 1; i < codedSubbands; i++) {
				int delta = readVLC(gb, vlcs().gain[1]);
				chan.gainData[i].numPoints = (chan.gainData[i - 1].numPoints + delta) & 7;
			}
		}
		break;
	case 3:
		if (chNum) {
			for (int i = 0; i < codedSubbands; i++)
				chan.gainData[i].numPoints = refChan.gainData[i].numPoints;
		} else {
			int deltaBits = (int)gb.getBits(2);
			int minVal = (int)gb.getBits(3);
			for (int i = 0; i < codedSubbands; i++) {
				chan.gainData[i].numPoints = minVal + (int)getBitsZ(gb, deltaBits);
				if (chan.gainData[i].numPoints > 7)
					return false;
			}
		}
		break;
	}
	return true;
}

void gaincLevelMode3s(GainInfo &dst, const GainInfo &ref) {
	for (int i = 0; i < dst.numPoints; i++)
		dst.levCode[i] = (i >= ref.numPoints) ? 7 : ref.levCode[i];
}

void gaincLevelMode1m(BitReader &gb, GainInfo &dst) {
	if (dst.numPoints > 0)
		dst.levCode[0] = readVLC(gb, vlcs().gain[2]);
	for (int i = 1; i < dst.numPoints; i++) {
		int delta = readVLC(gb, vlcs().gain[3]);
		dst.levCode[i] = (dst.levCode[i - 1] + delta) & 0xF;
	}
}

bool decodeGainCLevels(BitReader &gb, ChanUnitCtx &ctx, int chNum, int codedSubbands) {
	ChanParams &chan = ctx.channels[chNum];
	ChanParams &refChan = ctx.channels[0];

	switch (gb.getBits(2)) {
	case 0:
		for (int sb = 0; sb < codedSubbands; sb++)
			for (int i = 0; i < chan.gainData[sb].numPoints; i++)
				chan.gainData[sb].levCode[i] = (int)gb.getBits(4);
		break;
	case 1:
		if (chNum) {
			for (int sb = 0; sb < codedSubbands; sb++)
				for (int i = 0; i < chan.gainData[sb].numPoints; i++) {
					int delta = readVLC(gb, vlcs().gain[5]);
					int pred = (i >= refChan.gainData[sb].numPoints) ? 7 : refChan.gainData[sb].levCode[i];
					chan.gainData[sb].levCode[i] = (pred + delta) & 0xF;
				}
		} else {
			for (int sb = 0; sb < codedSubbands; sb++)
				gaincLevelMode1m(gb, chan.gainData[sb]);
		}
		break;
	case 2:
		if (chNum) {
			for (int sb = 0; sb < codedSubbands; sb++)
				if (chan.gainData[sb].numPoints > 0) {
					if (gb.getBits(1))
						gaincLevelMode1m(gb, chan.gainData[sb]);
					else
						gaincLevelMode3s(chan.gainData[sb], refChan.gainData[sb]);
				}
		} else {
			if (chan.gainData[0].numPoints > 0)
				gaincLevelMode1m(gb, chan.gainData[0]);
			for (int sb = 1; sb < codedSubbands; sb++)
				for (int i = 0; i < chan.gainData[sb].numPoints; i++) {
					int delta = readVLC(gb, vlcs().gain[4]);
					int pred = (i >= chan.gainData[sb - 1].numPoints) ? 7 : chan.gainData[sb - 1].levCode[i];
					chan.gainData[sb].levCode[i] = (pred + delta) & 0xF;
				}
		}
		break;
	case 3:
		if (chNum) {
			for (int sb = 0; sb < codedSubbands; sb++)
				gaincLevelMode3s(chan.gainData[sb], refChan.gainData[sb]);
		} else {
			int deltaBits = (int)gb.getBits(2);
			int minVal = (int)gb.getBits(4);
			for (int sb = 0; sb < codedSubbands; sb++)
				for (int i = 0; i < chan.gainData[sb].numPoints; i++) {
					chan.gainData[sb].levCode[i] = minVal + (int)getBitsZ(gb, deltaBits);
					if (chan.gainData[sb].levCode[i] > 15)
						return false;
				}
		}
		break;
	}
	return true;
}

void gaincLocMode0(BitReader &gb, GainInfo &dst, int pos) {
	if (!pos || dst.locCode[pos - 1] < 15) {
		dst.locCode[pos] = (int)gb.getBits(5);
	} else if (dst.locCode[pos - 1] >= 30) {
		dst.locCode[pos] = 31;
	} else {
		int deltaBits = avLog2((uint32)(30 - dst.locCode[pos - 1])) + 1;
		dst.locCode[pos] = dst.locCode[pos - 1] + (int)gb.getBits(deltaBits) + 1;
	}
}

void gaincLocMode1(BitReader &gb, GainInfo &dst) {
	if (dst.numPoints > 0) {
		dst.locCode[0] = (int)gb.getBits(5);
		for (int i = 1; i < dst.numPoints; i++) {
			const VLCTable &tab = (dst.levCode[i] <= dst.levCode[i - 1]) ? vlcs().gain[7] : vlcs().gain[9];
			dst.locCode[i] = dst.locCode[i - 1] + readVLC(gb, tab);
		}
	}
}

bool decodeGainCLocCodes(BitReader &gb, ChanUnitCtx &ctx, int chNum, int codedSubbands) {
	ChanParams &chan = ctx.channels[chNum];
	ChanParams &refChan = ctx.channels[0];

	switch (gb.getBits(2)) {
	case 0:
		for (int sb = 0; sb < codedSubbands; sb++)
			for (int i = 0; i < chan.gainData[sb].numPoints; i++)
				gaincLocMode0(gb, chan.gainData[sb], i);
		break;
	case 1:
		if (chNum) {
			for (int sb = 0; sb < codedSubbands; sb++) {
				if (chan.gainData[sb].numPoints <= 0)
					continue;
				GainInfo &dst = chan.gainData[sb];
				GainInfo &ref = refChan.gainData[sb];

				int delta = readVLC(gb, vlcs().gain[10]);
				int pred = ref.numPoints > 0 ? ref.locCode[0] : 0;
				dst.locCode[0] = (pred + delta) & 0x1F;

				for (int i = 1; i < dst.numPoints; i++) {
					bool moreThanRef = i >= ref.numPoints;
					if (dst.levCode[i] > dst.levCode[i - 1]) {
						if (moreThanRef) {
							delta = readVLC(gb, vlcs().gain[9]);
							dst.locCode[i] = dst.locCode[i - 1] + delta;
						} else {
							if (gb.getBits(1))
								gaincLocMode0(gb, dst, i);
							else
								dst.locCode[i] = ref.locCode[i];
						}
					} else {
						const VLCTable &tab = moreThanRef ? vlcs().gain[7] : vlcs().gain[10];
						delta = readVLC(gb, tab);
						if (moreThanRef)
							dst.locCode[i] = dst.locCode[i - 1] + delta;
						else
							dst.locCode[i] = (ref.locCode[i] + delta) & 0x1F;
					}
				}
			}
		} else {
			for (int sb = 0; sb < codedSubbands; sb++)
				gaincLocMode1(gb, chan.gainData[sb]);
		}
		break;
	case 2:
		if (chNum) {
			for (int sb = 0; sb < codedSubbands; sb++) {
				if (chan.gainData[sb].numPoints <= 0)
					continue;
				GainInfo &dst = chan.gainData[sb];
				GainInfo &ref = refChan.gainData[sb];
				if (dst.numPoints > ref.numPoints || gb.getBits(1)) {
					gaincLocMode1(gb, dst);
				} else {
					for (int i = 0; i < dst.numPoints; i++)
						dst.locCode[i] = ref.locCode[i];
				}
			}
		} else {
			for (int i = 0; i < chan.gainData[0].numPoints; i++)
				gaincLocMode0(gb, chan.gainData[0], i);

			for (int sb = 1; sb < codedSubbands; sb++) {
				if (chan.gainData[sb].numPoints <= 0)
					continue;
				GainInfo &dst = chan.gainData[sb];
				GainInfo &prevSb = chan.gainData[sb - 1];

				int delta = readVLC(gb, vlcs().gain[6]);
				int pred = prevSb.numPoints > 0 ? prevSb.locCode[0] : 0;
				dst.locCode[0] = (pred + delta) & 0x1F;

				for (int i = 1; i < dst.numPoints; i++) {
					bool moreThanRef = i >= prevSb.numPoints;
					int tabIdx = (dst.levCode[i] > dst.levCode[i - 1] ? 1 : 0) * 2 + (moreThanRef ? 1 : 0) + 6;
					delta = readVLC(gb, vlcs().gain[tabIdx]);
					if (moreThanRef)
						dst.locCode[i] = dst.locCode[i - 1] + delta;
					else
						dst.locCode[i] = (prevSb.locCode[i] + delta) & 0x1F;
				}
			}
		}
		break;
	case 3:
		if (chNum) {
			for (int sb = 0; sb < codedSubbands; sb++)
				for (int i = 0; i < chan.gainData[sb].numPoints; i++) {
					if (i >= refChan.gainData[sb].numPoints)
						gaincLocMode0(gb, chan.gainData[sb], i);
					else
						chan.gainData[sb].locCode[i] = refChan.gainData[sb].locCode[i];
				}
		} else {
			int deltaBits = (int)gb.getBits(2) + 1;
			int minVal = (int)gb.getBits(5);
			for (int sb = 0; sb < codedSubbands; sb++)
				for (int i = 0; i < chan.gainData[sb].numPoints; i++)
					chan.gainData[sb].locCode[i] = minVal + i + (int)gb.getBits(deltaBits);
		}
		break;
	}

	for (int sb = 0; sb < codedSubbands; sb++) {
		GainInfo &dst = chan.gainData[sb];
		for (int i = 0; i < dst.numPoints; i++) {
			if (dst.locCode[i] < 0 || dst.locCode[i] > 31 || (i && dst.locCode[i] <= dst.locCode[i - 1]))
				return false;
		}
	}
	return true;
}

bool decodeGainCData(BitReader &gb, ChanUnitCtx &ctx, int numChannels) {
	for (int ch = 0; ch < numChannels; ch++) {
		for (int i = 0; i < kSubbands; i++)
			ctx.channels[ch].gainData[i] = GainInfo();

		if (gb.getBits(1)) {
			int codedSubbands = (int)gb.getBits(4) + 1;
			if (gb.getBits(1))
				ctx.channels[ch].numGainSubbands = (int)gb.getBits(4) + 1;
			else
				ctx.channels[ch].numGainSubbands = codedSubbands;

			if (!decodeGainCNpoints(gb, ctx, ch, codedSubbands) ||
			    !decodeGainCLevels(gb, ctx, ch, codedSubbands) ||
			    !decodeGainCLocCodes(gb, ctx, ch, codedSubbands))
				return false;

			if (codedSubbands > 0) {
				for (int sb = codedSubbands; sb < ctx.channels[ch].numGainSubbands; sb++)
					ctx.channels[ch].gainData[sb] = ctx.channels[ch].gainData[sb - 1];
			}
		} else {
			ctx.channels[ch].numGainSubbands = 0;
		}
	}
	return true;
}

/* ---- bitstream decode: GHA tones ---- */

void decodeTonesEnvelope(BitReader &gb, ChanUnitCtx &ctx, int chNum, const int bandHasTones[]) {
	WavesData *dst = ctx.channels[chNum].tonesInfo;
	WavesData *ref = ctx.channels[0].tonesInfo;

	if (!chNum || !gb.getBits(1)) {
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb])
				continue;
			dst[sb].pendEnv.hasStartPoint = (int)gb.getBits(1);
			dst[sb].pendEnv.startPos = dst[sb].pendEnv.hasStartPoint ? (int)gb.getBits(5) : -1;
			dst[sb].pendEnv.hasStopPoint = (int)gb.getBits(1);
			dst[sb].pendEnv.stopPos = dst[sb].pendEnv.hasStopPoint ? (int)gb.getBits(5) : 32;
		}
	} else {
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb])
				continue;
			dst[sb].pendEnv = ref[sb].pendEnv;
		}
	}
}

bool decodeBandNumwavs(BitReader &gb, ChanUnitCtx &ctx, int chNum, const int bandHasTones[]) {
	WavesData *dst = ctx.channels[chNum].tonesInfo;
	WavesData *ref = ctx.channels[0].tonesInfo;

	int mode = (int)gb.getBits(chNum + 1);
	switch (mode) {
	case 0:
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++)
			if (bandHasTones[sb])
				dst[sb].numWavs = (int)gb.getBits(4);
		break;
	case 1:
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++)
			if (bandHasTones[sb])
				dst[sb].numWavs = readVLC(gb, vlcs().tone[1]);
		break;
	case 2:
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++)
			if (bandHasTones[sb]) {
				int delta = signExtend(readVLC(gb, vlcs().tone[2]), 3);
				dst[sb].numWavs = (ref[sb].numWavs + delta) & 0xF;
			}
		break;
	case 3:
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++)
			if (bandHasTones[sb])
				dst[sb].numWavs = ref[sb].numWavs;
		break;
	}

	for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
		if (bandHasTones[sb]) {
			if (ctx.wavesInfo->tonesIndex + dst[sb].numWavs > 48)
				return false;
			dst[sb].startIndex = ctx.wavesInfo->tonesIndex;
			ctx.wavesInfo->tonesIndex += dst[sb].numWavs;
		}
	}
	return true;
}

void decodeTonesFrequency(BitReader &gb, ChanUnitCtx &ctx, int chNum, const int bandHasTones[]) {
	WavesData *dst = ctx.channels[chNum].tonesInfo;
	WavesData *ref = ctx.channels[0].tonesInfo;

	if (!chNum || !gb.getBits(1)) {
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb] || !dst[sb].numWavs)
				continue;
			WaveParam *iwav = &ctx.wavesInfo->waves[dst[sb].startIndex];
			int direction = (dst[sb].numWavs > 1) ? (int)gb.getBits(1) : 0;
			if (direction) {
				iwav[dst[sb].numWavs - 1].freqIndex = (int)gb.getBits(10);
				for (int i = dst[sb].numWavs - 2; i >= 0; i--) {
					int nbits = avLog2((uint32)iwav[i + 1].freqIndex) + 1;
					iwav[i].freqIndex = (int)gb.getBits(nbits);
				}
			} else {
				for (int i = 0; i < dst[sb].numWavs; i++) {
					if (!i || iwav[i - 1].freqIndex < 512) {
						iwav[i].freqIndex = (int)gb.getBits(10);
					} else {
						int nbits = avLog2((uint32)(1023 - iwav[i - 1].freqIndex)) + 1;
						iwav[i].freqIndex = (int)gb.getBits(nbits) + 1024 - (1 << nbits);
					}
				}
			}
		}
	} else {
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb] || !dst[sb].numWavs)
				continue;
			WaveParam *iwav = &ctx.wavesInfo->waves[ref[sb].startIndex];
			WaveParam *owav = &ctx.wavesInfo->waves[dst[sb].startIndex];
			for (int i = 0; i < dst[sb].numWavs; i++) {
				int delta = signExtend(readVLC(gb, vlcs().tone[6]), 8);
				int pred = (i < ref[sb].numWavs) ? iwav[i].freqIndex
				                                  : (ref[sb].numWavs ? iwav[ref[sb].numWavs - 1].freqIndex : 0);
				owav[i].freqIndex = (pred + delta) & 0x3FF;
			}
		}
	}
}

void decodeTonesAmplitude(BitReader &gb, ChanUnitCtx &ctx, int chNum, const int bandHasTones[]) {
	WavesData *dst = ctx.channels[chNum].tonesInfo;
	WavesData *ref = ctx.channels[0].tonesInfo;
	int refwaves[48] = {0};

	if (chNum) {
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb] || !dst[sb].numWavs)
				continue;
			WaveParam *wsrc = &ctx.wavesInfo->waves[dst[sb].startIndex];
			WaveParam *wref = &ctx.wavesInfo->waves[ref[sb].startIndex];
			for (int j = 0; j < dst[sb].numWavs; j++) {
				int fi = 0, maxdiff = 1024;
				for (int i = 0; i < ref[sb].numWavs; i++) {
					int diff = ABS(wsrc[j].freqIndex - wref[i].freqIndex);
					if (diff < maxdiff) {
						maxdiff = diff;
						fi = i;
					}
				}
				if (maxdiff < 8)
					refwaves[dst[sb].startIndex + j] = fi + ref[sb].startIndex;
				else if (j < ref[sb].numWavs)
					refwaves[dst[sb].startIndex + j] = j + ref[sb].startIndex;
				else
					refwaves[dst[sb].startIndex + j] = -1;
			}
		}
	}

	int mode = (int)gb.getBits(chNum + 1);
	switch (mode) {
	case 0:
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb] || !dst[sb].numWavs)
				continue;
			if (ctx.wavesInfo->amplitudeMode) {
				for (int i = 0; i < dst[sb].numWavs; i++)
					ctx.wavesInfo->waves[dst[sb].startIndex + i].ampSf = (int)gb.getBits(6);
			} else {
				ctx.wavesInfo->waves[dst[sb].startIndex].ampSf = (int)gb.getBits(6);
			}
		}
		break;
	case 1:
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb] || !dst[sb].numWavs)
				continue;
			if (ctx.wavesInfo->amplitudeMode) {
				for (int i = 0; i < dst[sb].numWavs; i++)
					ctx.wavesInfo->waves[dst[sb].startIndex + i].ampSf = readVLC(gb, vlcs().tone[3]) + 20;
			} else {
				ctx.wavesInfo->waves[dst[sb].startIndex].ampSf = readVLC(gb, vlcs().tone[4]) + 24;
			}
		}
		break;
	case 2:
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb] || !dst[sb].numWavs)
				continue;
			for (int i = 0; i < dst[sb].numWavs; i++) {
				int delta = signExtend(readVLC(gb, vlcs().tone[5]), 5);
				int refIdx = refwaves[dst[sb].startIndex + i];
				int pred = refIdx >= 0 ? ctx.wavesInfo->waves[refIdx].ampSf : 34;
				ctx.wavesInfo->waves[dst[sb].startIndex + i].ampSf = (pred + delta) & 0x3F;
			}
		}
		break;
	case 3:
		for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
			if (!bandHasTones[sb])
				continue;
			for (int i = 0; i < dst[sb].numWavs; i++) {
				int refIdx = refwaves[dst[sb].startIndex + i];
				ctx.wavesInfo->waves[dst[sb].startIndex + i].ampSf =
					refIdx >= 0 ? ctx.wavesInfo->waves[refIdx].ampSf : 32;
			}
		}
		break;
	}
}

void decodeTonesPhase(BitReader &gb, ChanUnitCtx &ctx, int chNum, const int bandHasTones[]) {
	WavesData *dst = ctx.channels[chNum].tonesInfo;
	for (int sb = 0; sb < ctx.wavesInfo->numToneBands; sb++) {
		if (!bandHasTones[sb])
			continue;
		WaveParam *wparam = &ctx.wavesInfo->waves[dst[sb].startIndex];
		for (int i = 0; i < dst[sb].numWavs; i++)
			wparam[i].phaseIndex = (int)gb.getBits(5);
	}
}

bool decodeTonesInfo(BitReader &gb, ChanUnitCtx &ctx, int numChannels) {
	int bandHasTones[16];

	for (int ch = 0; ch < numChannels; ch++)
		for (int i = 0; i < kSubbands; i++)
			ctx.channels[ch].tonesInfo[i] = WavesData();

	ctx.wavesInfo->tonesPresent = (int)gb.getBits(1);
	if (!ctx.wavesInfo->tonesPresent)
		return true;

	for (int i = 0; i < 48; i++)
		ctx.wavesInfo->waves[i] = WaveParam();

	ctx.wavesInfo->amplitudeMode = (int)gb.getBits(1);
	if (!ctx.wavesInfo->amplitudeMode)
		return false; // GHA amplitude mode 0 not supported by the reference either

	ctx.wavesInfo->numToneBands = readVLC(gb, vlcs().tone[0]) + 1;

	if (numChannels == 2) {
		getSubbandFlags(gb, ctx.wavesInfo->toneSharing, ctx.wavesInfo->numToneBands);
		getSubbandFlags(gb, ctx.wavesInfo->toneMaster, ctx.wavesInfo->numToneBands);
		getSubbandFlags(gb, ctx.wavesInfo->invertPhase, ctx.wavesInfo->numToneBands);
	}

	ctx.wavesInfo->tonesIndex = 0;

	for (int ch = 0; ch < numChannels; ch++) {
		for (int i = 0; i < ctx.wavesInfo->numToneBands; i++)
			bandHasTones[i] = !ch ? 1 : !ctx.wavesInfo->toneSharing[i];

		decodeTonesEnvelope(gb, ctx, ch, bandHasTones);
		if (!decodeBandNumwavs(gb, ctx, ch, bandHasTones))
			return false;
		decodeTonesFrequency(gb, ctx, ch, bandHasTones);
		decodeTonesAmplitude(gb, ctx, ch, bandHasTones);
		decodeTonesPhase(gb, ctx, ch, bandHasTones);
	}

	if (numChannels == 2) {
		for (int i = 0; i < ctx.wavesInfo->numToneBands; i++) {
			if (ctx.wavesInfo->toneSharing[i])
				ctx.channels[1].tonesInfo[i] = ctx.channels[0].tonesInfo[i];
			if (ctx.wavesInfo->toneMaster[i])
				SWAP(ctx.channels[0].tonesInfo[i], ctx.channels[1].tonesInfo[i]);
		}
	}

	return true;
}

bool decodeChannelUnit(BitReader &gb, ChanUnitCtx &ctx, int numChannels) {
	ctx.numQuantUnits = (int)gb.getBits(5) + 1;
	if (ctx.numQuantUnits > 28 && ctx.numQuantUnits < 32)
		return false;

	ctx.muteFlag = (int)gb.getBits(1);

	if (!decodeQuantWordlen(gb, ctx, numChannels))
		return false;

	ctx.numSubbands = atrac3p_qu_to_subband[ctx.numQuantUnits - 1] + 1;
	ctx.numCodedSubbands = ctx.usedQuantUnits ? atrac3p_qu_to_subband[ctx.usedQuantUnits - 1] + 1 : 0;

	if (!decodeScaleFactors(gb, ctx, numChannels))
		return false;
	if (!decodeCodeTableIndexes(gb, ctx, numChannels))
		return false;

	decodeSpectrum(gb, ctx, numChannels);

	if (numChannels == 2) {
		getSubbandFlags(gb, ctx.swapChannels, ctx.numCodedSubbands);
		getSubbandFlags(gb, ctx.negateCoeffs, ctx.numCodedSubbands);
	}

	decodeWindowShape(gb, ctx, numChannels);

	if (!decodeGainCData(gb, ctx, numChannels))
		return false;
	if (!decodeTonesInfo(gb, ctx, numChannels))
		return false;

	ctx.noisePresent = (int)gb.getBits(1);
	if (ctx.noisePresent) {
		ctx.noiseLevelIndex = (int)gb.getBits(4);
		ctx.noiseTableIndex = (int)gb.getBits(4);
	}

	return true;
}

/* ---- DSP: gain compensation (own copy -- different constants from
 * plain ATRAC3's, id2exp_offset=6, loc_scale=2, ported from atrac.c's
 * real algorithm, same formulas, independent from audio/decoders/atrac3.cpp
 * to avoid coupling two otherwise-unrelated codecs' internals) ---- */

struct GainTables {
	float gainTab1[16];
	float gainTab2[31];

	GainTables() {
		for (int i = 0; i < 16; i++)
			gainTab1[i] = (float)pow(2.0, kGainIdToExpOffset - i);
		for (int i = -15; i < 16; i++)
			gainTab2[i + 15] = (float)pow(2.0, -1.0 / kGainLocSize * i);
	}
};

const GainTables &gainTables() {
	static const GainTables t;
	return t;
}

void gainCompensation(const float *in, float *prev, const GainInfo &gcNow, const GainInfo &gcNext,
                       int numSamples, float *out) {
	float gcScale = gcNext.numPoints ? gainTables().gainTab1[gcNext.levCode[0]] : 1.0f;

	if (gcNow.numPoints == 0) {
		for (int pos = 0; pos < numSamples; pos++)
			out[pos] = in[pos] * gcScale + prev[pos];
	} else {
		int pos = 0;
		for (int i = 0; i < gcNow.numPoints; i++) {
			int lastpos = gcNow.locCode[i] << kGainLocScaleBits;
			float lev = gainTables().gainTab1[gcNow.levCode[i]];
			int nextLevCode = (i + 1 < gcNow.numPoints) ? gcNow.levCode[i + 1] : kGainIdToExpOffset;
			float gainInc = gainTables().gainTab2[nextLevCode - gcNow.levCode[i] + 15];

			for (; pos < lastpos; pos++)
				out[pos] = (in[pos] * gcScale + prev[pos]) * lev;
			for (; pos < lastpos + kGainLocSize; pos++) {
				out[pos] = (in[pos] * gcScale + prev[pos]) * lev;
				lev *= gainInc;
			}
		}
		for (; pos < numSamples; pos++)
			out[pos] = in[pos] * gcScale + prev[pos];
	}

	memcpy(prev, in + numSamples, numSamples * sizeof(float));
}

/* ---- DSP: sine tables, GHA tone synthesis, power compensation ---- */

struct SineTables {
	float sine2048[2048];
	float hann256[256];
	float ampSfTab[64];
	float sine64[64];
	float sine128[128];

	SineTables() {
		for (int i = 0; i < 2048; i++)
			sine2048[i] = (float)sin(2.0 * M_PI * i / 2048.0);
		for (int i = 0; i < 256; i++)
			hann256[i] = (float)((1.0 - cos(2.0 * M_PI * i / 256.0)) * 0.5);
		for (int i = 0; i < 64; i++)
			ampSfTab[i] = (float)exp2((i - 3) / 4.0);
		for (int i = 0; i < 64; i++)
			sine64[i] = (float)sin((i + 0.5) * (M_PI / 128.0));
		for (int i = 0; i < 128; i++)
			sine128[i] = (float)sin((i + 0.5) * (M_PI / 256.0));
	}
};

const SineTables &sineTables() {
	static const SineTables t;
	return t;
}

inline int dequantPhase(int ph) {
	return (ph & 0x1F) << 6;
}

void wavesSynth(const WaveSynthParams &synthParam, const WavesData &wavesInfo,
                 const WaveEnvelope &envelope, int invertPhase, int regOffset, float *out) {
	const WaveParam *waveParam = &synthParam.waves[wavesInfo.startIndex];

	for (int wn = 0; wn < wavesInfo.numWavs; wn++, waveParam++) {
		double amp = sineTables().ampSfTab[waveParam->ampSf] *
		             (!synthParam.amplitudeMode ? (waveParam->ampIndex + 1) / 15.13 : 1.0);

		int inc = waveParam->freqIndex;
		int pos = (dequantPhase(waveParam->phaseIndex) - (regOffset ^ 128) * inc) & 2047;

		for (int i = 0; i < 128; i++) {
			out[i] += sineTables().sine2048[pos] * (float)amp;
			pos = (pos + inc) & 2047;
		}
	}

	if (invertPhase) {
		for (int i = 0; i < 128; i++)
			out[i] *= -1.0f;
	}

	if (envelope.hasStartPoint) {
		int pos = (envelope.startPos << 2) - regOffset;
		if (pos > 0 && pos <= 128) {
			memset(out, 0, pos * sizeof(float));
			if (!envelope.hasStopPoint || envelope.startPos != envelope.stopPos) {
				out[pos + 0] *= sineTables().hann256[0];
				out[pos + 1] *= sineTables().hann256[32];
				out[pos + 2] *= sineTables().hann256[64];
				out[pos + 3] *= sineTables().hann256[96];
			}
		}
	}

	if (envelope.hasStopPoint) {
		int pos = ((envelope.stopPos + 1) << 2) - regOffset;
		if (pos > 0 && pos <= 128) {
			out[pos - 4] *= sineTables().hann256[96];
			out[pos - 3] *= sineTables().hann256[64];
			out[pos - 2] *= sineTables().hann256[32];
			out[pos - 1] *= sineTables().hann256[0];
			memset(&out[pos], 0, (128 - pos) * sizeof(float));
		}
	}
}

void generateTones(ChanUnitCtx &chUnit, int chNum, int sb, float *out) {
	float wavreg1[128] = {0};
	float wavreg2[128] = {0};

	WavesData &tonesNow = chUnit.channels[chNum].tonesInfoPrev[sb];
	WavesData &tonesNext = chUnit.channels[chNum].tonesInfo[sb];

	if (tonesNext.pendEnv.hasStartPoint && tonesNext.pendEnv.startPos < tonesNext.pendEnv.stopPos) {
		tonesNext.currEnv.hasStartPoint = 1;
		tonesNext.currEnv.startPos = tonesNext.pendEnv.startPos + 32;
	} else if (tonesNow.pendEnv.hasStartPoint) {
		tonesNext.currEnv.hasStartPoint = 1;
		tonesNext.currEnv.startPos = tonesNow.pendEnv.startPos;
	} else {
		tonesNext.currEnv.hasStartPoint = 0;
		tonesNext.currEnv.startPos = 0;
	}

	if (tonesNow.pendEnv.hasStopPoint && tonesNow.pendEnv.stopPos >= tonesNext.currEnv.startPos) {
		tonesNext.currEnv.hasStopPoint = 1;
		tonesNext.currEnv.stopPos = tonesNow.pendEnv.stopPos;
	} else if (tonesNext.pendEnv.hasStopPoint) {
		tonesNext.currEnv.hasStopPoint = 1;
		tonesNext.currEnv.stopPos = tonesNext.pendEnv.stopPos + 32;
	} else {
		tonesNext.currEnv.hasStopPoint = 0;
		tonesNext.currEnv.stopPos = 64;
	}

	bool reg1EnvNonzero = tonesNow.currEnv.stopPos >= 32;
	bool reg2EnvNonzero = tonesNext.currEnv.startPos < 32;

	if (tonesNow.numWavs && reg1EnvNonzero)
		wavesSynth(*chUnit.wavesInfoPrev, tonesNow, tonesNow.currEnv,
		           chUnit.wavesInfoPrev->invertPhase[sb] & chNum, 128, wavreg1);

	if (tonesNext.numWavs && reg2EnvNonzero)
		wavesSynth(*chUnit.wavesInfo, tonesNext, tonesNext.currEnv,
		           chUnit.wavesInfo->invertPhase[sb] & chNum, 0, wavreg2);

	if (tonesNow.numWavs && tonesNext.numWavs && reg1EnvNonzero && reg2EnvNonzero) {
		for (int i = 0; i < 128; i++) {
			wavreg1[i] *= sineTables().hann256[128 + i];
			wavreg2[i] *= sineTables().hann256[i];
		}
	} else {
		if (tonesNow.numWavs && !tonesNow.currEnv.hasStopPoint)
			for (int i = 0; i < 128; i++)
				wavreg1[i] *= sineTables().hann256[128 + i];
		if (tonesNext.numWavs && !tonesNext.currEnv.hasStartPoint)
			for (int i = 0; i < 128; i++)
				wavreg2[i] *= sineTables().hann256[i];
	}

	for (int i = 0; i < 128; i++)
		out[i] += wavreg1[i] + wavreg2[i];
}

const byte kSubbandToQu[17] = {0, 8, 12, 16, 18, 20, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
const float kPwcLevs[16] = {
	3.96875f, 3.15625f, 2.5f, 2.0f, 1.59375f, 1.25f, 1.0f, 0.78125f,
	0.625f, 0.5f, 0.40625f, 0.3125f, 0.25f, 0.1875f, 0.15625f, 0.0f
};

void powerCompensation(ChanUnitCtx &ctx, int chIndex, float *sp, int rngIndex, int sb) {
	int swapCh = (ctx.unitType == kChUnitStereo && ctx.swapChannels[sb]) ? 1 : 0;

	if (ctx.channels[chIndex ^ swapCh].powerLevs[subband_to_powgrp[sb]] == kPowerCompOff)
		return;

	float pwcsp[kSubbandSamples];
	for (int i = 0; i < kSubbandSamples; i++, rngIndex++)
		pwcsp[i] = noise_tab[rngIndex & 0x3FF];

	GainInfo &g1 = ctx.channels[chIndex ^ swapCh].gainData[sb];
	GainInfo &g2 = ctx.channels[chIndex ^ swapCh].gainDataPrev[sb];

	int gainLev = (g1.numPoints > 0) ? (6 - g1.levCode[0]) : 0;
	int gcv = 0;
	for (int i = 0; i < g2.numPoints; i++)
		gcv = MAX(gcv, gainLev - (g2.levCode[i] - 6));
	for (int i = 0; i < g1.numPoints; i++)
		gcv = MAX(gcv, 6 - g1.levCode[i]);

	float grpLev = kPwcLevs[ctx.channels[chIndex ^ swapCh].powerLevs[subband_to_powgrp[sb]]] / (float)(1 << gcv);

	int quStart = kSubbandToQu[sb] + (!sb ? 2 : 0);
	for (int qu = quStart; qu < kSubbandToQu[sb + 1]; qu++) {
		if (ctx.channels[chIndex].quWordlen[qu] <= 0)
			continue;

		float quLev = ff_atrac3p_sf_tab[ctx.channels[chIndex].quSfIdx[qu]] *
		              ff_atrac3p_mant_tab[ctx.channels[chIndex].quWordlen[qu]] /
		              (float)(1 << ctx.channels[chIndex].quWordlen[qu]) * grpLev;

		float *dst = &sp[ff_atrac3p_qu_to_spec_pos[qu]];
		int nsp = ff_atrac3p_qu_to_spec_pos[qu + 1] - ff_atrac3p_qu_to_spec_pos[qu];

		for (int i = 0; i < nsp; i++)
			dst[i] += pwcsp[i] * quLev;
	}
}

/* ---- DSP: per-subband IMDCT ---- */

void imdctSubband(Math::MDCT &mdct, float *pIn, float *pOut, int windId, int sb) {
	if (sb & 1) {
		for (int i = 0; i < kSubbandSamples / 2; i++)
			SWAP(pIn[i], pIn[kSubbandSamples - 1 - i]);
	}

	mdct.calcIMDCT(pOut, pIn);

	// Two real MDCT windows: a plain sine window of size 256, or a
	// "steep" variant that's zero/one-padded 32 samples at each end.
	if (windId & 2) {
		memset(pOut, 0, sizeof(float) * 32);
		for (int i = 0; i < 64; i++)
			pOut[32 + i] *= sineTables().sine64[i];
	} else {
		for (int i = 0; i < kMDCTSize / 2; i++)
			pOut[i] *= sineTables().sine128[i];
	}

	if (windId & 1) {
		for (int i = 0; i < 64; i++)
			pOut[160 + i] *= sineTables().sine64[63 - i];
		memset(&pOut[224], 0, sizeof(float) * 32);
	} else {
		for (int i = 0; i < kMDCTSize / 2; i++)
			pOut[128 + i] *= sineTables().sine128[kMDCTSize / 2 - 1 - i];
	}
}

/* ---- DSP: 16-band polyphase (PQF) synthesis filter ---- */

// A real 16-point type-IV DCT (self-inverse up to a constant scale), the
// transform the reference computes via a generically-configured MDCT
// backend at length 16. ScummVM has no generic DCT-IV primitive and 16
// points is tiny, so this is a direct textbook evaluation rather than an
// FFT-based one. The real backend's output index order is the reverse of
// the textbook formula's natural k order -- confirmed by feeding identical
// real input through both a real ffmpeg build (with the actual transform
// instrumented) and this formula: values matched exactly once reversed.
void dctIV16(const float *in, float *out) {
	const double scale = 32.0 / 32768.0;
	for (int k = 0; k < 16; k++) {
		double sum = 0.0;
		for (int n = 0; n < 16; n++)
			sum += in[n] * cos(M_PI / 16.0 * (n + 0.5) * (k + 0.5));
		out[15 - k] = (float)(sum * scale);
	}
}

const int kMod23Lut[26] = {
	23, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
	12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 0
};

void ipqf(IPQFChannelCtx &hist, const float *in, float *out) {
	memset(out, 0, kFrameSamples * sizeof(float));

	float idctIn[kSubbands];
	float idctOut[kSubbands];

	for (int s = 0; s < kSubbandSamples; s++) {
		for (int sb = 0; sb < kSubbands; sb++)
			idctIn[sb] = in[sb * kSubbandSamples + s];

		dctIV16(idctIn, idctOut);

		for (int i = 0; i < 8; i++) {
			hist.buf1[hist.pos][i] = idctOut[i + 8];
			hist.buf2[hist.pos][i] = idctOut[7 - i];
		}

		int posNow = hist.pos;
		int posNext = kMod23Lut[posNow + 2];

		for (int t = 0; t < kPQFFIRLen; t++) {
			for (int i = 0; i < 8; i++) {
				out[s * 16 + i + 0] += hist.buf1[posNow][i] * ipqf_coeffs1[t][i] +
				                        hist.buf2[posNext][i] * ipqf_coeffs2[t][i];
				out[s * 16 + i + 8] += hist.buf1[posNow][7 - i] * ipqf_coeffs1[t][i + 8] +
				                        hist.buf2[posNext][7 - i] * ipqf_coeffs2[t][i + 8];
			}
			posNow = kMod23Lut[posNext + 2];
			posNext = kMod23Lut[posNow + 2];
		}

		hist.pos = kMod23Lut[hist.pos];
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Reusable stateful frame decoder. Holds exactly the cross-frame synthesis
// state that used to live inside ATRAC3PlusStream, so both the whole-stream
// SeekableAudioStream and a streaming video audio track can drive it one
// frame at a time.
// ---------------------------------------------------------------------------

struct ATRAC3PlusFrameDecoder::Impl {
	Math::MDCT mdct;
	Common::Array<ChanUnitCtx> units;
	int numChannelBlocks = 0;
	byte channelBlocks[5] = {0};
	byte channelMap[8] = {0};

	Common::Array<float> samples; // quantized MDCT spectrum, channel-major
	Common::Array<float> outBuf;  // final per-channel-unit output, channel-major
	uint16 channels = 0;

	Impl() : mdct(kMDCTBits, true, -1.0) {}

	void reset(uint16 nch) {
		const ChannelLayout &layout = kChannelLayouts[nch];
		numChannelBlocks = layout.numBlocks;
		for (int i = 0; i < 5; i++)
			channelBlocks[i] = layout.blocks[i];
		for (int i = 0; i < 8; i++)
			channelMap[i] = layout.map[i];

		units.clear();
		units.resize(numChannelBlocks);
		for (int i = 0; i < numChannelBlocks; i++)
			units[i].init();

		samples.resize((uint32)nch * kFrameSamples);
		outBuf.resize((uint32)nch * kFrameSamples);
		channels = nch;
	}

	static int16 clampInt16(float v) {
		v *= 32768.0f;
		if (v > 32767.0f)
			return 32767;
		if (v < -32768.0f)
			return -32768;
		return (int16)v;
	}

	void decodeResidualSpectrum(ChanUnitCtx &chUnit, float out[2][kFrameSamples], int numChannels) {
		int sbRngIndex[kSubbands] = {0};

		if (chUnit.muteFlag) {
			for (int ch = 0; ch < numChannels; ch++)
				memset(out[ch], 0, kFrameSamples * sizeof(float));
			return;
		}

		int rngIndex = 0;
		for (int qu = 0; qu < chUnit.usedQuantUnits; qu++)
			rngIndex += chUnit.channels[0].quSfIdx[qu] + chUnit.channels[1].quSfIdx[qu];
		for (int sb = 0; sb < chUnit.numCodedSubbands; sb++, rngIndex += 128)
			sbRngIndex[sb] = rngIndex & 0x3FC;

		for (int ch = 0; ch < numChannels; ch++) {
			memset(out[ch], 0, kFrameSamples * sizeof(float));

			for (int qu = 0; qu < chUnit.usedQuantUnits; qu++) {
				int16 *src = &chUnit.channels[ch].spectrum[ff_atrac3p_qu_to_spec_pos[qu]];
				float *dst = &out[ch][ff_atrac3p_qu_to_spec_pos[qu]];
				int nspeclines = ff_atrac3p_qu_to_spec_pos[qu + 1] - ff_atrac3p_qu_to_spec_pos[qu];

				if (chUnit.channels[ch].quWordlen[qu] > 0) {
					float q = ff_atrac3p_sf_tab[chUnit.channels[ch].quSfIdx[qu]] *
					          ff_atrac3p_mant_tab[chUnit.channels[ch].quWordlen[qu]];
					for (int i = 0; i < nspeclines; i++)
						dst[i] = src[i] * q;
				}
			}

			for (int sb = 0; sb < chUnit.numCodedSubbands; sb++)
				powerCompensation(chUnit, ch, &out[ch][0], sbRngIndex[sb], sb);
		}

		if (chUnit.unitType == kChUnitStereo) {
			for (int sb = 0; sb < chUnit.numCodedSubbands; sb++) {
				if (chUnit.swapChannels[sb]) {
					for (int i = 0; i < kSubbandSamples; i++)
						SWAP(out[0][sb * kSubbandSamples + i], out[1][sb * kSubbandSamples + i]);
				}
				if (chUnit.negateCoeffs[sb]) {
					for (int i = 0; i < kSubbandSamples; i++)
						out[1][sb * kSubbandSamples + i] = -out[1][sb * kSubbandSamples + i];
				}
			}
		}
	}

	void reconstructFrame(ChanUnitCtx &chUnit, float out[2][kFrameSamples], int numChannels) {
		float mdctBuf[kMDCTSize];

		for (int ch = 0; ch < numChannels; ch++) {
			for (int sb = 0; sb < chUnit.numSubbands; sb++) {
				imdctSubband(mdct, &out[ch][sb * kSubbandSamples], mdctBuf,
				             (chUnit.channels[ch].wndShapePrev[sb] << 1) + chUnit.channels[ch].wndShape[sb], sb);

				gainCompensation(mdctBuf, &chUnit.prevBuf[ch][sb * kSubbandSamples],
				                  chUnit.channels[ch].gainDataPrev[sb], chUnit.channels[ch].gainData[sb],
				                  kSubbandSamples, &out[ch][sb * kSubbandSamples]);
			}

			memset(&chUnit.prevBuf[ch][chUnit.numSubbands * kSubbandSamples], 0,
			       (kSubbands - chUnit.numSubbands) * kSubbandSamples * sizeof(float));

			if (chUnit.wavesInfo->tonesPresent || chUnit.wavesInfoPrev->tonesPresent) {
				for (int sb = 0; sb < chUnit.numSubbands; sb++)
					if (chUnit.channels[ch].tonesInfo[sb].numWavs || chUnit.channels[ch].tonesInfoPrev[sb].numWavs)
						generateTones(chUnit, ch, sb, &out[ch][sb * 128]);
			}
		}

		for (int ch = 0; ch < numChannels; ch++) {
			// ipqf() memsets its output before reading its input; in and
			// out must be distinct buffers (the real reference uses
			// separate time_buf/outp_buf arrays for exactly this reason).
			float ipqfOut[kFrameSamples];
			ipqf(chUnit.ipqfCtx[ch], &out[ch][0], ipqfOut);
			memcpy(out[ch], ipqfOut, sizeof(ipqfOut));
		}

		for (int ch = 0; ch < numChannels; ch++) {
			SWAP(chUnit.channels[ch].wndShape, chUnit.channels[ch].wndShapePrev);
			SWAP(chUnit.channels[ch].gainData, chUnit.channels[ch].gainDataPrev);
			SWAP(chUnit.channels[ch].tonesInfo, chUnit.channels[ch].tonesInfoPrev);
		}
		SWAP(chUnit.wavesInfo, chUnit.wavesInfoPrev);
	}

	bool decodeFrame(const byte *raw, uint32 blockAlign, int16 *out) {
		Common::BitStreamMemoryStream rawStream(raw, blockAlign);
		BitReader gb(rawStream);

		if (gb.getBits(1))
			return false; // real bitstream requires this start bit to be 0

		int chBlock = 0;
		int outChIndex = 0;
		while (gb.size() - gb.pos() >= 2) {
			int chUnitId = (int)gb.getBits(2);
			if (chUnitId == kChUnitTerminator)
				break;
			if (chUnitId == kChUnitExtension)
				return false; // not supported by the reference decoder either
			if (chBlock >= numChannelBlocks || channelBlocks[chBlock] != chUnitId)
				return false;

			ChanUnitCtx &unit = units[chBlock];
			unit.unitType = chUnitId;
			int channelsToProcess = chUnitId + 1;

			if (!decodeChannelUnit(gb, unit, channelsToProcess))
				return false;

			float residual[2][kFrameSamples];
			decodeResidualSpectrum(unit, residual, channelsToProcess);
			for (int ch = 0; ch < channelsToProcess; ch++)
				memcpy(&samples[(uint32)(outChIndex + ch) * kFrameSamples], residual[ch], sizeof(residual[ch]));

			float reconstructed[2][kFrameSamples];
			for (int ch = 0; ch < channelsToProcess; ch++)
				memcpy(reconstructed[ch], residual[ch], sizeof(residual[ch]));
			reconstructFrame(unit, reconstructed, channelsToProcess);

			for (int i = 0; i < channelsToProcess; i++)
				memcpy(&outBuf[(uint32)channelMap[outChIndex + i] * kFrameSamples], reconstructed[i],
				       sizeof(reconstructed[i]));

			chBlock++;
			outChIndex += channelsToProcess;
		}

		for (int i = 0; i < kFrameSamples; i++)
			for (int ch = 0; ch < channels; ch++)
				out[(uint32)i * channels + ch] = clampInt16(outBuf[(uint32)ch * kFrameSamples + i]);

		return true;
	}
};

ATRAC3PlusFrameDecoder::ATRAC3PlusFrameDecoder() : _impl(new Impl()), _channels(0) {}

ATRAC3PlusFrameDecoder::~ATRAC3PlusFrameDecoder() {
	delete _impl;
}

bool ATRAC3PlusFrameDecoder::init(uint16 channels, uint16 blockAlign) {
	// Channel layouts 1-4, 6, 7, 8 are implemented generically per FFmpeg's
	// atrac3plusdec.c. Real data only covers mono and stereo (CH_UNIT_MONO /
	// CH_UNIT_STEREO); the 3-8 channel configs are TO VERIFY LATER against
	// real data (see docs/middleware-plan.md).
	(void)blockAlign; // ATRAC3+ frame size is carried per-frame by the container
	if (channels == 0 || channels > 8 || channels == 5)
		return false;
	_impl->reset(channels);
	_channels = channels;
	return true;
}

int ATRAC3PlusFrameDecoder::frameSamples() const {
	return kFrameSamples;
}

bool ATRAC3PlusFrameDecoder::decodeFrame(const byte *frame, uint32 blockAlign, int16 *out) {
	if (!_channels || !frame || blockAlign == 0)
		return false;
	return _impl->decodeFrame(frame, blockAlign, out);
}

SeekableAudioStream *makeATRAC3PlusStream(
	Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse,
	uint16 channels, uint32 sampleRate, uint16 blockAlign, uint32 numSamples) {
	if (!stream || blockAlign == 0)
		return nullptr;
	if (channels == 0 || channels > 8 || channels == 5)
		return nullptr;

	ATRAC3PlusFrameDecoder *decoder = new ATRAC3PlusFrameDecoder();
	return new ATRACFrameAudioStream(stream, disposeAfterUse, decoder, channels, sampleRate, blockAlign, numSamples);
}

} // namespace Audio
