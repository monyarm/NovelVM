#include "common/formats/ahx.h"

#include "audio/audiostream.h"
#include "common/array.h"
#include "common/memstream.h"
#include "common/textconsole.h"
#include "common/util.h"

// CRI AHX (CRI "voice" format) decoder. Bitstream structure, Layer II
// bit-allocation/scale-factor parsing, quantization and the 32-band
// polyphase synthesis filterbank are ported from FFmpeg's MPEG-1/2 Audio
// Layer II decoder; CRI container facts (fixed 0xFFF5E0C0 frame header,
// VBR frame resync by scanning for the next matching header, zero-fill
// to the MPEG-declared frame size) follow vgmstream.
//
// FFmpeg (https://ffmpeg.org) is distributed under the GNU Lesser General
// Public License, version 2.1 or later. For the ported portion in this
// file, its copyright notice and license follow:
//
//   Copyright (c) 2001, 2002 Fabrice Bellard
//   Copyright (c) 2008-2025 FFmpeg developers
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU Lesser General Public License as
//   published by the Free Software Foundation; either version 2.1 of the
//   License, or (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful, but
//   WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//   Lesser General Public License for more details.
//
//   You should have received a copy of the GNU Lesser General Public
//   License along with FFmpeg; if not, write to the Free Software
//   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
//   02110-1301 USA.
//
// vgmstream (https://github.com/vgmstream/vgmstream) is distributed
// under the ISC license; see src/meta/ahx.c and
// src/coding/mpeg_custom_utils_ahx.c for the container facts used here.

namespace Common {

enum {
	kSBLimit            = 32, // number of subbands
	kFrameSamples       = 1152,
	kExpectedFrameBytes = 0x414, // MPEG frame size implied by the fixed header
	kOutShift           = 16 + 23 - 15, // WFRAC_BITS + FRAC_BITS - 15 = 24
};

namespace {

// ---- MPEG-1/2 Layer II tables (ported from FFmpeg mpegaudiodata.c) ----

// Number of active subbands per allocation table.
const int kSBLimitTable[5] = { 27, 30, 8, 12, 30 };

const int kQuantSteps[17] = {
	3, 5, 7, 9, 15, 31, 63, 127, 255, 511, 1023, 2047, 4095, 8191,
	16383, 32767, 65535,
};

// Negative value means 3 samples are transmitted grouped.
const int kQuantBits[17] = {
	-5, -7, 3, -10, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
};

// Layer II bit allocation tables. Row layout: [width, qidx(0)..qidx(2^w-1)].
static const unsigned char allocTable1[] = {
	 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
	 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
	 4,  0,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 3,  0,  1,  2,  3,  4,  5, 16,
	 2,  0,  1, 16,
	 2,  0,  1, 16,
	 2,  0,  1, 16,
	 2,  0,  1, 16,
	 2,  0,  1, 16,
	 2,  0,  1, 16,
	 2,  0,  1, 16,
};

static const unsigned char allocTable3[] = {
	 4,  0,  1,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
	 4,  0,  1,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
};

static const unsigned char allocTable4[] = {
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	 4,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 3,  0,  1,  3,  4,  5,  6,  7,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
	 2,  0,  1,  3,
};

const unsigned char *const kAllocTables[5] = {
	allocTable1, allocTable1, allocTable3, allocTable3, allocTable4,
};

// ---- MSB-first bit reader over a fixed frame (like FFmpeg's get_bits) ----

struct BitReader {
	const byte *buf;
	uint32 size; // bytes
	uint32 index; // bit index

	BitReader(const byte *p, uint32 n) : buf(p), size(n), index(0) {}

	uint32 getBits(uint32 n) {
		uint32 val = 0;
		for (uint32 i = 0; i < n; i++) {
			val <<= 1;
			if (index < size * 8 && (buf[index / 8] & (0x80 >> (index & 7))))
				val |= 1;
			index++;
		}
		return val;
	}
};

// ---- quantization exponents (ported from FFmpeg mpegaudiodec.c) ----

#define FRAC_ONE (1 << 23) // FFmpeg FRAC_BITS = 23
#define FRAC_BITS 23

// 2^(-j/3) * 2^(i+2) * 2, scaled to fixed point (scale_factor_mult[15][3]).
int32 scaleFactorMult[15][3];

// Mult table for Layer II group quantization (scale_factor_mult2[3][3]).
int32 scaleFactorMult2[3][3];

// Group decoding tables (division_tab3/5/9). Only indices 0,1,3 have
// negative quant_bits (steps 3, 5, 9).
int16 divisionTab3[1 << 6];
int16 divisionTab5[1 << 8];
int16 divisionTab9[1 << 11];
int16 *const kDivisionTabs[4] = { divisionTab3, divisionTab5, nullptr, divisionTab9 };

static bool sTablesReady = false;

void initTables() {
	if (sTablesReady)
		return;

	// FFmpeg fixed build (mpegaudiodec_fixed.c):
	//   FIXR(x) = (int)(x * FRAC_ONE + 0.5)
	//   MULLx(x,y,s) = MULL((int)x,(int)y,s) = ((int64)x * (int64)y) >> s
	for (int i = 0; i < 15; i++) {
		int64 norm = (((int64)1 << (i + 2)) * FRAC_ONE) / ((1 << (i + 2)) - 1);
		scaleFactorMult[i][0] = (int32)((norm * (int64)(int32)(2.0 * FRAC_ONE + 0.5)) >> FRAC_BITS);
		scaleFactorMult[i][1] = (int32)((norm * (int64)(int32)(0.7937005259 * 2.0 * FRAC_ONE + 0.5)) >> FRAC_BITS);
		scaleFactorMult[i][2] = (int32)((norm * (int64)(int32)(0.6299605249 * 2.0 * FRAC_ONE + 0.5)) >> FRAC_BITS);
	}

	// SCALE_GEN(v) = FIXR_OLD(v*{1, .7937, .62996}); same in fixed and float.
	const double base[3] = { 4.0 / 3.0, 4.0 / 5.0, 4.0 / 9.0 };
	const double mult[3] = { 1.0, 0.7937005259, 0.6299605249 };
	for (int step = 0; step < 3; step++)
		for (int m = 0; m < 3; m++)
			scaleFactorMult2[step][m] = (int32)(base[step] * mult[m] * FRAC_ONE + 0.5);

	// division tables, as FFmpeg's mpegaudiodec_common_init_static generates them.
	for (int i = 0; i < 4; i++) {
		if (kQuantBits[i] >= 0)
			continue;
		int steps = kQuantSteps[i];
		int width = 1 << (-kQuantBits[i] + 1);
		for (int j = 0; j < width; j++) {
			int val = j;
			int val1 = val % steps;
			val /= steps;
			int val2 = val % steps;
			int val3 = val / steps;
			kDivisionTabs[i][j] = (int16)(val1 + (val2 << 4) + (val3 << 8));
		}
	}

	sTablesReady = true;
}

inline int l1Unscale(int n, int mant, int scaleFactor) {
	int shift = scaleFactor / 3;
	int mod   = scaleFactor - shift * 3;

	int64 val = (int64)(int)(mant + (-1U << n) + 1) * scaleFactorMult[n - 1][mod];
	shift += n;
	return (int)((val + (1LL << (shift - 1))) >> shift);
}

inline int l2UnscaleGroup(int steps, int mant, int scaleFactor) {
	int shift = scaleFactor / 3;
	int mod   = scaleFactor - shift * 3;

	int val = (mant - (steps >> 1)) * scaleFactorMult2[steps >> 2][mod];
	if (shift > 0)
		val = (val + (1 << (shift - 1))) >> shift;
	return val;
}

// ---- 32-point DCT (ported from FFmpeg dct32_template.c, fixed build) ----

// Fixed-point version: FIXHR(x) scales by 2^32, MULH3 uses MULH (multiply
// high), and the butterfly state is accumulated in unsigned ints with
// wraparound as in FFmpeg's DCT32_FIXED.
#define FIXHR(x) ((int32)((x) * (1LL << 32) + 0.5))
#define MULH(a, b) ((int32)(((int64)(int32)(a) * (int64)(int32)(b)) >> 32))
#define MULH3(x, y, s) MULH((s) * (x), (y))

static void dct32(int32 *out, const int32 *tab) {
	uint32 val[32];
	uint32 tmp0, tmp1;

#define BF(a, b, c, s) \
	{ tmp0 = val[a] + val[b]; tmp1 = val[a] - val[b]; val[a] = tmp0; val[b] = MULH3(tmp1, c, 1 << (s)); }
#define BF0(a, b, c, s) \
	{ tmp0 = (uint32)tab[a] + (uint32)tab[b]; tmp1 = (uint32)tab[a] - (uint32)tab[b]; val[a] = tmp0; val[b] = MULH3(tmp1, c, 1 << (s)); }

	BF0( 0, 31, FIXHR(0.50060299823519630134 / 2), 1);
	BF0(15, 16, FIXHR(10.19000812354805681150 / 32), 5);
	BF( 0, 15, FIXHR(0.50241928618815570551 / 2), 1);
	BF(16, 31, -FIXHR(0.50241928618815570551 / 2), 1);
	BF0( 7, 24, FIXHR(0.67480834145500574602 / 2), 1);
	BF0( 8, 23, FIXHR(0.74453627100229844977 / 2), 1);
	BF( 7,  8, FIXHR(5.10114861868916385802 / 16), 4);
	BF(23, 24, -FIXHR(5.10114861868916385802 / 16), 4);
	BF( 0,  7, FIXHR(0.50979557910415916894 / 2), 1);
	BF( 8, 15, -FIXHR(0.50979557910415916894 / 2), 1);
	BF(16, 23, FIXHR(0.50979557910415916894 / 2), 1);
	BF(24, 31, -FIXHR(0.50979557910415916894 / 2), 1);
	BF0( 3, 28, FIXHR(0.53104259108978417447 / 2), 1);
	BF0(12, 19, FIXHR(1.48416461631416627724 / 4), 2);
	BF( 3, 12, FIXHR(0.64682178335999012954 / 2), 1);
	BF(19, 28, -FIXHR(0.64682178335999012954 / 2), 1);
	BF0( 4, 27, FIXHR(0.55310389603444452782 / 2), 1);
	BF0(11, 20, FIXHR(1.16943993343288495515 / 4), 2);
	BF( 4, 11, FIXHR(0.78815462345125022473 / 2), 1);
	BF(20, 27, -FIXHR(0.78815462345125022473 / 2), 1);
	BF( 3,  4, FIXHR(2.56291544774150617881 / 8), 3);
	BF(11, 12, -FIXHR(2.56291544774150617881 / 8), 3);
	BF(19, 20, FIXHR(2.56291544774150617881 / 8), 3);
	BF(27, 28, -FIXHR(2.56291544774150617881 / 8), 3);
	BF( 0,  3, FIXHR(0.54119610014619698439 / 2), 1);
	BF( 4,  7, -FIXHR(0.54119610014619698439 / 2), 1);
	BF( 8, 11, FIXHR(0.54119610014619698439 / 2), 1);
	BF(12, 15, -FIXHR(0.54119610014619698439 / 2), 1);
	BF(16, 19, FIXHR(0.54119610014619698439 / 2), 1);
	BF(20, 23, -FIXHR(0.54119610014619698439 / 2), 1);
	BF(24, 27, FIXHR(0.54119610014619698439 / 2), 1);
	BF(28, 31, -FIXHR(0.54119610014619698439 / 2), 1);

	BF0( 1, 30, FIXHR(0.50547095989754365998 / 2), 1);
	BF0(14, 17, FIXHR(3.40760841846871878570 / 8), 3);
	BF( 1, 14, FIXHR(0.52249861493968888062 / 2), 1);
	BF(17, 30, -FIXHR(0.52249861493968888062 / 2), 1);
	BF0( 6, 25, FIXHR(0.62250412303566481615 / 2), 1);
	BF0( 9, 22, FIXHR(0.83934964541552703873 / 2), 1);
	BF( 6,  9, FIXHR(1.72244709823833392782 / 4), 2);
	BF(22, 25, -FIXHR(1.72244709823833392782 / 4), 2);
	BF( 1,  6, FIXHR(0.60134488693504528054 / 2), 1);
	BF( 9, 14, -FIXHR(0.60134488693504528054 / 2), 1);
	BF(17, 22, FIXHR(0.60134488693504528054 / 2), 1);
	BF(25, 30, -FIXHR(0.60134488693504528054 / 2), 1);
	BF0( 2, 29, FIXHR(0.51544730992262454697 / 2), 1);
	BF0(13, 18, FIXHR(2.05778100995341155085 / 8), 3);
	BF( 2, 13, FIXHR(0.56694403481635770368 / 2), 1);
	BF(18, 29, -FIXHR(0.56694403481635770368 / 2), 1);
	BF0( 5, 26, FIXHR(0.58293496820613387367 / 2), 1);
	BF0(10, 21, FIXHR(0.97256823786196069369 / 2), 1);
	BF( 5, 10, FIXHR(1.06067768599034747134 / 4), 2);
	BF(21, 26, -FIXHR(1.06067768599034747134 / 4), 2);
	BF( 2,  5, FIXHR(0.89997622313641570463 / 2), 1);
	BF(10, 13, -FIXHR(0.89997622313641570463 / 2), 1);
	BF(18, 21, FIXHR(0.89997622313641570463 / 2), 1);
	BF(26, 29, -FIXHR(0.89997622313641570463 / 2), 1);
	BF( 1,  2, FIXHR(1.30656296487637652785 / 4), 2);
	BF( 5,  6, -FIXHR(1.30656296487637652785 / 4), 2);
	BF( 9, 10, FIXHR(1.30656296487637652785 / 4), 2);
	BF(13, 14, -FIXHR(1.30656296487637652785 / 4), 2);
	BF(17, 18, FIXHR(1.30656296487637652785 / 4), 2);
	BF(21, 22, -FIXHR(1.30656296487637652785 / 4), 2);
	BF(25, 26, FIXHR(1.30656296487637652785 / 4), 2);
	BF(29, 30, -FIXHR(1.30656296487637652785 / 4), 2);

#define BF1(a, b, c, d) \
	{ BF(a, b, FIXHR(0.70710678118654752439 / 2), 1); BF(c, d, -FIXHR(0.70710678118654752439 / 2), 1); val[c] += val[d]; }
#define BF2(a, b, c, d) \
	{ BF(a, b, FIXHR(0.70710678118654752439 / 2), 1); BF(c, d, -FIXHR(0.70710678118654752439 / 2), 1); val[c] += val[d]; val[a] += val[c]; val[c] += val[b]; val[b] += val[d]; }

	BF1( 0,  1,  2,  3);
	BF2( 4,  5,  6,  7);
	BF1( 8,  9, 10, 11);
	BF2(12, 13, 14, 15);
	BF1(16, 17, 18, 19);
	BF2(20, 21, 22, 23);
	BF1(24, 25, 26, 27);
	BF2(28, 29, 30, 31);

#define ADD(a, b) val[a] += val[b]
	ADD( 8, 12);
	ADD(12, 10);
	ADD(10, 14);
	ADD(14,  9);
	ADD( 9, 13);
	ADD(13, 11);
	ADD(11, 15);

	out[ 0] = val[0];
	out[16] = val[1];
	out[ 8] = val[2];
	out[24] = val[3];
	out[ 4] = val[4];
	out[20] = val[5];
	out[12] = val[6];
	out[28] = val[7];
	out[ 2] = val[8];
	out[18] = val[9];
	out[10] = val[10];
	out[26] = val[11];
	out[ 6] = val[12];
	out[22] = val[13];
	out[14] = val[14];
	out[30] = val[15];

	ADD(24, 28);
	ADD(28, 26);
	ADD(26, 30);
	ADD(30, 25);
	ADD(25, 29);
	ADD(29, 27);
	ADD(27, 31);

	out[ 1] = val[16] + val[24];
	out[17] = val[17] + val[25];
	out[ 9] = val[18] + val[26];
	out[25] = val[19] + val[27];
	out[ 5] = val[20] + val[28];
	out[21] = val[21] + val[29];
	out[13] = val[22] + val[30];
	out[29] = val[23] + val[31];
	out[ 3] = val[24] + val[20];
	out[19] = val[25] + val[21];
	out[11] = val[26] + val[22];
	out[27] = val[27] + val[23];
	out[ 7] = val[28] + val[18];
	out[23] = val[29] + val[19];
	out[15] = val[30] + val[17];
	out[31] = val[31];
#undef ADD
#undef BF
#undef BF0
#undef BF1
#undef BF2
#undef FIXHR
#undef MULH
#undef MULH3
}

// Half MPEG encoding window (full precision), from FFmpeg mpegaudiodsp_data.c.
static const int32 kMpaEnwindow[257] = {
	 0,  -1,  -1,  -1,  -1,  -1,  -1,  -2,  -2,  -2,  -2,  -3,  -3,  -4,  -4,  -5,
	-5,  -6,  -7,  -7,  -8,  -9, -10, -11, -13, -14, -16, -17, -19, -21, -24, -26,
  -29, -31, -35, -38, -41, -45, -49, -53, -58, -63, -68, -73, -79, -85, -91, -97,
 -104,-111,-117,-125,-132,-139,-147,-154,-161,-169,-176,-183,-190,-196,-202,-208,
  213, 218, 222, 225, 227, 228, 228, 227, 224, 221, 215, 208, 200, 189, 177, 163,
  146, 127, 106,  83,  57,  29,  -2, -36, -72,-111,-153,-197,-244,-294,-347,-401,
 -459,-519,-581,-645,-711,-779,-848,-919,-991,-1064,-1137,-1210,-1283,-1356,-1428,-1498,
-1567,-1634,-1698,-1759,-1817,-1870,-1919,-1962,-2001,-2032,-2057,-2075,-2085,-2087,-2080,-2063,
 2037, 2000, 1952, 1893, 1822, 1739, 1644, 1535, 1414, 1280, 1131,  970,  794,  605,  402,  185,
  -45, -288, -545, -814,-1095,-1388,-1692,-2006,-2330,-2663,-3004,-3351,-3705,-4063,-4425,-4788,
-5153,-5517,-5879,-6237,-6589,-6935,-7271,-7597,-7910,-8209,-8491,-8755,-8998,-9219,-9416,-9585,
-9727,-9838,-9916,-9959,-9966,-9935,-9863,-9750,-9592,-9389,-9139,-8840,-8492,-8092,-7640,-7134,
 6574, 5959, 5288, 4561, 3776, 2935, 2037, 1082,   70, -998,-2122,-3300,-4533,-5818,-7154,-8540,
-9975,-11455,-12980,-14548,-16155,-17799,-19478,-21189,-22929,-24694,-26482,-28289,-30112,-31947,-33791,-35640,
-37489,-39336,-41176,-43006,-44821,-46617,-48390,-50137,-51853,-53534,-55178,-56778,-58333,-59838,-61289,-62684,
-64019,-65290,-66494,-67629,-68692,-69679,-70590,-71420,-72169,-72835,-73415,-73908,-74313,-74630,-74856,-74992,
 75038,
};

int32 synthWindow[512 + 256];

void initSynthWindow() {
	// FFmpeg fixed build: window[i] = ff_mpa_enwindow[i] verbatim, with the
	// same anti-alias sign flips. Only the float build scaled by 1/2^39.
	for (int i = 0; i < 257; i++) {
		int32 v = kMpaEnwindow[i];
		synthWindow[i] = v;
		if ((i & 63) != 0)
			v = -v;
		if (i != 0)
			synthWindow[512 - i] = v;
	}
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 16; j++)
			synthWindow[512 + 16 * i + j] = synthWindow[64 * i + 32 - j];
	for (int i = 0; i < 8; i++)
		for (int j = 0; j < 16; j++)
			synthWindow[512 + 128 + 16 * i + j] = synthWindow[64 * i + 48 - j];
}

} // namespace

// 32-band polyphase synthesis filterbank (ported from FFmpeg's
// ff_mpa_synth_filter + ff_mpadsp_apply_window, float build, mono).
class SynthFilter {
public:
	SynthFilter() {
		reset();
	}

	void reset() {
		memset(_synthBuf, 0, sizeof(_synthBuf));
		_offset = 0;
		_dither = 0;
		initSynthWindow();
	}

	// Input: 32 int32 subband samples. Output: 32 int16 PCM samples.
	void synth(int16 *out, const int32 *sbSamples) {
		int32 *synthBuf = _synthBuf + _offset;
		dct32(synthBuf, sbSamples);

		applyWindow(synthBuf, out);

		_offset = (_offset - 32) & 511;
	}

private:
	int32 _synthBuf[512 * 2];
	int _offset;
	int32 _dither;

	// FFmpeg fixed build round_sample: extract the top OUT_SHIFT bits and
	// keep the low OUT_SHIFT bits as dither (fixed path DOES carry dither).
	static int roundSample(int64 &sum) {
		int sample = (int)(sum >> kOutShift);
		sum &= (1 << kOutShift) - 1;
		return CLIP<int>(sample, -32768, 32767);
	}

	void applyWindow(int32 *synthBuf, int16 *samples) {
		int64 sum, sum2;
		const int32 *w = synthWindow;
		const int32 *w2 = synthWindow + 31;
		const int32 *p;

		// copy to avoid wrap
		memcpy(synthBuf + 512, synthBuf, 32 * sizeof(int32));

		// sample 0
		sum = _dither;
		p = synthBuf + 16;
		sum += (int64)w[0 * 64] * p[0 * 64];
		sum += (int64)w[1 * 64] * p[1 * 64];
		sum += (int64)w[2 * 64] * p[2 * 64];
		sum += (int64)w[3 * 64] * p[3 * 64];
		sum += (int64)w[4 * 64] * p[4 * 64];
		sum += (int64)w[5 * 64] * p[5 * 64];
		sum += (int64)w[6 * 64] * p[6 * 64];
		sum += (int64)w[7 * 64] * p[7 * 64];
		p = synthBuf + 48;
		sum -= (int64)(w + 32)[0 * 64] * p[0 * 64];
		sum -= (int64)(w + 32)[1 * 64] * p[1 * 64];
		sum -= (int64)(w + 32)[2 * 64] * p[2 * 64];
		sum -= (int64)(w + 32)[3 * 64] * p[3 * 64];
		sum -= (int64)(w + 32)[4 * 64] * p[4 * 64];
		sum -= (int64)(w + 32)[5 * 64] * p[5 * 64];
		sum -= (int64)(w + 32)[6 * 64] * p[6 * 64];
		sum -= (int64)(w + 32)[7 * 64] * p[7 * 64];
		samples[0] = roundSample(sum);
		w++;

		int16 *samples2 = samples + 31;
		for (int j = 1; j < 16; j++) {
			// two samples computed together (SUM8P2 for sum and sum2)
			sum2 = 0;
			p = synthBuf + 16 + j;
			sum += (int64)w[0 * 64] * p[0 * 64];
			sum += (int64)w[1 * 64] * p[1 * 64];
			sum += (int64)w[2 * 64] * p[2 * 64];
			sum += (int64)w[3 * 64] * p[3 * 64];
			sum += (int64)w[4 * 64] * p[4 * 64];
			sum += (int64)w[5 * 64] * p[5 * 64];
			sum += (int64)w[6 * 64] * p[6 * 64];
			sum += (int64)w[7 * 64] * p[7 * 64];
			sum2 -= (int64)w2[0 * 64] * p[0 * 64];
			sum2 -= (int64)w2[1 * 64] * p[1 * 64];
			sum2 -= (int64)w2[2 * 64] * p[2 * 64];
			sum2 -= (int64)w2[3 * 64] * p[3 * 64];
			sum2 -= (int64)w2[4 * 64] * p[4 * 64];
			sum2 -= (int64)w2[5 * 64] * p[5 * 64];
			sum2 -= (int64)w2[6 * 64] * p[6 * 64];
			sum2 -= (int64)w2[7 * 64] * p[7 * 64];
			p = synthBuf + 48 - j;
			sum -= (int64)(w + 32)[0 * 64] * p[0 * 64];
			sum -= (int64)(w + 32)[1 * 64] * p[1 * 64];
			sum -= (int64)(w + 32)[2 * 64] * p[2 * 64];
			sum -= (int64)(w + 32)[3 * 64] * p[3 * 64];
			sum -= (int64)(w + 32)[4 * 64] * p[4 * 64];
			sum -= (int64)(w + 32)[5 * 64] * p[5 * 64];
			sum -= (int64)(w + 32)[6 * 64] * p[6 * 64];
			sum -= (int64)(w + 32)[7 * 64] * p[7 * 64];
			sum2 -= (int64)(w2 + 32)[0 * 64] * p[0 * 64];
			sum2 -= (int64)(w2 + 32)[1 * 64] * p[1 * 64];
			sum2 -= (int64)(w2 + 32)[2 * 64] * p[2 * 64];
			sum2 -= (int64)(w2 + 32)[3 * 64] * p[3 * 64];
			sum2 -= (int64)(w2 + 32)[4 * 64] * p[4 * 64];
			sum2 -= (int64)(w2 + 32)[5 * 64] * p[5 * 64];
			sum2 -= (int64)(w2 + 32)[6 * 64] * p[6 * 64];
			sum2 -= (int64)(w2 + 32)[7 * 64] * p[7 * 64];

			samples[j] = roundSample(sum);
			sum += sum2;
			*samples2 = roundSample(sum);
			samples2--;
			w++;
			w2--;
		}

		// sample 16
		p = synthBuf + 32;
		sum -= (int64)(w + 32)[0 * 64] * p[0 * 64];
		sum -= (int64)(w + 32)[1 * 64] * p[1 * 64];
		sum -= (int64)(w + 32)[2 * 64] * p[2 * 64];
		sum -= (int64)(w + 32)[3 * 64] * p[3 * 64];
		sum -= (int64)(w + 32)[4 * 64] * p[4 * 64];
		sum -= (int64)(w + 32)[5 * 64] * p[5 * 64];
		sum -= (int64)(w + 32)[6 * 64] * p[6 * 64];
		sum -= (int64)(w + 32)[7 * 64] * p[7 * 64];
		samples[16] = roundSample(sum);
		_dither = sum; // low OUT_SHIFT bits carry to the next window
	}
};

// Decode one Layer II frame (mono) into 36*32 = 1152 int32 subband samples
// (fixed point, as FFmpeg's MPADecodeContext sb_samples). `frame` is the
// zero-padded, full-size frame including its 4-byte MPEG header.
bool decodeLayer2Frame(const byte *frame, uint32 frameBytes, int32 *out) {
	uint32 hdr = READ_BE_UINT32(frame);
	if ((hdr >> 21) != 0x7FF) // sync
		return false;
	if (((hdr >> 17) & 3) != 2) // Layer II
		return false;

	initTables();

	// AHX uses MPEG-2/LSF Layer II, which always selects allocation table 4.
	const unsigned char *allocTable = kAllocTables[4];
	int sblimit = kSBLimitTable[4];

	BitReader br(frame + 4, frameBytes - 4);

	byte bitAlloc[kSBLimit];
	byte scaleCode[kSBLimit];
	byte scaleFactors[kSBLimit][3];

	int j = 0;
	for (int i = 0; i < sblimit; i++) {
		int bits = allocTable[j];
		bitAlloc[i] = (byte)br.getBits(bits);
		j += 1 << bits;
	}

	for (int i = 0; i < sblimit; i++) {
		if (bitAlloc[i])
			scaleCode[i] = (byte)br.getBits(2);
	}


	for (int i = 0; i < sblimit; i++) {
		if (!bitAlloc[i])
			continue;
		byte *sf = scaleFactors[i];
		switch (scaleCode[i]) {
		case 0:
			sf[0] = (byte)br.getBits(6);
			sf[1] = (byte)br.getBits(6);
			sf[2] = (byte)br.getBits(6);
			break;
		case 2:
			sf[0] = (byte)br.getBits(6);
			sf[1] = sf[0];
			sf[2] = sf[0];
			break;
		case 1:
			sf[0] = (byte)br.getBits(6);
			sf[2] = (byte)br.getBits(6);
			sf[1] = sf[0];
			break;
		default: // 3
			sf[0] = (byte)br.getBits(6);
			sf[2] = (byte)br.getBits(6);
			sf[1] = sf[2];
			break;
		}
	}


	// mono: bound == sblimit, so the joint-stereo second subband loop is absent.
	for (int k = 0; k < 3; k++) {
		for (int l = 0; l < 12; l += 3) {
			j = 0;
			for (int i = 0; i < sblimit; i++) {
				int bits = allocTable[j];
				int b = bitAlloc[i];
				if (b) {
					int scale = scaleFactors[i][k];
					int qindex = allocTable[j + b];
					int qbits = kQuantBits[qindex];
					if (qbits < 0) {
						// 3 values written together
						int v = (int)br.getBits(-qbits);
						int v2 = kDivisionTabs[qindex][v];
						int steps = kQuantSteps[qindex];
						out[(k * 12 + l + 0) * kSBLimit + i] =
							l2UnscaleGroup(steps, v2 & 15, scale);
						out[(k * 12 + l + 1) * kSBLimit + i] =
							l2UnscaleGroup(steps, (v2 >> 4) & 15, scale);
						out[(k * 12 + l + 2) * kSBLimit + i] =
							l2UnscaleGroup(steps, v2 >> 8, scale);
					} else {
						for (int m = 0; m < 3; m++) {
							int v = (int)br.getBits(qbits);
							int un = l1Unscale(qbits - 1, v, scale);
							out[(k * 12 + l + m) * kSBLimit + i] = un;
						}
					}
				} else {
					out[(k * 12 + l + 0) * kSBLimit + i] = 0;
					out[(k * 12 + l + 1) * kSBLimit + i] = 0;
					out[(k * 12 + l + 2) * kSBLimit + i] = 0;
				}
				j += 1 << bits;
			}
			// fill remaining subbands with zero
			for (int i = sblimit; i < kSBLimit; i++) {
				out[(k * 12 + l + 0) * kSBLimit + i] = 0;
				out[(k * 12 + l + 1) * kSBLimit + i] = 0;
				out[(k * 12 + l + 2) * kSBLimit + i] = 0;
			}
		}
	}
	return true;
}

// ----- CRI AHX container header -----

struct AHXHeader {
	uint32 sampleRate;
	uint32 numSamples;
	uint32 dataOffset;
	bool valid;
};

bool parseAHXHeader(SeekableReadStream &f, AHXHeader &hdr) {
	hdr.valid = false;

	byte head[0x14];
	if (f.read(head, sizeof(head)) != sizeof(head))
		return false;

	if (READ_BE_UINT16(head + 0x00) != 0x8000) // CRI magic
		return false;

	uint32 startDelta = READ_BE_UINT16(head + 0x02);
	uint32 start = startDelta + 0x04;

	byte type = head[0x04];
	if (type != 0x10 && type != 0x11)
		return false;
	if (head[0x05] != 0 || head[0x06] != 0) // frame size / bps must be 0 for AHX
		return false;
	if (head[0x07] != 1) // only mono AHX can be created
		return false;

	uint32 rate = READ_BE_UINT32(head + 0x08);
	uint32 numSamples = READ_BE_UINT32(head + 0x0c);
	if (head[0x12] != 0x06) // version
		return false;
	byte enc = head[0x13];
	if (enc != 0) { // only unencrypted streams are supported
		warning("AHX: encrypted streams (type 0x%02x) not supported", enc);
		return false;
	}

	// banner "(c" followed by ")CRI" right before the audio data
	f.seek(start - 6);
	byte banner[6];
	if (f.read(banner, sizeof(banner)) != sizeof(banner))
		return false;
	if (READ_BE_UINT16(banner) != 0x2863 || READ_BE_UINT32(banner + 2) != 0x29435249)
		return false;

	hdr.sampleRate = rate;
	hdr.numSamples = numSamples;
	hdr.dataOffset = start;
	hdr.valid = true;
	return true;
}

// Find the size of the frame at `buf`. The window was read at stream offset
// `streamOffset` from a file of `fileSize` bytes; `windowBytes` of it are
// valid. Returns bytes up to the next matching frame header, or the end of
// the available window (the frame reaches EOF).
uint32 scanFrameSize(const byte *buf, uint32 windowBytes, int64 streamOffset, int64 fileSize) {
	uint32 currHeader = READ_BE_UINT32(buf);

	// Probe positions 4..windowBytes-4 (inclusive where a full header fits).
	uint32 pos = 4;
	while (pos + 4 <= windowBytes && pos <= kExpectedFrameBytes) {
		if (buf[pos] == 0xFF && currHeader == READ_BE_UINT32(buf + pos))
			return pos;
		if (streamOffset + pos + 0x10 >= fileSize)
			return pos;
		pos++;
	}
	return windowBytes;
}

// ----- Stream -----

class AHXStream : public Audio::SeekableAudioStream {
public:
	AHXStream(SeekableReadStream *stream, DisposeAfterUse::Flag dispose)
		: _stream(stream), _dispose(dispose), _synth(new SynthFilter()) {
		const int64 pos = _stream->pos();
		_ok = parseAHXHeader(*_stream, _header);
		_stream->seek(pos);
		if (!_ok)
			return;

		_frameBuf.resize(kExpectedFrameBytes);
		_tmpSubbands.resize(kFrameSamples);
		_decoded.resize(kFrameSamples);
		samplesRead = 0;
		_bufPos = kFrameSamples;
		_eofPending = false;

		_stream->seek(_header.dataOffset);
	}

	~AHXStream() override {
		if (_dispose == DisposeAfterUse::YES)
			delete _stream;
		delete _synth;
	}

	int readBuffer(int16 *buffer, const int numSamples) override {
		if (!_ok)
			return 0;

		int produced = 0;
		while (produced < numSamples && !endOfData()) {
			if (_bufPos >= kFrameSamples && !decodeFrame())
				break;

			int toCopy = MIN((int32)kFrameSamples - (int32)_bufPos, numSamples - produced);
			if ((int64)samplesRead + toCopy > _header.numSamples)
				toCopy = _header.numSamples - samplesRead;
			if (toCopy <= 0)
				break;
			memcpy(buffer + produced, _decoded.data() + _bufPos, toCopy * sizeof(int16));
			produced += toCopy;
			_bufPos += toCopy;
			samplesRead += toCopy;
		}
		return produced;
	}

	bool isStereo() const override { return false; }
	int getRate() const override { return (int)_header.sampleRate; }

	bool endOfData() const override {
		return _eofPending || (_header.valid && samplesRead >= _header.numSamples);
	}

	bool rewind() override {
		_stream->seek(_header.dataOffset);
		samplesRead = 0;
		_bufPos = kFrameSamples;
		_eofPending = false;
		_synth->reset();
		return true;
	}

	bool seek(const Audio::Timestamp &where) override {
		uint32 target = (uint32)where.convertToFramerate(_header.sampleRate).totalNumberOfFrames();
		if (target >= _header.numSamples) {
			samplesRead = _header.numSamples;
			return true;
		}

		rewind();

		// AHX frames are VBR, so seeking decodes from the start (voice clips
		// are small).
		while ((int64)samplesRead + (int64)kFrameSamples <= target) {
			if (!decodeFrame())
				return false;
			samplesRead += kFrameSamples;
			_bufPos = kFrameSamples; // fully consumed this frame
		}
		if ((int64)samplesRead < (int64)target) {
			if (!decodeFrame())
				return false;
			_bufPos = target - samplesRead;
			samplesRead = target;
		} else {
			_bufPos = kFrameSamples;
		}
		return true;
	}

	Audio::Timestamp getLength() const override {
		return Audio::Timestamp(0, _header.numSamples, _header.sampleRate);
	}

private:
	SeekableReadStream *_stream;
	DisposeAfterUse::Flag _dispose;
	AHXHeader _header;
	bool _ok = false;

	SynthFilter *_synth;
	Common::Array<byte> _frameBuf;
	Common::Array<int32> _tmpSubbands;
	Common::Array<int16> _decoded;
	uint32 samplesRead = 0;
	uint32 _bufPos = kFrameSamples;
	bool _eofPending = false;

	bool decodeFrame() {
		int64 frameStart = _stream->pos();
		int64 remaining = _stream->size() - frameStart;
		if (remaining <= 0) {
			_eofPending = true;
			return false;
		}
		// Read at most the expected window; near EOF fewer bytes remain.
		uint32 window = (uint32)MIN<int64>(kExpectedFrameBytes, remaining);
		uint32 got = _stream->read(_frameBuf.data(), window);
		if (got < 4) {
			_eofPending = true;
			return false;
		}
		// Probe only the bytes we actually have.
		uint32 frameSize = scanFrameSize(_frameBuf.data(), got, frameStart, _stream->size());

		// zero-fill the tail of the expected window (frame bytes past the
		// real end are junk that the decoder must not interpret)
		if (got < kExpectedFrameBytes)
			memset(_frameBuf.data() + got, 0, kExpectedFrameBytes - got);

		if (!decodeLayer2Frame(_frameBuf.data(), kExpectedFrameBytes, _tmpSubbands.data())) {
			_eofPending = true;
			return false;
		}

		// synthesis filterbank: 36 rows of 32 subband samples -> 1152 PCM
		for (int row = 0; row < 36; row++)
			_synth->synth(&_decoded[row * kSBLimit], _tmpSubbands.data() + row * kSBLimit);

		_stream->seek(frameStart + frameSize);
		if (frameStart + frameSize + 0x10 >= _stream->size())
			_eofPending = true;

		_bufPos = 0;
		return true;
	}
};

} // namespace Common

Audio::SeekableAudioStream *Common::makeAHXStream(Common::SeekableReadStream *stream, DisposeAfterUse::Flag disposeAfterUse) {
	AHXHeader hdr;
	int64 pos = stream->pos();
	bool ok = parseAHXHeader(*stream, hdr);
	stream->seek(pos);
	if (!ok)
		return nullptr;

	return new Common::AHXStream(stream, disposeAfterUse);
}
