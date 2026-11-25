# NovelVM

NovelVM is a fork of [ScummVM](https://www.scummvm.org/) focused on visual
novels and related Japanese adventure games. It is developed as its own
project: upstream ScummVM changes are merged into NovelVM, but NovelVM is never
merged back upstream. See [doc/updating.md](doc/updating.md).

## Engines

NovelVM carries a small set of engines instead of ScummVM's full list:

| Engine | Status |
| --- | --- |
| `smt` | CRIWare testbed for Persona 3/3 FES/4 (PS2), Persona 3 Portable (PSP) and Persona 4 Golden/5/5 Royal (PSVita). Reads file formats; no gameplay yet. |
| `director` | Macromedia Director, restricted to games matched on VNDB. Detection hashes are upstream's; everything else still falls back to generic Director detection. |
| `snatcher` | Snatcher, fully playable. Detects the English Sega CD NA and EU/PAL releases. |
| `ags` | Adventure Game Studio, with VNDB ids and support levels for the VN titles that match. |
| `bibleblack` | Bible Black. A98SYS PAK reader and music playback only. |
| `koihime_doki` | Koihime † Musou. EXWLCS archive reader only. |

An engine that only reads a game's file formats is marked `File Formats` in
`--list-games`; the game is detected and its data can be inspected, but it
does not run yet.

## VNDB metadata

Visual novels are identified by [VNDB](https://vndb.org/) id in the detection
tables, together with a support level. A game is only added after a confirmed
VNDB match, so a title in the list is the same work on both sides.

`./novelvm --list-games` prints four columns — Game ID, Full Title, State and
URL:

```
Game ID                        Full Title                                                   State                         URL
snatcher                       Snatcher                                                     Playable                      https://vndb.org/v1305
```

`./novelvm --list-games-json` exposes the same values as `state` and `url`.
Rows with no recorded metadata stay blank, which is what most upstream ScummVM
rows look like. See [doc/updating.md](doc/updating.md) for the table
conventions.

## Building

No prebuilt binaries are published; build it yourself:

```sh
./configure
make -j"$(nproc)"
./novelvm
```

`./configure --enable-all-engines` also enables the work-in-progress engines.
`make test` builds and runs the unit tests, and `make devtools` builds the
helper tools under `devtools/`. Platform-specific notes are in
[doc/QuickStart](doc/QuickStart).

## Getting involved

* [CONTRIBUTING.md](CONTRIBUTING.md) — commit style, coding style, licensing.
* [AI-GUIDELINES.md](AI-GUIDELINES.md) — this fork's AI policy, including the
  required `Assisted-by` trailer and the ban on AI-generated assets.
* [doc/updating.md](doc/updating.md) — how upstream ScummVM changes are merged
  and reviewed.
* [Issues](https://github.com/monyarm/NovelVM/issues) and the issue forms for
  bug reports and engine requests.

## License

NovelVM is licensed under the GNU GPLv3 or later; see [COPYING](COPYING).
Upstream ScummVM copyrights are retained.
