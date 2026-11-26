/* NovelVM - Visual Novel Engine
 *
 * NovelVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on ai5-sdl2/libai5, Copyright (C) 2023 Nunuhara Cabbage
 * (GPL-2.0-or-later).
 */

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/system.h"

#include "ai5/ai5.h"
#include "ai5/animplay.h"
#include "ai5/config.h"
#include "ai5/cursor.h"
#include "ai5/gfx.h"
#include "ai5/input.h"
#include "ai5/text.h"
#include "ai5/vm.h"

#include "ai5/games/shuusaku.h"
#include "ai5/games/shuusaku_data.h"

namespace AI5 {

namespace {

enum {
	kCharsX = 80,
	kCharsY = 32,
	kCharsW = 480,
	kCharsH = 288,
	kParts   = 1,
	kBg      = 2,
	kAnim    = 6,
	kScreen  = 0,
	kInputBg = 42
};

enum MojiMode {
	kModeKana,
	kModeKanji,
	kModeKigou
};

struct BtnRect {
	int x, y, w, h;
};

bool btnContains(const BtnRect &r, int x, int y) {
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

struct Button;

struct TextInput {
	BtnRect pos;
	int cursor;
	int nrChars;
	uint32 chars[5];
};

void drawButtonNormal(Button *btn);
void drawButtonPressed(Button *btn);
void charButtonPressed(Button *btn);
void modoruPressed(Button *btn);
void ketteiPressed(Button *btn);
void leftArrowPressed(Button *btn);
void rightArrowPressed(Button *btn);
void myoujiPressed(Button *btn);
void namaePressed(Button *btn);
void kanaPressed(Button *btn);
void kanjiPressed(Button *btn);
void kigouPressed(Button *btn);
void navJumpPressed(Button *btn);
void navUpPressed(Button *btn);
void navDownPressed(Button *btn);

struct Button {
	BtnRect partsNormal;
	BtnRect partsPressed;
	BtnRect pos;
	bool hold;
	uint32 data;
	void (*pressed)(Button *btn);
	bool isRadio;
};

void makeButton(Button *btn, int normalX, int normalY, int pressedX, int pressedY,
		int w, int h, int posX, int posY, bool hold, uint32 data,
		void (*pressed)(Button *btn), bool isRadio = false) {
	btn->partsNormal = { normalX, normalY, w, h };
	btn->partsPressed = { pressedX, pressedY, w, h };
	btn->pos = { posX, posY, w, h };
	btn->hold = hold;
	btn->data = data;
	btn->pressed = pressed;
	btn->isRadio = isRadio;
}

// The character-entry screens are composed in indexed buffers.
struct CharScreen {
	byte *pixels;
	int w, h;

	CharScreen() : pixels(nullptr), w(0), h(0) {}
	~CharScreen() { delete[] pixels; }

	void alloc(int width, int height) {
		w = width;
		h = height;
		pixels = new byte[w * h];
		memset(pixels, kMaskColor, w * h);
	}

	void surface(GfxSurface *s) {
		s->pixels = pixels;
		s->w = w;
		s->h = h;
		s->pitch = w;
		s->bytesPerPixel = 1;
	}
};

TextInput myoujiInput = { { 136, 408, 0, 0 }, 0, 0, { 0 } };
TextInput namaeInput = { { 280, 408, 0, 0 }, 0, 0, { 0 } };

int mojiMode = kModeKana;
bool kettei = false;

// moji mode radio group (kana/kanji/kigou)
Button mojiButtons[3];
int mojiDown = 0;

// name target radio group (myouji/namae)
Button nameButtons[2];
int nameDown = 0;

Button leftArrow, rightArrow, modoru, ketteiButton;
Button kanjiNavBtn[12];
const uint kanjiNavJump[10] = { 0, 14, 58, 104, 129, 136, 160, 167, 173, 184 };

Common::Array<Button> kanaBtn;
Common::Array<Common::Array<Button>> kanjiBtn;
Common::Array<Button> kigouBtn;

CharScreen kanaScreen, kanjiScreen, kigouScreen;
uint kanjiNrLines = 0;
uint kanjiRow = 0;

//
// Masked copies between indexed buffers and game surfaces.
//

void maskedBlitToSurface(const CharScreen *src, int sx, int sy, int w, int h, int dx, int dy, int dstI) {
	GfxSurface *dst = gfxGetSurface(dstI);
	for (int row = 0; row < h; row++) {
		if (sy + row < 0 || sy + row >= src->h)
			continue;
		const byte *s = src->pixels + (sy + row) * src->w + sx;
		byte *d = dst->pixels + (dy + row) * dst->pitch + dx * dst->bytesPerPixel;
		for (int col = 0; col < w; col++) {
			if (s[col] != kMaskColor)
				*d = s[col];
			d += dst->bytesPerPixel;
		}
	}
	gfxDirty(dstI, dx, dy, w, h);
}

void maskedBlitFromSurface(int srcI, int sx, int sy, int w, int h, CharScreen *dst, int dx, int dy) {
	GfxSurface *src = gfxGetSurface(srcI);
	for (int row = 0; row < h; row++) {
		const byte *s = src->pixels + (sy + row) * src->pitch + sx;
		byte *d = dst->pixels + (dy + row) * dst->w + dx;
		for (int col = 0; col < w; col++) {
			if (s[col] != kMaskColor)
				d[col] = s[col];
		}
	}
}

uint32 decodeUtf8CodePoint(const char *&s) {
	const byte *p = (const byte *)s;
	int k = 0;
	byte lead = *p;
	while (lead & (0x80 >> k))
		k++;
	uint32 value = *p & ((1 << (8 - k)) - 1);
	p++;
	for (int i = 1; i < k; i++) {
		if ((*p & 0xc0) != 0x80)
			break;
		value = (value << 6) + (*p & 0x3f);
		p++;
	}
	s = (const char *)p;
	return value;
}

//
// Button drawing
//

void drawButtonNormal(Button *btn) {
	gfxCopyMasked(btn->partsNormal.x, btn->partsNormal.y, btn->partsNormal.w, btn->partsNormal.h,
			kParts, btn->pos.x, btn->pos.y, kScreen, kMaskColor);
}

void drawButtonPressed(Button *btn) {
	gfxCopyMasked(btn->partsPressed.x, btn->partsPressed.y, btn->partsPressed.w, btn->partsPressed.h,
			kParts, btn->pos.x, btn->pos.y, kScreen, kMaskColor);
}

//
// Text input cursor
//

void drawCursor(TextInput *input) {
	gfxCopyMasked(48, 80, 24, 23, kParts, input->pos.x + input->cursor * 24, input->pos.y, kScreen, kMaskColor);
}

void clearCursor(TextInput *input) {
	int x = input->pos.x + input->cursor * 24;
	int y = input->pos.y;
	gfxFill(x, y, 24, 3, kScreen, kInputBg);
	gfxFill(x, y + 20, 24, 3, kScreen, kInputBg);
	gfxFill(x, y + 3, 2, 17, kScreen, kInputBg);
	gfxFill(x + 22, y + 3, 2, 17, kScreen, kInputBg);
}

void drawChar(TextInput *input, int i, uint32 ch) {
	gfxTextDrawGlyph(input->pos.x + 2 + i * 24, input->pos.y + 2, kScreen, ch);
}

void clearChar(TextInput *input, int i) {
	gfxFill(input->pos.x + i * 24, input->pos.y, 24, 23, kScreen, kInputBg);
}

//
// Screens
//

void clearCharsScreen() {
	gfxCopy(kCharsX, kCharsY, kCharsW, kCharsH, kBg, kCharsX, kCharsY, kScreen);
}

void drawKanaScreen() {
	for (uint i = 0; i < kanaBtn.size(); i++)
		drawButtonNormal(&kanaBtn[i]);
	maskedBlitToSurface(&kanaScreen, 0, 0, kCharsW, kCharsH, kCharsX, kCharsY, kScreen);
	// clear the kanji navigation strip
	gfxCopy(72, 80, 480, 16, kParts, 80, 328, kScreen);
}

void drawKigouScreen() {
	for (uint i = 0; i < kigouBtn.size(); i++)
		drawButtonNormal(&kigouBtn[i]);
	maskedBlitToSurface(&kigouScreen, 0, 0, kCharsW, kCharsH, kCharsX, kCharsY, kScreen);
	// clear the kanji navigation strip
	gfxCopy(72, 80, 480, 16, kParts, 80, 328, kScreen);
}

void drawKanjiScreen(bool drawNav) {
	for (int i = 0; i < 12 && kanjiRow + i < kanjiNrLines; i++) {
		uint row = kanjiRow + i;
		for (uint j = 0; j < kanjiBtn[row].size(); j++) {
			Button *btn = &kanjiBtn[row][j];
			btn->pos.y = kCharsY + i * 24;
			drawButtonNormal(btn);
		}
	}
	maskedBlitToSurface(&kanjiScreen, 0, kanjiRow * 24, kCharsW, kCharsH, kCharsX, kCharsY, kScreen);
	if (drawNav) {
		for (int i = 0; i < 12; i++)
			drawButtonNormal(&kanjiNavBtn[i]);
	}
}

//
// Button creation
//

void makeCharButton(Button *btn, int x, int y, uint32 ch) {
	makeButton(btn, 0, 80, 24, 80, 24, 24, kCharsX + x, kCharsY + y, false, ch, charButtonPressed);
}

void makeKanaScreen() {
	kanaScreen.alloc(kCharsW, kCharsH);
	GfxSurface s;
	kanaScreen.surface(&s);
	for (uint i = 0; i < ARRAYSIZE(kKana); i++) {
		int x = 48 + (i % 3) * 144;
		int y = (i / 3) * 24;
		const char *line = kKana[i];
		for (int col = 0; col < 5 && *line; col++) {
			uint32 ch = decodeUtf8CodePoint(line);
			if (ch == 0x3007)
				continue;
			gfxTextDrawGlyphTo(&s, x + 2, y + 2, ch);
			Button btn;
			makeCharButton(&btn, x - kCharsX + 0, y, ch);
			btn.pos.x = x;
			btn.pos.y = y;
			kanaBtn.push_back(btn);
			x += 24;
		}
	}
}

void makeKigouScreen() {
	kigouScreen.alloc(kCharsW, kCharsH);
	GfxSurface s;
	kigouScreen.surface(&s);
	for (uint i = 0; i < ARRAYSIZE(kKigou); i++) {
		int x = 48;
		int y = i * 24;
		const char *line = kKigou[i];
		for (int col = 0; col < 18 && *line; col++) {
			uint32 ch = decodeUtf8CodePoint(line);
			gfxTextDrawGlyphTo(&s, x + 2, y + 2, ch);
			Button btn;
			makeCharButton(&btn, x, y, ch);
			kigouBtn.push_back(btn);
			x += 24;
		}
	}
}

void makeKanjiScreen() {
	// decode UTF-8 characters for each block
	uint32 blockChars[ARRAYSIZE(kKanji)][23 * 18];
	int blockNrChars[ARRAYSIZE(kKanji)];
	for (uint i = 0; i < ARRAYSIZE(kKanji); i++) {
		const char *s = kKanji[i].kanji;
		int j;
		for (j = 0; *s; j++)
			blockChars[i][j] = decodeUtf8CodePoint(s);
		blockChars[i][j] = 0;
		blockNrChars[i] = j;
	}

	// count the number of lines required
	int nrLines = 0;
	for (uint i = 0; i < ARRAYSIZE(kKanji); i++) {
		nrLines += blockNrChars[i] / 18;
		if (blockNrChars[i] % 18)
			nrLines++;
	}

	kanjiScreen.alloc(kCharsW, nrLines * 24);
	GfxSurface s;
	kanjiScreen.surface(&s);

	kanjiBtn.clear();
	int y = 0;
	for (uint i = 0; i < ARRAYSIZE(kKanji); i++) {
		// kana label
		const char *kanaS = kKanji[i].kana;
		uint32 kanaCh = decodeUtf8CodePoint(kanaS);
		gfxTextDrawGlyphTo(&s, 13, y + 2, kanaCh);
		// label chip from the parts surface
		maskedBlitFromSurface(kParts, 420, 48, 40, 24, &kanjiScreen, 4, y);

		// kanji
		int x = 48;
		Common::Array<Button> lineBtn;
		for (int j = 0; blockChars[i][j]; j++, x += 24) {
			if (j > 0 && j % 18 == 0) {
				// push the completed line and wrap to the next
				kanjiBtn.push_back(lineBtn);
				lineBtn.clear();
				x = 48;
				y += 24;
			}
			// draw glyph
			gfxTextDrawGlyphTo(&s, x + 2, y + 2, blockChars[i][j]);
			// create button
			Button btn;
			makeCharButton(&btn, x, y, blockChars[i][j]);
			lineBtn.push_back(btn);
		}
		if (!lineBtn.empty()) {
			kanjiBtn.push_back(lineBtn);
			y += 24;
		}
	}
	kanjiNrLines = nrLines;
}

//
// Peephole eye (namean.gpx anim surface)
//

const int kPeepDstX = 560, kPeepDstY = 96;
const int kPeepBgSrcX = 152, kPeepBgSrcY = 240, kPeepW = 56, kPeepH = 32;
const int kPeepMaskSrcX = 0, kPeepMaskSrcY = 256;
const int kPeepBgCenterX = 590, kPeepBgCenterY = 112;
const int kPeepMaskCenterX = 30, kPeepMaskCenterY = 272;
const int kPupilSrcX = 81, kPupilSrcY = 241, kPupilW = 14, kPupilH = 14;

Common::Point prevMouse;
int prevPupilX = kPeepBgCenterX, prevPupilY = kPeepBgCenterY;

void updateEye(const Common::Point &mouse) {
	if (mouse == prevMouse)
		return;
	prevMouse = mouse;

	// Walk from the eye center toward the mouse over the mask surface
	// and stop at the first opaque pixel.
	GfxSurface *anim = gfxGetSurface(kAnim);
	int x = kPeepBgCenterX, y = kPeepBgCenterY;
	int dx = ABS(mouse.x - x), dy = ABS(mouse.y - y);
	int sx = x < mouse.x ? 1 : -1;
	int sy = y < mouse.y ? 1 : -1;
	int err = (dx > dy ? dx : -dy) / 2;
	int pupilX = x, pupilY = y;
	for (;;) {
		int px = kPeepMaskCenterX + (kPeepBgCenterX - x);
		int py = kPeepMaskCenterY - (kPeepBgCenterY - y);
		if (px < 0 || px >= anim->w || py < 0 || py >= anim->h)
			break;
		byte c = anim->pixels[py * anim->pitch + px];
		if (c != kMaskColor) {
			break;
		}
		pupilX = x;
		pupilY = y;
		if (x == mouse.x && y == mouse.y)
			break;
		int e2 = err;
		if (e2 > -dx) {
			err -= dy;
			x += sx;
		}
		if (e2 < dy) {
			err += dx;
			y += sy;
		}
	}

	if (pupilX == prevPupilX && pupilY == prevPupilY)
		return;
	prevPupilX = pupilX;
	prevPupilY = pupilY;

	gfxCopy(kPeepBgSrcX, kPeepBgSrcY, kPeepW, kPeepH, kAnim, kPeepDstX, kPeepDstY, kScreen);
	gfxCopyMasked(kPupilSrcX, kPupilSrcY, kPupilW, kPupilH, kAnim,
			pupilX - 7, pupilY - 7, kScreen, kMaskColor);
	gfxCopyMasked(kPeepMaskSrcX, kPeepMaskSrcY, kPeepW, kPeepH, kAnim,
			kPeepDstX, kPeepDstY, kScreen, kMaskColor);
}

//
// Button behavior
//

TextInput *getTextInput() {
	return nameDown == 0 ? &myoujiInput : &namaeInput;
}

void kanaPressed(Button *) {
	mojiMode = kModeKana;
}

void kanjiPressed(Button *) {
	mojiMode = kModeKanji;
}

void kigouPressed(Button *) {
	mojiMode = kModeKigou;
}

void (*mojiModePressed[3])(Button *) = { kanaPressed, kanjiPressed, kigouPressed };

void mojiRadioPressed(Button *btn) {
	Button *prev = &mojiButtons[mojiDown];
	if (prev == btn)
		return;
	for (int i = 0; i < 3; i++) {
		if (&mojiButtons[i] == btn) {
			mojiDown = i;
			break;
		}
	}
	drawButtonNormal(prev);
	mojiModePressed[mojiDown](btn);
}

void myoujiPressed(Button *) {
	clearCursor(&namaeInput);
	nameDown = 0;
	drawCursor(&myoujiInput);
}

void namaePressed(Button *) {
	clearCursor(&myoujiInput);
	nameDown = 1;
	drawCursor(&namaeInput);
}

void (*nameModePressed[2])(Button *) = { myoujiPressed, namaePressed };

void nameRadioPressed(Button *btn) {
	Button *prev = &nameButtons[nameDown];
	if (prev == btn)
		return;
	for (int i = 0; i < 2; i++) {
		if (&nameButtons[i] == btn) {
			nameDown = i;
			break;
		}
	}
	nameModePressed[nameDown](btn);
}

void charButtonPressed(Button *btn) {
	TextInput *input = getTextInput();
	clearChar(input, input->cursor);
	drawChar(input, input->cursor, btn->data);
	input->chars[input->cursor] = btn->data;
	if (input->cursor == input->nrChars)
		input->nrChars++;
	if (input->cursor < 4)
		input->cursor++;
	drawCursor(input);
}

void leftArrowPressed(Button *) {
	TextInput *input = getTextInput();
	if (input->cursor > 0) {
		clearCursor(input);
		input->cursor--;
		drawCursor(input);
	}
}

void rightArrowPressed(Button *) {
	TextInput *input = getTextInput();
	if (input->cursor < input->nrChars) {
		clearCursor(input);
		input->cursor++;
		drawCursor(input);
	}
}

void modoruPressed(Button *) {
	TextInput *input = getTextInput();
	if (input->cursor == 0)
		return;
	clearChar(input, input->cursor - 1);
	int limit = input->nrChars + (input->cursor == input->nrChars ? 1 : 0);
	for (int i = input->cursor; i < limit; i++) {
		// shift the following characters left
		gfxCopy(input->pos.x + i * 24, input->pos.y, 24, 23, kScreen,
				input->pos.x + (i - 1) * 24, input->pos.y, kScreen);
		input->chars[i - 1] = input->chars[i];
	}
	input->cursor--;
	input->nrChars--;
	clearChar(input, input->nrChars + (input->cursor == input->nrChars ? 1 : 0));
}

void ketteiPressed(Button *) {
	kettei = true;
}

void navJumpPressed(Button *btn) {
	kanjiRow = btn->data;
	clearCharsScreen();
	drawKanjiScreen(false);
}

void navUpPressed(Button *) {
	if (kanjiRow == 0)
		return;
	kanjiRow--;
	clearCharsScreen();
	drawKanjiScreen(false);
}

void navDownPressed(Button *) {
	// don't go below the "wa" label
	if (kanjiRow + 2 >= kanjiNrLines)
		return;
	kanjiRow++;
	clearCharsScreen();
	drawKanjiScreen(false);
}

//
// Input handling
//

Button *pressedButton = nullptr;

bool hitTest(Button *btn, int x, int y) {
	return btnContains(btn->pos, x, y);
}

void updateButtons(const Common::Point &mouse) {
	if (pressedButton) {
		if (inputDown(kInputActivate))
			return;
		if (!pressedButton->hold)
			drawButtonNormal(pressedButton);
		pressedButton = nullptr;
	}
	if (!inputDown(kInputActivate))
		return;

	Button *hit = nullptr;
	// fixed buttons
	Button *fixed[9] = {
		&mojiButtons[0], &mojiButtons[1], &mojiButtons[2],
		&leftArrow, &rightArrow, &modoru, &ketteiButton,
		&nameButtons[0], &nameButtons[1]
	};
	for (int i = 0; i < 9 && !hit; i++) {
		if (hitTest(fixed[i], mouse.x, mouse.y))
			hit = fixed[i];
	}
	// kanji navigation
	if (!hit && mojiMode == kModeKanji) {
		for (int i = 0; i < 12 && !hit; i++) {
			if (hitTest(&kanjiNavBtn[i], mouse.x, mouse.y))
				hit = &kanjiNavBtn[i];
		}
	}
	// character buttons
	if (!hit) {
		if (mojiMode == kModeKana) {
			for (uint i = 0; i < kanaBtn.size() && !hit; i++) {
				if (hitTest(&kanaBtn[i], mouse.x, mouse.y))
					hit = &kanaBtn[i];
			}
		} else if (mojiMode == kModeKanji) {
			for (int i = 0; i < 12 && !hit; i++) {
				uint row = kanjiRow + i;
				if (row >= kanjiBtn.size())
					break;
				for (uint j = 0; j < kanjiBtn[row].size() && !hit; j++) {
					Button *btn = &kanjiBtn[row][j];
					btn->pos.y = kCharsY + i * 24;
					if (hitTest(btn, mouse.x, mouse.y))
						hit = btn;
				}
			}
		} else {
			for (uint i = 0; i < kigouBtn.size() && !hit; i++) {
				if (hitTest(&kigouBtn[i], mouse.x, mouse.y))
					hit = &kigouBtn[i];
			}
		}
	}

	if (hit) {
		drawButtonPressed(hit);
		pressedButton = hit;
		hit->pressed(hit);
	}
}

void nameScreenHandleInput() {
	uint mx, my;
	cursorGetPos(mx, my);
	Common::Point mouse(mx, my);
	if (!animStreamRunning(0))
		updateEye(mouse);
	updateButtons(mouse);
}

//
// Unicode -> Shift-JIS conversion (for the save data).
//

Common::HashMap<uint32, uint16> *sjisMap = nullptr;

uint16 unicodeToSjis(uint32 ch) {
	if (!sjisMap) {
		sjisMap = new Common::HashMap<uint32, uint16>();
		for (int hi = 0x81; hi <= 0x9F; hi++) {
			for (int lo = 0x40; lo <= 0xFC; lo++) {
				if (lo == 0x7F)
					continue;
				char buf[3] = { (char)hi, (char)lo, 0 };
				uint32 c = 0;
				sjisNextChar(buf, c);
				if (c && !sjisMap->contains(c))
					(*sjisMap)[c] = (hi << 8) | lo;
			}
		}
		for (int hi = 0xE0; hi <= 0xEF; hi++) {
			for (int lo = 0x40; lo <= 0xFC; lo++) {
				if (lo == 0x7F)
					continue;
				char buf[3] = { (char)hi, (char)lo, 0 };
				uint32 c = 0;
				sjisNextChar(buf, c);
				if (c && !sjisMap->contains(c))
					(*sjisMap)[c] = (hi << 8) | lo;
			}
		}
	}
	if (sjisMap->contains(ch))
		return (*sjisMap)[ch];
	error("Failed to convert character to SJIS: %x", ch);
}

} // namespace

void shuusakuNameInputScreen(byte *myouji, byte *namae) {
	kettei = false;
	mojiMode = kModeKana;
	mojiDown = 0;
	nameDown = 0;
	kanjiRow = 0;
	prevMouse = Common::Point(-1, -1);
	prevPupilX = kPeepBgCenterX;
	prevPupilY = kPeepBgCenterY;
	for (int i = 0; i < 5; i++) {
		myoujiInput.chars[i] = 0;
		namaeInput.chars[i] = 0;
	}
	myoujiInput.cursor = 0;
	myoujiInput.nrChars = 0;
	namaeInput.cursor = 0;
	namaeInput.nrChars = 0;

	// fixed buttons
	makeButton(&mojiButtons[0], 0, 24, 0, 0, 64, 24, 72, 368, true, 0, mojiRadioPressed, true);
	makeButton(&mojiButtons[1], 64, 24, 64, 0, 64, 24, 144, 368, true, 1, mojiRadioPressed, true);
	makeButton(&mojiButtons[2], 128, 24, 128, 0, 64, 24, 216, 368, true, 2, mojiRadioPressed, true);
	makeButton(&leftArrow, 192, 24, 192, 0, 64, 24, 288, 368, false, 0, leftArrowPressed);
	makeButton(&rightArrow, 256, 24, 256, 0, 64, 24, 360, 368, false, 0, rightArrowPressed);
	makeButton(&modoru, 320, 24, 320, 0, 64, 24, 432, 368, false, 0, modoruPressed);
	makeButton(&ketteiButton, 384, 24, 384, 0, 64, 24, 504, 368, false, 0, ketteiPressed);
	makeButton(&nameButtons[0], 448, 24, 448, 0, 64, 24, 416, 408, true, 0, nameRadioPressed, true);
	makeButton(&nameButtons[1], 512, 24, 512, 0, 64, 24, 488, 408, true, 1, nameRadioPressed, true);

	// kanji navigation buttons
	for (int i = 0; i < 10; i++)
		makeButton(&kanjiNavBtn[i], i * 32, 64, i * 32, 48, 32, 16, 80 + 36 * i, 328, false, kanjiNavJump[i], navJumpPressed);
	makeButton(&kanjiNavBtn[10], 320, 64, 320, 48, 48, 16, 460, 328, false, 0, navUpPressed);
	makeButton(&kanjiNavBtn[11], 368, 64, 368, 48, 48, 16, 512, 328, false, 0, navDownPressed);

	// character screens
	makeKanaScreen();
	makeKanjiScreen();
	makeKigouScreen();

	uint32 savedBg, savedFg;
	gfxTextGetColors(savedBg, savedFg);
	gfxTextSetColors(savedBg, 58);
	gfxTextSetSize(20, 0);
	g_textShadow = kTextShadowNone;

	drawKanaScreen();
	drawButtonPressed(&mojiButtons[mojiDown]);
	drawButtonPressed(&nameButtons[nameDown]);
	drawCursor(&myoujiInput);

	gfxTextSetColors(savedBg, savedFg);
	shuusakuCrossfade(g_mem->palette, false);

	animStart(0);
	cursorLoad(0, 1, nullptr);

	while (!kettei && !g_engine->shouldQuit()) {
		vmPeek();
		int oldMode = mojiMode;
		nameScreenHandleInput();
		if (mojiMode != oldMode) {
			clearCharsScreen();
			if (mojiMode == kModeKana)
				drawKanaScreen();
			else if (mojiMode == kModeKanji)
				drawKanjiScreen(true);
			else
				drawKigouScreen();
		}
		vmDelay(16);
	}

	cursorUnload();

	g_textShadow = kTextShadowB;
	gfxTextSetSize(16, 1);

	if (kettei) {
		for (int i = 0; i < myoujiInput.nrChars; i++) {
			uint16 c = unicodeToSjis(myoujiInput.chars[i]);
			myouji[i * 2] = c >> 8;
			myouji[i * 2 + 1] = c & 0xff;
		}
		myouji[myoujiInput.nrChars * 2] = 0xff;
		for (int i = 0; i < namaeInput.nrChars; i++) {
			uint16 c = unicodeToSjis(namaeInput.chars[i]);
			namae[i * 2] = c >> 8;
			namae[i * 2 + 1] = c & 0xff;
		}
		namae[namaeInput.nrChars * 2] = 0xff;
	}
}

} // namespace AI5
