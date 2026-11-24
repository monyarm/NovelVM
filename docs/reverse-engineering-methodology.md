# Methodology for reverse-engineered engine ports

Practices from porting `ikura` (see issue #93, PR #99) that generalize to any
engine ported from an abandoned reference implementation with no active
maintainer to ask — the other VileVN families (`windy`/`crowd`/`cware`/`will`/
`jast`, see #4) especially, but not only those.

## Real-data-driven development

**The reference source is a hypothesis, not ground truth.** It's the starting
point, but real files it was meant to read are the actual spec. Every time
this session trusted the reference's own claim about a payload shape over
real bytes, it shipped a bug:

- `IOP_STS`'s reference source claims a 2-byte payload; every real call in
  Snow Sakura's scripts is 8 bytes.
- `IOP_GO`'s reference source reads 7 bytes and never validates length; every
  real call carries an 8th trailing pad byte.
- An opcode value was transcribed wrong early on (`IOP_IM` filed under the
  numeric value that's actually `IOP_IH`) and only surfaced by cross-checking
  `iopcodes.h` again while implementing something else.

Before trusting a reference struct/payload layout, decrypt and dump the real
bytes of a few real instances and check the shape actually matches. Do this
even when the reference source looks authoritative — decompiled/hand-written
reference code has the same bug potential as any other code.

**Prioritize by what real data actually exercises, not by what looks
important in the reference.** Build a throwaway (never committed — delete it
after reading the result) test that runs real extracted game files through
the actual production decode/interpret path and counts what happens: which
opcodes fire, which ones block further execution, how many real scenes reach
a given feature. Implement whichever unimplemented thing is actually blocking
real scripts next, not whichever opcode number is next in the reference's
header file. Re-run this after every batch of new opcodes — coverage numbers
that don't move are a sign something's still wrong upstream of where you're
looking.

**Prove "it works" by running the real production path against real data,
not by code review or a synthetic fixture alone.** A unit test with a 1x1
synthetic image proves the plumbing doesn't crash; it doesn't prove a real
640x480 background decodes with the right colors. Concretely, this session's
proof pattern:

- Graphics: decode a real asset through the real decoder, dump the resulting
  `Graphics::Surface` to PNG (`Image::writePNG`), and actually look at it.
- Audio: decode a real asset through the real decoder, dump to WAV, play it
  through `mpv` and listen.
- A full scene (not just one asset): run an actual real script end to end
  through the real interpreter (not a hand-built probe calling individual
  API methods) and screenshot the composited result. This caught a real gap
  this session initially missed — every earlier "does graphics work"
  screenshot was a hand-built probe calling `loadImage`/`copyGraphic`
  directly for one image at a time, never a real script driving multiple
  layers together, so whether real scripts actually composited a scene
  correctly (backgrounds + character sprites + text + UI) had silently never
  been checked. Don't mark a subsystem done until something as close to the
  real end-to-end path as possible has actually been run.

**When only one real game's data is available, say so.** A finding backed by
one game is real evidence, not proof of a family-wide convention. State the
caveat in the code comment and the docs, and flag it as something to
cross-check once a second game's pipeline works, rather than either hiding
the uncertainty or refusing to act on single-game evidence at all.

**Don't stop at the first reference function you read.** A comment like "no
support for X (yet)" or a function that looks like an instant fallback can be
hiding a second reference file that shows real behavior was implemented
elsewhere. This session read `IOP_GGE`'s handler, saw "No support for effect
files (yet)," and shipped an instant blit with no animation, correctly citing
the comment — but the same function also calls `AddAnimation(new
Fade(...))`, using a `Fade` class whose real source (`src/widgets/fade.cpp`)
was sitting in the same reference checkout the whole time. `IOP_GV` (screen
shake) got the same treatment: dismissed as "no script-observable state,"
when `src/widgets/slide.cpp` shows it queues real, well-defined animations.
Before writing "no real behavior to port" or "needs an animation pipeline we
don't have," grep the reference tree for the class/function actually being
called, not just the one opcode handler already open.

**When you're blocked citing "no test data," check whether it's actually the
algorithm that's missing, not the data.** Pixel-precise hotspot hit-testing
(`IOP_IHGL`) was initially left unimplemented for "no test data" reasons —
but the algorithm (load an image, read a hotspot index out of its green
channel) doesn't need a real hitmap file to be correctly implemented from the
reference's stated logic, just to be *verified* against one. Implement it,
mark it clearly as unverified against real bytes (see below), and stop
citing missing data as a reason not to write code at all.

**Reference-ported-but-unverified code needs an explicit trust tier.**
Code with no real script exercising it yet (this session: `IOP_GV`,
`IOP_IHGL`/`IOP_IHGC`) is real, but a different confidence level than
code a real script actually walked through. Say so directly in both the
code comment and any status doc — "ported from the reference's stated
logic, not exercised by any real script in this engine's test corpus" —
rather than letting it read the same as verified behavior.

**A hard crash from real data is worth chasing to the actual root cause.**
`Graphics::Surface::copyRectToSurface` turned out to assert the *entire*
pasted rect fits in the destination — it doesn't clip a partially
out-of-range paste at all, unlike `blitFrom`/a hand-rolled per-pixel blend.
The fix wasn't a defensive `try`/bounds-guess, it was reading the actual
assertion message and understanding the real contract.

## Working with tools and the environment

**Install the real tool. Never hand-roll a substitute.** If a format-
conversion or decode capability is missing, `nix profile install` the actual
library/tool (and its `.dev` output for headers — nixpkgs splits these, a
common trap) rather than writing a workaround. This session installed
`ccd2iso`, `freetype`, `dejavu_fonts`, and `gdb` this way rather than
inventing substitutes.

**Cross-check environment quirks before assuming code is wrong.** Two real
bugs this session turned out to be pure environment issues, not logic bugs:
nixpkgs' `sdl2-compat` reports a broken include path from `sdl2-config`
(needed a small wrapper proxying to `pkg-config`), and enabling
`USE_FREETYPE2` needs a second linker pass over
`common/compression/libcompression.a` in `test/module.mk` because `ld` only
scans each static archive once and `ttf.o`'s `Common::makeZipArchive`
dependency comes from a library listed earlier in the link order.

**No real per-game asset for something (a font, in this case) doesn't mean
invent a fake one.** None of this engine's real games ship a font file, and
the reference's real answer (a path in its own separate config file) doesn't
transfer to a from-scratch port. The honest move was picking a real,
redistributable system font via FreeType with a documented, inert-until-used
hook (`Font::Mode::kAccurate`) for a future per-game font if one's ever
found — not silently rendering with an arbitrary made-up substitute.

## Documentation and communication

**Status docs are a progress report, not a devlog.** No session
self-references, no narration of what tool got installed when, no "as of
this session" phrasing that rots the moment the doc is read later.
Structure: what's implemented and how it matches (or deliberately diverges
from) the reference, what's real-data-verified vs. ported-but-unverified,
what's genuinely not implemented and why, real numbers from the last
real-data pass.

**State corrections plainly, don't bury them.** When something written
earlier turns out wrong ("no script-observable state," a mislabeled opcode,
"portrait" for what turned out to be a name-tag image), say so directly in
the next revision rather than quietly changing the claim.

**Every deliberately-skipped feature needs a concrete, falsifiable reason,**
not a vague "not implemented yet." "No per-frame animation pipeline exists"
was a real reason until a per-frame animation pipeline got built for fades —
at which point it stopped being true for the other opcodes that needed the
same pipeline, and they got revisited. A reason that can't be checked later
against new facts isn't a reason, it's a placeholder.

## Process

- Don't commit until explicitly asked. Work can stay uncommitted across many
  turns of a session.
- When asked to commit, split into logically ordered, buildable commits
  (dependency order: leaf modules before the code that calls them), not one
  giant commit — match this repo's existing `type(scope): summary` commit
  style (`git log --oneline` before guessing at a convention).
- Every commit needs `Assisted-by: <tool>:<model>` per `AI-GUIDELINES.md` —
  never `Co-Authored-By`, this repo's guidelines explicitly forbid AI
  (co-)authorship of commits.
- Real, hard-crash-risk, or shared-state actions (force-push, `git reset
  --hard`, deleting branches, pushing) need either standing authorization or
  an explicit ask in the moment — a prior approval doesn't carry forward to
  a new request of the same kind.
- Never run `find`/`du`/`ls` recursively over `/` or the whole `/nix/store`.
  Never pipe `nix build`/`nix eval` through `head`/`tail`.
- Don't run multiple long/heavy operations in parallel or backgrounded by
  default in this environment — sequential, foreground work is more
  reliable here even when parallelizing would normally be faster.
