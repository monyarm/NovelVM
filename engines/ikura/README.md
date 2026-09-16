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

## Known compatibility gaps

### DRS format (Digital Romance System) — broken in reference

The DRS script-format parser (`IkuraScript::LoadDRS` in the reference's `iscript.cpp`) is incomplete. Testing against real Kana Imouto DRS-format script members (e.g., `RESTART.DRS`, `START.DRS` from DVD Edition) shows the header-field layout assumed by the reference (tsize/count pair at offset 0x19 after decryption) does not match real data. The reference code has stray debug output and commented exit-code at that function (lines 379+), indicating active-but-incomplete debugging when last touched.

**Impact**: Kana Imouto (DRS format) cannot be loaded. Other DRS-format games (if any exist in the 8-game family) will also fail.

**No fix available without**: reverse-engineering the real DRS header format from a disassembler trace or a working decoder from a game binary, neither of which is present in the VileVN reference tree.

### ISF/GDL revision mismatch — Kana Okaeri only

Real Kana Okaeri `.ISF` script files decode to ssoffset `0x41284b5f` (1.09 billion), far exceeding file size. The raw header bytes are identical across all sampled files (TITLE.ISF, START.ISF: first 7 bytes `5f 28 4b 41 cd cd 4b 42...`), ruling out corruption and pointing to either a format variant or a later script-file revision. By contrast, Snow Sakura ISF files (also real data, confirmed working) have sensible ssoffset values (e.g., TITLE.ISF: `0x0000007c` = 124 bytes). 

The reference source tree's stable branch hardcodes a revision check `0x9597` that Snow Sakura matches exactly but Kana Okaeri does not (reads `0xcdcd`). The vilevn branch omits that hardcoded check but provides no alternative format handler. Game-specific code (e.g., `kanaoka.cpp`) adds no preprocessing.

**Impact**: Kana Okaeri (ISF format) fails to load all 60 script members.

**No fix available without**: source code or executable from a Kana Okaeri build that loads these scripts successfully, or documentation of the ISF-format evolution between Snow Sakura (2007) and Kana Okaeri (2004 orig / 2011 Windows remaster).
