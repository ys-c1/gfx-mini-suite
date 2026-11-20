#!/bin/sh
# Regression proof: the five pre-existing projects are unchanged.
#
# Extracts the base commit (the repository's initial import) into a
# scratch directory, rebuilds and tests the five projects that existed
# before this one, and compares both full test output and source hashes
# against the working tree.  Only projects/mini-audio-decim and one row
# of the suite README are allowed to differ.
set -eu

BASE=${BASE:-da546e9}
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT="$HERE/results/regression"
TMP="${TMPDIR:-/tmp}/decim-regression-$$"
mkdir -p "$OUT"
rm -rf "$TMP"
mkdir -p "$TMP"

PROJECTS="mini-grid-cull mini-bitplane-pack mini-tone-lut mini-palette-dither mini-audio-fifo"

git -C "$ROOT" archive "$BASE" | tar -x -C "$TMP"

hash_tree() {
    # $1 = directory containing projects/
    (cd "$1" && find projects -type f \( -name '*.c' -o -name '*.h' \
        -o -name '*.cpp' -o -name '*.py' -o -name 'Makefile' \
        -o -name '*.md' \) | sort | xargs sha256sum)
}

hash_tree "$TMP" > "$OUT/source_sha256_base.txt"
hash_tree "$ROOT" | grep -v 'projects/mini-audio-decim/' \
    > "$OUT/source_sha256_work.txt"

status=0

# Source comparison: the new project cannot exist in the base, so its
# rows are removed from the working hashes before the diff.
if diff -u "$OUT/source_sha256_base.txt" "$OUT/source_sha256_work.txt" \
        > "$OUT/source_diff.txt" 2>&1; then
    echo "sources: UNCHANGED" > "$OUT/summary.txt"
else
    echo "sources: CHANGED (see source_diff.txt)" > "$OUT/summary.txt"
    status=1
fi

: > "$OUT/tests_base.txt"
: > "$OUT/tests_work.txt"
for p in $PROJECTS; do
    ( cd "$TMP/projects/$p" && make -s clean >/dev/null 2>&1 || true
      cd "$TMP/projects/$p" && make test 2>&1 ) \
        | sed "s|$TMP|@BASE@|g" >> "$OUT/tests_base.txt"
    ( cd "$ROOT/projects/$p" && make -s clean >/dev/null 2>&1 || true
      cd "$ROOT/projects/$p" && make test 2>&1 ) \
        | sed "s|$ROOT|@BASE@|g" >> "$OUT/tests_work.txt"
done

if diff -u "$OUT/tests_base.txt" "$OUT/tests_work.txt" \
        > "$OUT/tests_diff.txt" 2>&1; then
    echo "tests: UNCHANGED ($(wc -l < "$OUT/tests_base.txt") lines)" \
        >> "$OUT/summary.txt"
else
    echo "tests: CHANGED (see tests_diff.txt)" >> "$OUT/summary.txt"
    status=1
fi

# The only allowed edit outside this project is the suite README row.
readme_delta=$(git -C "$ROOT" diff --stat "$BASE" -- README.md | tail -1)
echo "suite README: ${readme_delta:-no change}" >> "$OUT/summary.txt"

rm -rf "$TMP"
cat "$OUT/summary.txt"
exit $status
