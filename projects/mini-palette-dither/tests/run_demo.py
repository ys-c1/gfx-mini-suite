"""Run the 256 -> 32 selection on three generated images.

For every image this prints the error of our selection, the error of the two
baselines, the source indices / RGB colors we selected, and the search time.
It also writes `before_*.png` (mapped with the full table) and
`after_*.png` (mapped with the selected 32 colors) next to the report, and
checks that the full-table mapping is unchanged.

Usage:
    python3 tests/run_demo.py [--out DIR]
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

import numpy as np

import demo_images
import subset
from palette import map_nearest


def source_palette():
    """A deterministic 256-color table: 6x6x6 cube + 40-step gray ramp."""
    cube = [(r * 51, g * 51, b * 51)
            for r in range(6) for g in range(6) for b in range(6)]
    grays = [(5 * k, 5 * k, 5 * k) for k in range(1, 41)]
    table = cube + grays
    assert len(table) == 256 and len(set(table)) == 256
    return table


def timed_select(palette, k, weights, repeats=3):
    """Return (indices, best_ms) and confirm repeated runs agree."""
    results = []
    times = []
    for _ in range(repeats):
        t0 = time.perf_counter()
        results.append(subset.select_palette(palette, k, weights))
        times.append((time.perf_counter() - t0) * 1000.0)
    assert all(r == results[0] for r in results), "selection is not deterministic"
    return results[0], min(times)


def main(argv):
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "demo_out")
    if "--out" in argv:
        out_dir = os.path.abspath(argv[argv.index("--out") + 1])
    os.makedirs(out_dir, exist_ok=True)

    palette = source_palette()
    k = 32

    print("source palette: %d colors, selecting k=%d" % (len(palette), k))
    print()
    header = ("image      pixels  used  |  ours      greedy    median-cut"
              "  | ours/best  search-ms  | full map")
    print(header)
    print("-" * len(header))

    rows = []
    for name, img in demo_images.build_images():
        pixels = img.reshape(-1, 3)
        weights = subset.histogram(pixels, palette)

        ours, ms = timed_select(palette, k, weights)
        report = subset.compare_methods(palette, k, weights)
        best = report["best_baseline"]
        ratio = report["ratio"] if best else 1.0

        demo_images.write_png(os.path.join(out_dir, "before_%s.png" % name), img)
        # The "after" image is for the eye only: each pixel takes its nearest
        # selected color.  The library's full-table mapping is untouched.
        selected = np.asarray([palette[i] for i in ours], dtype=np.int64)
        diff = pixels[:, None, :].astype(np.int64) - selected[None, :, :]
        d = (diff * diff).sum(axis=-1)
        after_img = selected[d.argmin(axis=1)].reshape(img.shape).astype(np.uint8)
        demo_images.write_png(os.path.join(out_dir, "after_%s.png" % name), after_img)

        # The full-table mapping must be unchanged.  Proven here on the
        # actual image: every pixel is mapped with the whole table, and that
        # mapping must equal what the project's own mapper returns.
        full_idx = subset.map_nearest_rows(pixels, palette)
        full_ok = full_idx.tolist() == map_nearest(
            [tuple(int(v) for v in p) for p in pixels], palette)

        used = int((weights > 0).sum())
        print("%-9s %6d %5d  | %9d %9d %9d  |   %8.4f  %8.1f  | %s"
              % (name, len(pixels), used, report["ours"]["error"],
                 report["greedy"]["error"], report["median-cut"]["error"],
                 ratio, ms, "unchanged" if full_ok else "CHANGED"))
        rows.append((name, report, ours, ms))

    print()
    for name, report, ours, _ in rows:
        print("%s: selected %d colors" % (name, len(ours)))
        print("  indices: %s" % " ".join(str(i) for i in ours))
        print("  colors : %s" % " ".join("(%d,%d,%d)" % c for c in report["ours"]["colors"]))
    print()
    print("before_*.png use the unchanged full-table mapping; after_*.png use")
    print("the selected colors.  Written to: %s" % out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
