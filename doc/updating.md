# Updating NovelVM from upstream ScummVM

NovelVM is a fork of [ScummVM](https://www.scummvm.org/) focused on visual
novel games. This document describes how upstream changes are merged into the
fork, how they are reviewed, and how the weekly automated sync works.

## Remotes

| Remote | Purpose |
| --- | --- |
| `origin` | NovelVM fork (`monyarm/NovelVM`) |
| `scummvm` | Real upstream ScummVM; never pushed to or rewritten |

`./sync-upstream.sh` adds the `scummvm` remote when it is missing, using
`SCUMMVM_REMOTE_URL`. A remote whose URL does not contain `scummvm/scummvm` is
rejected. CI has no SSH key, so it sets
`NOVELVM_SCUMMVM_REMOTE_URL=https://github.com/scummvm/scummvm.git`; the
environment variable overrides the URL from the configuration file.

## The merge model

The sync is a scoped three-way merge, not a patch:

```text
OURS   = HEAD
BASE   = rewritten previous upstream snapshot
THEIRS = rewritten new upstream snapshot

KEEP = NOVELVM_ENGINES ∪ PATCHED_ENGINES
```

For each of `BASE` and `THEIRS` the script:

1. Rewrites path components containing `scummvm` to `novelvm` (case
   preserving) with `devtools/rewrite-upstream-paths.py`. File contents are
   never changed, and a path collision aborts the sync.
2. Removes every `engines/<name>` directory that is not in `KEEP`.
3. Applies the owned-path rules for `REVIEW_PATHS` (see below) so that our
   copy of a fork-owned file wins.
4. Builds a synthetic commit for the filtered tree with `git commit-tree`.

It then runs `git merge-recursive` with those synthetic commits and writes the
result into the normal index and working tree. This means upstream renames,
additions, and deletions merge against our local edits, including edits inside
patched engines. There are no patch files, no `git am`, and no sparse
checkout: unkept engines simply never enter `BASE` or `THEIRS`. A legacy
sparse-checkout file from an older sync is disabled automatically at startup;
the script resolves its path with `git rev-parse --git-path info/sparse-checkout`,
so this also works in a linked worktree.

On a clean merge the script makes one ordinary single-parent commit:

```text
ALL: Sync with ScummVM rev: <full upstream sha>
```

If the merged tree equals `HEAD` and nothing needs review, the script reports
`already up to date` and creates no commit. Before touching the branch it
creates a safety ref under `refs/novelvm/safety/sync-<utc>-<pid>`.

Each successful sync commits `.novelvm-sync`:

```text
upstream=<full upstream sha>
remote=scummvm
branch=master
rewritten-tree=<tree id of the new rewritten snapshot>
```

The next run reads `upstream` from that file to choose `BASE`; older sync
commits are the fallback. `--from REV` overrides it.

## Configuration

`.novelvm-update.conf` is branch-local and drives the sync:

| Key | Meaning |
| --- | --- |
| `SCUMMVM_REMOTE` | Remote name for upstream (`scummvm`) |
| `SCUMMVM_REMOTE_URL` | Expected upstream URL; must contain `scummvm/scummvm` |
| `SCUMMVM_BRANCH` | Upstream branch to follow (`master`) |
| `REWRITE_REF` | Legacy full-history rewrite ref, kept for inspection; the sync never regenerates or rebases onto it |
| `REWRITE_TOOL` | Path to the path-rewrite tool |
| `NOVELVM_ENGINES` | Fork-only engines, never sourced from upstream |
| `PATCHED_ENGINES` | Upstream engines the fork carries and may edit locally |
| `REVIEW_ENGINES` | Subset of `PATCHED_ENGINES` that must be reviewed before committing |
| `REVIEW_PATHS` | Literal fork-owned paths that must be reviewed when upstream touches them |
| `SYNC_METADATA_FILE` | Committed sync metadata file (`.novelvm-sync`) |

Current values:

```sh
NOVELVM_ENGINES="bibleblack koihime_doki smt"
PATCHED_ENGINES="director snatcher"
REVIEW_ENGINES="director"
REVIEW_PATHS="engines/director/detection_tables.h"
```

The configuration is validated before anything is fetched:

- `NOVELVM_ENGINES` and `PATCHED_ENGINES` are disjoint and free of duplicates;
- `REVIEW_ENGINES` is a subset of `PATCHED_ENGINES`;
- every `REVIEW_PATHS` entry is a literal path (no globs, no `..`, not
  absolute), and an entry under `engines/<e>/` requires `<e>` to be in `KEEP`;
- engine names match `[A-Za-z0-9_]+`;
- the remote URL looks like the ScummVM repository.

### Engine classes

- **Fork-only (`NOVELVM_ENGINES`)** — engines that exist only in NovelVM.
  They are never taken from upstream. If upstream ever adds an engine with the
  same name, the sync stops instead of overwriting ours. A fork-only engine
  missing from `HEAD` is a configuration error.
- **Patched (`PATCHED_ENGINES`)** — upstream engines that carry local edits.
  Their local commits are merged with upstream changes instead of being
  replaced by the upstream snapshot.
- **Review (`REVIEW_ENGINES`)** — patched engines that stop the sync before
  the sync commit when both sides changed or when they are newly seeded.
  `REVIEW_PATHS` adds individual files to the same review. A conflict anywhere,
  including inside a review engine, stops earlier with exit 2. Snatcher is
  deliberately not a review engine: upstream Snatcher changes merge and commit
  silently. Upstream-only changes to untouched Director files also merge
  silently.

For every patched engine the script applies this table (`yes`/`no` means the
engine directory exists in that tree):

| HEAD | BASE | THEIRS | Action |
| --- | --- | --- | --- |
| yes | yes | yes | Normal merge; gate only if it is a review engine and both sides changed |
| yes | yes | no | Stop (exit 4): upstream removed or renamed an engine we carry |
| yes | no | no | Stop (exit 4): fork-only content under an upstream-owned name |
| yes | no | yes | Stop (exit 4): name collision, we added an engine upstream also added |
| no | yes | yes | Seed it (re-add via `PATCHED_ENGINES`); gate if it is a review engine |
| no | yes | no | Both sides removed it; no-op |
| no | no | yes | New upstream engine; seed it and gate if it is a review engine |
| no | no | no | Not present; no-op |

Unkept engines are filtered out of `BASE` and `THEIRS`; any that still exist
in `HEAD` are removed from the merged tree. Every `KEEP` engine is then
verified to have `engines/<engine>/configure.engine` in the index, the working
tree, and the commit.

## Review gates and owned paths

The sync stops before committing (exit 3) when:

- a review engine changed on both sides, even if the merge was textually
  clean;
- a review engine is newly seeded from upstream (first import);
- a `REVIEW_PATHS` file changed upstream (`THEIRS` differs from `BASE`);
- a new owned path is imported from upstream;
- an owned path collides with a path upstream now also adds.

A conflict anywhere, including inside a review engine, stops earlier with
exit 2 and is handled by the pending-state workflow below.

Our copy of an owned path always wins in the merge. When an owned path changes
upstream, the script prints the added and removed `{ "id", "title" }` entries
so a new Director title cannot slip in unnoticed. Review that delta and apply
the accepted changes as a separate follow-up commit; the sync commit itself
only contains the merge.

Owned-path decisions:

| HEAD | BASE | THEIRS | Action |
| --- | --- | --- | --- |
| yes | yes | yes | Force our copy into `BASE` and `THEIRS`; gate if upstream changed it |
| yes | yes | no | Stop (exit 4): upstream deleted or renamed a fork-owned file |
| yes | no | no | Fork-only file; force our copy; no gate |
| yes | no | yes | Collision; force our copy and gate |
| no | yes | yes | If the parent engine is being seeded, take upstream and gate as a new owned path; otherwise stop (exit 4) — we deleted a file upstream still changes |
| no | yes | no | Both sides deleted it; no-op |
| no | no | yes | First import; take upstream and gate as a new owned path |
| no | no | no | Not present; no-op |

The first-import rule is what seeds the Director detection table when it is
absent from `HEAD` and `BASE`: it arrives as an addition and stops for review
instead of looking like a modify/delete conflict.

## Conflicts and pending state

Any unresolved conflict anywhere stops the sync with exit 2, regardless of
engine class. On a conflict or gate the merge is left staged in the index and
working tree, and the script writes its pending state file with the upstream
SHA, the base/theirs commit SHAs, the original `HEAD`, and the gate list. The
path comes from `git rev-parse --git-path novelvm-sync-pending`; in a linked
worktree that is under `.git/worktrees/<name>/`, not `.git/` itself. It also
keeps the synthetic commits alive under
`refs/novelvm/pending/base` and `refs/novelvm/pending/theirs`.

Commands:

```sh
./sync-upstream.sh             # fetch, merge, and commit
./sync-upstream.sh --dry-run   # fetch and preview; changes nothing
./sync-upstream.sh --report    # preview the already-fetched remote state
./sync-upstream.sh --continue  # finish a stopped merge after resolving it
./sync-upstream.sh --abort     # discard a stopped merge
./sync-upstream.sh --from REV  # override the previous upstream revision
./sync-upstream.sh --to REV    # override the target upstream revision
```

To resolve a conflict: edit the files, `git add` them, then run
`./sync-upstream.sh --continue`. `--continue` refuses to run while unmerged
entries exist or if `HEAD` moved since the sync stopped. It re-checks the kept
engines, removes unkept ones, writes `.novelvm-sync`, and creates the sync
commit from whatever is staged. Policy edits (for example the accepted
Director table delta) belong in a follow-up commit, not in the sync commit.

`--abort` resets hard to the recorded `HEAD` and clears the pending state. It
also refuses if `HEAD` moved. If a gate produced no tree change (for example
only an owned path changed upstream), `--continue` still creates the
metadata-only sync commit that records the new upstream revision.

Exit codes:

| Code | Meaning |
| --- | --- |
| 0 | Success, or already up to date |
| 1 | Unexpected error (configuration, fetch, merge machinery) |
| 2 | Unresolved conflicts |
| 3 | Review gate |
| 4 | Policy stop: deleted/renamed patched engine or owned path, name collision, or a fork deletion upstream still changes |

`--dry-run` and `--report` use the same codes without changing the branch.
They preview the merge with `git merge-tree`, list conflicts, gates, and the
owned-path table delta.

A fresh sync requires an attached branch, a clean tracked tree, no rebase in
progress, `python3`, the rewrite tool, and the upstream remote. Untracked files
that the merge would overwrite make the merge fail loudly; that is intentional.
`--continue` and `--abort` skip the attached-branch and clean-tree checks so a
stopped merge can be finished or discarded from the state it left behind.

## Adding, removing, and moving engines

### Adding an upstream engine

1. Add the engine name to `PATCHED_ENGINES` (not `NOVELVM_ENGINES`; that list
   is only for fork-only engines).
2. If its detection tables or other files need review, add the engine to
   `REVIEW_ENGINES` and any specific files to `REVIEW_PATHS`.
3. Run `./sync-upstream.sh --dry-run` and check the engine add, the changed
   paths, and the gates.
4. Run `./sync-upstream.sh`. A new patched engine is seeded from upstream; a
   review engine stops for inspection first.

The weekly bot never expands `KEEP` on its own. An upstream engine that is not
in the configuration is filtered out and never appears in the fork.

### Removing or moving an engine to a branch

The configuration is branch-local. When a fork-only engine moves off `master`
to its own branch, remove it from `master`'s `NOVELVM_ENGINES` in the same
change that deletes the engine; on the engine branch, keep it listed (for
example `NOVELVM_ENGINES="smt bibleblack"`) so the configuration stays valid if
a sync is run there. The plan is `"smt"` on master after Bible Black and
Koihime move, `"smt bibleblack"` on the Bible Black branch, and
`"smt koihime_doki"` on the Koihime branch.

For a patched engine that moves to a branch, remove it from `PATCHED_ENGINES`
(and from `REVIEW_ENGINES`/`REVIEW_PATHS` if present). The next sync treats it
as unkept and removes any remaining copy from the merged tree. Do this in a
dedicated commit so the intent is clear in history.

Fork-only engines are never pulled from upstream because upstream has no
authoritative copy of them: the fork's tree is the only source, and if upstream
ever adds an engine with the same name the sync stops instead of merging it.
This prevents an unrelated upstream engine from silently replacing fork code.

## VNDB metadata conventions

NovelVM identifies visual novels with [VNDB](https://vndb.org/) ids and records
them in the detection tables. A game is only added to a fork table after a
confirmed VNDB match; weak title matches are reviewed by hand and never
auto-added.

Detection rows use the four-field `PlainGameDescriptor` form:

```cpp
{ "game-id", "Game Title", kStableGame, "https://vndb.org/v1234" }
```

The state column uses ScummVM's existing `GameSupportLevel`, extended by the
fork rather than replaced:

| Value | Meaning in the fork |
| --- | --- |
| `kUnknownGame` | No metadata recorded; upstream two-field rows stay blank |
| `kStableGame` | Fully supported / fully running |
| `kTestingGame` | Ready for public testing |
| `kUnstableGame` | Not ready for public testing |
| `kUnsupportedGame` | Deliberately unsupported |
| `kWarningGame` | Runs, but warn the user |
| `kFileFormatsGame` | Fork: the engine only reads the game's file formats |

Conventions:

- URLs are `https://vndb.org/vN` for VNDB-matched games. SMT keeps its
  `megamitensei.fandom.com` URLs: it is not on VNDB, even though the fork
  treats it as a visual novel for scope purposes.
- Bible Black, Koihime, and SMT are `kFileFormatsGame`. Snatcher is
  `kStableGame` because it runs fully, even though upstream marks its detection
  entries `ADGF_TESTING`.
- Snatcher (VNDB `v1305`) keeps upstream's native US and EU/PAL Sega CD
  entries, their MD5s, and the fan-translation handling. There is no native
  Japanese entry: VNDB's official Sega CD releases are the English NA and EU
  editions, while the Japanese releases are PC-88, MSX, PC Engine CD, PS1, and
  Saturn. `snatcher_translations.dat` translates the detected English build; it
  is not a Japanese detection path.
- Director keeps only VNDB-matched games in its detection tables, preserving
  the upstream ids and detection hashes. The row state is derived from the
  upstream `ADGF_*` flags (`ADGF_TESTING` → `kTestingGame`,
  `ADGF_UNSTABLE` → `kUnstableGame`, `ADGF_UNSUPPORTED` →
  `kUnsupportedGame`, `ADGF_WARNING` → `kWarningGame`, otherwise
  `kStableGame`). When one id has several `gameDescriptions[]` entries, use
  the most severe state in the order
  `Unsupported > Warning > Unstable > Testing > Stable`. Unknown Director
  games still use the `director` fallback, which is `kUnknownGame`.
- The sync imports upstream's Director table as-is and gates on the delta; the
  fork's VNDB-only table is maintained on top of that as a separate change.

## Weekly automated sync

`.github/workflows/weekly-upstream-sync.yml` runs every Monday at 06:00 UTC
and on `workflow_dispatch`, with `concurrency: group: upstream-sync` and no
cancellation of an in-progress run. It is the only CI job that runs
`devtools/test-sync-upstream.sh`; that integration test is not run per commit.

Flow:

1. Check out `master` with full history, run the integration test, and stop
   without touching any PR if it fails.
2. Create `automation/upstream-sync` and run `./sync-upstream.sh` with
   `NOVELVM_SCUMMVM_REMOTE_URL` pointing at the HTTPS upstream URL.
3. Branch on the exit code:
   - `0` with `HEAD` unchanged: nothing to do; leave an open PR as-is.
   - `0` with `HEAD` advanced: push the clean sync.
   - `3` (gate): run `./sync-upstream.sh --continue` and push; the draft PR is
     the review gate and its body carries the Director table delta.
   - `2` (conflicts): commit the conflicted tree with its markers, push, and
     list the paths in the PR body. Do not `--continue`.
   - `4` (policy stop): print the report, fail the job, and leave the PR
     untouched.
4. Force-push `automation/upstream-sync` and create or update exactly one
   draft PR against `master`. The PR body lists the upstream range, changed
   paths, engine adds/removes, conflicts, gate/table delta, and the workflow
   run.

The bot never auto-merges and never pushes to `master`.

Authentication is a fine-grained PAT stored as `SYNC_BOT_TOKEN` with
`Contents: RW`, `Pull requests: RW`, and `Workflows: RW`. A PAT is used
because GitHub suppresses workflow runs for events caused by `GITHUB_TOKEN`, so
a `GITHUB_TOKEN`-created PR would get no CI. The token is passed to
`actions/checkout`, `git push`, and `gh`, and must be rotated yearly; an
expired token fails the job without touching the PR.

One-time repository setup: protect `master` (require a pull request and CI,
include administrators) so the PAT cannot push to it directly. The bot only
writes `automation/upstream-sync`.

The branch is bot-owned and force-pushed weekly, so human edits on it are
replaced on the next run. A conflict resolution must be merged into `master`
before the next weekly run or it is lost.

## Build and verification

Run the integration test before a real sync:

```sh
bash -n devtools/test-sync-upstream.sh
./devtools/test-sync-upstream.sh
```

Preview, then sync:

```sh
./sync-upstream.sh --dry-run
./sync-upstream.sh
```

After a sync or a manual engine change, verify a full build and the game list:

```sh
make distclean
./configure --enable-all-engines
make -j"$(nproc)"
make test
./novelvm --list-games
```

`--enable-all-engines` catches configuration and link errors in engines that
the default scan might skip. `--list-games` should show the VNDB URLs and the
expected support levels, and must not list engines that are not in `KEEP`.
Also confirm that every kept engine has `engines/<engine>/configure.engine`
and that `git status` is clean apart from intentional follow-up edits.

To roll back a sync, reset to the safety ref printed by the script:

```sh
git log --oneline -1 refs/novelvm/safety/sync-<utc>-<pid>
git reset --hard refs/novelvm/safety/sync-<utc>-<pid>
```

For a stopped merge, prefer `./sync-upstream.sh --abort`, which also clears the
pending state.

## Notes

The Android port's `NovelVMActivity.java` intentionally still references
`scummvm.ini` internally to migrate existing ScummVM user data.
