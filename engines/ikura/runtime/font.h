#ifndef IKURA_RUNTIME_FONT_H
#define IKURA_RUNTIME_FONT_H

#include "graphics/font.h"

namespace Ikura::Runtime {

// Resolves a real scalable font for dialogue text (VileVN reference:
// Printer's font_ttf, opened via TTF_OpenFont(font_name, font_size) -
// see Presentation::drawText()'s comment). VileVN's own font_name comes
// from a path in vile.ini, a VileVN-side config file that doesn't apply
// to a from-scratch port; none of the games this engine has real
// pipelines for ship a font file of their own either (checked their
// extracted resources - none exist). This picks a real system-installed
// TTF instead (first match from a short list of conventional install
// paths across the platforms ScummVM targets), and falls back to
// ScummVM's own always-available built-in bitmap font - fixed size, no
// bold/italic - only if none is found or USE_FREETYPE2 isn't built in.
class Font {
public:
	// "Accurate" is a placeholder for a future per-game font source (a
	// real font some game ships, once one turns up) - behaves exactly
	// like kSystem today, since none exists yet for any game this
	// engine supports. Exists now so a future options-menu toggle has
	// a real thing to switch between, not a hypothetical API added
	// speculatively - see engines/ikura/README.md.
	enum class Mode { kSystem, kAccurate };

	static void setMode(Mode mode) { _mode = mode; }
	static Mode mode() { return _mode; }

	// size/bold/italic mirror what IOP_PB/IOP_SETFONTSTYLE store on
	// Presentation. Cached per (size, bold, italic) combination - real
	// TTF loading/rasterization at a given size isn't free, and dialogue
	// text re-requests the same combination every frame. Returns nullptr
	// only if even the built-in fallback is unavailable, which
	// shouldn't happen in practice (FontMan always has one).
	static const Graphics::Font *get(int size, bool bold, bool italic);

private:
	static Mode _mode;
};

} // End of namespace Ikura::Runtime

#endif
