"""Benchmark: nearest mapping vs. error diffusion on three synthetic images.

Run from the project root:  python3 tools/bench_dither.py
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

import palette

PALETTE = [(0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 0, 255)]
WIDTH, HEIGHT = 128, 96


def img_gradient():
    """Smooth two-axis ramp: the case where plain thresholding bands badly."""
    rows = []
    for y in range(HEIGHT):
        row = []
        for x in range(WIDTH):
            row.append((x * 255 // (WIDTH - 1),
                        y * 255 // (HEIGHT - 1),
                        (x + y) * 255 // (WIDTH + HEIGHT - 2)))
        rows.append(row)
    return rows


def img_ramp_bars():
    """Hard-edged vertical bars plus a soft horizontal falloff."""
    rows = []
    for y in range(HEIGHT):
        row = []
        for x in range(WIDTH):
            bar = (x // 16) % 2
            v = int(32 + (x % 16) * 191 / 15)
            g = (v + y * 255 // (HEIGHT - 1)) // 2
            row.append((v, 255 - g, 64 * bar + (64 if bar else 0)))
        rows.append(row)
    return rows


def img_disc():
    """Radial shading: exposes banding on curved edges."""
    cx, cy = WIDTH / 2.0, HEIGHT / 2.0
    rmax = min(cx, cy)
    rows = []
    for y in range(HEIGHT):
        row = []
        for x in range(WIDTH):
            dx, dy = x - cx, y - cy
            r = (dx * dx + dy * dy) ** 0.5
            t = max(0.0, 1.0 - r / rmax)
            row.append((int(255 * t),
                        int(255 * (1.0 - t)),
                        int(128 + 127 * t)))
        rows.append(row)
    return rows


IMAGES = (
    ("gradient", img_gradient),
    ("ramp_bars", img_ramp_bars),
    ("disc", img_disc),
)


def _flat(rows):
    return [px for row in rows for px in row]


def _timed(fn):
    t0 = time.perf_counter()
    result = fn()
    return result, (time.perf_counter() - t0) * 1e3


def block_mean_error(rows, indices, palette, block=8):
    """Mean absolute error of 8x8 block averages (low-frequency fidelity).

    Per-pixel SSE penalises diffusion by construction -- nearest is the
    per-pixel L2 optimum.  What diffusion improves is local average tone,
    which this metric measures.
    """
    height, width = len(rows), len(rows[0])
    total, count = 0.0, 0
    for by in range(0, height - block + 1, block):
        for bx in range(0, width - block + 1, block):
            for c in range(3):
                src = dst = 0.0
                for yy in range(by, by + block):
                    row = rows[yy]
                    for xx in range(bx, bx + block):
                        src += row[xx][c]
                        dst += palette[indices[yy * width + xx]][c]
                total += abs(src - dst)
                count += block * block
    return total / count


def _run_variant(label, rows, flat, scan):
    if scan is None:                      # existing mapping path, untouched
        indices, ms = _timed(lambda: palette.map_nearest(flat, PALETTE))
    else:
        indices, ms = _timed(
            lambda: palette.map_diffused(flat, PALETTE, WIDTH, scan=scan))
    err = palette.quantization_error(flat, indices, PALETTE)
    counts = palette.palette_usage(indices, len(PALETTE))
    entropy = palette.usage_entropy(indices, len(PALETTE))
    return {
        "label": label,
        "error": err,
        "mean_err": err / len(flat),
        "block_err": block_mean_error(rows, indices, PALETTE),
        "counts": counts,
        "entropy": entropy,
        "ms": ms,
        "indices": indices,
    }


def main():
    print("palette:", PALETTE)
    print("image size: %dx%d (%d pixels)\n" % (WIDTH, HEIGHT, WIDTH * HEIGHT))

    for name, make in IMAGES:
        rows = make()
        flat = _flat(rows)
        runs = [
            _run_variant("nearest", rows, flat, None),
            _run_variant("diffused/forward", rows, flat, "forward"),
            _run_variant("diffused/serpentine", rows, flat, "serpentine"),
        ]

        print("== %s ==" % name)
        print("  %-22s %12s %10s %11s %-22s %7s %9s"
              % ("variant", "total_err", "mean_err", "block8_err",
                 "usage(4 colors)", "entropy", "time_ms"))
        for r in runs:
            print("  %-22s %12.0f %10.3f %11.3f %-22s %7.3f %9.3f"
                  % (r["label"], r["error"], r["mean_err"], r["block_err"],
                     str(r["counts"]), r["entropy"], r["ms"]))

        f, s = runs[1], runs[2]
        assert f["indices"] != s["indices"], "scan orders produced identical output"
        delta = abs(f["error"] - s["error"]) / max(f["error"], s["error"], 1.0)
        print("  forward vs serpentine: different output, "
              "relative total-error delta = %.2f%%\n" % (delta * 100.0))

    print("== scan-mode rejection ==")
    for bad in ("zigzag", "FORWARD", None):
        try:
            palette.quantize_diffused([[PALETTE[0]]], PALETTE, scan=bad)
        except ValueError as exc:
            print("  scan=%-12r -> ValueError: %s" % (bad, exc))


if __name__ == "__main__":
    main()
