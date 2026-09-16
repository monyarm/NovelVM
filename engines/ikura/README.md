# engines/ikura (planned)

Native NovelVM implementation of the Ikura/GDL visual novel interpreter,
superseding the `ikura` family of the abandoned GPLv3 VileVN project
(SourceForge `vilevn`, https://sourceforge.net/projects/vilevn/). Tracked
under issue #4. Full provenance, license inventory, and the decision to
split VileVN into six separate engines instead of one `engines/vilevn` are
recorded in `docs/vilevn-audit/PROVENANCE.md` — read that first.

Cabinet archive parsing (both the `SM2MPX10` and headerless/DRS variants),
ISF/DRS script container parsing, the opcode interpreter, input translation,
and a real load-and-run pipeline exist and are under test. See "Known
compatibility gaps" below for what's still missing.

## Scope

Ikura/GDL is the one part of VileVN with a genuinely shared bytecode VM
(`src/ikura/ikuradecoder.cpp`, `iparser.cpp`, `iscript.cpp`, `iopcodes.h` in
the VileVN reference tree), plus a DRS-format variant
(`src/ikura/drsdecoder.*`, "Digital Romance System", the predecessor format
Ikura/GDL evolved from and still overrides). It drives 8 games:

catgirl, crescendo, hitomi, idols, kana, kanaoka, sagara, snow — see
`tests/compatibility-matrix.md` for the per-game detail, and "Per-game
status" below for what's actually been tested against real data so far.

## Per-game status

| Game | Data sourced | Cabinet opens | Script parses/runs | Images decode |
|---|---|---|---|---|
| snow (Snow Sakura) | ✅ | ✅ `SM2MPX10` (ISF/GGD/data) | ✅ 133/133 real scripts | ✅ GGA 58/58, GGD8 10/10 |
| kanaoka (Kana Okaeri) | ✅ (3 discs) | ✅ `SM2MPX10` (ISF/GGD/GGDX/DATA) | ❌ known gap - reference's own revision check rejects it | ✅ GGP 80/80 |
| kana (Kana Imouto) | ✅ | ✅ headerless/DRS (DrsGRP/DrsSNR) | ❌ known gap - DRS parser unfinished in the reference itself | ✅ GGD24 60/60, hand-verified vs reference too |
| catgirl (Cat Girl Alliance) | ❌ no seeders found | — | — | — |
| crescendo | ❌ no source found | — | — | — |
| hitomi (Hitomi -My Stepsister-) | ❌ no seeders found | — | — | — |
| idols (Idols Galore) | ❌ no seeders found | — | — | — |
| sagara (Sagara Family) | ❌ only a "Remastered" release exists with seeders, presumed a different (non-ikura) engine | — | — | — |

"Script parses/runs" only covers `Script::load()`/`getOpcode()` succeeding
on real bytecode, not full playthroughs - see "Runtime: presentation"
below for how far real execution actually gets once a script does parse.
The two ❌ script-parsing rows are detailed in "Known compatibility gaps".

## Provenance baseline

Source: `git clone https://git.code.sf.net/p/vilevn/vilevn`, commit
`f6e48d6b2419574e3d3cb92d7b819a5429786786` (2012-08-07). License: GPLv3,
copyright "ViLE Team" — see `LICENSES/README.md` for the full inventory.

## Real-data validation

Tested against real cabinet/resource files pulled from three real games
(Snow Sakura, Kana Okaeri, Kana Imouto), covering both cabinet formats and
every image sub-format in the family:

| Format | Decoder | Real files tested | Result |
|---|---|---|---|
| `SM2MPX10` cabinet | `Cabinet::parseSM2MPX10` | Snow Sakura + Kana Okaeri, all cabinets | all open correctly |
| Headerless/DRS cabinet | `Cabinet::parseHeaderless` | Kana Imouto DrsGRP/DrsSNR | all open correctly |
| ISF script | `Script::loadISF` | Snow Sakura, 133 scripts | 133/133 parse and step cleanly |
| GGP (PNG-wrapped) image | `decodeGGP` | Kana Okaeri, 80 images | 80/80 decode |
| GGA (32-bit alpha) image | `decodeGGA` | Snow Sakura, 58 images | 58/58 decode |
| GGD8 (8-bit palette) image | `decodeGGD8` | Snow Sakura, 10 images | 10/10 decode |
| GGD24 (24-bit) image | `decodeGGD24` | Kana Imouto, 60 images | 60/60 decode; also hand-verified byte-for-byte against the reference's `CIkura::ggd_24b` |
| Audio (Vorbis/WAV) | `Format::Audio::decodeAudioStream` | Snow Sakura, real music track | decodes to correct PCM (44.1kHz stereo, exact requested duration); confirmed by ear, not just assertions |

GAN (animation) has no real sample in any of the three games tested;
verification there is still synthetic-fixture-only.

## Runtime: presentation (Task 5, in progress)

**Implemented**: `IOP_GL` (load a named image resource into one of 255
graphic buffers), `IOP_GP` basic blit and alpha-blend (cmd 0/20 and
1/21 - buffer 0 is the screen by convention; cmd 7/15/16/17/19's
real end-state is implemented too, see further down). Also implemented in
the interpreter because real scripts can't reach a single display
opcode without them: `IOP_CALC` (expression evaluation), `IOP_TIMERSET`/
`IOP_TIMERGET` (a stopwatch), `IOP_STX` (jump on a system flag or engine
condition), and a run of confirmed reference no-ops - `IOP_SVF`,
`IOP_PPF`, `IOP_IM`, `IOP_WO`, `IOP_WC`, `IOP_KIDCLR`, `IOP_KIDSCAN`,
`IOP_MF` - all empty case blocks or debug-only logs in the reference
itself, not shortcuts taken here.

**Real-data status**: all 133 Snow Sakura scripts run past their
startup sequence; `GL` fires 62 times and `GP` fires 4 times across the
corpus, leaving real decoded images sitting in graphic buffers.

**Not implemented**: the internal graphic buffer array still sizes
itself to whatever gets blitted into it rather than a fixed game
resolution (see the `ponytail:` note in `Presentation::copyGraphic`) -
separate from the real screen output added later (below), which *is*
a fixed 640x480 now.

Also implemented since: `IOP_GC` (fill a buffer with a solid color -
`Presentation::fillBuffer`), `IOP_PB` (font size - stored, nothing reads
it yet), and `IOP_GGE` (its own reference comment says "No support for
effect files (yet)" - it reads a filename plus type/dir/len fields
shaped like "load a named grayscale mask to drive a custom wipe", but
the reference never implements that and just falls back to a bare
full-buffer blit onto the screen; this does exactly that fallback via
the same `copyGraphic()` `IOP_GP` uses, no new blit logic needed).
Implementing real effect-file support would be an improvement *over*
the reference, not a port of it - worth revisiting once the engine
matches the reference everywhere else.

**Real bug caught by real data**: `IOP_STS`'s payload didn't match
between the reference's decompiled source (`[dst:1][val:1]`, 2 bytes)
and every real call in Snow Sakura's scripts (`[dst:4][val:4]`, 8
bytes, both `decodeValue`'d operands) - confirmed by two real calls
sharing identical bytes at offset 0-3 but differing at offset 4, which
only makes sense if offset 4 is `val`. Went with what real data
actually contains. Caveat: Snow Sakura is currently the *only* game
with a working ISF pipeline (see "Known compatibility gaps"), so this
is single-game evidence - it might be this build's convention rather
than universal across the ikura family, not yet cross-checked against
a second game.

**Real bug caught by re-reading the reference**: `IOP_GV` (screen shake)
was previously dismissed here as "no script-observable state at all" -
wrong. The reference's real source (`src/widgets/slide.cpp`, found
later while implementing other animation opcodes) shows it queues
`count` real `Slide` animations, each linearly moving the whole screen
from (0,0) to (x,y) over `duration/2`, snapping back to (0,0) the
instant the next one activates - a repeated slide-out-and-snap-back
shake. Implemented for real now (`Presentation::beginShake`,
`compositeScreen`), reusing the same real-time compositing pass the
fade transitions below use. Not exercised by any real script in this
engine's test corpus (all real occurrences are still-unimplemented
stops as of the last probe), so ported straight from the reference's
literal behavior and flagged unverified against real bytes -
confirmed only by a synthetic screenshot, not real game data.

Also implemented since: `IOP_GO` (graphic fade-out), which needed the
third kind of pause flagged above - a real countdown deadline
(reference: `timerend`/`IS_WAITTIMER`, checked against wall-clock time)
rather than an input signal, except the reference's own `EventGameTick`
lets a player-advance signal cut the wait short too, same escape hatch
as `IOP_PM`/`IOP_CLK`/`IOP_TIMEREND`. `StepResult` gained a third value,
`kWaitingForTimer`, and `IkuraEngine`'s single `bool _waitingForAdvance`
became a three-way `PauseState` enum so the main loop can gate on
"deadline passed OR advance pressed" instead of just "advance pressed".
The fade animation itself is skipped (same reasoning as `IOP_GGE`'s wipe
- no render pipeline to drive it), but the fill-then-wait is real:
`Presentation::fillBuffer(0, r, g, b)` runs, then the wait. `IOP_ATIMES`
(delay-value setter) is also implemented - stores the value via
`Context::setDelayValue()` for `IOP_AWAIT` to read later, but `IOP_AWAIT`
itself isn't implemented yet, so nothing consumes it yet. `IOP_PCML`
(PCM/voice playback) plays a named clip once on its own single channel,
same shape as `IOP_ML`'s music load but non-looping and through a new
"voice" cabinet (`Runtime::Audio` gained `playVoice()`/`isVoicePlaying()`
and a fourth optional cabinet parameter). `IOP_IHGC` turned out to be
"close hitmap" (`w_display->DropMap()` in the reference) - a mouse-hover
hotspot system for clickable regions we haven't built, so like `IOP_IC`
it's a no-op for now (grouped in the same shared no-op case).

Also implemented since: `IOP_LS`/`IOP_LSBS`/`IOP_SRET` (switch the
running script to a different named one, or call it as a subscript and
return - `Context` gained a script stack for this: `jumpScript()`
replaces the current frame, `callScript()`/`returnScript()` push/pop
one). Required a small refactor: `loadTitleScript()` used to open the
"isf" cabinet and discard it once `TITLE.ISF` was extracted, but
`IOP_LS`/`IOP_LSBS` need to look up *other* named scripts from that same
cabinet at any point during the run, so `IkuraEngine` now keeps it alive
for the whole session (same lifetime as the graphics/music/SE cabinets)
and `Context` holds a reference to it. Script names in real payloads
don't include the `.ISF` extension the cabinet's members actually have
(reference: `LoadScript(Data, "ISF")` appends it), so lookup tries the
literal name first and falls back to name+`.ISF`, the same pattern
`Audio::resolveStream` already established for music/SE. Also
implemented: `IOP_IC` (no-op - reference's shared empty case block),
`IOP_IGRELEASE` (clears a pending advance/cancel signal without
reporting it - maps directly onto `Input::consumeAdvance/consumeCancel`),
`IOP_CWC` (hides the text window - `Presentation::setTextVisible`).

**Real-data status**: across all 133 real scripts, resolves 37,689
pause/yield points now (up from 9,568 two passes ago) with zero hangs.
`IOP_LS`/`IOP_LSBS` never fired in this sample - likely only the
title/main scripts chain scenes that way, not individual event scripts -
but the one real bare `IOP_SRET` (no active call) was handled exactly as
designed, reporting the same "nothing left to run" result as falling
off the end of a script.

Also implemented since: `IOP_AWAIT`, the real consumer of `IOP_ATIMES`'s
stored delay value - reuses the same `kWaitingForTimer` pause `IOP_GO`
introduced, just without a graphic side effect. Its reference handler
reads no payload fields at all, only the value `IOP_ATIMES` stored
earlier, so this doesn't validate a length either.

Voice playback confirmed by ear the same way music was: decoded a real
Snow Sakura voice clip (`FEM0011.OGG`, the "voice" cabinet, mono 22050Hz)
through `Format::Audio::decodeAudioStream` and played the dump through
mpv. Also implemented `IOP_PCMS` (stops the voice channel - direct
companion to `IOP_PCML`, same shape as `IOP_SET`/`IOP_SES` for SE).

**Real bug caught by real data**: `IOP_GO`'s payload didn't match
between the reference's decompiled source (`[delay:4][r:1][g:1][b:1]`,
7 bytes, never length-checked) and every real call across 3 real scripts
(TITLE.ISF, EVC_007.ISF, EVT_011.ISF - 38 calls total) - all 8 bytes,
with a trailing unused pad byte the reference just never reads. Same
shape as `IOP_GC`'s unused `par2`-`par4` bytes. Fixed the length check
(7 -> 8) and regenerated the one dependent test fixture.

**Real-data status**: ran the proper multi-pass probe (auto-resolving
every pause, same methodology as the 37,689-point stat above, just with
a much larger per-script step budget so long scripts actually finish
instead of hitting a shallow budget wall) - resolves **395,914**
pause/yield points across the same 133 real scripts, with only 15 real
stops left in the entire corpus, zero of them `kMalformedOpcode`.

Implemented since: `IOP_SETFONTSTYLE` (bold/italic/shadow bitmask,
window 0 only - `Presentation::setFontStyle`, same shape as `IOP_PB`'s
`setFontSize`) and `IOP_SETFONTCOLOR` (real payload confirmed as the
reference's 5-byte `[window:1][font:1][r:1][g:1][b:1]` variant, not its
other 14-byte decodeValue'd "Cat Girl Alliance" variant - window 0/font
0 only, `Presentation::setFontColor`). `IOP_IHK`/`IOP_IHKDEF` (keyboard
remap config) and `IOP_IHGL` (hitmap image load) are no-ops, same
reasoning as `IOP_IHGC` - the reference's own handlers do real work
(`SetKeyMap`, `SetMap`), but nothing in this engine has a keymap-driven
input system or hit-test system to be the consumer, and there's no
near-term plan to build either just to store inert data.

Of the 15 remaining real stops, all 13 non-`IOP_GV` ones are `IOP_IXY`
(mouse position) and `IOP_EXA` (extra flag/var space) - neither is
implemented in the *reference* either (both fall through its own
dispatch to `iop_unknown`, `IOP_EXA` is even commented out of its
shared no-op case block), so these aren't gaps on our side to close,
just genuinely unsupported opcodes in this build's ISF revision.

**Also implemented since: real on-screen output.** Every decoder and
opcode above only ever touched an in-memory `Graphics::ManagedSurface` -
nothing reached an actual visible screen, the same gap audio had before
the mpv proof. `IkuraEngine::run()` now requests an RGBA32 screen
(`initGraphics(640, 480, &format)` - matches what every decoder in
`formats/graphic/` already produces, so no format-conversion step is
needed) and blits `Presentation::buffer(0)` onto it every frame via
`g_system->lockScreen()`/`Surface::copyRectToSurface()`/
`g_system->unlockScreen()`, reusing the same screen surface ScummVM
already manages rather than any new blit path. Verified with a real
decode: loaded Snow Sakura's `BG01A.GG0` background through
`Presentation::loadImage`/`copyGraphic` into buffer 0 (the exact path
the frame blit reads from) and dumped it to PNG via `Image::writePNG` -
a correct, undistorted 640x480 room background, confirmed by eye.

**Also implemented since: real font rendering.** See the "dialogue
text" section below for the details - `Presentation::drawText()`
composites onto the real screen the same way `IkuraEngine::run()`
already composites buffer 0 onto it: as a separate step, in the same
frame, after the background blit - matching the reference's own
widget-compositing order (`Printer::Copy` blends its glyph surface over
whatever the scene already drew).

(`IOP_PM`'s voice/nameplate/character-name commands and `IOP_IG`'s
hotspot-selection branch were the next blockers here as of the last
edit - both are real now, see the "presentation" section above.)

## Runtime: dialogue text and the input-wait state machine (Task 5, in progress)

**Implemented**: `IOP_PM`, the opcode that prints a line of dialogue.
Its payload is a small bytecode of its own (nameplate image, voice,
character name, font color, "previously read" tracking, plain text) -
clear (cmd `0x03`), print (cmd `0xFF`), voice playback, nameplate
image/name, and inline font color all do something real now (see below
for the details on each); "previously read" line tracking is still
parsed and skipped only. Also implemented: `IOP_CLK` and `IOP_TIMEREND`
(both unconditional pauses - `TIMEREND`'s reference dispatch has an
empty case body that leaves its "still waiting" default untouched, so
it behaves the same as `CLK` even though that looks like it may be an
oversight in the original), and `IOP_IG` (mouse/click polling - writes
a hotspot index and an event flag into two script variables; the
hotspot hit-test is real now too, see below).

`StepResult` gained two pause values instead of one, because the
opcodes above pause for two genuinely different reasons needing two
different resume policies: `kWaitingForAdvance` (`PM`/`CLK`/`TIMEREND`)
truly blocks until the *caller* observes and consumes a real advance
signal - calling `run()` again unconditionally would race straight past
the pause. `kYielded` (`IG`, when it finds nothing new) is different:
the *script's own bytecode* re-checks via a jump-back loop, so the
caller just calls `run()` again next frame unconditionally, and `IG`
consumes whatever's fresh in `Input` itself when the script loops back
into it. Conflating these two into one value was tried and caught by
hand-tracing the resume flow before it shipped - a single "waiting"
value would have made the outer loop consume the advance signal before
`IG` ever saw it.

No VM-side state was needed to support any of this - a script's
position tracking already remembers exactly where to resume, so pausing
and resuming is just calling `run()` again. `IkuraEngine::run()` keeps
a script alive across a wait, gating on `Input` correctly for each of
the two pause kinds.

**Real-data status**: driving all 133 real Snow Sakura scripts through
this pause/resume loop (simulating a player mashing advance every
frame, up to 200 simulated frames per script) resolves 9,568 real
pause/yield points across the corpus with zero hangs or crashes -
including real story text extracted through the full real pipeline
(`Cabinet` -> `Script` -> interpreter -> `Presentation::currentText()`),
e.g. *"I guess I'm not as dumb as I thought."* and *"Rustle rustle."*

**Real font rendering for dialogue text.**
`Presentation::drawText(Graphics::Surface &target)` word-wraps
`currentText()` (via `Graphics::Font::wordWrapText`) and draws it at a
fixed position near the bottom of the screen (`kTextWindowX/Y/W/H` - an
approximation of the reference's default textview box; real per-game
coordinates live in each game's own `.ini`, not reverse-engineered
here). Draws directly onto whatever surface it's given - not into any
numbered buffer - matching the reference's `Printer::Copy` compositing
its glyph surface over the scene as a separate pass, not baking text
into the background image data. No-ops while the window is hidden
(`IOP_CWC`) or there's no current text, matching the reference widget
being invisible/empty. Font color defaults to white (`Presentation`'s
`_fontColorR/G/B` match the reference `Printer` constructor's
`SetFontColor(0xFFFFFFFF)`) and honors `IOP_SETFONTCOLOR` for real. No
background box - the reference's own `Printer::back_alpha` defaults to
fully transparent too, since a game's real textbox frame art comes from
`IOP_GL`/`IOP_GP` into the scene, same as everything else `Presentation`
already draws.

**Real per-game-agnostic font loading** (`runtime/font.{h,cpp}`,
`USE_FREETYPE2`). None of this engine's real games ship a font file of
their own (checked their extracted resources), and the reference's real
answer - a path in VileVN's own `vile.ini` - doesn't apply to a
from-scratch port, so `Runtime::Font::get(size, bold, italic)` resolves
a real system-installed TTF instead: first match from a short list of
conventional install paths across the platforms ScummVM targets
(DejaVu/Liberation on Linux, `arial.ttf` on Windows, Helvetica/Arial on
macOS), loaded via `Graphics::findTTFace` at the requested size, with
real bold/italic honored - either a genuine separate face if the system
has one, or FreeType's synthetic emboldening/slant (`findTTFace`'s own
`_fakeBold`/`_fakeItalic`) when it doesn't, both for free from the same
call. Falls back to ScummVM's built-in fixed-size `FontMan` font if
`USE_FREETYPE2` isn't built in or no system font is found - never a hard
requirement. `Font::Mode` (`kSystem`/`kAccurate`) exists as a real
switch point for a future options-menu setting, even though both modes
currently resolve the same way (no "accurate" per-game font exists yet
for any supported game) - not built speculatively, just not blocking a
menu that gets added later.

**`IOP_PB`/`IOP_SETFONTSTYLE` are now honored for real** through
`Runtime::Font`, closing the gap the previous round left open (both were
stored but unusable against a fixed-size, non-styleable fallback font).
The reference's own `iop_setfontstyle` reads as having a copy-paste bug
(its italic branch ORs in `TTF_STYLE_BOLD` instead of `TTF_STYLE_ITALIC`,
so italic silently never renders in the original) - ported as bold bit
-> bold, italic bit -> italic instead of reproducing what looks like a
typo, since the ask here is the reference's rendering *style*, not
faithfully copying an evident mistake. Its shadow bit doesn't have a
real behavior to port either way - the reference's own
`SetFontShadow` is a debug-log stub with nothing behind it.

**A real typewriter reveal** (`IOP_PF`, `Presentation::revealedChars()`/
`completeTextReveal()`). `showText()` starts a per-character reveal
computed lazily from wall-clock elapsed time (no per-frame tick call
needed) at `textInterval()` milliseconds/char - default 50, matching the
reference `Printer` constructor's own default - and `drawText()` now
draws only the revealed prefix. `IOP_PF` sets the interval for real
(previously unimplemented). Matches the reference's real two-press
dialogue UX: `IkuraEngine::run()`'s `kWaitingForAdvance` gating now
checks `isTextFullyRevealed()` first - the *first* advance press while
still typing calls `completeTextReveal()` instead of resuming the
script (VileVN reference: `EventGameTick`'s `IS_WAITTEXT` branch,
`keyok` -> `CompleteText()`), and only a press once the line is fully
shown actually advances.

**`IOP_PM`'s voice and nameplate/name commands are real now.**
Cmd `0x13`'s voice-filename branch plays it via `Audio::playVoice()`
(previously parsed and dropped). Cmd `0x11` turned out to be a
character-name **nameplate image** ("FW%3.3d" naming,
`w_textview->Blit`), not "portrait rendering" as a much earlier comment
here claimed - real Snow Sakura data confirms it: every single one of
30,407 real `IOP_PM` calls carries this command, against 55 distinct
`FWnnn.GG2` images in its GGD cabinet. `Presentation::setNameplate()`
loads and composites it (needed extension-fallback resolution the same
shape as `Audio::resolveStream`, since the script names it bare
("FW001") but the real cabinet member has an extension - now shared by
`loadImage()` too). `IOP_CNS` (a new opcode: registers a character name
into a small index->name table on `Context`) and cmd `0x13`'s
string-table name-lookup branch (`Presentation::setNameText()`) are also
implemented - simplified from the reference, which literally appends
the looked-up name into the *same* text buffer the dialogue line uses;
kept as a separate field instead and drawn next to the nameplate image,
avoiding a real interaction bug that append would have caused (the
name's own instant-complete colliding with the dialogue line's
still-in-progress typewriter reveal). Low real value either way - the
image nameplate is the dominant mechanism in real data, this path exists
for completeness.

**Real bug caught by real data: `IOP_IM`/`IOP_IH` were swapped.** An
earlier round assigned opcode `0x84` to `IOP_IM` ("reading mouse cursor
data", a debug-log-only stub in the reference) - but `iopcodes.h`
actually puts `IOP_IM` at `0x80`; `0x84` is `IOP_IH`, a real, different
opcode ("defines screen areas") that had been silently landing in the
no-op bucket this whole time. Investigating `IOP_IG`'s hotspot-selection
branch (previously unimplemented, always took the "ordinary clicks"
path) surfaced this - `IOP_IH` is exactly what registers the hotspot
rectangles `IOP_IG` is supposed to hit-test against. Both are fixed now:
`IOP_IM` correctly no-ops at `0x80`; `IOP_IH` at `0x84` registers a
rectangular clickable region (`Presentation::defineHotspot`), and
`IOP_IG` hit-tests the live mouse position against them
(`Presentation::hitTestHotspot`, `Input` gained real mouse-position
tracking from `EVENT_MOUSEMOVE`/click events). Simplified from the
reference, which separately tracks "currently hovered" vs. "hovered at
the moment of the last click" so a click only counts against whatever
was under the cursor *at click time* - this does one live hit-test per
poll instead, which only differs if the mouse crosses a hotspot boundary
within a single poll tick. The reference's other, pixel-precise
"hitmap" hotspot model (`IOP_IHGL`/`IOP_IHGC`) still isn't implemented,
same reasoning those two stayed no-ops.

**`IOP_GP`'s effect sub-commands (cmd 7/15/16/17/19)** - turns out the
reference's own "vertical/horizontal/move/fade" branches just do an
instant blit/blend into buffer 0 *regardless of the payload's `dst`
field* (`dst` is repurposed as an animation duration for these, per the
reference's own "Destination is duration" comment) - real,
script-observable end state, ported the same way `IOP_GGE`'s fallback
already was.

**Real per-frame fade animation** (`IOP_GO`, `IOP_GP` cmd 7/19).
The reference additionally registers a `Fade` animation object for
these ("cmd 7/19" and `IOP_GO`'s own delayed color fill) on top of the
instant buffer-0 mutation - previously skipped ("no per-frame animation
pipeline yet"); now real. `Presentation::beginFade(durationMs)` snapshots
buffer 0's content right before the mutation that opcode already does;
`compositeScreen(target, nowMs)` (called once per frame from
`IkuraEngine::run()` instead of a plain buffer-0 copy) crossfades from
that snapshot to buffer 0's already-final content over `[start,
start+duration)`, using the same format-agnostic `blendBlit` the
nameplate/`IOP_GP` blend fix below already introduced, now with a
global-alpha parameter for a whole-frame fade rather than per-pixel
source alpha. Matches the reference's real behavior of ending an
in-flight animation the instant its wait ends, however it ends -
`IkuraEngine::run()` calls `completeFade()` right there, whether the
wait resolved by deadline or an early player press (VileVN reference:
`EventGameTick`'s `IS_WAITTIMER` handling calls `SkipAnimation()` in
*both* its natural-timeout and early-skip branches, not just one).
`IOP_GP` cmd 15/16/17 ("vertical/horizontal/move") don't start a fade -
the reference itself only ever calls `AddAnimation(new Fade(...))` from
the cmd 7/19 branch specifically, the others are a bare instant blit
with no animation object even there.

**Verified real character-sprite compositing** - prompted by a direct
question about whether sprites work at all. They do: every prior
screenshot in this README came from a hand-built probe calling
`loadImage`/`copyGraphic` directly for one image at a time, never a real
script driving multiple layers together, so this had never actually
been checked. Ran a real scene script (`EVS_006.ISF`) end to end through
the real interpreter instead: 42 real `IOP_GL` calls, 95 real `IOP_GP`
calls, correctly composited a standing character sprite (real Snow
Sakura data has 131 of 133 scripts loading a character-prefixed
resource - `C01`-`C22`, `CH00`-`CHxx`, `EYE01`-`EYE06` mouth/eye-blink
parts among them) over a room background. No separate "sprite system"
was ever needed - the existing generic `IOP_GL`/`IOP_GP` mechanism
already handles it correctly; the gap was in verification, not code.

**Real bug caught by real data: `ManagedSurface::blendBlitFrom` was
silently doing nothing.** The first test to actually assert on a
blended pixel's color (added for the GP effect-commands work above)
found it: `blendBlitFrom` only works with `Graphics::BlendBlit`'s own
hardcoded pixel layout (its warning even calls this "RGBA32", but the
layout is actually ABGR32's shift pattern) - every decoder in
`formats/graphic/` (and thus every surface `Presentation` ever blends)
produces `Graphics::PixelFormat::createFormatRGBA32()`, which silently
fails that check and no-ops instead of blending, with no error
anywhere a script or a color-blind test could see it. This had been
latent since `IOP_GP`'s alpha-blend commands (1/21) were first
implemented - nothing exercised them with a real pixel assertion until
now. Fixed with a small format-agnostic per-pixel blend
(`Presentation.cpp`'s anonymous-namespace `blendBlit`) instead of
converting surfaces in and out of ABGR32 on every call, or changing
every decoder's output format (which the real screen output, PNG dumps,
and every other `RGBToColor` call already depend on staying RGBA32).
Used for `IOP_GP`'s blend commands and the nameplate image's
compositing (previously a plain opaque copy).

**`IOP_IG`'s pixel-precise "hitmap" hit-test mode is real too**
(`IOP_IHGL`/`IOP_IHGC`) - an alternative to the rectangular "spots"
model above: a loaded image where each pixel's green channel directly
encodes a hotspot index (0xFF = none), taking priority over spots while
loaded (VileVN reference: `MouseMove`'s `if(hitmap){...}else{spots}`).
Not exercised by any real script in this engine's test corpus, ported
from the reference's stated logic and unverified against real bytes.

**`IOP_GGE`'s real fade is wired in too** - its own comment ("No
support for effect files (yet)") only covers the grayscale-mask-driven
wipe it never implements; the reference *does* register a real `Fade`
animation using the payload's `tick` field, same mechanism `IOP_GO`
already has. `IOP_GP` cmd 15/16/17 ("vertical"/"horizontal"/"move")
still don't animate - confirmed the reference itself never registers a
`Fade` (or equivalent) for those three specifically, only cmd 7/19 and
`IOP_GGE` do.

**Deliberately not started: video.** `IkuraVideo` was named in the
original Task 5 plan alongside presentation/input/audio, but no real
game in this engine's test data (Snow Sakura, Kana Okaeri, Kana Imouto)
ships a single video file - nothing exists to build or validate
`IOP_AVIP` against without inventing behavior from nothing, which breaks
this whole port's real-data-only discipline. Revisit once a game that
actually uses video is available to test against.

**Real-data status**: rendered a real decoded background
(`BG02A.GG0`) through a real `IOP_GP` fade command (cmd 19, exercising
the fixed blend path), a real nameplate image (`FW001.GG2`, alpha-
blended), and a real line of dialogue text at a custom color through a
real system TrueType font, all via the exact frame-composition sequence
`IkuraEngine::run()` uses - correctly blended, word-wrapped, and
positioned, confirmed by eye. Separately verified the real animated
fade itself: three real frames (0%/50%/100%) of a real `IOP_GO`-style
transition from a real decoded background to black, confirmed by eye to
be an actual crossfade, not an instant cut.

## Runtime: audio (Task 5, in progress)

**Implemented**: `IOP_ML` (load a named track and loop it indefinitely,
replacing whatever's playing), `IOP_MS` (stop music), `IOP_SER` (play a
named sound effect once on one of 32 channels), `IOP_SET` (stop a
channel, or every channel if negative). `IOP_MP`/`IOP_SEP`/`IOP_KIDFN`
are no-ops (confirmed no-ops in the reference too). Music/SE decoding
dispatches straight to ScummVM's own Vorbis and WAV decoders - this is
the one ikura resource type that needs no bespoke codec at all.

**Real-data status**: decoded a real Snow Sakura music track
(`MP01.OGG`) end to end through the real Vorbis decoder and confirmed
the output by ear. Across all 133 real scripts, `ML` fires 65 times,
`MS` fires 65 times, `SER` fires 2 times, and a script left real music
genuinely playing on the mixer.

**Oddity**: scripts reference music by a `.wav` filename (e.g.
`MP01.wav`) that doesn't exist in the shipped cabinet - only a
re-encoded `.OGG` version does. `Audio::resolveStream` resolves this by
extension fallback, matching how the reference engine's own resource
lookup tolerates the mismatch.

Also implemented: `IOP_SES` (stop sound effect by channel) - the
reference's `iop_ses` is identical to `iop_set`, just with an extra
unused duration field in its payload.

## Known compatibility gaps

VileVN's own ikura support is itself incomplete, not just NovelVM's port of
it: real-data testing below traces both gaps back into the reference
source itself (unfinished DRS parsing code, a revision check with no
fallback for revisions it doesn't recognize), not into a mistake made
while porting it. NovelVM's parser matches the reference's own logic
line-for-line; the reference was simply never finished for this data.

### DRS format (Digital Romance System) — broken in reference

The DRS script-format parser (`IkuraScript::LoadDRS` in the reference's `iscript.cpp`) is incomplete. Testing against real Kana Imouto DRS-format script members (e.g., `RESTART.DRS`, `START.DRS` from DVD Edition) shows the header-field layout assumed by the reference (tsize/count pair at offset 0x19 after decryption) does not match real data. The reference code has stray debug output and commented exit-code at that function (lines 379+), indicating active-but-incomplete debugging when last touched.

**Impact**: Kana Imouto (DRS format) cannot be loaded. Other DRS-format games (if any exist in the 8-game family) will also fail.

**No fix available without**: reverse-engineering the real DRS header format from a disassembler trace or a working decoder from a game binary, neither of which is present in the VileVN reference tree.

### ISF/GDL revision mismatch — Kana Okaeri only

Real Kana Okaeri `.ISF` script files decode to ssoffset `0x41284b5f` (1.09 billion), far exceeding file size. The raw header bytes are identical across all sampled files (TITLE.ISF, START.ISF: first 7 bytes `5f 28 4b 41 cd cd 4b 42...`), ruling out corruption and pointing to either a format variant or a later script-file revision. By contrast, Snow Sakura ISF files (also real data, confirmed working) have sensible ssoffset values (e.g., TITLE.ISF: `0x0000007c` = 124 bytes). 

The reference source tree's stable branch hardcodes a revision check `0x9597` that Snow Sakura matches exactly but Kana Okaeri does not (reads `0xcdcd`). The vilevn branch omits that hardcoded check but provides no alternative format handler. Game-specific code (e.g., `kanaoka.cpp`) adds no preprocessing.

**Impact**: Kana Okaeri (ISF format) fails to load all 60 script members.

**No fix available without**: source code or executable from a Kana Okaeri build that loads these scripts successfully, or documentation of the ISF-format evolution between Snow Sakura (2007) and Kana Okaeri (2004 orig / 2011 Windows remaster).
