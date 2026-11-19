#!/usr/bin/env bash
# Prove that adding mini-audio-downmix did not change any other project.
#
# Method: extract the committed tree (HEAD) into a scratch directory,
# build and run each pre-existing project there, then do the same in the
# working tree, and compare
#   (a) sha256 of every tracked source file outside mini-audio-downmix
#   (b) the complete stdout of every project's `make test`
#
# Usage: scripts/check_regression.sh [results-dir]
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$ROOT/../.." && pwd)
OUT=${1:-"$ROOT/results/regression"}
PROJECTS="mini-audio-fifo mini-bitplane-pack mini-grid-cull mini-palette-dither mini-tone-lut"

mkdir -p "$OUT" || { echo "FAIL cannot create $OUT"; exit 2; }

# Create the scratch tree on the repository's volume. /tmp may be small
# (or full); a failed scratch directory must abort rather than let a
# later `cd` silently fall back to the current project.
SCRATCH_BASE=${SCRATCH_BASE:-$REPO/.scratch-regression}
mkdir -p "$SCRATCH_BASE" || { echo "FAIL cannot create $SCRATCH_BASE"; exit 2; }
WORK=$(mktemp -d "$SCRATCH_BASE/run.XXXXXX") ||
    { echo "FAIL mktemp under $SCRATCH_BASE"; exit 2; }
trap 'rm -rf "$WORK"' EXIT
echo "scratch: $WORK"

die() { echo "FAIL $*" >&2; exit 2; }
status=0

echo "== baseline: committed tree $(git -C "$REPO" rev-parse --short HEAD)"
mkdir -p "$WORK/base" || die "mkdir base"
git -C "$REPO" archive HEAD | tar -x -C "$WORK/base" || die "archive extract"
for p in $PROJECTS; do
    [ -f "$WORK/base/projects/$p/Makefile" ] ||
        die "baseline missing $p/Makefile"
done

: > "$OUT/source_sha256_base.txt"
: > "$OUT/source_sha256_work.txt"
: > "$OUT/tests_base.txt"
: > "$OUT/tests_work.txt"

for p in $PROJECTS; do
    # (a) source hashes, excluding build products and caches
    ( cd "$WORK/base" && find "projects/$p" -type f \
        -not -path '*/build/*' -not -path '*__pycache__*' -print0 |
        sort -z | xargs -0 sha256sum ) >> "$OUT/source_sha256_base.txt"
    ( cd "$REPO" && find "projects/$p" -type f \
        -not -path '*/build/*' -not -path '*__pycache__*' -print0 |
        sort -z | xargs -0 sha256sum ) >> "$OUT/source_sha256_work.txt"

    # (b) full test output, from a verified directory
    echo "=== $p" >> "$OUT/tests_base.txt"
    ( cd "$WORK/base/projects/$p" || exit 99
      make -s clean >/dev/null 2>&1
      make test 2>&1 ) >> "$OUT/tests_base.txt"
    echo "=== $p" >> "$OUT/tests_work.txt"
    ( cd "$REPO/projects/$p" || exit 99
      make -s clean >/dev/null 2>&1
      make test 2>&1 ) >> "$OUT/tests_work.txt"
done

# Guard against the original silent-fallback failure: both files must
# contain a test result line for every project.
for f in "$OUT/tests_base.txt" "$OUT/tests_work.txt"; do
    if [ "$(grep -cE '^[0-9]+ checks' "$f")" -ne 5 ]; then
        echo "FAIL $f does not contain five test summaries"
        status=1
    fi
done
if diff -q "$OUT/source_sha256_base.txt" "$OUT/source_sha256_work.txt" >/dev/null; then
    echo "PASS source: every file of the five existing projects is byte-identical"
else
    echo "FAIL source: files differ"
    diff "$OUT/source_sha256_base.txt" "$OUT/source_sha256_work.txt"
    status=1
fi

if diff -q "$OUT/tests_base.txt" "$OUT/tests_work.txt" >/dev/null; then
    echo "PASS tests: identical stdout from all five projects"
    grep -E '^[0-9]+ checks' "$OUT/tests_work.txt" | sed 's/^/       /'
else
    echo "FAIL tests: output differs"
    diff "$OUT/tests_base.txt" "$OUT/tests_work.txt"
    status=1
fi

# Record the summary the report quotes.
{
    echo "baseline_commit=$(git -C "$REPO" rev-parse HEAD)"
    echo "result=$([ $status -eq 0 ] && echo UNCHANGED || echo CHANGED)"
    echo "projects=$PROJECTS"
} > "$OUT/summary.txt"

cat "$OUT/summary.txt"
exit $status
