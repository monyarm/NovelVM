# Contributing to NovelVM

NovelVM is a fork of [ScummVM](https://www.scummvm.org/) focused on visual
novel and related adventure games. It is developed as its own project and is
never merged back into upstream ScummVM, so this repository's rules win where
they differ from upstream's.

**Read [AI-GUIDELINES.md](AI-GUIDELINES.md) before your first change.** It is
mandatory, not background reading.

## AI assistance

AI may author commits and write commit messages in this fork. Every commit that
AI touched must carry a trailer of the form:

```
Assisted-by: AGENT:MODEL [TOOL ...]
```

For example `Assisted-by: OpenCode:Deepseek`. Do not use `Co-Authored-By` for
AI, keep AI names out of the code itself, and do not ship AI-generated art or
media assets. A human still owns the change: understand it, test it, and be
ready to answer review questions about it. The mechanical
`ALL: Sync with ScummVM rev: <sha>` commit is exempt.

## Commit messages

Use ScummVM/NovelVM style, not Conventional Commits:

```
SUBSYSTEM: Short (50 chars or less) summary

More detailed explanatory text, if necessary. Wrap it to about 72
characters or so. The blank line separating the summary from the body
is critical.

Write the summary in the present tense: "Fix bug", not "Fixed bug".
```

Use the subsystem that matches what you touched (`ENGINES`, `CONFIGURE`,
`DOCS`, `GITHUB`, `CI`, ...). Keep the history linear: rebase instead of
merging, and no merge commits in a pull request.

## Coding style

Follow ScummVM's conventions, described on the wiki:

* [Code formatting](https://wiki.scummvm.org/index.php/Code_Formatting_Conventions)
* [Coding conventions](https://wiki.scummvm.org/index.php/Coding_Conventions)

Tabs for indentation, space-padded `if`/`for`/`while`, braces on the same line,
and the C++17 subset ScummVM already uses. Match the file you are editing; new
headers carry the standard GPLv3+ header.

## Building and testing

```sh
./configure                # --enable-all-engines adds the WIP engines
make -j"$(nproc)"
./novelvm
make test                  # unit tests
make devtools              # helper tools under devtools/
```

Build what you submit. If a change can be run, run it, and say in the commit or
PR what you could not test and why.

## Bug reports and engine requests

Use the GitHub issue forms:

* [Bug report](https://github.com/monyarm/NovelVM/issues/new?template=bug.yml)
  — a reproducible problem, with the build and game details.
* [Engine request](https://github.com/monyarm/NovelVM/issues/new?template=engine.yml)
  — a new engine or engine family, with links to research and tools.
* [Feature request](https://github.com/monyarm/NovelVM/issues/new?template=feature.yml)

Do not attach game data. Report only what is needed to reproduce.

## License and provenance

NovelVM is licensed under the GNU GPLv3 or later; see [COPYING](COPYING). All
contributions must be compatible with it. Do not paste code whose license you
cannot establish — if the origin of a snippet is unclear, leave it out. You are
responsible for the provenance of what you submit.

## Keeping up with upstream

Upstream ScummVM changes arrive through a scoped merge performed by
`./sync-upstream.sh`, not by merging upstream by hand. See
[doc/updating.md](doc/updating.md) for the workflow, the review gates, and the
weekly automated sync pull request.
