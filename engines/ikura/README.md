# engines/ikura (planned)

Native NovelVM implementation of the Ikura/GDL visual novel interpreter,
superseding the `ikura` family of the abandoned GPLv3 VileVN project
(SourceForge `vilevn`, https://sourceforge.net/projects/vilevn/). Tracked
under issue #4. Full provenance, license inventory, and the decision to
split VileVN into six separate engines instead of one `engines/vilevn` are
recorded in `docs/vilevn-audit/PROVENANCE.md` — read that first.

This directory currently contains audit documentation only. No engine code
exists yet; that starts once this audit is reviewed.

## Scope

Ikura/GDL is the one part of VileVN with a genuinely shared bytecode VM
(`src/ikura/ikuradecoder.cpp`, `iparser.cpp`, `iscript.cpp`, `iopcodes.h` in
the VileVN reference tree), plus a DRS-format variant
(`src/ikura/drsdecoder.*`, "Digital Romance System", the predecessor format
Ikura/GDL evolved from and still overrides). It drives 8 games:

catgirl, crescendo, hitomi, idols, kana, kanaoka, sagara, snow — see
`tests/compatibility-matrix.md` for the per-game detail.

## Provenance baseline

Source: `git clone https://git.code.sf.net/p/vilevn/vilevn`, commit
`f6e48d6b2419574e3d3cb92d7b819a5429786786` (2012-08-07). License: GPLv3,
copyright "ViLE Team" — see `LICENSES/README.md` for the full inventory.
