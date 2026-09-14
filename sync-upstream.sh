#!/usr/bin/env bash
# Apply the delta between two rewritten upstream snapshots as one commit.

set -u

CONF=.novelvm-update.conf
DRY_RUN=0
REPORT=0
FROM=

usage() {
	cat <<'EOF'
Usage: ./sync-upstream.sh [--dry-run] [--report] [--from REV]

Fetch ScummVM, rewrite only the old and new snapshots, and apply their delta
to the current NovelVM branch. Conflicts are left for manual resolution.

  --dry-run       fetch and report the delta without changing the worktree
  --report        report the current remote state without fetching or changing
  --from REV      explicitly select the old upstream revision
EOF
}

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
msg() { printf '%s\n' "$*"; }

while [ "$#" -gt 0 ]; do
	case "$1" in
		--dry-run) DRY_RUN=1 ;;
		--report) REPORT=1 ;;
		--from)
			[ "$#" -ge 2 ] || die "--from requires a revision"
			FROM=$2
			shift
			;;
		--help|-h) usage; exit 0 ;;
		*) die "unknown option: $1" ;;
	esac
	shift
done

[ -f "$CONF" ] || die "$CONF not found; run from the repository root"
# shellcheck disable=SC1090
. "./$CONF"
: "${SCUMMVM_REMOTE:?}" "${SCUMMVM_REMOTE_URL:?}" "${SCUMMVM_BRANCH:?}"
: "${REWRITE_TOOL:?}" "${NOVELVM_ENGINES:?}" "${SPARSE_FILE:?}"
SYNC_METADATA_FILE=${SYNC_METADATA_FILE:-.novelvm-sync}

ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || die "not inside a Git repository"
[ "$(pwd)" = "$ROOT" ] || die "must be run from the repository root: $ROOT"
[ -f "$REWRITE_TOOL" ] || die "rewrite tool missing: $REWRITE_TOOL"
command -v python3 >/dev/null || die "python3 is required"
[ ! -d .git/rebase-merge ] && [ ! -d .git/rebase-apply ] ||
	die "a rebase is already in progress"

BRANCH=$(git symbolic-ref --quiet --short HEAD) || die "detached HEAD; check out a branch first"
[ -z "$(git status --porcelain --untracked-files=no)" ] ||
	die "working tree has tracked changes; commit or stash first"

git remote get-url "$SCUMMVM_REMOTE" >/dev/null 2>&1 ||
	git remote add "$SCUMMVM_REMOTE" "$SCUMMVM_REMOTE_URL"
case "$(git remote get-url "$SCUMMVM_REMOTE")" in
		*scummvm/scummvm*) ;;
		*) die "remote $SCUMMVM_REMOTE has unexpected URL" ;;
esac

if [ "$REPORT" -eq 0 ]; then
	git fetch "$SCUMMVM_REMOTE" "$SCUMMVM_BRANCH" || die "fetch failed"
fi
NEW=$(git rev-parse --verify "$SCUMMVM_REMOTE/$SCUMMVM_BRANCH") ||
	die "upstream ref is unavailable"

if [ -z "$FROM" ] && git cat-file -e "HEAD:$SYNC_METADATA_FILE" 2>/dev/null; then
	FROM=$(git show "HEAD:$SYNC_METADATA_FILE" | awk -F= '$1 == "upstream" {print $2; exit}')
fi
if [ -z "$FROM" ]; then
	FROM=$(git log --format=%B -n 2000 HEAD |
		sed -nE 's/.*Sync with ScummVM rev[^0-9a-f]*([0-9a-f]{40}).*/\1/p' |
		sed -n '1p')
fi
[ -n "$FROM" ] || die "no previous upstream revision; use --from REV"
FROM=$(git rev-parse --verify "$FROM^{commit}") || die "invalid old upstream revision"
git cat-file -e "$NEW^{commit}" || die "invalid upstream revision"
git merge-base --is-ancestor "$FROM" "$NEW" ||
	die "old upstream revision is not an ancestor of the fetched revision"

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/novelvm-sync.XXXXXX") || die "cannot create temporary directory"
cleanup() {
	git update-ref -d "$OLD_REF" 2>/dev/null || true
	git update-ref -d "$NEW_REF" 2>/dev/null || true
	rm -rf "$TMP_ROOT"
}
trap cleanup EXIT HUP INT TERM
OLD_REF="refs/novelvm/tmp/sync-$$-old"
NEW_REF="refs/novelvm/tmp/sync-$$-new"

rewrite_snapshot() {
	local source=$1 destination=$2 stream=$TMP_ROOT/stream
	git fast-export --no-data --reference-excluded-parents --signed-tags=strip \
		"$source^..$source" |
		python3 "$REWRITE_TOOL" rewrite "$source" "$destination" >"$stream" ||
		die "path rewriting failed"
	git fast-import --force --quiet <"$stream" || die "snapshot import failed"
	python3 "$REWRITE_TOOL" validate "$destination^{tree}" ||
		die "rewritten snapshot failed collision validation"
}

rewrite_snapshot "$FROM" "$OLD_REF"
rewrite_snapshot "$NEW" "$NEW_REF"
PATCH=$TMP_ROOT/upstream.patch
git diff --binary --full-index --no-renames "$OLD_REF^{tree}" "$NEW_REF^{tree}" \
	-- . ':(exclude,glob)engines/*/**' >"$PATCH"

old_short=$(git rev-parse --short=12 "$FROM")
new_short=$(git rev-parse --short=12 "$NEW")
changed=$(git diff --name-only "$OLD_REF^{tree}" "$NEW_REF^{tree}" \
	-- . ':(exclude,glob)engines/*/**' | wc -l | tr -d ' ')
upstream_engines=0
for engine in $(git ls-tree -d --name-only HEAD:engines); do
	keep=0
	for novelvm_engine in $NOVELVM_ENGINES; do
		[ "$engine" = "$novelvm_engine" ] && keep=1
	done
	[ "$keep" -eq 1 ] || upstream_engines=$((upstream_engines + 1))
done
msg "upstream: $old_short -> $new_short ($changed changed paths, $upstream_engines engine directories to remove)"
[ "$changed" -ne 0 ] || [ "$upstream_engines" -ne 0 ] || {
	msg "no upstream changes; nothing to commit"
	exit 0
}

if [ "$REPORT" -eq 1 ] || [ "$DRY_RUN" -eq 1 ]; then
	git diff --stat "$OLD_REF^{tree}" "$NEW_REF^{tree}"
	msg "dry run: $BRANCH was not changed"
	exit 0
fi

SAFETY_REF="refs/novelvm/safety/sync-$(date -u +%Y%m%dT%H%M%SZ)-$$"
git update-ref "$SAFETY_REF" HEAD || die "could not create safety ref"
msg "safety ref: $SAFETY_REF"

if [ -s "$PATCH" ] && ! git apply --3way --index "$PATCH"; then
	die "upstream delta has conflicts; resolve them manually, stage the result, and commit with the metadata below:\nupstream=$NEW\nremote=$SCUMMVM_REMOTE\nbranch=$SCUMMVM_BRANCH"
fi

for engine in $NOVELVM_ENGINES; do
	git cat-file -e ":engines/$engine/configure.engine" ||
		die "upstream delta removed protected engine from the index: $engine"
	[ -f "engines/$engine/configure.engine" ] ||
		die "protected engine missing from working tree: $engine"
done

# Sync commits intentionally do not retain upstream engine directories. They
# are removed from the index, not merely hidden by sparse checkout.
for engine in $(git ls-tree -d --name-only HEAD:engines); do
	keep=0
	for novelvm_engine in $NOVELVM_ENGINES; do
		[ "$engine" = "$novelvm_engine" ] && keep=1
	done
	[ "$keep" -eq 1 ] || git rm -r -q -f --sparse -- "engines/$engine" ||
		die "could not remove upstream engine from sync tree: $engine"
done

git diff --cached --check || die "staged upstream delta contains whitespace errors"

printf 'upstream=%s\nremote=%s\nbranch=%s\nrewritten-tree=%s\n' \
	"$NEW" "$SCUMMVM_REMOTE" "$SCUMMVM_BRANCH" \
	"$(git rev-parse "$NEW_REF^{tree}")" >"$SYNC_METADATA_FILE"
git add "$SYNC_METADATA_FILE" || die "could not stage sync metadata"
git commit -m "ALL: Sync with ScummVM rev: $NEW" || die "sync commit failed"

for engine in $NOVELVM_ENGINES; do
	git cat-file -e "HEAD:engines/$engine/configure.engine" ||
		die "protected engine missing from commit: $engine"
	[ -f "engines/$engine/configure.engine" ] ||
		die "protected engine missing from working tree: $engine"
done

git sparse-checkout init --no-cone 2>/dev/null || true
{
	printf '%s\n' '/*'
	git ls-tree -d --name-only HEAD:engines | while IFS= read -r engine; do
		keep=0
		for novelvm_engine in $NOVELVM_ENGINES; do
			[ "$engine" = "$novelvm_engine" ] && keep=1
		done
		[ "$keep" -eq 1 ] || printf '!/engines/%s/\n' "$engine"
	done
} >"$SPARSE_FILE"
git sparse-checkout reapply 2>/dev/null || git read-tree -mu HEAD
[ "$(git symbolic-ref --short HEAD)" = "$BRANCH" ] || die "branch changed unexpectedly"
msg "sync complete: $(git rev-parse --short=12 HEAD)"
