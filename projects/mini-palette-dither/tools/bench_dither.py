"""Deterministic benchmark: 64- and 256-slot palettes.

For each configuration it reports, with deduplication on and off:

* palette slots before / after and the number of slots saved
* the dither error bound and the error actually measured against the source
  pixels (max and mean squared RGB error)
* wall time for dedupe + remap + dither

The baseline is hand-computed.  Each case's palette is laid out as a known
number of *base* colours, *duplicate* slots and *unused* slots, so the
expected compacted size and the expected old -> new index table are written
out arithmetically below (``expected_remap``), not derived by the code under
test.  The benchmark asserts that the measured remap matches that hand table
exactly.

Usage:  python3 tools/bench_dither.py [--json out.json]

Pixels come from a deterministic generator, so every column except the
millisecond timings is reproducible.
"""

import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

import dither
import palette

IMAGE_W = 96
IMAGE_H = 64
NPIX = IMAGE_W * IMAGE_H


class Lcg:
    """Tiny deterministic generator; no global random state involved."""

    def __init__(self, seed):
        self.state = seed & 0x7FFFFFFF

    def next(self):
        self.state = (1103515245 * self.state + 12345) & 0x7FFFFFFF
        return self.state & 0xFF


def _grey_ramp(cells, lo, hi):
    """``cells`` distinct greys in ``[lo, hi)``."""
    span = hi - lo
    if span < cells:
        raise ValueError("range of %d greys cannot hold %d" % (span, cells))
    return [(lo + i * span // cells,) * 3 for i in range(cells)]


def _filler(cursor, taken):
    """Next deterministic unique colour not in *taken*."""
    while True:
        cursor += 1
        candidate = (cursor * 37 % 256, cursor * 91 % 256, cursor * 151 % 256)
        if candidate not in taken:
            taken.add(candidate)
            return candidate, cursor


def build_case(name, size, base_n, dup_n, unused_n, seed):
    """Palette of exactly *size* slots with a hand-known defect layout.

    Layout: ``base_n`` used colours (grey ramp), ``dup_n`` copies of earlier
    base colours, ``unused_n`` unique colours no pixel will use.

    The usage histogram is hand-written rather than derived from the image:
    base slots get 10 hits, duplicate slots 3 (an old asset may have split
    usage across its duplicate slots), unused slots 0.  Hand-computed
    compaction: base slots become 0..base_n-1; duplicate slot ``base_n + j``
    maps to ``j % base_n``; every unused slot maps to -1.
    """
    assert base_n + dup_n + unused_n == size
    base = _grey_ramp(base_n, 0, 241)
    dup = [base[j % base_n] for j in range(dup_n)]
    taken = set(base)
    unused = []
    cursor = 0
    while len(unused) < unused_n:
        colour, cursor = _filler(cursor, taken)
        unused.append(colour)
    entries = base + dup + unused

    hand_usage = [10] * base_n + [3] * dup_n + [0] * unused_n

    expected_remap = (
        list(range(base_n))
        + [j % base_n for j in range(dup_n)]
        + [-1] * unused_n
    )
    assert len(expected_remap) == size

    # Grey gradient: every level in 0..255 appears, with a small deterministic
    # jitter so intermediate values exist.  Dithering can only pay off where
    # a source value falls between two palette entries, which the gradient
    # guarantees for both a 64- and a 256-slot palette.  Each row starts at a
    # different phase so the image is not a single repeated column pattern.
    rng = Lcg(seed)
    pixels = []
    for i in range(NPIX):
        y, x = divmod(i, IMAGE_W)
        v = ((x * 256) // IMAGE_W + y) % 256
        jitter = rng.next() % 3 - 1
        pixels.append((min(255, max(0, v + jitter)),) * 3)

    return {
        "name": name,
        "size": size,
        "original": entries,
        "pixels": pixels,
        "expected_size": base_n,
        "expected_saved": dup_n + unused_n,
        "expected_remap": expected_remap,
        "usage": hand_usage,
    }


def _errors(pixels, indices, entries, width=IMAGE_W, block=8):
    """Per-pixel and per-block errors, both squared RGB.

    Per-pixel error is what the dither bound constrains; block mean error is
    what dithering is *for*: averaging over an 8x8 block, the diffused result
    tracks the source mean more closely than nearest-colour snapping does.
    """
    total = 0
    worst = 0
    for p, i in zip(pixels, indices):
        c = entries[i]
        d = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2
        total += d
        if d > worst:
            worst = d

    height = len(pixels) // width
    block_total = 0
    block_n = 0
    for by in range(0, height, block):
        for bx in range(0, width, block):
            cells = [
                (y, x)
                for y in range(by, min(by + block, height))
                for x in range(bx, min(bx + block, width))
            ]
            for k in range(3):
                src = sum(pixels[y * width + x][k] for y, x in cells)
                dst = sum(entries[indices[y * width + x]][k] for y, x in cells)
                block_total += (src - dst) ** 2
                block_n += 1
    return worst, total / len(pixels), block_total / block_n


def _run(entries, pixels, counts, enabled):
    t0 = time.perf_counter()
    mapping = palette.dedupe_palette(entries, counts, enabled=enabled)
    indices, stats = dither.dither(
        pixels,
        IMAGE_W,
        mapping.palette,
        factor=dither.OVERSHOOT_FACTOR if enabled else 0.0,
    )
    elapsed = time.perf_counter() - t0
    worst, mean, block = _errors(pixels, indices, mapping.palette)
    stats["max_error"] = worst
    stats["mean_error"] = mean
    stats["block_error"] = block
    stats["slots"] = len(mapping.palette)
    return mapping, indices, stats, elapsed


def main(argv):
    out_json = None
    if "--json" in argv:
        out_json = argv[argv.index("--json") + 1]

    # 64 slots: 52 base + 7 duplicate slots + 5 unused.
    # 256 slots: 200 base + 36 duplicate slots + 20 unused.
    specs = [
        ("64", 64, 52, 7, 5, 1),
        ("256", 256, 200, 36, 20, 2),
    ]
    if "--quick" in argv:
        specs = specs[:1]
    rows = []
    for name, size, base_n, dup_n, unused_n, seed in specs:
        case = build_case(name, size, base_n, dup_n, unused_n, seed)
        entries = case["original"]
        counts = case["usage"]

        on_map, on_idx, on_stats, on_time = _run(
            entries, case["pixels"], counts, enabled=True
        )
        off_map, off_idx, off_stats, off_time = _run(
            entries, case["pixels"], counts, enabled=False
        )

        # Dedupe off must reproduce the historical nearest-colour map exactly.
        assert off_idx == palette.map_nearest(case["pixels"], entries)
        assert off_map.palette == entries and off_map.remap == list(range(size))

        # Dedupe on must match the hand-computed mapping.
        assert on_map.remap == case["expected_remap"], (
            on_map.remap,
            case["expected_remap"],
        )
        assert len(on_map.palette) == case["expected_size"]
        assert on_map.saved_slots == case["expected_saved"]
        assert on_map.usage == [
            sum(counts[old] for old, new in enumerate(on_map.remap) if new == i)
            for i in range(len(on_map.palette))
        ]
        # The derived bound must hold for the computed errors.
        assert on_stats["max_error"] <= on_stats["error_bound"], on_stats
        # Both paths must agree on the error bound rule they quote.
        assert on_stats["within_bound"] and off_stats["within_bound"]

        rows.append(
            {
                "case": name,
                "slots_before": size,
                "slots_off": off_stats["slots"],
                "slots_on": on_stats["slots"],
                "saved": on_map.saved_slots,
                "saved_hand": case["expected_saved"],
                "bound_on": on_stats["error_bound"],
                "max_error_on": on_stats["max_error"],
                "mean_error_on": on_stats["mean_error"],
                "max_error_off": off_stats["max_error"],
                "mean_error_off": off_stats["mean_error"],
                "block_error_on": on_stats["block_error"],
                "block_error_off": off_stats["block_error"],
                "ms_on": on_time * 1000.0,
                "ms_off": off_time * 1000.0,
            }
        )

    columns = (
        ("case", 4, "%s"),
        ("before", 6, "%d"),
        ("off", 5, "%d"),
        ("on", 5, "%d"),
        ("saved", 5, "%d"),
        ("hand", 4, "%d"),
        ("bound", 10, "%.1f"),
        ("max_on", 6, "%d"),
        ("max_off", 7, "%d"),
        ("mean_on", 7, "%.2f"),
        ("mean_off", 8, "%.2f"),
        ("blk_on", 8, "%.0f"),
        ("blk_off", 8, "%.0f"),
        ("ms_on", 7, "%.1f"),
        ("ms_off", 7, "%.1f"),
    )
    keys = (
        "case",
        "slots_before",
        "slots_off",
        "slots_on",
        "saved",
        "saved_hand",
        "bound_on",
        "max_error_on",
        "max_error_off",
        "mean_error_on",
        "mean_error_off",
        "block_error_on",
        "block_error_off",
        "ms_on",
        "ms_off",
    )
    line = " ".join(("%" + str(w) + f[1:]) for _, w, f in columns)
    header = " ".join(("%" + str(w) + "s") % title for title, w, _ in columns)
    print(header)
    for row in rows:
        print(line % tuple(row[k] for k in keys))
    print()
    print("before/off/on: slots with dedupe off and on; saved vs hand count.")
    print("max/mean: squared RGB error vs source pixels; bound: derived limit (on).")
    print("blk: mean squared error of 8x8 block means; dithering works on this.")
    print("remap matched the hand-written table for every case.")

    if out_json:
        with open(out_json, "w", encoding="utf-8") as fh:
            json.dump(rows, fh, indent=2)
        print("\nwrote", out_json)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
