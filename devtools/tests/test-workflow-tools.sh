#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)

help=$("$ROOT/sync-upstream.sh" --help)
printf '%s\n' "$help" | grep -F -- '--dry-run'
printf '%s\n' "$help" | grep -F -- '--from'

printf '%s\n' 'workflow tool option checks passed'
