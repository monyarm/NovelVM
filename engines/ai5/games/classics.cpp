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

#include "common/textconsole.h"

#include "ai5/animplay.h"
#include "ai5/audio.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/sys.h"
#include "ai5/text.h"
#include "ai5/vm.h"
#include "ai5/games/classics.h"

namespace AI5 {

// indexed by AudioChannel (BGM, SE0)
static byte audioVol[2] = { 31, 31 };

static uint fadeTime(byte fromVol, byte toVol) {
	uint diff = ABS((int)toVol - (int)fromVol);
	return diff * 100 + 50;
}

// XXX: Volume is a value in the range [0,31], which corresponds to the range
//      [-5000,0] in increments of 156 (volume in dB).
static int getVolumeDb(byte vol) {
	if (vol == 31)
		return 0;
	return -5000 + vol * 156;
}

static void classicsAudioFade(AudioChannel ch, byte vol, bool stop, bool sync) {
	// XXX: a bit strange, but this is how it works...
	if (vol > audioVol[ch])
		stop = false;
	else if (vol == audioVol[ch])
		return;

	audioMixerFade(ch, getVolumeDb(vol), fadeTime(audioVol[ch], vol), stop, sync);
	audioVol[ch] = vol;
}

static void classicsAudioFadeOut(AudioChannel ch, byte vol, bool sync) {
	if (vol == audioVol[ch]) {
		audioStop(ch);
		return;
	}
	audioMixerFade(ch, getVolumeDb(vol), fadeTime(audioVol[ch], vol), true, sync);
	audioVol[ch] = vol;
}

static void classicsAudioSetVolume(AudioChannel ch, byte vol) {
	audioVol[ch] = vol;
	audioSetVolume(ch, getVolumeDb(vol));
}

static void classicsAudioRestoreVolume(AudioChannel ch) {
	if (!audioIsPlaying(ch))
		return;
	if (audioVol[ch] == 31) {
		audioStop(ch);
		return;
	}
	audioMixerFade(ch, kAudioVolumeMax, fadeTime(audioVol[ch], 31), false, false);
	audioVol[ch] = 31;
}

void classicsAudio(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:  audioBgmPlay(vmStringParam(params, 1), true); break;
	case 2:  audioStop(kAudioChBgm); break;
	case 3:  audioSePlay(vmStringParam(params, 1), 0); break;
	case 4:  classicsAudioFade(kAudioChBgm, vmExprParam(params, 2), vmExprParam(params, 3), true); break;
	case 5:  classicsAudioSetVolume(kAudioChBgm, vmExprParam(params, 1)); break;
	case 7:  classicsAudioFade(kAudioChBgm, vmExprParam(params, 2), vmExprParam(params, 3), false); break;
	case 9:  classicsAudioFadeOut(kAudioChBgm, vmExprParam(params, 1), true); break;
	case 10: classicsAudioFadeOut(kAudioChBgm, vmExprParam(params, 2), false); break;
	case 12: audioStop(kAudioChSe0); break;
	case 18: classicsAudioRestoreVolume(kAudioChBgm); break;
	default: error("System.Audio.function[%u] not implemented", params.params[0].val);
	}
}

void classicsCursor(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: cursorReload(); break;
	case 1: cursorUnload(); break;
	case 2: sysCursorSavePos(params); break;
	case 3: cursorSetPos(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 4: cursorLoad(vmExprParam(params, 1) * 2, 2, nullptr); break;
	case 5: cursorShow(); break;
	case 6: cursorHide(); break;
	default: error("System.Cursor.function[%u] not implemented", params.params[0].val);
	}
}

void classicsAnim(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: animInitStream(vmExprParam(params, 1), vmExprParam(params, 2)); break;
	case 1: animStart(vmExprParam(params, 1)); break;
	case 2: animStop(vmExprParam(params, 1)); break;
	case 3: animHalt(vmExprParam(params, 1)); break;
	// TODO
	case 4: warning("System.Anim.function[4] not implemented"); break;
	case 5: animStopAll(); break;
	case 6: animHaltAll(); break;
	default: error("System.Anim.function[%u] not implemented", params.params[0].val);
	}
}

static void checkRgbParam(ParamList &params, uint i, byte &r, byte &g, byte &b) {
	uint32 c = vmExprParam(params, i);
	r = ((c >> 4) & 0xf) * 17;
	g = ((c >> 8) & 0xf) * 17;
	b = (c & 0xf) * 17;
}

static void paletteSet(ParamList &params) {
	if (params.nrParams > 1) {
		byte r, g, b;
		byte pal[256 * 4];
		checkRgbParam(params, 1, r, g, b);
		// XXX: the original only sets the first 64 entries and leaves the rest
		//      uninitialized
		memset(pal, 0, sizeof(pal));
		for (int i = 0; i < 256; i += 4) {
			pal[i + 0] = b;
			pal[i + 1] = g;
			pal[i + 2] = r;
			pal[i + 3] = 0;
		}
		gfxPaletteSet(pal, 0, 256);
	} else {
		gfxPaletteSet(g_mem->palette, 0, 256);
	}
}

static void paletteCrossfade1(ParamList &params) {
	if (params.nrParams > 1) {
		byte r, g, b;
		checkRgbParam(params, 1, r, g, b);
		gfxPaletteCrossfadeTo(r, g, b, 0, 256, 240);
	} else {
		gfxPaletteCrossfade(g_mem->palette, 0, 256, 240);
	}
}

static void paletteCrossfade2(ParamList &params) {
	// XXX: t is a value from 0-15 corresponding to the interval [0-3600]
	//      in increments of 240
	uint32 t = vmExprParam(params, 1);
	if (params.nrParams > 2) {
		byte r, g, b;
		checkRgbParam(params, 2, r, g, b);
		gfxPaletteCrossfadeTo(r, g, b, 0, 256, (t & 0xf) * 240);
	} else {
		gfxPaletteCrossfade(g_mem->palette, 0, 256, (t & 0xf) * 240);
	}
}

void classicsPalette(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0: paletteSet(params); break;
	case 1: paletteCrossfade1(params); break;
	case 2: paletteCrossfade2(params); break;
	case 3: gfxDisplayHide(0); break;
	case 4: gfxDisplayUnhide(); break;
	default: error("System.Palette.function[%u] not implemented", params.params[0].val);
	}
}

void classicsGraphics(ParamList &params) {
	switch (vmExprParam(params, 0)) {
	case 0:  sysGraphicsCopy(params); break;
	case 1:  sysGraphicsCopyMasked(params); break;
	case 2:  sysGraphicsFillBg(params); break;
	case 3:  sysGraphicsCopySwap(params); break;
	case 4:  sysGraphicsSwapBgFg(params); break;
	case 5:  sysGraphicsCompose(params); break;
	case 6:  sysGraphicsInvertColors(params); break;
	case 20: sysGraphicsCopyProgressive(params); break;
	default: error("System.Image.function[%u] not implemented", params.params[0].val);
	}
}

void classicsGetCursorSegment(ParamList &params) {
	sysGetCursorSegmentAt(vmExprParam(params, 0), vmExprParam(params, 1), vmExprParam(params, 2));
}

void classicsGetTextColors(ParamList &params) {
	uint32 bg, fg;
	gfxTextGetColors(bg, fg);
	memSetVar32(18, ((bg & 0xf) << 4) | (fg & 0xf));
}

void classicsSetOps(Game &game) {
	vmSetDefaultOps(game);
	for (uint i = 0; i < ARRAYSIZE(game.stmtOp); i++)
		game.stmtOp[i] = nullptr;

	game.exprOp[0xf6] = vmExprPtr32Get16;
	game.exprOp[0xf7] = vmExprPtr32Get8;
	game.exprOp[0xf8] = vmExprVar32;

	game.stmtOp[0x01] = vmStmtTxtNewLog;
	game.stmtOp[0x02] = vmStmtStrNewLog;
	game.stmtOp[0x03] = vmStmtSetFlagConst16;
	game.stmtOp[0x04] = vmStmtSetVar16Const8;
	game.stmtOp[0x05] = vmStmtSetFlagExpr;
	game.stmtOp[0x06] = vmStmtPtr16Set8;
	game.stmtOp[0x07] = vmStmtPtr16Set16;
	game.stmtOp[0x08] = vmStmtPtr32Set32;
	game.stmtOp[0x09] = vmStmtPtr32Set16;
	game.stmtOp[0x0a] = vmStmtPtr32Set8;
	game.stmtOp[0x0b] = vmStmtJz;
	game.stmtOp[0x0c] = vmStmtJmp;
	game.stmtOp[0x0d] = vmStmtSys;
	game.stmtOp[0x0e] = vmStmtMesjmp;
	game.stmtOp[0x0f] = vmStmtMescall;
	game.stmtOp[0x10] = vmStmtDefmenu;
	game.stmtOp[0x11] = vmStmtCall;
	game.stmtOp[0x12] = vmStmtUtil;
	game.stmtOp[0x13] = vmStmtLine;
	game.stmtOp[0x14] = vmStmtDefproc;
	game.stmtOp[0x15] = vmStmtMenuexec;
	game.stmtOp[0x16] = vmStmtSetVar32Const8;

	game.vm.exec = vmExec;
	game.vm.eval = vmEval;
	game.vm.readParams = vmReadParams;
	game.vm.endCode = 0x00;
}

} // End of namespace AI5
