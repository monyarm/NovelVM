#include "ikura/runtime/presentation.h"

#include "common/system.h"
#include "graphics/font.h"
#include "ikura/formats/graphic/graphic.h"
#include "ikura/runtime/font.h"

namespace Ikura::Runtime {

namespace {

// Real scripts sometimes name a graphic resource without the extension
// its cabinet member actually has (e.g. IOP_PM cmd 0x11's "FW%3.3d"
// nameplate pattern - confirmed against real Snow Sakura data: the
// script-side name is bare "FW001", the real GGD member is
// "FW001.GG2"). Same extension-fallback shape as Audio::resolveStream
// and interpreter.cpp's loadNamedScript: try the literal name first,
// then the same base name under every extension Format::Graphic::
// decodeImage knows how to dispatch on.
Graphics::ManagedSurface *decodeNamedImage(Common::Archive *cabinet, const Common::String &name) {
	if (!cabinet)
		return nullptr;

	static const char *kKnownExtensions[] = {
		".GGP", ".GGD", ".DRG",
		".GG0", ".GG1", ".GG2", ".GG3", ".GG4",
		".GG5", ".GG6", ".GG7", ".GG8", ".GG9",
	};

	Common::Path path(name);
	Common::String actualName = name;
	if (!cabinet->hasFile(path)) {
		bool found = false;
		for (const char *ext : kKnownExtensions) {
			Common::String candidate = name + ext;
			if (cabinet->hasFile(Common::Path(candidate))) {
				path = Common::Path(candidate);
				actualName = candidate;
				found = true;
				break;
			}
		}
		if (!found)
			return nullptr;
	}

	Common::SeekableReadStream *stream = cabinet->createReadStreamForMember(path);
	if (!stream)
		return nullptr;

	Graphics::Surface *decoded = Format::Graphic::decodeImage(actualName, *stream);
	delete stream;
	if (!decoded)
		return nullptr;

	Graphics::ManagedSurface *surface = new Graphics::ManagedSurface();
	surface->copyFrom(*decoded);
	decoded->free();
	delete decoded;
	return surface;
}

// Real bug caught by real data: ManagedSurface::blendBlitFrom only
// works on its own hardcoded pixel layout (Graphics::BlendBlit::
// getSupportedPixelFormat(), which is ABGR32's shift pattern despite
// blendBlitFrom's own warning calling it "RGBA32") - anything else
// (including every decoder in formats/graphic/, which all produce
// Graphics::PixelFormat::createFormatRGBA32()) silently no-ops instead
// of blending, with no error surfaced anywhere a script or test could
// see it short of inspecting actual pixel output. Caught by the first
// real test asserting on blended pixel colors, which is also the first
// time any test exercised IOP_GP's alpha-blend commands (1/21) at all.
// A format-agnostic per-pixel blend sidesteps it entirely rather than
// converting surfaces in and out of ABGR32 on every call, or changing
// every decoder's output format (which the real screen output, PNG
// dumps, and every existing RGBToColor call already depend on staying
// RGBA32).
// globalAlpha additionally scales every source pixel's alpha uniformly
// (default 255 = no change) - used for Presentation::compositeScreen's
// whole-frame crossfade, where the "source" advances from fully
// transparent to fully opaque over time regardless of its own per-pixel
// alpha.
void blendBlit(Graphics::Surface &dst, const Graphics::Surface &src, const Common::Rect &srcRect, const Common::Point &destPos, byte globalAlpha = 255) {
	for (int y = 0; y < srcRect.height(); y++) {
		int sy = srcRect.top + y;
		int dy = destPos.y + y;
		if (dy < 0 || dy >= dst.h)
			continue;
		for (int x = 0; x < srcRect.width(); x++) {
			int sx = srcRect.left + x;
			int dx = destPos.x + x;
			if (dx < 0 || dx >= dst.w)
				continue;

			byte sa, sr, sg, sb;
			src.format.colorToARGB(src.getPixel(sx, sy), sa, sr, sg, sb);
			sa = (byte)(sa * globalAlpha / 255);
			if (sa == 0)
				continue;
			if (sa == 255) {
				dst.setPixel(dx, dy, dst.format.ARGBToColor(255, sr, sg, sb));
				continue;
			}

			byte da, dr, dg, db;
			dst.format.colorToARGB(dst.getPixel(dx, dy), da, dr, dg, db);
			byte outR = (byte)((sr * sa + dr * (255 - sa)) / 255);
			byte outG = (byte)((sg * sa + dg * (255 - sa)) / 255);
			byte outB = (byte)((sb * sa + db * (255 - sa)) / 255);
			byte outA = (byte)(sa + da * (255 - sa) / 255);
			dst.setPixel(dx, dy, dst.format.ARGBToColor(outA, outR, outG, outB));
		}
	}
}

} // namespace

Presentation::Presentation(Common::Archive *graphicsCabinet) : _cabinet(graphicsCabinet) {
	for (int i = 0; i < kBufferCount; i++)
		_buffers[i] = nullptr;
}

Presentation::~Presentation() {
	for (int i = 0; i < kBufferCount; i++)
		delete _buffers[i];
	delete _nameplate;
	delete _hitmap;
}

void Presentation::loadImage(int index, const Common::String &name) {
	if (index < 0 || index >= kBufferCount)
		return;
	Graphics::ManagedSurface *decoded = decodeNamedImage(_cabinet, name);
	if (!decoded)
		return;
	delete _buffers[index];
	_buffers[index] = decoded;
}

void Presentation::defineHotspot(int index, const Common::Rect &area) {
	if (index < 0)
		return;
	_hotspots[index] = area;
}

int Presentation::hitTestHotspot(const Common::Point &pos) const {
	if (_hitmap) {
		if (pos.x < 0 || pos.y < 0 || pos.x >= _hitmap->w || pos.y >= _hitmap->h)
			return -1;
		byte a, r, g, b;
		_hitmap->format.colorToARGB(_hitmap->getPixel(pos.x, pos.y), a, r, g, b);
		return g != 0xFF ? g : -1;
	}

	for (Common::HashMap<int, Common::Rect>::iterator it = _hotspots.begin(); it != _hotspots.end(); ++it) {
		if (it->_value.contains(pos))
			return it->_key;
	}
	return -1;
}

void Presentation::loadHitmap(const Common::String &name) {
	Graphics::ManagedSurface *decoded = decodeNamedImage(_cabinet, name);
	if (!decoded)
		return;
	delete _hitmap;
	_hitmap = decoded;
}

void Presentation::clearHitmap() {
	delete _hitmap;
	_hitmap = nullptr;
}

void Presentation::setNameplate(int index) {
	Graphics::ManagedSurface *decoded = decodeNamedImage(_cabinet, Common::String::format("FW%03d", index));
	if (!decoded)
		return;
	delete _nameplate;
	_nameplate = decoded;
}

void Presentation::copyGraphic(int cmd, int src, const Common::Rect &srcRect, int dst, const Common::Point &dstPos) {
	// cmd 7/15/16/17/19 hardcode buffer 0 as the real target in the
	// reference (dst is an animation duration for these, not a buffer
	// index) - see this method's header comment.
	bool isEffectCmd = (cmd == 7 || cmd == 15 || cmd == 16 || cmd == 17 || cmd == 19);
	bool isFadeCmd = (cmd == 7 || cmd == 19);
	if (isFadeCmd)
		beginFade(dst > 0 ? (uint32)dst : 0);
	if (isEffectCmd)
		dst = 0;

	if (src < 0 || src >= kBufferCount || dst < 0 || dst >= kBufferCount || !_buffers[src])
		return;

	// A freshly-touched buffer needs real dimensions/format before any
	// blit can write into it. This engine has no fixed screen canvas yet
	// (see engines/ikura/README.md), so size it to exactly fit this blit.
	// ponytail: a later blit into the same buffer at a larger offset will
	// still need this to grow, which isn't handled - resize once a real
	// screen canvas exists and buffers stop being sized ad hoc.
	if (!_buffers[dst]) {
		_buffers[dst] = new Graphics::ManagedSurface();
		_buffers[dst]->create(dstPos.x + srcRect.width(), dstPos.y + srcRect.height(), _buffers[src]->format);
	}

	if (cmd == 0 || cmd == 20 || cmd == 15 || cmd == 16 || cmd == 17)
		_buffers[dst]->blitFrom(*_buffers[src], srcRect, dstPos);
	else if (cmd == 1 || cmd == 21 || cmd == 7 || cmd == 19)
		blendBlit(*_buffers[dst]->surfacePtr(), _buffers[src]->rawSurface(), srcRect, dstPos);
	// Any other cmd: unknown to the reference too, no-op there as well.
}

void Presentation::beginFade(uint32 durationMs) {
	if (!_buffers[0] || durationMs == 0) {
		_fade.active = false;
		return;
	}
	_fade.from.copyFrom(*_buffers[0]);
	_fade.startMs = g_system->getMillis();
	_fade.durationMs = durationMs;
	_fade.active = true;
}

void Presentation::beginShake(int offsetX, int offsetY, int count, uint32 halfDurationMs) {
	if (count <= 0 || halfDurationMs == 0) {
		_shake.active = false;
		return;
	}
	_shake.offsetX = offsetX;
	_shake.offsetY = offsetY;
	_shake.count = count;
	_shake.startMs = g_system->getMillis();
	_shake.halfDurationMs = halfDurationMs;
	_shake.active = true;
}

void Presentation::compositeScreen(Graphics::Surface &target, uint32 nowMs) const {
	if (!_buffers[0])
		return;

	Common::Point offset(0, 0);
	if (_shake.active) {
		uint32 elapsed = nowMs - _shake.startMs;
		int cycle = (int)(elapsed / _shake.halfDurationMs);
		if (cycle < _shake.count) {
			double progress = (double)(elapsed % _shake.halfDurationMs) / _shake.halfDurationMs;
			offset.x = (int)(_shake.offsetX * progress);
			offset.y = (int)(_shake.offsetY * progress);
		}
	}

	// Graphics::Surface::copyRectToSurface asserts the *entire* pasted
	// rect fits inside target (destX/destY >= 0 AND destX/destY + the
	// source rect's width/height <= target's) - it doesn't clip a
	// partially-out-of-range paste at all, unlike blitFrom/blendBlit.
	// Shifting a same-size background by a nonzero offset always pushes
	// part of it past the far edge, so crop the source rect instead of
	// moving the whole thing - shows only what still fits, exposing a
	// black margin at the near edge (filled below) where the shifted
	// image doesn't reach. Offsets are always non-negative (see
	// beginShake()'s comment), and also clamped here in case one ever
	// exceeds a real screen's size.
	offset.x = MIN((int)offset.x, MIN(target.w, _buffers[0]->w) - 1);
	offset.y = MIN((int)offset.y, MIN(target.h, _buffers[0]->h) - 1);
	offset.x = MAX((int)offset.x, 0);
	offset.y = MAX((int)offset.y, 0);

	if (offset.x != 0 || offset.y != 0)
		target.fillRect(Common::Rect(0, 0, target.w, target.h), target.format.RGBToColor(0, 0, 0));

	Common::Rect visible(0, 0, _buffers[0]->w - offset.x, _buffers[0]->h - offset.y);

	if (_fade.active && nowMs - _fade.startMs < _fade.durationMs) {
		byte alpha = (byte)(255 * (nowMs - _fade.startMs) / _fade.durationMs);
		target.copyRectToSurface(_fade.from.rawSurface(), offset.x, offset.y,
			Common::Rect(0, 0, MIN(visible.width(), _fade.from.w), MIN(visible.height(), _fade.from.h)));
		blendBlit(target, _buffers[0]->rawSurface(), visible, offset, alpha);
		return;
	}

	target.copyRectToSurface(_buffers[0]->rawSurface(), offset.x, offset.y, visible);
}

const Graphics::ManagedSurface *Presentation::buffer(int index) const {
	if (index < 0 || index >= kBufferCount)
		return nullptr;
	return _buffers[index];
}

void Presentation::fillBuffer(int index, byte r, byte g, byte b) {
	if (index < 0 || index >= kBufferCount || !_buffers[index])
		return;
	_buffers[index]->fillRect(Common::Rect(0, 0, _buffers[index]->w, _buffers[index]->h),
		_buffers[index]->format.RGBToColor(r, g, b));
}

void Presentation::showText(const Common::String &text) {
	_currentText = text;
	_revealedChars = 0;
	_revealStartMs = g_system->getMillis();
}

uint32 Presentation::revealedChars() const {
	uint32 fromElapsed;
	if (_textIntervalMs == 0) {
		fromElapsed = (uint32)_currentText.size();
	} else {
		uint32 elapsed = g_system->getMillis() - _revealStartMs;
		fromElapsed = elapsed / _textIntervalMs;
	}
	// _revealedChars only ever moves forward manually via
	// completeTextReveal() (an instant floor, not a substitute for the
	// elapsed-time computation above - the two are combined with MAX so
	// a completed reveal stays complete even across a showText() call
	// that didn't reset it).
	return MIN((uint32)_currentText.size(), MAX(_revealedChars, fromElapsed));
}

void Presentation::drawText(Graphics::Surface &target) const {
	if (!_textVisible)
		return;

	if (_nameplate) {
		int y = kTextWindowY - _nameplate->h - 4;
		if (y < 0)
			y = 0;
		blendBlit(target, _nameplate->rawSurface(), Common::Rect(0, 0, _nameplate->w, _nameplate->h), Common::Point(kTextWindowX, y));
	}

	if (!_nameText.empty()) {
		if (const Graphics::Font *nameFont = Font::get(_fontSize, isFontBold(), isFontItalic())) {
			int x = kTextWindowX + (_nameplate ? _nameplate->w + 8 : 0);
			int y = kTextWindowY - nameFont->getFontHeight() - 4;
			if (y < 0)
				y = 0;
			nameFont->drawString(&target, _nameText, x, y, kTextWindowW, target.format.RGBToColor(_fontColorR, _fontColorG, _fontColorB));
		}
	}

	if (_currentText.empty())
		return;

	const Graphics::Font *font = Font::get(_fontSize, isFontBold(), isFontItalic());
	if (!font)
		return;

	Common::Array<Common::String> lines;
	font->wordWrapText(_currentText, kTextWindowW, lines);

	// Approximation: wraps the full line for stable layout, then cuts
	// off drawing once revealedChars() characters have been consumed
	// across the wrapped lines - word-wrap drops the whitespace it
	// wraps on, so this can be off by a character or two right at a
	// wrap boundary versus the reference's own char-by-char wrap-as-
	// you-type. Not worth chasing exactly; the fully-revealed result is
	// identical either way.
	uint32 remaining = revealedChars();
	uint32 color = target.format.RGBToColor(_fontColorR, _fontColorG, _fontColorB);
	int y = kTextWindowY;
	for (uint i = 0; i < lines.size() && y + font->getFontHeight() <= kTextWindowY + kTextWindowH; i++) {
		if (remaining == 0)
			break;
		Common::String visible = lines[i];
		if (remaining < visible.size()) {
			visible = visible.substr(0, remaining);
			remaining = 0;
		} else {
			remaining -= visible.size();
		}
		font->drawString(&target, visible, kTextWindowX, y, kTextWindowW, color);
		y += font->getFontHeight();
	}
}

} // End of namespace Ikura::Runtime
