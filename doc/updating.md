# Updating NovelVM from upstream ScummVM

NovelVM is a fork of [ScummVM](https://www.scummvm.org/) focused on visual
novel games. This document describes the incremental update workflow.

## Remotes

| Remote | Purpose |
| --- | --- |
| `origin` | NovelVM fork (`monyarm/NovelVM`) |
| `scummvm` | Real upstream ScummVM; never pushed to or rewritten |

If `scummvm` is missing, `./sync-upstream.sh` adds it using the URL in
`.novelvm-update.conf`.

## Update Workflow

The workflow is deliberately snapshot-based:

1. Fetch `scummvm/master` normally. Its remote-tracking ref is never modified.
2. Read the previous upstream revision from `.novelvm-sync`, or from the SHA
   in an older `Sync with ScummVM rev:` commit.
3. Rewrite only the old and new upstream snapshots, changing path components
   containing `scummvm` to `novelvm` while leaving file contents unchanged.
4. Exclude upstream engine directories from the delta, remove any such
   directories already tracked by NovelVM, and apply the remaining rewritten
   delta as one ordinary commit.

The full upstream history is never replayed. Path collisions abort the update.
Conflicts stop for manual resolution; the script never selects ours or theirs
globally.

```sh
./sync-upstream.sh             # fetch, apply delta, and commit
./sync-upstream.sh --dry-run   # fetch and report without changing the branch
./sync-upstream.sh --report    # report current remote state without fetching
```

The script requires a clean tracked worktree, an attached branch, and no
pending rebase. Before changing the branch it creates a safety ref under
`refs/novelvm/safety/`. Temporary rewritten snapshot refs are removed when the
script exits. Each successful sync commits `.novelvm-sync` with the upstream
SHA, remote, branch, and rewritten tree ID.

Configuration lives in `.novelvm-update.conf`. The path rewrite implementation
is `devtools/rewrite-upstream-paths.py`; its `validate` mode checks for
destination collisions.

## Engine Filtering

Upstream engine directories are removed from each sync commit and hidden by
non-cone sparse checkout. Shared `engines/` framework files and these NovelVM
engines remain available:

```text
engines/bibleblack
engines/koihime_doki
engines/smt
```

## Building

Sync removes upstream engine directories, so the normal configure scan finds
only the NovelVM engines. No explicit engine list is needed:

```sh
./configure
make -j$(nproc)
```

After an update, build from a clean state:

```sh
make distclean
./configure
make -j$(nproc)
```

The Android port's `NovelVMActivity.java` intentionally still references
`scummvm.ini` internally to migrate existing ScummVM user data.
