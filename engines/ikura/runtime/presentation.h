#ifndef IKURA_RUNTIME_PRESENTATION_H
#define IKURA_RUNTIME_PRESENTATION_H

#include "common/archive.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/managed_surface.h"
#include "graphics/surface.h"

namespace Ikura::Runtime {

// Owns the Ikura/GDL graphic buffer array (VileVN reference: IkuraDisplay,
// src/ikura/idisplay.cpp) and drives it from the interpreter's IOP_GL/
// IOP_GP handlers directly - see script/interpreter.cpp. Buffer 0 is the
// visible screen by script convention (scripts blit into it to show
// anything); nothing here treats it specially.
class Presentation {
public:
	// Matches the reference's GBSIZE.
	static const int kBufferCount = 255;

	// See drawText()'s comment - an approximation of the reference's
	// default textview box (bottom of a 640x480 screen), not a value
	// pulled from any real game's .ini.
	static const int kTextWindowX = 40;
	static const int kTextWindowY = 360;
	static const int kTextWindowW = 560;
	static const int kTextWindowH = 100;

	// graphicsCabinet is not owned; must outlive this Presentation.
	explicit Presentation(Common::Archive *graphicsCabinet);
	~Presentation();

	// IOP_GL: decodes the named resource (see Format::Graphic::decodeImage)
	// and stores it at buffers[index], replacing whatever was there. No-op
	// if index is out of range, the cabinet doesn't have name, or it fails
	// to decode.
	void loadImage(int index, const Common::String &name);

	// IOP_PM cmd 0x11 (VileVN reference: IkuraDecoder::iop_pm's
	// "FW%3.3d" naming convention, w_textview->Blit). A small name-tag
	// graphic, not a character sprite - a prior comment here called this
	// "portrait", which was wrong; confirmed against real Snow Sakura
	// data (every one of 30,407 real IOP_PM calls carries this command,
	// with 55 distinct FWnnn.GG2 nameplate images in its GGD cabinet).
	// No-op if the cabinet doesn't have a matching FW%03d resource.
	void setNameplate(int index);

	// IOP_PM cmd 0x13/0x04 (VileVN reference: s_names.GetString ->
	// w_textview->PrintText/CompleteText - the reference literally
	// appends the name into the same text buffer the dialogue line
	// itself uses, with a newline before whatever prints after it.
	// Simplified here: kept as its own field instead, drawn next to the
	// nameplate image rather than spliced into currentText()'s
	// typewriter-reveal state - avoids the name's own instant-complete
	// interacting with the dialogue line's still-in-progress reveal
	// (they're different real mechanisms for showing "who's talking";
	// games use one or the other in practice - cmd 0x11's image is the
	// far more common one in real Snow Sakura data, see setNameplate()).
	void setNameText(const Common::String &name) { _nameText = name; }
	const Common::String &nameText() const { return _nameText; }

	// IOP_GP. cmd 0/20 basic blit, 1/21 alpha blend - both into dst.
	// cmd 7/15/16/17/19 (VileVN reference: its "vertical/horizontal/
	// move/fade" effect branches) turn out to blit/blend into buffer 0
	// specifically regardless of dst (the reference hardcodes 0 as the
	// blit target there - dst is repurposed as an animation *duration*
	// instead, per its own "Destination is duration" comment) - real,
	// script-observable end state, so ported the same way. cmd 7/19
	// ("fade") additionally start a real animated crossfade (see
	// beginFade()) using that duration - cmd 15/16/17 ("vertical/
	// horizontal/move") don't; the reference itself only ever calls
	// AddAnimation(new Fade(...)) from the cmd 7/19 branch, the others
	// are a bare instant blit with no animation object at all even
	// there. Any other cmd value is a no-op, matching the reference's
	// own "UNKNOWN/BAD GP OPCODE" error branches doing nothing but log.
	void copyGraphic(int cmd, int src, const Common::Rect &srcRect, int dst, const Common::Point &dstPos);

	// VileVN reference: AddAnimation(new Fade(...)) (IOP_GO, IOP_GP cmd
	// 7/19). Snapshots buffer 0's current content as the fade's start
	// point - call right *before* mutating buffer 0 to its final state,
	// so compositeScreen() has something to crossfade away from. No-op
	// (clears any prior fade) if buffer 0 isn't sized yet or durationMs
	// is 0 - nothing meaningful to animate either way, matches the
	// instant behavior those opcodes already had.
	void beginFade(uint32 durationMs);

	// VileVN reference: EventGameTick's "Skip state and fall back" ->
	// SkipAnimation(), called whenever a IS_WAITTIMER wait ends, however
	// it ends (deadline reached *or* skipped early by a player press) -
	// IkuraEngine::run() calls this exactly there. Ends an in-flight
	// fade immediately; harmless if none is active.
	void completeFade() { _fade.active = false; }

	// IOP_GV (VileVN reference: IkuraDecoder::iop_gv, its queued Slide
	// animations - src/widgets/slide.cpp). Not exercised by any real
	// script in this engine's test corpus (all 12 real occurrences are
	// currently unimplemented-opcode stops), ported from the reference's
	// literal behavior and unverified against real bytes: `count`
	// sequential cycles, each sliding the whole screen from (0,0) to
	// (offsetX,offsetY) over halfDurationMs, then snapping back to
	// (0,0) at the start of the next cycle (VileVN reference: each
	// queued Slide resets to its own rstart the instant it becomes
	// active - Slide::Copy's `if(!start && duration){ ...; Move(rstart);
	// }`) - a repeated slide-out-and-snap-back shake, not a smooth
	// back-and-forth oscillation. offsetX/offsetY are always
	// non-negative (the reference reads them as raw bytes, 0-255).
	void beginShake(int offsetX, int offsetY, int count, uint32 halfDurationMs);

	// Composites the current frame onto target: crossfades from the
	// pre-fade snapshot towards buffer 0's already-final content if
	// beginFade() started one and it hasn't finished yet (offset by
	// any in-progress beginShake() shake), otherwise just copies
	// buffer 0 as-is (still offset by an in-progress shake). Call once
	// per frame instead of copying buffer 0 directly - see
	// IkuraEngine::run().
	void compositeScreen(Graphics::Surface &target, uint32 nowMs) const;

	// nullptr if that buffer has never been loaded into.
	const Graphics::ManagedSurface *buffer(int index) const;

	// IOP_PM's cmd 0xFF/0x03 (VileVN reference: IkuraDecoder::iop_pm,
	// w_textview->PrintText/ClearText). Starts (or clears) the
	// typewriter reveal below - see revealedChars()'s comment.
	void showText(const Common::String &text);
	void clearText() { _currentText.clear(); _revealedChars = 0; _nameText.clear(); }
	const Common::String &currentText() const { return _currentText; }

	// IOP_PF (VileVN reference: IkuraDecoder::iop_pf, w_textview->
	// SetTextInterval/Printer::SetInterval). Milliseconds per revealed
	// character; 0 means "show instantly", same as the reference's
	// `if(interval){...} else {...}` branch in Printer::Copy. Default
	// matches the reference's own Printer constructor default (50).
	void setTextInterval(uint32 ms) { _textIntervalMs = ms; }
	uint32 textInterval() const { return _textIntervalMs; }

	// VileVN reference: Printer::printnext's per-character typewriter
	// reveal, driven every frame from Printer::Copy by wall-clock time
	// rather than a per-frame counter - computed lazily here from
	// g_system->getMillis() instead of needing a dedicated tick() call.
	uint32 revealedChars() const;
	bool isTextFullyRevealed() const { return revealedChars() >= _currentText.size(); }

	// VileVN reference: EventGameTick's IS_WAITTEXT branch, `keyok` ->
	// CompleteText(). The *first* advance press while text is still
	// typing jumps straight to fully revealed instead of resuming the
	// script - IkuraEngine::run() calls this instead of stepping the
	// interpreter when !isTextFullyRevealed(), matching the reference's
	// real two-press dialogue UX (press 1 completes typing, press 2
	// advances to the next line).
	void completeTextReveal() { _revealedChars = (uint32)_currentText.size(); }

	// IOP_GC (VileVN reference: IkuraDecoder::iop_gc, w_display->
	// FillSurface). Fills the buffer with a solid color, replacing
	// whatever was in it. No-op if index is out of range or the buffer
	// has never been sized by a prior IOP_GL/IOP_GP (there's no fixed
	// screen canvas yet to fill - see the `ponytail:` note in
	// copyGraphic()).
	void fillBuffer(int index, byte r, byte g, byte b);

	// IOP_PB/IOP_GPB (VileVN reference: w_textview->SetFontSize). Real
	// point size, honored by drawText() via Runtime::Font.
	void setFontSize(int size) { _fontSize = size; }
	int fontSize() const { return _fontSize; }

	// IOP_SETFONTSTYLE (VileVN reference: IkuraDecoder::iop_setfontstyle,
	// w_textview->SetFontStyle/SetFontShadow). Raw bold(0x01)/
	// italic(0x02)/shadow(0x04) bitmask. Bold/italic are honored by
	// drawText() via Runtime::Font (real face if the system font has
	// one, synthetic emboldening/slant otherwise - see font.cpp). The
	// reference's own SetFontShadow is a debug-log stub with no real
	// rendering behind it (src/widgets/printer.cpp), so the shadow bit
	// has nothing to faithfully port - not implemented. The reference's
	// iop_setfontstyle also has what reads as a copy-paste bug (its
	// italic branch OR's in TTF_STYLE_BOLD instead of TTF_STYLE_ITALIC,
	// so italic never actually renders in the original) - ported as
	// probably-intended (bold bit -> bold, italic bit -> italic) rather
	// than faithfully reproducing what looks like a typo, since the
	// ask here is the reference's rendering *style*, not its bugs.
	void setFontStyle(int style) { _fontStyle = style; }
	int fontStyle() const { return _fontStyle; }
	bool isFontBold() const { return (_fontStyle & 0x01) != 0; }
	bool isFontItalic() const { return (_fontStyle & 0x02) != 0; }

	// IOP_SETFONTCOLOR (VileVN reference: IkuraDecoder::iop_setfontcolor,
	// w_textview->SetFontColor). Real payloads are always the 5-byte
	// [window:1][font:1][r:1][g:1][b:1] shape - the reference's other
	// 14-byte decodeValue'd variant ("Found colors stored as DWORDs in Cat
	// Girl Alliance") isn't Snow Sakura's format, not implemented.
	void setFontColor(byte r, byte g, byte b) { _fontColorR = r; _fontColorG = g; _fontColorB = b; }
	byte fontColorR() const { return _fontColorR; }
	byte fontColorG() const { return _fontColorG; }
	byte fontColorB() const { return _fontColorB; }

	// IOP_CWC/IOP_WSH/IOP_WSS (VileVN reference: w_textview->SetVisible).
	// drawText() below no-ops while this is false, same as the reference
	// widget being hidden.
	void setTextVisible(bool visible) { _textVisible = visible; }
	bool isTextVisible() const { return _textVisible; }

	// IOP_IH (VileVN reference: IkuraDecoder::iop_ih, w_display->
	// SetSpot). Registers (or replaces) a rectangular clickable region
	// tagged index. No-op if index is negative, matching the reference.
	void defineHotspot(int index, const Common::Rect &area);

	// IOP_IG's hotspot hit-test (VileVN reference: w_display->
	// GetSelected, driven by MouseMove's hit-test over registered
	// spots, or the pixel-precise hitmap below if one's loaded). Returns
	// the index of whichever registered hotspot contains pos (or
	// whatever the hitmap's green channel says pos is over), or -1 if
	// none does.
	int hitTestHotspot(const Common::Point &pos) const;

	// IOP_IHGL (VileVN reference: IkuraDecoder::iop_ihgl, w_display->
	// SetMap). Loads a named image where each pixel's green channel
	// directly encodes a hotspot index (0xFF = no hotspot there) - a
	// pixel-precise alternative to the rectangular "spots" model above;
	// while one's loaded, hitTestHotspot() uses it exclusively instead
	// (VileVN reference: MouseMove's `if(hitmap){...}else{spots...}`).
	// Not exercised by any real script in this engine's test corpus,
	// ported from the reference's stated logic and unverified against
	// real bytes. No-op if the cabinet doesn't have a matching resource.
	void loadHitmap(const Common::String &name);

	// IOP_IHGC (VileVN reference: IkuraDecoder::iop_ihgc, w_display->
	// DropMap). Clears whatever loadHitmap() loaded, reverting
	// hitTestHotspot() to the rectangular "spots" model.
	void clearHitmap();

	// VileVN reference: Widgets::Textview's default position (a fixed box
	// near the bottom of the screen - exact per-game coordinates come from
	// each game's own .ini, not reverse-engineered here) and Printer::Copy
	// (fills a background box only if its alpha is nonzero, then blends a
	// word-wrapped, colored glyph surface on top). Draws directly onto
	// target - not into any numbered buffer - matching the reference's
	// separate widget-compositing pass over whatever the scene buffers
	// already drew (its own default background alpha is 0, i.e. no box;
	// the game's own textbox frame art comes from IOP_GL/IOP_GP into the
	// scene, same as here). No-ops if the window is hidden or there's no
	// current text.
	//
	// Draws only the currently-revealed prefix (see revealedChars()) -
	// real typewriter effect, not the full line at once. Uses
	// Runtime::Font::get(fontSize(), isFontBold(), isFontItalic()) - see
	// runtime/font.h for what "real" means there (a system TTF via
	// FreeType if one's found, else ScummVM's fixed-size built-in font).
	// Also draws the nameplate (see setNameplate()) just above the text
	// box, if one's loaded - the reference blits it unpositioned at the
	// textview widget's own (0,0), i.e. inside the same box as the text;
	// drawing it just above avoids it overlapping wrapped dialogue text,
	// same "real box position is approximated" caveat as kTextWindowX/Y.
	void drawText(Graphics::Surface &target) const;

private:
	Common::Archive *_cabinet;
	Graphics::ManagedSurface *_buffers[kBufferCount];
	Graphics::ManagedSurface *_nameplate = nullptr;
	Common::String _currentText;
	Common::String _nameText;
	uint32 _revealedChars = 0;
	uint32 _revealStartMs = 0;
	// VileVN reference: Printer's constructor default (interval=50).
	uint32 _textIntervalMs = 50;
	int _fontSize = 0;
	int _fontStyle = 0;
	// VileVN reference: Printer's constructor defaults (SetFontColor(0xFFFFFFFF)).
	byte _fontColorR = 255, _fontColorG = 255, _fontColorB = 255;
	bool _textVisible = true;
	Common::HashMap<int, Common::Rect> _hotspots;
	Graphics::ManagedSurface *_hitmap = nullptr;

	struct FadeTransition {
		Graphics::ManagedSurface from;
		uint32 startMs = 0;
		uint32 durationMs = 0;
		bool active = false;
	};
	FadeTransition _fade;

	struct ShakeTransition {
		int offsetX = 0, offsetY = 0;
		int count = 0;
		uint32 startMs = 0;
		uint32 halfDurationMs = 0;
		bool active = false;
	};
	ShakeTransition _shake;
};

} // End of namespace Ikura::Runtime

#endif
