#!/usr/bin/env bash
# Merge the delta between two rewritten upstream snapshots into NovelVM.
#
# This is a scoped three-way merge, not a patch:
#   OURS   = HEAD
#   BASE   = previous rewritten upstream snapshot
#   THEIRS = new rewritten upstream snapshot
#
# Engine directories not listed in NOVELVM_ENGINES or PATCHED_ENGINES are
# filtered out of BASE/THEIRS, so upstream engines never leak into the fork.
# Local edits to PATCHED_ENGINES merge with upstream changes. REVIEW_ENGINES
# and REVIEW_PATHS stop before committing so the change can be reviewed.

set -u

CONF=.novelvm-update.conf
DRY_RUN=0
REPORT=0
FROM=
TO=
CONTINUE=0
ABORT=0
PENDING_FILE=.git/novelvm-sync-pending

usage() {
	cat <<'EOF'
Usage: ./sync-upstream.sh [options]

Fetch ScummVM, rewrite only the old and new snapshots, and merge their delta
into the current NovelVM branch. Engine edits are merged with upstream changes.

  --dry-run       fetch, report, and preview conflicts without changing anything
  --report        report the already-fetched remote state without fetching
  --from REV      explicitly select the old upstream revision
  --to REV        sync to this upstream revision (default: SCUMMVM_BRANCH)
  --continue      finish a stopped merge after resolving conflicts or gates
  --abort         discard a stopped merge and return to the pre-merge HEAD
  --help, -h      show this help

Exit codes: 0 success/no-op, 1 error, 2 conflicts, 3 review gate,
            4 policy stop (deleted/renamed engine or owned path, collision).
EOF
}

die() { printf 'error: %s\n' "$*" >&2; exit 1; }
die4() { printf 'policy stop: %s\n' "$*" >&2; exit 4; }
msg() { printf '%s\n' "$*"; }

while [ "$#" -gt 0 ]; do
	case "$1" in
		--dry-run) DRY_RUN=1 ;;
		--report) REPORT=1 ;;
		--continue) CONTINUE=1 ;;
		--abort) ABORT=1 ;;
		--from)
			[ "$#" -ge 2 ] || die "--from requires a revision"
			FROM=$2
			shift
			;;
		--to)
			[ "$#" -ge 2 ] || die "--to requires a revision"
			TO=$2
			shift
			;;
		--help|-h) usage; exit 0 ;;
		*) die "unknown option: $1" ;;
	esac
	shift
done

[ "$CONTINUE" -eq 0 ] || [ "$ABORT" -eq 0 ] ||
	die "--continue and --abort are mutually exclusive"
[ "$DRY_RUN" -eq 0 ] || [ "$REPORT" -eq 0 ] ||
	die "--dry-run and --report are mutually exclusive"
if [ "$CONTINUE" -eq 1 ] || [ "$ABORT" -eq 1 ]; then
	[ "$DRY_RUN" -eq 0 ] && [ "$REPORT" -eq 0 ] ||
		die "--continue/--abort cannot be combined with --dry-run/--report"
fi

[ -f "$CONF" ] || die "$CONF not found; run from the repository root"
# shellcheck disable=SC1090
. "./$CONF"
: "${SCUMMVM_REMOTE:?}" "${SCUMMVM_REMOTE_URL:?}" "${SCUMMVM_BRANCH:?}"
: "${REWRITE_TOOL:?}" "${NOVELVM_ENGINES:?}"
PATCHED_ENGINES=${PATCHED_ENGINES:-}
REVIEW_ENGINES=${REVIEW_ENGINES:-}
REVIEW_PATHS=${REVIEW_PATHS:-}
SYNC_METADATA_FILE=${SYNC_METADATA_FILE:-.novelvm-sync}
SCUMMVM_REMOTE_URL=${NOVELVM_SCUMMVM_REMOTE_URL:-$SCUMMVM_REMOTE_URL}

ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || die "not inside a Git repository"
[ "$(pwd)" = "$ROOT" ] || die "must be run from the repository root: $ROOT"
[ -f "$REWRITE_TOOL" ] || die "rewrite tool missing: $REWRITE_TOOL"
command -v python3 >/dev/null || die "python3 is required"
[ ! -d .git/rebase-merge ] && [ ! -d .git/rebase-apply ] ||
	die "a rebase is already in progress"

in_list() {
	local needle=$1 list=$2 item
	for item in $list; do
		[ "$item" = "$needle" ] && return 0
	done
	return 1
}
in_keep() { in_list "$1" "$NOVELVM_ENGINES" || in_list "$1" "$PATCHED_ENGINES"; }
engine_exists() { git cat-file -e "$1:engines/$2" 2>/dev/null; }
path_exists() { git cat-file -e "$1:$2" 2>/dev/null; }

validate_engine_list() {
	local name=$1 list=$2 item other seen=
	for item in $list; do
		[[ $item =~ ^[A-Za-z0-9_]+$ ]] || die "invalid engine name in $name: $item"
		for other in $seen; do
			[ "$other" = "$item" ] && die "duplicate engine in $name: $item"
		done
		seen="$seen $item"
	done
}

validate_engine_list NOVELVM_ENGINES "$NOVELVM_ENGINES"
validate_engine_list PATCHED_ENGINES "$PATCHED_ENGINES"
validate_engine_list REVIEW_ENGINES "$REVIEW_ENGINES"

for engine in $NOVELVM_ENGINES; do
	in_list "$engine" "$PATCHED_ENGINES" &&
		die "engine is in both NOVELVM_ENGINES and PATCHED_ENGINES: $engine"
done
for engine in $REVIEW_ENGINES; do
	in_list "$engine" "$PATCHED_ENGINES" ||
		die "REVIEW_ENGINES entry is not in PATCHED_ENGINES: $engine"
done
for path in $REVIEW_PATHS; do
	case "$path" in
		*'*'*|*'?'*|*'['*|/*|*'..'*)
			die "REVIEW_PATHS entries must be literal repository paths: $path"
			;;
	esac
	case "$path" in
		engines/*/*)
			engine=${path#engines/}
			engine=${engine%%/*}
			in_keep "$engine" ||
				die "REVIEW_PATHS entry is under an engine not in KEEP: $path"
			;;
	esac
done
case "$SCUMMVM_REMOTE_URL" in
	*scummvm/scummvm*) ;;
	*) die "SCUMMVM_REMOTE_URL does not look like the ScummVM repository: $SCUMMVM_REMOTE_URL" ;;
esac

# Old syncs used a sparse checkout to hide upstream engines. Unkept engines
# never enter the merge now, so make sure the kept directories are present.
if [ -f .git/info/sparse-checkout ]; then
	msg "disabling legacy sparse checkout"
	git sparse-checkout disable >/dev/null 2>&1 ||
		{ rm -f .git/info/sparse-checkout; git read-tree -mu HEAD; } ||
		die "could not disable the legacy sparse checkout"
fi

if [ "$ABORT" -eq 1 ]; then
	[ -f "$PENDING_FILE" ] || die "no sync is pending"
	head=$(sed -n 's/^head=//p' "$PENDING_FILE")
	[ -n "$head" ] || die "pending state has no HEAD"
	[ "$(git rev-parse HEAD)" = "$head" ] ||
		die "HEAD moved since the sync stopped; refusing to abort"
	git reset --hard "$head" || die "could not reset to $head"
	git update-ref -d refs/novelvm/pending/base 2>/dev/null || true
	git update-ref -d refs/novelvm/pending/theirs 2>/dev/null || true
	rm -f "$PENDING_FILE"
	msg "sync aborted; HEAD is $(git rev-parse --short=12 HEAD)"
	exit 0
fi

if [ "$CONTINUE" -eq 1 ]; then
	[ -f "$PENDING_FILE" ] || die "no sync is pending"
	pending_head=$(sed -n 's/^head=//p' "$PENDING_FILE")
	upstream=$(sed -n 's/^upstream=//p' "$PENDING_FILE")
	remote=$(sed -n 's/^remote=//p' "$PENDING_FILE")
	branch=$(sed -n 's/^branch=//p' "$PENDING_FILE")
	rewritten_tree=$(sed -n 's/^rewritten-tree=//p' "$PENDING_FILE")
	pending_keep=$(sed -n 's/^keep=//p' "$PENDING_FILE")
	[ -n "$pending_head" ] && [ -n "$upstream" ] && [ -n "$rewritten_tree" ] ||
		die "pending state is incomplete"
	[ "$(git rev-parse HEAD)" = "$pending_head" ] ||
		die "HEAD moved since the sync stopped; run --abort or resolve manually"
	[ -z "$(git diff --name-only --diff-filter=U)" ] ||
		die "unmerged entries remain; resolve and stage them first"

	KEEP=${pending_keep:-"$NOVELVM_ENGINES $PATCHED_ENGINES"}
	for engine in $KEEP; do
		git cat-file -e ":engines/$engine/configure.engine" 2>/dev/null ||
			die "kept engine missing from the index: $engine"
		[ -f "engines/$engine/configure.engine" ] ||
			die "kept engine missing from the working tree: $engine"
	done
	while IFS= read -r engine; do
		[ -n "$engine" ] || continue
		in_list "$engine" "$KEEP" || git rm -r -q -f -- "engines/$engine" ||
			die "could not remove unkept engine: $engine"
	done < <(git ls-tree -d --name-only :engines 2>/dev/null || true)
	git diff --cached --check || die "staged sync contains whitespace errors"

	printf 'upstream=%s\nremote=%s\nbranch=%s\nrewritten-tree=%s\n' \
		"$upstream" "$remote" "$branch" "$rewritten_tree" >"$SYNC_METADATA_FILE"
	git add "$SYNC_METADATA_FILE" || die "could not stage sync metadata"
	rm -f .git/MERGE_HEAD .git/MERGE_MSG .git/MERGE_MODE .git/AUTO_MERGE
	git commit -m "ALL: Sync with ScummVM rev: $upstream" || die "sync commit failed"
	git update-ref -d refs/novelvm/pending/base 2>/dev/null || true
	git update-ref -d refs/novelvm/pending/theirs 2>/dev/null || true
	rm -f "$PENDING_FILE"
	for engine in $KEEP; do
		git cat-file -e "HEAD:engines/$engine/configure.engine" 2>/dev/null ||
			die "kept engine missing from the commit: $engine"
		[ -f "engines/$engine/configure.engine" ] ||
			die "kept engine missing from the working tree: $engine"
	done
	msg "sync complete: $(git rev-parse --short=12 HEAD)"
	exit 0
fi

BRANCH=$(git symbolic-ref --quiet --short HEAD) || die "detached HEAD; check out a branch first"
[ -z "$(git status --porcelain --untracked-files=no)" ] ||
	die "working tree has tracked changes; commit or stash first"

if ! git remote get-url "$SCUMMVM_REMOTE" >/dev/null 2>&1; then
	git remote add "$SCUMMVM_REMOTE" "$SCUMMVM_REMOTE_URL" ||
		die "could not add remote $SCUMMVM_REMOTE"
fi
case "$(git remote get-url "$SCUMMVM_REMOTE")" in
	*scummvm/scummvm*) ;;
	*) die "remote $SCUMMVM_REMOTE has an unexpected URL" ;;
esac

if [ "$REPORT" -eq 0 ]; then
	git fetch "$SCUMMVM_REMOTE" "$SCUMMVM_BRANCH" || die "fetch failed"
fi

if [ -n "$TO" ]; then
	NEW=$(git rev-parse --verify "$TO^{commit}") || die "invalid --to revision: $TO"
else
	NEW=$(git rev-parse --verify "$SCUMMVM_REMOTE/$SCUMMVM_BRANCH^{commit}") ||
		die "upstream ref is unavailable; run without --report first"
fi

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
	die "old upstream revision is not an ancestor of the selected revision"

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/novelvm-sync.XXXXXX") || die "cannot create temporary directory"
OLD_REF="refs/novelvm/tmp/sync-$$-old"
NEW_REF="refs/novelvm/tmp/sync-$$-new"
cleanup() {
	git update-ref -d "$OLD_REF" 2>/dev/null || true
	git update-ref -d "$NEW_REF" 2>/dev/null || true
	rm -rf "$TMP_ROOT"
}
trap cleanup EXIT HUP INT TERM

rewrite_snapshot() {
	local source=$1 destination=$2 stream=$TMP_ROOT/stream range
	if git rev-parse --verify "$source^" >/dev/null 2>&1; then
		range="$source^..$source"
	else
		range=$source
	fi
	git fast-export --no-data --reference-excluded-parents --signed-tags=strip \
		"$range" |
		python3 "$REWRITE_TOOL" rewrite "$source" "$destination" >"$stream" ||
		die "path rewriting failed"
	git fast-import --force --quiet <"$stream" || die "snapshot import failed"
	python3 "$REWRITE_TOOL" validate "$destination^{tree}" ||
		die "rewritten snapshot failed collision validation"
}

rewrite_snapshot "$FROM" "$OLD_REF"
rewrite_snapshot "$NEW" "$NEW_REF"
OLD_TREE=$(git rev-parse "$OLD_REF^{tree}")
NEW_TREE=$(git rev-parse "$NEW_REF^{tree}")
OURS_TREE=$(git rev-parse "HEAD^{tree}")
OURS_C=$(git rev-parse HEAD)
old_short=$(git rev-parse --short=12 "$FROM")
new_short=$(git rev-parse --short=12 "$NEW")
KEEP="$NOVELVM_ENGINES $PATCHED_ENGINES"

# --- policy checks and owned-path decisions ---------------------------------

patched_state() {
	local engine=$1 h=0 b=0 t=0
	engine_exists HEAD "$engine" && h=1
	engine_exists "$OLD_TREE" "$engine" && b=1
	engine_exists "$NEW_TREE" "$engine" && t=1
	printf '%s%s%s\n' "$h" "$b" "$t"
}

SEED_ENGINES=
for engine in $PATCHED_ENGINES; do
	case "$(patched_state "$engine")" in
		111) ;;
		110) die4 "upstream removed or renamed patched engine: $engine" ;;
		100) die4 "fork-only content under a patched engine name: $engine" ;;
		101) die4 "name collision: we added $engine and upstream also added it" ;;
		011|001) SEED_ENGINES="$SEED_ENGINES $engine" ;; # re-add via PATCHED_ENGINES opt-in; REVIEW_ENGINES gates it
		010|000) ;;
		*) die "internal: bad patched-engine state for $engine" ;;
	esac
done
SEED_ENGINES=${SEED_ENGINES# }
for engine in $NOVELVM_ENGINES; do
	engine_exists HEAD "$engine" ||
		die4 "config error: fork-only engine $engine is absent from HEAD"
	engine_exists "$NEW_TREE" "$engine" &&
		die4 "upstream now has an engine named like fork-only engine $engine"
done

owned_action() {
	local path=$1 h=0 b=0 t=0
	path_exists HEAD "$path" && h=1
	path_exists "$OLD_TREE" "$path" && b=1
	path_exists "$NEW_TREE" "$path" && t=1
	case "$h$b$t" in
		111) printf 'force\n' ;;
		110) return 4 ;;
		100) printf 'none\n' ;;
		101) printf 'force_collision\n' ;;
		011)
			# The path is missing from HEAD but upstream still has it.  This is
			# expected when its engine is being seeded from upstream (for
			# example the first Director import).  In that case take the
			# upstream file and gate it; otherwise treat it as a deletion we
			# made on purpose and stop.
			case "$path" in
				engines/*/*)
					local engine=${path#engines/}
					engine=${engine%%/*}
					if ! engine_exists HEAD "$engine" && engine_exists "$NEW_TREE" "$engine"; then
						printf 'take\n'
						return 0
					fi
					;;
			esac
			return 4
			;;
		010) printf 'noop\n' ;;
		001) printf 'take\n' ;;
		000) printf 'noop\n' ;;
		*) die "internal: bad owned-path state for $path" ;;
	esac
}
for path in $REVIEW_PATHS; do
	action=$(owned_action "$path") || {
		rc=$?
		[ "$rc" -eq 4 ] &&
			die4 "upstream deleted or renamed a fork-owned path, or we deleted one upstream still changes: $path"
		exit "$rc"
	}
	case "$action" in
		force|force_collision|take|none|noop) ;;
		*) die "internal: bad owned-path action for $path" ;;
	esac
done

force_head_path() {
	local index=$1 path=$2 mode sha
	read -r mode _ sha _ < <(git ls-tree HEAD -- "$path") || die "cannot read HEAD path: $path"
	[ -n "$mode" ] && [ -n "$sha" ] || die "HEAD path is missing: $path"
	GIT_INDEX_FILE=$index git update-index --add --cacheinfo "$mode,$sha,$path" ||
		die "could not force HEAD content into the filtered tree: $path"
}

filter_tree() {
	local source=$1 dest=$2
	local index=$TMP_ROOT/index.$dest engine path action
	GIT_INDEX_FILE=$index git read-tree "$source" || die "could not read tree: $source"
	while IFS= read -r engine; do
		[ -n "$engine" ] || continue
		in_keep "$engine" ||
			GIT_INDEX_FILE=$index git rm -q --cached -r --ignore-unmatch -- "engines/$engine" ||
			die "could not filter engine from tree: $engine"
	done < <(git ls-tree -d --name-only "$source:engines" 2>/dev/null || true)
	if [ "$dest" = base ]; then
		for engine in $SEED_ENGINES; do
			GIT_INDEX_FILE=$index git rm -q --cached -r --ignore-unmatch -- "engines/$engine" ||
				die "could not seed engine from base tree: $engine"
		done
	fi
	for path in $REVIEW_PATHS; do
		action=$(owned_action "$path") || exit $?
		case "$action" in
			force) force_head_path "$index" "$path" ;;
			force_collision)
				if [ "$dest" = theirs ]; then
					force_head_path "$index" "$path"
				fi
				;;
			take)
				# First import: the path must look added by THEIRS, not
				# deleted by us relative to BASE.
				if [ "$dest" = base ]; then
					GIT_INDEX_FILE=$index git rm -q --cached --ignore-unmatch -- "$path" ||
						die "could not seed owned path from base tree: $path"
				fi
				;;
			none|noop) ;;
		esac
	done
	GIT_INDEX_FILE=$index git write-tree || die "could not write filtered tree: $dest"
	rm -f "$index"
}

BASE_TREE=$(filter_tree "$OLD_TREE" base) || exit $?
THEIRS_TREE=$(filter_tree "$NEW_TREE" theirs) || exit $?
BASE_C=$(git -c commit.gpgsign=false commit-tree "$BASE_TREE" -m "novelvm sync base") ||
	die "could not create base commit"
THEIRS_C=$(git -c commit.gpgsign=false commit-tree "$THEIRS_TREE" -m "novelvm sync theirs") ||
	die "could not create theirs commit"

# --- reporting --------------------------------------------------------------

changed_paths=$(git diff --name-only "$OLD_TREE" "$NEW_TREE" | wc -l | tr -d ' ')
msg "upstream: $old_short -> $new_short ($changed_paths changed paths)"
if [ "$changed_paths" -ne 0 ]; then
	msg "engines changed:"
	git diff --name-only "$OLD_TREE" "$NEW_TREE" -- ':(glob)engines/*/**' |
		awk -F/ 'NF > 2 { print $2 }' | sort | uniq -c |
		while read -r count engine; do
			[ -n "$engine" ] && printf '  %s (%s paths)\n' "$engine" "$count"
		done
fi
old_engines=$(git ls-tree -d --name-only "$OLD_TREE:engines" 2>/dev/null | sort)
new_engines=$(git ls-tree -d --name-only "$NEW_TREE:engines" 2>/dev/null | sort)
added_engines=$(comm -13 <(printf '%s\n' "$old_engines") <(printf '%s\n' "$new_engines") | sed '/^$/d')
removed_engines=$(comm -23 <(printf '%s\n' "$old_engines") <(printf '%s\n' "$new_engines") | sed '/^$/d')
[ -z "$added_engines" ] || msg "new upstream engines: $(printf '%s ' $added_engines)"
[ -z "$removed_engines" ] || msg "removed upstream engines: $(printf '%s ' $removed_engines)"
for engine in $(git ls-tree -d --name-only HEAD:engines 2>/dev/null); do
	in_keep "$engine" || msg "fork engine to remove: $engine"
done

print_table_delta() {
	local path=$1
	python3 - "$OLD_TREE" "$NEW_TREE" "$path" <<'PY'
import re
import subprocess
import sys

old_tree, new_tree, path = sys.argv[1:4]

def read(tree):
    try:
        return subprocess.check_output(
            ["git", "show", f"{tree}:{path}"],
            stderr=subprocess.DEVNULL, text=True, errors="replace",
        )
    except subprocess.CalledProcessError:
        return ""

def pairs(text):
    return set(re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"', text))

old = pairs(read(old_tree))
new = pairs(read(new_tree))
for label, items in (("added", new - old), ("removed", old - new)):
    for game_id, title in sorted(items):
        print(f"  {label}: {game_id} — {title}")
PY
}

gate_reasons=()
add_gate() { gate_reasons+=("$1"); }

for engine in $REVIEW_ENGINES; do
	if ! engine_exists HEAD "$engine" && engine_exists "$NEW_TREE" "$engine"; then
		add_gate "new review engine seeded from upstream: $engine"
	fi
	if engine_exists HEAD "$engine" && engine_exists "$OLD_TREE" "$engine"; then
		if ! git diff --quiet "$OLD_TREE" HEAD -- "engines/$engine" &&
			! git diff --quiet "$OLD_TREE" "$NEW_TREE" -- "engines/$engine"; then
			add_gate "review engine changed on both sides: $engine"
		fi
	fi
done
for path in $REVIEW_PATHS; do
	action=$(owned_action "$path") || exit $?
	case "$action" in
		force)
			if ! git diff --quiet "$OLD_TREE" "$NEW_TREE" -- "$path"; then
				add_gate "owned path changed upstream: $path"
			fi
			;;
		force_collision)
			add_gate "owned path collides with a new upstream path: $path"
			;;
		take)
			add_gate "new owned path imported from upstream: $path"
			;;
	esac
done

if [ "$DRY_RUN" -eq 1 ] || [ "$REPORT" -eq 1 ]; then
	merge_tree_out=$(git merge-tree --write-tree --name-only \
		--merge-base="$BASE_C" "$OURS_C" "$THEIRS_C" 2>&1)
	merge_tree_rc=$?
	if [ "$merge_tree_rc" -eq 1 ]; then
		msg "conflicts:"
		printf '%s\n' "$merge_tree_out" | sed -n '2,$p' | sed 's/^/  /'
	elif [ "$merge_tree_rc" -ne 0 ]; then
		printf '%s\n' "$merge_tree_out" >&2
		die "merge preview failed"
	else
		msg "conflicts: none"
	fi
	for reason in "${gate_reasons[@]}"; do
		msg "gate: $reason"
	done
	for path in $REVIEW_PATHS; do
		action=$(owned_action "$path") || exit $?
		case "$action" in
			force|force_collision|take)
				if ! git diff --quiet "$OLD_TREE" "$NEW_TREE" -- "$path"; then
					msg "table delta: $path"
					print_table_delta "$path"
				fi
				;;
		esac
	done
	if [ "$merge_tree_rc" -eq 1 ]; then
		msg "dry run: conflicts; $BRANCH was not changed"
		exit 2
	elif [ "${#gate_reasons[@]}" -gt 0 ]; then
		msg "dry run: review gate; $BRANCH was not changed"
		exit 3
	fi
	msg "dry run: clean; $BRANCH was not changed"
	exit 0
fi

SAFETY_REF="refs/novelvm/safety/sync-$(date -u +%Y%m%dT%H%M%SZ)-$$"
git update-ref "$SAFETY_REF" HEAD || die "could not create safety ref"
msg "safety ref: $SAFETY_REF"

write_pending() {
	local gates=$1
	cat >"$PENDING_FILE" <<EOF
upstream=$NEW
remote=$SCUMMVM_REMOTE
branch=$SCUMMVM_BRANCH
rewritten-tree=$NEW_TREE
head=$(git rev-parse HEAD)
base=$BASE_C
theirs=$THEIRS_C
keep=$KEEP
gates=$gates
EOF
	git update-ref refs/novelvm/pending/base "$BASE_C" || die "could not save pending base"
	git update-ref refs/novelvm/pending/theirs "$THEIRS_C" || die "could not save pending theirs"
}

git -c commit.gpgsign=false merge-recursive "$BASE_C" -- "$OURS_C" "$THEIRS_C"
MERGE_RC=$?

if [ "$MERGE_RC" -eq 1 ]; then
	conflicts=$(git diff --name-only --diff-filter=U)
	write_pending "conflicts"
	printf '%s\n' "$conflicts" | sed 's/^/  conflict: /'
	msg "sync stopped with conflicts; resolve and stage them, then run ./sync-upstream.sh --continue"
	msg "or run ./sync-upstream.sh --abort to discard the merge"
	exit 2
elif [ "$MERGE_RC" -ne 0 ]; then
	die "merge failed with exit code $MERGE_RC"
fi

for engine in $REVIEW_ENGINES; do
	if [ -n "$(git diff --name-only --diff-filter=U -- "engines/$engine")" ]; then
		add_gate "conflict inside review engine: $engine"
	fi
done

if [ "$(git write-tree)" = "$OURS_TREE" ] && [ "${#gate_reasons[@]}" -eq 0 ]; then
	git reset --hard HEAD >/dev/null 2>&1 || true
	msg "already up to date"
	exit 0
fi

if [ "${#gate_reasons[@]}" -gt 0 ]; then
	gates=$(printf '%s; ' "${gate_reasons[@]}")
	write_pending "$gates"
	for reason in "${gate_reasons[@]}"; do
		msg "gate: $reason"
	done
	for path in $REVIEW_PATHS; do
		action=$(owned_action "$path") || exit $?
		case "$action" in
			force|force_collision|take)
				if ! git diff --quiet "$OLD_TREE" "$NEW_TREE" -- "$path"; then
					msg "table delta: $path"
					print_table_delta "$path"
				fi
				;;
		esac
	done
	msg "sync stopped for review; inspect the staged merge, then run ./sync-upstream.sh --continue"
	msg "or run ./sync-upstream.sh --abort to discard it"
	exit 3
fi

for engine in $KEEP; do
	git cat-file -e ":engines/$engine/configure.engine" 2>/dev/null ||
		die "kept engine missing from the merged index: $engine"
	[ -f "engines/$engine/configure.engine" ] ||
		die "kept engine missing from the working tree: $engine"
done
while IFS= read -r engine; do
	[ -n "$engine" ] || continue
	in_keep "$engine" || git rm -r -q -f -- "engines/$engine" ||
		die "could not remove unkept engine: $engine"
done < <(git ls-tree -d --name-only :engines 2>/dev/null || true)
git diff --cached --check || die "merged sync contains whitespace errors"

printf 'upstream=%s\nremote=%s\nbranch=%s\nrewritten-tree=%s\n' \
	"$NEW" "$SCUMMVM_REMOTE" "$SCUMMVM_BRANCH" "$NEW_TREE" >"$SYNC_METADATA_FILE"
git add "$SYNC_METADATA_FILE" || die "could not stage sync metadata"
rm -f .git/MERGE_HEAD .git/MERGE_MSG .git/MERGE_MODE .git/AUTO_MERGE
git commit -m "ALL: Sync with ScummVM rev: $NEW" || die "sync commit failed"
for engine in $KEEP; do
	git cat-file -e "HEAD:engines/$engine/configure.engine" 2>/dev/null ||
		die "kept engine missing from the commit: $engine"
	[ -f "engines/$engine/configure.engine" ] ||
		die "kept engine missing from the working tree: $engine"
done
msg "sync complete: $(git rev-parse --short=12 HEAD)"
