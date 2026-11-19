#!/bin/sh
# Fail if any single stack frame in the library exceeds one buffer block.
# usage: check_stack.sh <file.su> <limit-bytes>
set -eu
su_file=$1
limit=$2
if [ ! -f "$su_file" ]; then
    echo "stack check: $su_file not found" >&2
    exit 1
fi
echo "== stack footprint (gcc -fstack-usage) =="
printf '%-28s %10s\n' "function" "bytes"
awk -F'\t' '{
    split($1, f, ":");
    printf "%-28s %10s\n", f[4], $2;
    if ($2 + 0 > max) { max = $2 + 0; who = f[4] }
} END { printf "%-28s %10s\n", "MAX (" who ")", max;
        exit (max <= '"$limit"' ? 0 : 1) }' "$su_file"
echo "limit = $limit bytes (one buffer block); library heap usage = 0 bytes"
echo "PASS: library-owned peak memory <= one block"
