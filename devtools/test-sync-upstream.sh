#!/usr/bin/env bash
#
# test-sync-upstream.sh - integration test for the merge-based upstream sync
#
# ScummVM is the legal property of its developers, whose names
# are too numerous to list here. Please refer to the COPYRIGHT
# file distributed with this source distribution.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Builds a throwaway fork repository and a fake ScummVM upstream in a
# temporary directory, then exercises sync-upstream.sh offline.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
SYNC_SCRIPT=$REPO_ROOT/sync-upstream.sh
REWRITE_TOOL=$REPO_ROOT/devtools/rewrite-upstream-paths.py

for required in "$SYNC_SCRIPT" "$REWRITE_TOOL"; do
	[ -f "$required" ] ||
		{ printf 'missing required file: %s\n' "$required" >&2; exit 1; }
done

TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/novelvm-sync-test.XXXXXX")
trap 'rm -rf "$TMP_ROOT"' EXIT HUP INT TERM

CASE=startup
LOG=
SYNC_RC=0
BASE=
NEW=
UP=
FORK=

fail() {
	printf 'FAIL %s: %s\n' "$CASE" "$*" >&2
	if [ -n "$LOG" ] && [ -f "$LOG" ]; then
		printf -- '--- sync output ---\n' >&2
		cat "$LOG" >&2
		printf -- '--- end output ---\n' >&2
	fi
	exit 1
}

pass() { printf 'PASS %s\n' "$CASE"; }

# --- repository helpers -----------------------------------------------------

init_repo() {
	local dir=$1
	git init -q -b master "$dir"
	git -C "$dir" config user.name "NovelVM Sync Test"
	git -C "$dir" config user.email "sync-test@example.invalid"
	git -C "$dir" config commit.gpgsign false
	git -C "$dir" config tag.gpgsign false
}

write_file() {
	local dir=$1 path=$2
	mkdir -p "$(dirname "$dir/$path")"
	cat >"$dir/$path"
}

upstream_commit() {
	git -C "$UP" add -A
	git -C "$UP" commit -q -m "$1"
	UPSTREAM_SHA=$(git -C "$UP" rev-parse HEAD)
}

fork_commit() {
	git -C "$FORK" add -A
	git -C "$FORK" commit -q -m "$1"
}

# Copy the upstream base snapshot into the fork, drop engines the fork does
# not carry, and add the fork-only smt engine.
fork_from_base() {
	local dir name
	git -C "$UP" archive "$BASE" | tar -x -C "$FORK"
	for dir in "$FORK"/engines/*/; do
		[ -d "$dir" ] || continue
		name=$(basename "$dir")
		case "$name" in
			smt|director|snatcher) ;;
			*) rm -rf "$dir" ;;
		esac
	done
	mkdir -p "$FORK/engines/smt"
	printf 'smt\n' >"$FORK/engines/smt/configure.engine"
}

install_sync() {
	cp "$SYNC_SCRIPT" "$FORK/sync-upstream.sh"
	chmod +x "$FORK/sync-upstream.sh"
	mkdir -p "$FORK/devtools"
	cp "$REWRITE_TOOL" "$FORK/devtools/rewrite-upstream-paths.py"
	git -C "$FORK" remote add scummvm "$UP"
}

write_conf() {
	local novelvm=$1 patched=$2 review=$3 paths=$4
	cat >"$FORK/.novelvm-update.conf" <<EOF
SCUMMVM_REMOTE="scummvm"
SCUMMVM_REMOTE_URL="$UP"
SCUMMVM_BRANCH="master"
REWRITE_REF="refs/rewrites/scummvm-master"
REWRITE_TOOL="devtools/rewrite-upstream-paths.py"
NOVELVM_ENGINES="$novelvm"
PATCHED_ENGINES="$patched"
REVIEW_ENGINES="$review"
REVIEW_PATHS="$paths"
SYNC_METADATA_FILE=".novelvm-sync"
EOF
}

run_cmd() {
	set +e
	(cd "$FORK" && ./sync-upstream.sh "$@") >"$LOG" 2>&1
	SYNC_RC=$?
	set -e
}

run_sync() { run_cmd --from "$BASE" --to "$NEW"; }

# --- assertions -------------------------------------------------------------

expect_rc() {
	[ "$SYNC_RC" -eq "$1" ] ||
		fail "expected exit status $1, got $SYNC_RC"
}

expect_contains() {
	grep -Fq -- "$1" "$LOG" || fail "output does not contain: $1"
}

expect_not_contains() {
	if grep -Fq -- "$1" "$LOG"; then
		fail "output unexpectedly contains: $1"
	fi
}

expect_file_content() {
	local path=$1 expected=$2
	[ -f "$FORK/$path" ] || fail "missing file: $path"
	[ "$(cat "$FORK/$path")" = "$expected" ] ||
		fail "unexpected content in $path: $(cat "$FORK/$path")"
}

expect_file_contains() {
	local path=$1 needle=$2
	[ -f "$FORK/$path" ] || fail "missing file: $path"
	grep -Fq -- "$needle" "$FORK/$path" ||
		fail "$path does not contain: $needle"
}

expect_file_lacks() {
	local path=$1 needle=$2
	if [ -f "$FORK/$path" ] && grep -Fq -- "$needle" "$FORK/$path"; then
		fail "$path unexpectedly contains: $needle"
	fi
}

expect_no_path() {
	[ ! -e "$FORK/$1" ] || fail "unexpected path: $1"
}

expect_head() {
	local got
	got=$(git -C "$FORK" rev-parse HEAD)
	[ "$got" = "$1" ] || fail "HEAD is $got, expected $1"
}

expect_commit_count() {
	local got
	got=$(git -C "$FORK" rev-list --count HEAD)
	[ "$got" -eq "$1" ] || fail "HEAD has $got commits, expected $1"
}

expect_linear_head() {
	local fields
	fields=$(git -C "$FORK" rev-list --parents -n 1 HEAD | wc -w)
	[ "$fields" -eq 2 ] || fail "sync commit is not single-parent"
}

expect_meta_upstream() {
	local got
	got=$(sed -n 's/^upstream=//p' "$FORK/.novelvm-sync" 2>/dev/null || true)
	[ "$got" = "$1" ] || fail ".novelvm-sync upstream is '$got', expected '$1'"
}

expect_pending() {
	[ -f "$FORK/.git/novelvm-sync-pending" ] ||
		fail "expected pending sync state"
}

expect_no_pending() {
	[ ! -e "$FORK/.git/novelvm-sync-pending" ] ||
		fail "pending sync state was left behind"
}

expect_unmerged() {
	local path=$1
	git -C "$FORK" diff --name-only --diff-filter=U | grep -Fxq -- "$path" ||
		fail "path is not unmerged: $path"
}

# --- fixtures ---------------------------------------------------------------

# The upstream base snapshot shared by most cases.  engines/alpha stands in for
# every upstream engine the fork filters out.
base_upstream() {
	write_file "$UP" common/shared.cpp <<'EOF'
// shared code
EOF
	write_file "$UP" common/conflict.cpp <<'EOF'
// conflict target, version 1
EOF
	write_file "$UP" engines/director/configure.engine <<'EOF'
director
EOF
	write_file "$UP" engines/director/director.cpp <<'EOF'
// director engine
EOF
	write_file "$UP" engines/director/detection_tables.h <<'EOF'
static const PlainGameDescriptor directorGames[] = {
	{ "dir1", "Director Game One" },
	{ 0, 0 }
};
EOF
	write_file "$UP" engines/snatcher/configure.engine <<'EOF'
snatcher
EOF
	write_file "$UP" engines/snatcher/snatcher.cpp <<'EOF'
// snatcher engine
EOF
	write_file "$UP" engines/alpha/configure.engine <<'EOF'
alpha
EOF
}

begin_case() {
	CASE=$1
	local case_dir=$TMP_ROOT/$CASE
	UP=$case_dir/scummvm/scummvm
	FORK=$case_dir/fork
	LOG=$case_dir/sync.log
	mkdir -p "$case_dir/scummvm"
	init_repo "$UP"
	init_repo "$FORK"
}

# Standard configuration: one fork-only engine, two patched engines, and the
# Director detection table gated for review.
default_conf() {
	write_conf "smt" "director snatcher" "director" \
		"engines/director/detection_tables.h"
}

# --- 1. clean sync ----------------------------------------------------------

case_clean_sync() {
	local before

	begin_case clean-sync
	base_upstream
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	write_file "$UP" common/added.cpp <<'EOF'
// added upstream
EOF
	write_file "$UP" engines/snatcher/snatcher.cpp <<'EOF'
// snatcher engine, upstream update
EOF
	upstream_commit "upstream new"
	NEW=$UPSTREAM_SHA

	fork_from_base
	write_file "$FORK" common/shared.cpp <<'EOF'
// shared code, fork edit
EOF
	write_file "$FORK" common/local.cpp <<'EOF'
// fork only
EOF
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-list --count HEAD)
	run_sync
	expect_rc 0
	expect_contains "sync complete"
	expect_file_content common/shared.cpp "// shared code, fork edit"
	expect_file_content common/local.cpp "// fork only"
	expect_file_content common/added.cpp "// added upstream"
	expect_file_content engines/snatcher/snatcher.cpp \
		"// snatcher engine, upstream update"
	expect_no_path engines/alpha
	expect_meta_upstream "$NEW"
	expect_commit_count $((before + 1))
	expect_linear_head
	expect_no_pending
	pass
}

# --- 2. shared-file conflict ------------------------------------------------

case_shared_conflict() {
	local before

	begin_case shared-file-conflict
	base_upstream
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	write_file "$UP" common/conflict.cpp <<'EOF'
// conflict target, upstream edit
EOF
	upstream_commit "upstream new"
	NEW=$UPSTREAM_SHA

	fork_from_base
	write_file "$FORK" common/conflict.cpp <<'EOF'
// conflict target, fork edit
EOF
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-parse HEAD)
	run_sync
	expect_rc 2
	expect_contains "conflict: common/conflict.cpp"
	expect_unmerged common/conflict.cpp
	expect_pending

	run_cmd --continue
	expect_rc 1
	expect_contains "unmerged entries remain"

	run_cmd --abort
	expect_rc 0
	expect_head "$before"
	expect_no_pending
	pass
}

# --- 3. conflict inside a review engine -------------------------------------

case_engine_conflict() {
	local before

	begin_case review-engine-conflict
	base_upstream
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	write_file "$UP" engines/director/director.cpp <<'EOF'
// director engine, upstream edit
EOF
	upstream_commit "upstream new"
	NEW=$UPSTREAM_SHA

	fork_from_base
	write_file "$FORK" engines/director/director.cpp <<'EOF'
// director engine, fork edit
EOF
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-parse HEAD)
	run_sync
	expect_rc 2
	expect_contains "conflict: engines/director/director.cpp"
	expect_unmerged engines/director/director.cpp
	expect_pending

	run_cmd --abort
	expect_rc 0
	expect_head "$before"
	expect_no_pending
	pass
}

# --- 4. review gate for a changed owned path --------------------------------

case_review_gate() {
	local before

	begin_case review-gate
	base_upstream
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	write_file "$UP" engines/director/detection_tables.h <<'EOF'
static const PlainGameDescriptor directorGames[] = {
	{ "dir1", "Director Game One" },
	{ "dir2", "Director Game Two" },
	{ 0, 0 }
};
EOF
	upstream_commit "upstream new"
	NEW=$UPSTREAM_SHA

	fork_from_base
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-list --count HEAD)
	run_sync
	expect_rc 3
	expect_contains \
		"gate: owned path changed upstream: engines/director/detection_tables.h"
	expect_contains "added: dir2"
	expect_contains "Director Game Two"
	expect_pending
	expect_commit_count "$before"

	run_cmd --continue
	expect_rc 0
	expect_contains "sync complete"
	expect_commit_count $((before + 1))
	expect_linear_head
	expect_meta_upstream "$NEW"
	expect_no_pending
	# Our table wins in the merge; the upstream delta is reviewed separately.
	expect_file_contains engines/director/detection_tables.h "dir1"
	expect_file_lacks engines/director/detection_tables.h "dir2"
	pass
}

# --- 5. new-engine seeding --------------------------------------------------

case_new_engine_seeding() {
	local before

	begin_case new-engine-seeding
	base_upstream
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	write_file "$UP" engines/director/director.cpp <<'EOF'
// director engine, upstream update
EOF
	upstream_commit "upstream new"
	NEW=$UPSTREAM_SHA

	fork_from_base
	rm -rf "$FORK/engines/director" "$FORK/engines/snatcher"
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-list --count HEAD)
	run_sync
	expect_rc 3
	expect_contains "gate: new review engine seeded from upstream: director"
	expect_not_contains "gate: new review engine seeded from upstream: snatcher"
	expect_contains "gate: new owned path imported from upstream"
	expect_commit_count "$before"

	run_cmd --continue
	expect_rc 0
	expect_file_content engines/snatcher/configure.engine "snatcher"
	expect_file_content engines/director/configure.engine "director"
	expect_commit_count $((before + 1))
	expect_linear_head
	pass
}

# --- 6. upstream removes a patched engine -----------------------------------

case_patched_engine_deleted() {
	local before

	begin_case patched-engine-deleted
	base_upstream
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	rm -rf "$UP/engines/snatcher"
	upstream_commit "upstream removes snatcher"
	NEW=$UPSTREAM_SHA

	fork_from_base
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-parse HEAD)
	run_sync
	expect_rc 4
	expect_contains "upstream removed or renamed patched engine: snatcher"
	expect_head "$before"
	expect_no_pending
	pass
}

# --- 7. name collision ------------------------------------------------------

case_name_collision() {
	local before

	begin_case name-collision
	base_upstream
	rm -rf "$UP/engines/snatcher"
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	write_file "$UP" engines/snatcher/configure.engine <<'EOF'
snatcher upstream
EOF
	write_file "$UP" engines/snatcher/snatcher.cpp <<'EOF'
// upstream snatcher
EOF
	upstream_commit "upstream adds snatcher"
	NEW=$UPSTREAM_SHA

	fork_from_base
	write_file "$FORK" engines/snatcher/configure.engine <<'EOF'
snatcher fork
EOF
	write_file "$FORK" engines/snatcher/snatcher.cpp <<'EOF'
// fork snatcher
EOF
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-parse HEAD)
	run_sync
	expect_rc 4
	expect_contains "name collision: we added snatcher and upstream also added it"
	expect_head "$before"
	expect_no_pending
	pass
}

# --- 8. first import of an owned path ---------------------------------------

case_owned_path_first_import() {
	local before

	begin_case owned-path-first-import
	base_upstream
	rm -f "$UP/engines/director/detection_tables.h"
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	write_file "$UP" engines/director/detection_tables.h <<'EOF'
static const PlainGameDescriptor directorGames[] = {
	{ "dir1", "Director Game One" },
	{ 0, 0 }
};
EOF
	upstream_commit "upstream adds director table"
	NEW=$UPSTREAM_SHA

	fork_from_base
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-list --count HEAD)
	run_sync
	expect_rc 3
	expect_contains \
		"gate: new owned path imported from upstream: engines/director/detection_tables.h"
	expect_not_contains "conflict:"
	expect_commit_count "$before"

	run_cmd --continue
	expect_rc 0
	expect_file_contains engines/director/detection_tables.h "dir1"
	expect_commit_count $((before + 1))
	expect_linear_head
	pass
}

# --- 9. fork deleted an owned path upstream still changes -------------------

case_fork_deleted_owned_path() {
	local before

	begin_case fork-deleted-owned-path
	base_upstream
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	write_file "$UP" engines/director/detection_tables.h <<'EOF'
static const PlainGameDescriptor directorGames[] = {
	{ "dir1", "Director Game One" },
	{ "dir2", "Director Game Two" },
	{ 0, 0 }
};
EOF
	upstream_commit "upstream changes director table"
	NEW=$UPSTREAM_SHA

	fork_from_base
	rm -f "$FORK/engines/director/detection_tables.h"
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-parse HEAD)
	run_sync
	expect_rc 4
	expect_contains "policy stop:"
	expect_head "$before"
	expect_no_pending
	pass
}

# --- 10. no-op --------------------------------------------------------------

case_noop() {
	local before meta_before

	begin_case no-op
	base_upstream
	upstream_commit "upstream base"
	BASE=$UPSTREAM_SHA

	git -C "$UP" commit -q --allow-empty -m "upstream no-op"
	NEW=$(git -C "$UP" rev-parse HEAD)

	fork_from_base
	write_file "$FORK" .novelvm-sync <<EOF
upstream=$BASE
remote=scummvm
branch=master
rewritten-tree=0000000000000000000000000000000000000000
EOF
	fork_commit "fork base"
	install_sync
	default_conf

	before=$(git -C "$FORK" rev-parse HEAD)
	meta_before=$(cat "$FORK/.novelvm-sync")
	run_sync
	expect_rc 0
	expect_contains "already up to date"
	expect_head "$before"
	expect_commit_count 1
	[ "$(cat "$FORK/.novelvm-sync")" = "$meta_before" ] ||
		fail ".novelvm-sync changed during a no-op sync"
	expect_no_pending
	pass
}

# --- run --------------------------------------------------------------------

case_clean_sync
case_shared_conflict
case_engine_conflict
case_review_gate
case_new_engine_seeding
case_patched_engine_deleted
case_name_collision
case_owned_path_first_import
case_fork_deleted_owned_path
case_noop

printf 'all sync-upstream tests passed\n'
