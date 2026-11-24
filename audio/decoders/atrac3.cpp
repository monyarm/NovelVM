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

// The decode algorithm here (spectral/tonal component decoding, gain
// compensation, QMF synthesis filterbank, joint-stereo matrixing/
// weighting, and the fixed tables below) is ported from FFmpeg's
// libavcodec/atrac3.c and atrac.c -- real facts/formulas only, re-expressed
// in ScummVM idiom, no code copied verbatim. Those files are:
//   Copyright (c) 2006-2008 Maxim Poliakovski
//   Copyright (c) 2006-2008 Benjamin Larsson
// and licensed under the GNU Lesser General Public License, version 2.1 or
// later -- see LICENSES/COPYING.LGPL in this repository for the full text.

#include "audio/decoders/atrac3.h"

#include "audio/audiostream.h"
#include "common/array.h"
#include "common/bitstream.h"
#include "common/util.h"
#include "math/mdct.h"

#include <math.h>

namespace Audio {

namespace {

const int kSamplesPerFrame = 1024;
const int kMDCTSize = 512;
const int kMDCTBits = 9; // Math::MDCT: N = 2^bits = 512
const int kNumBands = 4;
const int kBandSize = 256;
const int kVLCTableBits = 8;
const int kMaxJSPairs = 4; // up to 8 channels

const int kIdToExpOffset = 4;
const int kLocScaleBits = 3;
const int kLocSize = 1 << kLocScaleBits;

// Real fixed tables from the ATRAC3 spec (facts, not expression) --
// see atrac3data.h/atrac.c above for the reference values.

struct HuffLen { byte symbol; byte length; };

const HuffLen kHuffTab1[9] = {
	{31, 1}, {32, 3}, {33, 3}, {34, 4}, {35, 4},
	{36, 5}, {37, 5}, {38, 5}, {39, 5}
};
const HuffLen kHuffTab2[5] = {
	{31, 1}, {32, 3}, {30, 3}, {33, 3}, {29, 3}
};
const HuffLen kHuffTab3[7] = {
	{31, 1}, {32, 3}, {30, 3}, {33, 4}, {29, 4}, {34, 4}, {28, 4}
};
const HuffLen kHuffTab4[9] = {
	{31, 1}, {32, 3}, {30, 3}, {33, 4}, {29, 4},
	{34, 5}, {28, 5}, {35, 5}, {27, 5}
};
const HuffLen kHuffTab5[15] = {
	{31, 2}, {32, 3}, {30, 3}, {33, 4}, {29, 4},
	{34, 4}, {28, 4}, {38, 4}, {24, 4}, {35, 5},
	{27, 5}, {36, 6}, {26, 6}, {37, 6}, {25, 6}
};
const HuffLen kHuffTab6[31] = {
	{31, 3}, {32, 4}, {30, 4}, {33, 4}, {29, 4}, {34, 4},
	{28, 4}, {46, 4}, {16, 4}, {35, 5}, {27, 5}, {36, 5},
	{26, 5}, {37, 5}, {25, 5}, {38, 6}, {24, 6}, {39, 6},
	{23, 6}, {40, 6}, {22, 6}, {41, 6}, {21, 6}, {42, 7},
	{20, 7}, {43, 7}, {19, 7}, {44, 7}, {18, 7}, {45, 7},
	{17, 7}
};
const HuffLen kHuffTab7[63] = {
	{31, 3}, {62, 4}, {0, 4}, {32, 5}, {30, 5}, {33, 5},
	{29, 5}, {34, 5}, {28, 5}, {35, 5}, {27, 5}, {36, 5},
	{26, 5}, {37, 6}, {25, 6}, {38, 6}, {24, 6}, {39, 6},
	{23, 6}, {40, 6}, {22, 6}, {41, 6}, {21, 6}, {42, 6},
	{20, 6}, {43, 6}, {19, 6}, {44, 6}, {18, 6}, {45, 7},
	{17, 7}, {46, 7}, {16, 7}, {47, 7}, {15, 7}, {48, 7},
	{14, 7}, {49, 7}, {13, 7}, {50, 7}, {12, 7}, {51, 7},
	{11, 7}, {52, 8}, {10, 8}, {53, 8}, {9, 8}, {54, 8},
	{8, 8}, {55, 8}, {7, 8}, {56, 8}, {6, 8}, {57, 8},
	{5, 8}, {58, 8}, {4, 8}, {59, 8}, {3, 8}, {60, 8},
	{2, 8}, {61, 8}, {1, 8}
};

const HuffLen *const kHuffTabs[7] = {
	kHuffTab1, kHuffTab2, kHuffTab3, kHuffTab4, kHuffTab5, kHuffTab6, kHuffTab7
};
const byte kHuffTabSizes[7] = {9, 5, 7, 9, 15, 31, 63};
// Every real (symbol, length) pair above is biased by this amount to
// produce the actual decoded value (e.g. symbol 31 with the shortest code
// decodes to mantissa/index 0).
const int kHuffBias = -31;

const byte kClcLengthTab[8] = {0, 4, 3, 3, 4, 4, 5, 6};
const int8 kMantissaClcTab[4] = {0, 1, -2, -1};
const int8 kMantissaVlcTab[18] = {
	0, 0, 0, 1, 0, -1, 1, 0, -1, 0, 1, 1, 1, -1, -1, 1, -1, -1
};

const float kInvMaxQuant[8] = {
	0.0f, 1.0f / 1.5f, 1.0f / 2.5f, 1.0f / 3.5f,
	1.0f / 4.5f, 1.0f / 7.5f, 1.0f / 15.5f, 1.0f / 31.5f
};

const uint16 kSubbandTab[33] = {
	0, 8, 16, 24, 32, 40, 48, 56,
	64, 80, 96, 112, 128, 144, 160, 176,
	192, 224, 256, 288, 320, 352, 384, 416,
	448, 480, 512, 576, 640, 704, 768, 896,
	1024
};

const float kMatrixCoeffs[8] = {0.0f, 2.0f, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 1.0f};

// Half of the real 48-tap QMF synthesis window (mirrored below).
const float kQMFHalfTaps[24] = {
	-0.00001461907f, -0.00009205479f, -0.000056157569f, 0.00030117269f,
	0.0002422519f, -0.00085293897f, -0.0005205574f, 0.0020340169f,
	0.00078333891f, -0.0042153862f, -0.00075614988f, 0.0078402944f,
	-0.000061169922f, -0.01344162f, 0.0024626821f, 0.021736089f,
	-0.007801671f, -0.034090221f, 0.01880949f, 0.054326009f,
	-0.043596379f, -0.099384367f, 0.13207909f, 0.46424159f
};

// Tables that only need computing once (pure functions of the constants
// above); built lazily on first use.
struct Tables {
	float sfTable[64];
	float mdctWindow[kMDCTSize];
	float qmfWindow[48];
	float gainTab1[16];
	float gainTab2[31];
	struct VLCSlot { int8 value; byte length; };
	VLCSlot vlcTable[7][1 << kVLCTableBits];

	Tables() {
		for (int i = 0; i < 64; i++)
			sfTable[i] = (float)pow(2.0, (i - 15) / 3.0);

		for (int i = 0; i < 24; i++) {
			float s = kQMFHalfTaps[i] * 2.0f;
			qmfWindow[i] = qmfWindow[47 - i] = s;
		}

		for (int i = 0, j = 255; i < 128; i++, j--) {
			float wi = (float)sin(((i + 0.5) / 256.0 - 0.5) * M_PI) + 1.0f;
			float wj = (float)sin(((j + 0.5) / 256.0 - 0.5) * M_PI) + 1.0f;
			float w = 0.5f * (wi * wi + wj * wj);
			mdctWindow[i] = mdctWindow[511 - i] = wi / w;
			mdctWindow[j] = mdctWindow[511 - j] = wj / w;
		}

		for (int i = 0; i < 16; i++)
			gainTab1[i] = (float)pow(2.0, kIdToExpOffset - i);
		for (int i = -15; i < 16; i++)
			gainTab2[i + 15] = (float)pow(2.0, -1.0 / kLocSize * i);

		for (int t = 0; t < 7; t++)
			buildVLCTable(kHuffTabs[t], kHuffTabSizes[t], vlcTable[t]);
	}

	// Canonical-Huffman flat lookup table: codes are assigned to entries
	// in the order given (not sorted by symbol), shortest length first --
	// matches FFmpeg's ff_vlc_init_from_lengths() for a single-level table
	// (every real code here is at most kVLCTableBits long).
	static void buildVLCTable(const HuffLen *entries, int count, VLCSlot *table) {
		int blCount[kVLCTableBits + 1] = {0};
		for (int i = 0; i < count; i++)
			blCount[entries[i].length]++;

		int nextCode[kVLCTableBits + 1] = {0};
		int code = 0;
		for (int len = 1; len <= kVLCTableBits; len++) {
			code = (code + blCount[len - 1]) << 1;
			nextCode[len] = code;
		}

		for (int i = 0; i < count; i++) {
			int len = entries[i].length;
			int c = nextCode[len]++;
			int shift = kVLCTableBits - len;
			int base = c << shift;
			int span = 1 << shift;
			int8 value = (int8)(entries[i].symbol + kHuffBias);
			for (int k = 0; k < span; k++) {
				table[base + k].value = value;
				table[base + k].length = (byte)len;
			}
		}
	}
};

const Tables &tables() {
	static const Tables t;
	return t;
}

int16 floatToInt16(float v) {
	if (v > 32767.0f)
		return 32767;
	if (v < -32768.0f)
		return -32768;
	return (int16)(v);
}

typedef Common::BitStreamMemory8MSB BitReader;

int32 getSBits(BitReader &bits, int n) {
	if (n == 0)
		return 0;
	int32 v = (int32)bits.getBits(n);
	if (v & (1 << (n - 1)))
		v -= (1 << n);
	return v;
}

int32 getVLC(BitReader &bits, int tableIndex) {
	const Tables::VLCSlot *table = tables().vlcTable[tableIndex];
	uint32 peeked = bits.peekBits(kVLCTableBits);
	const Tables::VLCSlot &slot = table[peeked];
	bits.skip(slot.length);
	return slot.value;
}

struct GainInfo {
	int numPoints = 0;
	int levCode[8] = {0};
	int locCode[8] = {0};
};

struct GainBlock {
	GainInfo block[kNumBands];
};

struct TonalComponent {
	int pos = 0;
	int numCoefs = 0;
	float coef[8] = {0};
};

struct ChannelUnit {
	int bandsCoded = 0;
	int numComponents = 0;
	float prevFrame[kSamplesPerFrame] = {0};
	int gcBlkSwitch = 0;
	TonalComponent components[64];
	GainBlock gainBlock[2];
	float spectrum[kSamplesPerFrame] = {0};
	float imdctBuf[kMDCTSize] = {0};
	float delayBuf1[46] = {0};
	float delayBuf2[46] = {0};
	float delayBuf3[46] = {0};
};

// Mantissa decoding: which Huffman/CLC table the coded values use, and how
// many values to produce.
void readQuantSpectralCoeffs(BitReader &bits, int selector, bool clcCoding, int32 *mantissas, int numCodes) {
	if (selector == 1)
		numCodes /= 2;

	if (clcCoding) {
		int numBits = kClcLengthTab[selector];
		if (selector > 1) {
			for (int i = 0; i < numCodes; i++)
				mantissas[i] = numBits ? getSBits(bits, numBits) : 0;
		} else {
			for (int i = 0; i < numCodes; i++) {
				int code = numBits ? (int)bits.getBits(numBits) : 0;
				mantissas[i * 2] = kMantissaClcTab[code >> 2];
				mantissas[i * 2 + 1] = kMantissaClcTab[code & 3];
			}
		}
	} else {
		if (selector != 1) {
			for (int i = 0; i < numCodes; i++)
				mantissas[i] = getVLC(bits, selector - 1);
		} else {
			for (int i = 0; i < numCodes; i++) {
				int huffSymb = getVLC(bits, 0);
				if (huffSymb < 0 || huffSymb > 8)
					huffSymb = 0;
				mantissas[i * 2] = kMantissaVlcTab[huffSymb * 2];
				mantissas[i * 2 + 1] = kMantissaVlcTab[huffSymb * 2 + 1];
			}
		}
	}
}

int decodeSpectrum(BitReader &bits, float *output) {
	int subbandVlcIndex[32] = {0};
	int sfIndex[32] = {0};
	int32 mantissas[128];

	int numSubbands = (int)bits.getBits(5);
	bool clcCoding = bits.getBits(1) != 0;

	for (int i = 0; i <= numSubbands; i++)
		subbandVlcIndex[i] = (int)bits.getBits(3);

	for (int i = 0; i <= numSubbands; i++) {
		if (subbandVlcIndex[i] != 0)
			sfIndex[i] = (int)bits.getBits(6);
	}

	int first = 0;
	for (int i = 0; i <= numSubbands; i++) {
		first = kSubbandTab[i];
		int last = kSubbandTab[i + 1];
		int subbandSize = last - first;

		if (subbandVlcIndex[i] != 0) {
			readQuantSpectralCoeffs(bits, subbandVlcIndex[i], clcCoding, mantissas, subbandSize);

			float scaleFactor = tables().sfTable[sfIndex[i]] * kInvMaxQuant[subbandVlcIndex[i]];
			for (int j = 0; first < last; first++, j++)
				output[first] = mantissas[j] * scaleFactor;
		} else {
			memset(output + first, 0, subbandSize * sizeof(float));
		}
	}

	first = kSubbandTab[numSubbands + 1];
	memset(output + first, 0, (kSamplesPerFrame - first) * sizeof(float));
	return numSubbands;
}

// Returns -1 on a malformed bitstream, otherwise the tonal component count.
int decodeTonalComponents(BitReader &bits, TonalComponent *components, int numBands) {
	int bandFlags[4];
	int32 mantissa[8];
	int componentCount = 0;

	int nbComponents = (int)bits.getBits(5);
	if (nbComponents == 0)
		return 0;

	int codingModeSelector = (int)bits.getBits(2);
	if (codingModeSelector == 2)
		return -1;
	bool clcCoding = (codingModeSelector & 1) != 0;

	for (int i = 0; i < nbComponents; i++) {
		for (int b = 0; b <= numBands; b++)
			bandFlags[b] = (int)bits.getBits(1);

		int codedValuesPerComponent = (int)bits.getBits(3);

		int quantStepIndex = (int)bits.getBits(3);
		if (quantStepIndex <= 1)
			return -1;

		if (codingModeSelector == 3)
			clcCoding = bits.getBits(1) != 0;

		for (int b = 0; b < (numBands + 1) * 4; b++) {
			if (bandFlags[b >> 2] == 0)
				continue;

			int codedComponents = (int)bits.getBits(3);

			for (int c = 0; c < codedComponents; c++) {
				if (componentCount >= 64)
					return -1;
				TonalComponent &cmp = components[componentCount];

				int sfIndex = (int)bits.getBits(6);
				cmp.pos = b * 64 + (int)bits.getBits(6);

				int maxCodedValues = kSamplesPerFrame - cmp.pos;
				int codedValues = MIN(maxCodedValues, codedValuesPerComponent + 1);

				float scaleFactor = tables().sfTable[sfIndex] * kInvMaxQuant[quantStepIndex];

				readQuantSpectralCoeffs(bits, quantStepIndex, clcCoding, mantissa, codedValues);

				cmp.numCoefs = codedValues;
				for (int m = 0; m < codedValues; m++)
					cmp.coef[m] = mantissa[m] * scaleFactor;

				componentCount++;
			}
		}
	}

	return componentCount;
}

// Returns -1 on a malformed bitstream.
int decodeGainControl(BitReader &bits, GainBlock &block, int numBands) {
	int b;
	for (b = 0; b <= numBands; b++) {
		GainInfo &gain = block.block[b];
		gain.numPoints = (int)bits.getBits(3);
		for (int j = 0; j < gain.numPoints; j++) {
			gain.levCode[j] = (int)bits.getBits(4);
			gain.locCode[j] = (int)bits.getBits(5);
			if (j && gain.locCode[j] <= gain.locCode[j - 1])
				return -1;
		}
	}
	for (; b < kNumBands; b++)
		block.block[b].numPoints = 0;
	return 0;
}

int addTonalComponents(float *spectrum, int numComponents, const TonalComponent *components) {
	int lastPos = -1;
	for (int i = 0; i < numComponents; i++) {
		lastPos = MAX(components[i].pos + components[i].numCoefs, lastPos);
		float *output = &spectrum[components[i].pos];
		for (int j = 0; j < components[i].numCoefs; j++)
			output[j] += components[i].coef[j];
	}
	return lastPos;
}

inline float interpolate(float oldV, float newV, int nsample) {
	return oldV + (float)nsample * 0.125f * (newV - oldV);
}

void reverseMatrixing(float *su1, float *su2, const int *prevCode, const int *currCode) {
	int band = 0;
	for (int i = 0; i < kNumBands; i++, band += kBandSize) {
		int s1 = prevCode[i];
		int s2 = currCode[i];
		int nsample = band;

		if (s1 != s2) {
			float mc1l = kMatrixCoeffs[s1 * 2], mc1r = kMatrixCoeffs[s1 * 2 + 1];
			float mc2l = kMatrixCoeffs[s2 * 2], mc2r = kMatrixCoeffs[s2 * 2 + 1];

			for (; nsample < band + 8; nsample++) {
				float c1 = su1[nsample];
				float c2 = su2[nsample];
				c2 = c1 * interpolate(mc1l, mc2l, nsample - band) +
				     c2 * interpolate(mc1r, mc2r, nsample - band);
				su1[nsample] = c2;
				su2[nsample] = c1 * 2.0f - c2;
			}
		}

		switch (s2) {
		case 0:
			for (; nsample < band + kBandSize; nsample++) {
				float c1 = su1[nsample], c2 = su2[nsample];
				su1[nsample] = c2 * 2.0f;
				su2[nsample] = (c1 - c2) * 2.0f;
			}
			break;
		case 1:
			for (; nsample < band + kBandSize; nsample++) {
				float c1 = su1[nsample], c2 = su2[nsample];
				su1[nsample] = (c1 + c2) * 2.0f;
				su2[nsample] = c2 * -2.0f;
			}
			break;
		default: // 2 or 3
			for (; nsample < band + kBandSize; nsample++) {
				float c1 = su1[nsample], c2 = su2[nsample];
				su1[nsample] = c1 + c2;
				su2[nsample] = c1 - c2;
			}
			break;
		}
	}
}

void getChannelWeights(int index, int flag, float ch[2]) {
	if (index == 7) {
		ch[0] = 1.0f;
		ch[1] = 1.0f;
	} else {
		ch[0] = (index & 7) / 7.0f;
		ch[1] = (float)sqrt(2.0f - ch[0] * ch[0]);
		if (flag)
			SWAP(ch[0], ch[1]);
	}
}

void channelWeighting(float *su1, float *su2, const int *weightingDelay) {
	if (weightingDelay[1] != 7 || weightingDelay[3] != 7) {
		float w0[2], w1[2];
		getChannelWeights(weightingDelay[1], weightingDelay[0], w0);
		getChannelWeights(weightingDelay[3], weightingDelay[2], w1);

		for (int band = kBandSize; band < kNumBands * kBandSize; band += kBandSize) {
			int nsample = band;
			for (; nsample < band + 8; nsample++) {
				su1[nsample] *= interpolate(w0[0], w0[1], nsample - band);
				su2[nsample] *= interpolate(w1[0], w1[1], nsample - band);
			}
			for (; nsample < band + kBandSize; nsample++) {
				su1[nsample] *= w1[0];
				su2[nsample] *= w1[1];
			}
		}
	}
}

// Gain compensation and 50%-overlap TDAC reconstruction for one band's
// worth (256 samples) of unwindowed-but-windowed-on-input IMDCT output.
// `in` is the 512-sample IMDCT+window output for this frame's band; its
// second half is saved into `prev` for the next frame's overlap.
void gainCompensation(float *in, float *prev, const GainInfo &gcNow, const GainInfo &gcNext, float *out) {
	float gcScale = gcNext.numPoints ? tables().gainTab1[gcNext.levCode[0]] : 1.0f;

	if (gcNow.numPoints == 0) {
		for (int pos = 0; pos < kBandSize; pos++)
			out[pos] = in[pos] * gcScale + prev[pos];
	} else {
		int pos = 0;
		for (int i = 0; i < gcNow.numPoints; i++) {
			int lastpos = gcNow.locCode[i] << kLocScaleBits;
			float lev = tables().gainTab1[gcNow.levCode[i]];
			int nextLevCode = (i + 1 < gcNow.numPoints) ? gcNow.levCode[i + 1] : kIdToExpOffset;
			float gainInc = tables().gainTab2[nextLevCode - gcNow.levCode[i] + 15];

			for (; pos < lastpos; pos++)
				out[pos] = (in[pos] * gcScale + prev[pos]) * lev;

			for (; pos < lastpos + kLocSize; pos++) {
				out[pos] = (in[pos] * gcScale + prev[pos]) * lev;
				lev *= gainInc;
			}
		}
		for (; pos < kBandSize; pos++)
			out[pos] = in[pos] * gcScale + prev[pos];
	}

	memcpy(prev, in + kBandSize, kBandSize * sizeof(float));
}

// Quadrature mirror synthesis filter: combines a low and a high subband
// (nIn samples each) into 2*nIn samples via a 48-tap FIR, carrying a
// 46-sample tail across calls in `delayBuf`. `temp` must have room for
// nIn*2 + 46 floats.
void iqmf(const float *inlo, const float *inhi, int nIn, float *pOut, float *delayBuf, float *temp) {
	memcpy(temp, delayBuf, 46 * sizeof(float));
	float *p3 = temp + 46;

	for (int i = 0; i < nIn; i += 2) {
		p3[2 * i + 0] = inlo[i] + inhi[i];
		p3[2 * i + 1] = inlo[i] - inhi[i];
		p3[2 * i + 2] = inlo[i + 1] + inhi[i + 1];
		p3[2 * i + 3] = inlo[i + 1] - inhi[i + 1];
	}

	const float *p1 = temp;
	for (int j = nIn; j != 0; j--) {
		float s1 = 0.0f, s2 = 0.0f;
		for (int i = 0; i < 48; i += 2) {
			s1 += p1[i] * tables().qmfWindow[i];
			s2 += p1[i + 1] * tables().qmfWindow[i + 1];
		}
		pOut[0] = s2;
		pOut[1] = s1;
		p1 += 2;
		pOut += 2;
	}

	memcpy(delayBuf, temp + nIn * 2, 46 * sizeof(float));
}

} // namespace

// Stateful plain-ATRAC3 frame decoder. Holds exactly the cross-frame
// synthesis state that used to live inside ATRAC3Stream.
struct ATRAC3FrameDecoder::Impl {
	ATRAC3CodingMode codingMode;
	Math::MDCT mdct;
	Common::Array<ChannelUnit> units;
	Common::Array<int> matrixCoeffIndexPrev, matrixCoeffIndexNow, matrixCoeffIndexNext;
	Common::Array<int> weightingDelay;
	uint16 channels = 0;
	uint16 jsBlockAlign = 0;
	Common::Array<byte> decodedBytesBuffer;
	Common::Array<float> outSamples;
	float iqmfTemp[1070];

	explicit Impl(ATRAC3CodingMode mode) : codingMode(mode), mdct(kMDCTBits, true, 1.0) {}

	void reset(uint16 nch, uint16 blockAlign) {
		channels = nch;
		units.clear();
		units.resize(nch);
		int numPairs = (nch + 1) / 2;
		matrixCoeffIndexPrev.clear();
		matrixCoeffIndexPrev.resize(numPairs * 4, 3);
		matrixCoeffIndexNow.clear();
		matrixCoeffIndexNow.resize(numPairs * 4, 3);
		matrixCoeffIndexNext.clear();
		matrixCoeffIndexNext.resize(numPairs * 4, 3);
		weightingDelay.clear();
		weightingDelay.resize(numPairs * 6);
		for (int p = 0; p < numPairs; p++) {
			weightingDelay[p * 6 + 0] = 0;
			weightingDelay[p * 6 + 1] = 7;
			weightingDelay[p * 6 + 2] = 0;
			weightingDelay[p * 6 + 3] = 7;
			weightingDelay[p * 6 + 4] = 0;
			weightingDelay[p * 6 + 5] = 7;
		}
		jsBlockAlign = (blockAlign / nch) * 2;
		decodedBytesBuffer.resize(jsBlockAlign);
		outSamples.resize((uint32)nch * kSamplesPerFrame);
	}

	// Regular 512-point IMDCT without overlapping, save for the odd-band
	// spectral reversal the QMF's reversed spectra requires.
	void imlt(float *input, float *output, bool oddBand) {
		if (oddBand) {
			for (int i = 0; i < 128; i++)
				SWAP(input[i], input[255 - i]);
		}
		mdct.calcIMDCT(output, input);
		for (int i = 0; i < kMDCTSize; i++)
			output[i] *= tables().mdctWindow[i];
	}

	bool decodeChannelSoundUnit(BitReader &bits, ChannelUnit &unit, float *output, int channelNum, ATRAC3CodingMode mode) {
		GainBlock &gain1 = unit.gainBlock[unit.gcBlkSwitch];
		GainBlock &gain2 = unit.gainBlock[1 - unit.gcBlkSwitch];

		if (mode == kATRAC3CodingJointStereo && (channelNum % 2) == 1) {
			if (bits.getBits(2) != 3)
				return false;
		} else {
			if (bits.getBits(6) != 0x28)
				return false;
		}

		unit.bandsCoded = (int)bits.getBits(2);

		if (decodeGainControl(bits, gain2, unit.bandsCoded) < 0)
			return false;

		unit.numComponents = decodeTonalComponents(bits, unit.components, unit.bandsCoded);
		if (unit.numComponents < 0)
			return false;

		int numSubbands = decodeSpectrum(bits, unit.spectrum);

		int lastTonal = addTonalComponents(unit.spectrum, unit.numComponents, unit.components);

		int numBands = (kSubbandTab[numSubbands + 1] - 1) >> 8;
		if (lastTonal >= 0)
			numBands = MAX((lastTonal + kBandSize) >> 8, numBands);

		for (int band = 0; band < kNumBands; band++) {
			if (band <= numBands)
				imlt(&unit.spectrum[band * kBandSize], unit.imdctBuf, (band & 1) != 0);
			else
				memset(unit.imdctBuf, 0, sizeof(unit.imdctBuf));

			gainCompensation(unit.imdctBuf, &unit.prevFrame[band * kBandSize],
			                  gain1.block[band], gain2.block[band], &output[band * kBandSize]);
		}

		unit.gcBlkSwitch ^= 1;
		return true;
	}

	bool decodeFrame(const byte *raw, uint32 blockAlign, int16 *out) {
		bool ok = true;
		// NOTE: the JOINT_STEREO branch (channel-pair matrixing/weighting,
		// byte-reversed second sound unit) and the >2-channel layout are
		// implemented from FFmpeg's atrac3.c spec, but no real
		// joint-stereo/multichannel plain-ATRAC3 sample exists in the
		// corpus -- TO VERIFY LATER against real data. Only the SINGLE
		// (per-channel) mode is real-data-verified (see docs/middleware-plan.md).
		if (codingMode == kATRAC3CodingJointStereo) {
			for (int ch = 0; ch + 1 < channels && ok; ch += 2) {
				int pair = ch / 2;
				const byte *jsData = raw + (uint32)pair * jsBlockAlign;

				Common::BitStreamMemoryStream rawStream1(jsData, jsBlockAlign);
				BitReader bits1(rawStream1);
				ok = decodeChannelSoundUnit(bits1, units[ch], &outSamples[(uint32)ch * kSamplesPerFrame], ch, kATRAC3CodingJointStereo);
				if (!ok)
					break;

				// The second sound unit of a joint-stereo pair is stored
				// byte-reversed in the real bitstream.
				for (uint16 i = 0; i < jsBlockAlign; i++)
					decodedBytesBuffer[i] = jsData[jsBlockAlign - 1 - i];

				uint16 skip = 0;
				while (skip < jsBlockAlign && decodedBytesBuffer[skip] == 0xF8)
					skip++;
				if (skip >= jsBlockAlign) {
					ok = false;
					break;
				}

				Common::BitStreamMemoryStream rawStream2(decodedBytesBuffer.data() + skip, jsBlockAlign - skip);
				BitReader bits2(rawStream2);

				int *wd = &weightingDelay[pair * 6];
				wd[0] = wd[2];
				wd[1] = wd[3];
				wd[2] = wd[4];
				wd[3] = wd[5];
				wd[4] = (int)bits2.getBits(1);
				wd[5] = (int)bits2.getBits(3);

				int *prev = &matrixCoeffIndexPrev[pair * 4];
				int *now = &matrixCoeffIndexNow[pair * 4];
				int *next = &matrixCoeffIndexNext[pair * 4];
				for (int i = 0; i < 4; i++) {
					prev[i] = now[i];
					now[i] = next[i];
					next[i] = (int)bits2.getBits(2);
				}

				ok = decodeChannelSoundUnit(bits2, units[ch + 1], &outSamples[(uint32)(ch + 1) * kSamplesPerFrame], ch + 1, kATRAC3CodingJointStereo);
				if (!ok)
					break;

				reverseMatrixing(&outSamples[(uint32)ch * kSamplesPerFrame], &outSamples[(uint32)(ch + 1) * kSamplesPerFrame], prev, now);
				channelWeighting(&outSamples[(uint32)ch * kSamplesPerFrame], &outSamples[(uint32)(ch + 1) * kSamplesPerFrame], wd);
			}
		} else {
			uint16 perChannel = blockAlign / channels;
			for (int ch = 0; ch < channels && ok; ch++) {
				Common::BitStreamMemoryStream rawStream(raw + (uint32)ch * perChannel, perChannel);
				BitReader bits(rawStream);
				ok = decodeChannelSoundUnit(bits, units[ch], &outSamples[(uint32)ch * kSamplesPerFrame], ch, kATRAC3CodingSingle);
			}
		}

		if (!ok)
			return false;

		for (int ch = 0; ch < channels; ch++) {
			float *o = &outSamples[(uint32)ch * kSamplesPerFrame];
			float *p1 = o, *p2 = o + 256, *p3 = o + 512, *p4 = o + 768;
			iqmf(p1, p2, 256, p1, units[ch].delayBuf1, iqmfTemp);
			iqmf(p4, p3, 256, p3, units[ch].delayBuf2, iqmfTemp);
			iqmf(p1, p3, 512, p1, units[ch].delayBuf3, iqmfTemp);
		}

		for (int i = 0; i < kSamplesPerFrame; i++) {
			for (int ch = 0; ch < channels; ch++)
				out[(uint32)i * channels + ch] = floatToInt16(outSamples[(uint32)ch * kSamplesPerFrame + i]);
		}

		return true;
	}
};

ATRAC3FrameDecoder::ATRAC3FrameDecoder(ATRAC3CodingMode codingMode) : _impl(new Impl(codingMode)), _channels(0) {}

ATRAC3FrameDecoder::~ATRAC3FrameDecoder() {
	delete _impl;
}

int ATRAC3FrameDecoder::frameSamples() const {
	return kSamplesPerFrame;
}

bool ATRAC3FrameDecoder::init(uint16 channels, uint16 blockAlign) {
	if (channels == 0 || channels > 8 || blockAlign == 0)
		return false;
	if (_impl->codingMode == kATRAC3CodingJointStereo && (channels % 2) != 0)
		return false;
	_impl->reset(channels, blockAlign);
	_channels = channels;
	return true;
}

bool ATRAC3FrameDecoder::decodeFrame(const byte *frame, uint32 blockAlign, int16 *out) {
	if (!_channels || !frame || blockAlign == 0)
		return false;
	return _impl->decodeFrame(frame, blockAlign, out);
}

SeekableAudioStream *makeATRAC3Stream(
	Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse,
	uint16 channels, uint32 sampleRate, uint16 blockAlign, ATRAC3CodingMode codingMode, uint32 numSamples) {
	if (!stream || channels == 0 || channels > 8 || blockAlign == 0)
		return nullptr;
	if (codingMode == kATRAC3CodingJointStereo && (channels % 2) != 0)
		return nullptr;

	ATRAC3FrameDecoder *decoder = new ATRAC3FrameDecoder(codingMode);
	return new ATRACFrameAudioStream(stream, disposeAfterUse, decoder, channels, sampleRate, blockAlign, numSamples);
}

} // namespace Audio
