#!/bin/sh
# NovelVM upstream update script.
#
# Rebases master onto current scummvm/master with all upstream paths
# containing 'scummvm' (any casing) rewritten to 'novelvm', then hides
# upstream game-engine directories from the working tree via sparse
# checkout while keeping them in Git history.
#
# Usage:
#   ./rebase.sh            Run a full update.
#   ./rebase.sh --continue Resume after manually resolving conflicts.
#
# The script is fail-fast: on any unresolved conflict it stops without
# resetting or deleting anything. Re-run with --continue once conflicts
# are resolved. Running it twice with no upstream changes is a no-op.

set -eu

CONF=".novelvm-update.conf"

msg()  { printf '%s\n' "$*"; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }

[ -f "$CONF" ] || die "$CONF not found; run from the repository root"
# shellcheck disable=SC1090
. "./$CONF"

: "${SCUMMVM_REMOTE:?}" "${SCUMMVM_REMOTE_URL:?}" "${SCUMMVM_BRANCH:?}"
: "${REWRITE_REF:?}" "${REWRITE_TOOL:?}"
: "${PROTECTED_ENGINES:?}" "${SPARSE_FILE:?}"

# 1-2. Strict mode is on; verify we are at the repository root.
ROOT=$(git rev-parse --show-toplevel 2>/dev/null) ||
    die "not inside a Git repository"
[ "$(pwd)" = "$ROOT" ] || die "must be run from the repository root: $ROOT"
[ -f "$REWRITE_TOOL" ] || die "rewrite tool missing: $REWRITE_TOOL"
command -v python3 >/dev/null || die "python3 is required"

is_protected_engine_dir() {
    for p in $PROTECTED_ENGINES; do
        [ "$1" = "$p" ] && return 0
    done
    return 1
}

# Resolve rebase conflicts restricted to unprotected upstream engine paths
# by keeping the upstream side. Returns nonzero if anything else is left.
resolve_engine_conflicts() {
    remaining=0

    while IFS= read -r path; do
        [ -n "$path" ] || continue
        case "$path" in
            engines/*/*)
                eng=${path#engines/}
                eng=${eng%%/*}
                if is_protected_engine_dir "$eng"; then
                    msg "CONFLICT (protected engine, manual): $path"
                    remaining=$((remaining + 1))
                    continue
                fi
                ;;
        esac
        case "$path" in
            dists/*novelvm*|dists/*NovelVM*|icons/*novelvm*|po/*novelvm*)
                st=$(git ls-files -u -- "$path" | cut -f3 | sort | tr '\n' ' ')
                case "$st" in
                    *2*3*|*3*2*)
                        git checkout --theirs -- "$path" &&
                            git add -- "$path" &&
                            msg "resolved (kept fork): $path" ||
                            remaining=$((remaining + 1))
                        continue
                        ;;
                esac
                ;;
        esac
        # Keep the upstream version when present, else record the deletion.
        # Index-only operations resolve skip-worktree (sparse) paths too.
        entry=$(git ls-files -u -z -- "$path" | tr '\0' '\n' |
                awk -F'\t' '$1 ~ / 2$/ {split($1,a," "); print a[1]","a[2]","$2}')
        if [ -n "$entry" ]; then
            printf '%s\n' "$entry" |
                xargs -r -n 1 git update-index --add --cacheinfo &&
                msg "resolved (kept upstream): $path" ||
                remaining=$((remaining + 1))
        else
            git rm -qf -- "$path" ||
                remaining=$((remaining + 1))
            msg "resolved (kept deletion): $path"
        fi
    done < <(git diff --name-only --diff-filter=U)

    [ "$remaining" -eq 0 ]
}

rebuild_sparse_checkout() {
    # Non-cone sparse checkout:
    #   include everything, exclude every non-protected engines/<name>/
    git sparse-checkout init --no-cone 2>/dev/null || true
    {
        echo '/*'
        git ls-tree -d --name-only HEAD:engines |
        while IFS= read -r d; do
            is_protected_engine_dir "$d" || echo "!/engines/$d/"
        done
    } >"$SPARSE_FILE"
    git sparse-checkout reapply 2>/dev/null || git read-tree -mu HEAD
}

validate() {
    # Collision check on the rewritten upstream tip.
    python3 "$REWRITE_TOOL" validate "$REWRITE_REF^{tree}" ||
        die "rewritten upstream tree failed collision validation"
    # Protected engines must exist in the committed tree and on disk.
    for p in $PROTECTED_ENGINES; do
        git cat-file -e "HEAD:engines/$p/configure.engine" 2>/dev/null ||
            die "protected engine missing from commit: engines/$p"
        [ -f "engines/$p/configure.engine" ] ||
            die "protected engine missing from working tree: engines/$p"
    done
}

if [ "${1:-}" = "--continue" ]; then
    [ -d .git/rebase-merge ] || die "no rebase in progress"
    resolve_engine_conflicts ||
        die "unresolved conflicts remain; resolve them, then re-run '$0 --continue'"
    GIT_EDITOR=true git rebase --continue ||
        die "rebase could not continue; see 'git status'"
    rebuild_sparse_checkout
    validate
    msg "update finished at $(git rev-parse --short=10 HEAD)"
    exit 0
fi

# 3. Refuse to operate with uncommitted changes to tracked files.
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    die "working tree has uncommitted changes; commit or stash first"
fi

BRANCH=$(git symbolic-ref --quiet --short HEAD) ||
    die "detached HEAD; check out a branch before updating"

# 4-5. Ensure the upstream remote exists with the expected URL.
if ! git remote get-url "$SCUMMVM_REMOTE" >/dev/null 2>&1; then
    msg "adding remote $SCUMMVM_REMOTE -> $SCUMMVM_REMOTE_URL"
    git remote add "$SCUMMVM_REMOTE" "$SCUMMVM_REMOTE_URL"
fi
ACTUAL_URL=$(git remote get-url "$SCUMMVM_REMOTE")
case "$ACTUAL_URL" in
    *"scummvm/scummvm"*) ;;  # ssh or https forms are both fine
    *) die "remote $SCUMMVM_REMOTE has unexpected URL: $ACTUAL_URL" ;;
esac

BEFORE_UPSTREAM=$(git rev-parse --short=10 "$SCUMMVM_REMOTE/$SCUMMVM_BRANCH")

# 6. Fetch upstream.
git fetch "$SCUMMVM_REMOTE" "$SCUMMVM_BRANCH" ||
    die "failed to fetch $SCUMMVM_REMOTE/$SCUMMVM_BRANCH"

AFTER_FETCH=$(git rev-parse --short=10 "$SCUMMVM_REMOTE/$SCUMMVM_BRANCH")
msg "upstream: $BEFORE_UPSTREAM -> $AFTER_FETCH"

# 7. Regenerate the deterministic rewritten upstream ref.
# The FULL upstream history is rewritten from its roots: every path
# component containing 'scummvm' is renamed at the commit that introduced
# it, so all rewritten trees are internally consistent. Determinism means
# identical upstream input produces identical commits; an unchanged
# upstream regenerates byte-identical history (idempotent, no churn).
TMPSTREAM=$(mktemp)
trap 'rm -f "$TMPSTREAM"' EXIT
git fast-export --no-data --signed-tags=strip --reencode=no \
    "$SCUMMVM_REMOTE/$SCUMMVM_BRANCH" |
    python3 "$REWRITE_TOOL" rewrite "full" "$REWRITE_REF" \
        >"$TMPSTREAM" ||
    die "path rewriting failed; $REWRITE_REF not modified"
git fast-import --force --quiet <"$TMPSTREAM" ||
    die "fast-import failed"
git update-ref -d "$REWRITE_REF.lock" 2>/dev/null || true

NEW_TIP=$(git rev-parse --verify "$REWRITE_REF") ||
    die "rewritten ref was not created"

# 8. Rebase the current branch onto the rewritten ref.
if git merge-base --is-ancestor "$REWRITE_REF" HEAD 2>/dev/null; then
    msg "branch $BRANCH already contains $NEW_TIP; nothing to rebase"
else
    msg "rebasing $BRANCH onto $(git rev-parse --short=10 "$REWRITE_REF")..."
    GIT_EDITOR=true git rebase --onto "$REWRITE_REF" \
        "$(git merge-base HEAD "$REWRITE_REF")" "$BRANCH" --empty=drop &&
        REBASE_OK=1 || {
            resolve_engine_conflicts && {
                GIT_EDITOR=true git rebase --continue ||
                    die "rebase did not finish; see 'git status'. After fixing, run '$0 --continue'"
            } || die "conflicts require manual resolution (see above). Fix them, then run '$0 --continue'. No work was reset."
            REBASE_OK=1
        }
fi

# 9-11. Engine filtering, validation.
rebuild_sparse_checkout
validate

msg "upstream: $BEFORE_UPSTREAM -> $(git rev-parse --short=10 "$SCUMMVM_REMOTE/$SCUMMVM_BRANCH")"
msg "master:   $(git rev-parse --short=10 HEAD)"
msg "update complete"
