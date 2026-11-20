"""Benchmark brute-force nearest_index vs the kd-tree PaletteIndex.

Run from the project root:

    python3 -I tests/bench_palette.py

Prints, for palettes of 64 / 200 / 1024 colours:
  * correctness cross-check against the brute-force implementation;
  * build time (kd-tree) and per-colour add() time;
  * per-query time and nodes visited;
  * the observed scaling of average nodes visited.
"""

import random
import sys
import time

sys.path.insert(0, "src")

import palette

SIZES = (64, 200, 1024)
QUERY_ROUNDS = 5          # timing rounds
RNG_SEED = 20261120
PROBES = 2000


def _time_per_call(fn, probes, rounds):
    best = None
    for _ in range(rounds):
        t0 = time.perf_counter()
        for p in probes:
            fn(p)
        dt = time.perf_counter() - t0
        best = dt if best is None else min(best, dt)
    return best / len(probes)


def main():
    rng = random.Random(RNG_SEED)
    print("palette  correct  build(ms)  add(ms)   brute us/query"
          "  kd us/query  speedup  visited/query  brute n")
    prev_log = None
    for n in SIZES:
        pal = [(rng.randrange(256), rng.randrange(256), rng.randrange(256))
               for _ in range(n)]
        probes = [(rng.randrange(256), rng.randrange(256), rng.randrange(256))
                  for _ in range(PROBES)]

        # Correctness: kd-tree must agree with the brute-force reference.
        idx = palette.PaletteIndex(pal)
        mismatches = sum(
            1 for p in probes if idx.query(p) != palette.nearest_index(p, pal)
        )

        # Build cost.
        t0 = time.perf_counter()
        palette.PaletteIndex(pal)
        build_ms = (time.perf_counter() - t0) * 1000.0

        # Single-colour add() = append + full rebuild.
        extra = (rng.randrange(256), rng.randrange(256), rng.randrange(256))
        t0 = time.perf_counter()
        idx.add(extra)
        add_ms = (time.perf_counter() - t0) * 1000.0

        # Query cost, best of several rounds.
        brute_s = _time_per_call(
            lambda p: palette.nearest_index(p, pal), probes, QUERY_ROUNDS
        )
        kd_s = _time_per_call(idx.query, probes, QUERY_ROUNDS)

        # Real traversal work: nodes touched per query.
        visits = 0
        for p in probes:
            _, _, v = palette._nn_search(idx._root, idx._palette, p)
            visits += v
        avg_visited = visits / len(probes)

        import math
        log_ratio = ""
        if prev_log is not None:
            log_ratio = "x%.2f vs log2 ratio %.2f" % (
                avg_visited / prev_log[0],
                (avg_visited / prev_log[0]) /
                (math.log(n) / math.log(prev_log[1])),
            )
        prev_log = (avg_visited, n)

        print("%7d  %7s  %9.3f  %7.3f  %13.3f  %11.3f  %7.2f  %13.2f  %7d  %s"
              % (n, "OK" if mismatches == 0 else "FAIL(%d)" % mismatches,
                 build_ms, add_ms, brute_s * 1e6, kd_s * 1e6,
                 brute_s / kd_s, avg_visited, n, log_ratio))

    print()
    print("Complexity (k=3 fixed):")
    print("  build/rebuild/add : O(n log^2 n) time, O(n) space "
          "(per-node median sort)")
    print("  query             : O(log n) average nodes visited, "
          "O(n) worst case")
    print("  brute-force query : O(n) time, O(1) extra space")


if __name__ == "__main__":
    main()
