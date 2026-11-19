import sys
sys.path.insert(0, "src")

import palette


PAL4 = [(0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 0, 255)]


def _gradient_rows(width=24, height=16):
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            row.append((x * 255 // (width - 1),
                        y * 255 // (height - 1),
                        (x + y) * 127 // (width + height - 2)))
        rows.append(row)
    return rows


def test_nearest_identity():
    pal = [(0, 0, 0), (255, 255, 255), (255, 0, 0)]
    assert palette.map_nearest([(1, 1, 1), (250, 250, 250), (250, 10, 10)], pal) == [0, 1, 2]


def test_nearest_unchanged_by_diffusion():
    # Existing mapping path must keep producing the same indices.
    pixels = [(10, 10, 10), (200, 200, 200), (180, 20, 20), (20, 20, 180)]
    assert palette.map_nearest(pixels, PAL4) == [0, 1, 2, 3]


def test_uniform_levels():
    out = palette.quantize_uniform([0, 64, 128, 200, 255], 4)
    assert len(out) == 5
    assert out[0] == 0 and out[-1] == 255
    assert all(0 <= v <= 255 for v in out)


def test_usage_counts():
    assert palette.palette_usage([0, 0, 1, 2, 2, 2], 3) == [2, 1, 3]


def test_invalid_levels():
    try:
        palette.quantize_uniform([1, 2], 1)
    except ValueError:
        return
    raise AssertionError("expected ValueError")


def test_reject_unknown_scan():
    rows = [[(0, 0, 0)]]
    for bad in ("zigzag", "FORWARD", "", None, 1, "serpentine "):
        try:
            palette.quantize_diffused(rows, PAL4, scan=bad)
        except ValueError:
            continue
        raise AssertionError("expected ValueError for %r" % (bad,))


def test_scan_modes_differ_but_error_close():
    rows = _gradient_rows()
    flat = [p for row in rows for p in row]
    fwd = palette.map_diffused(flat, PAL4, len(rows[0]), scan="forward")
    ser = palette.map_diffused(flat, PAL4, len(rows[0]), scan="serpentine")
    assert fwd != ser
    e_fwd = palette.quantization_error(flat, fwd, PAL4)
    e_ser = palette.quantization_error(flat, ser, PAL4)
    # Both orders must land near the same total error.
    assert abs(e_fwd - e_ser) / max(e_fwd, e_ser, 1.0) < 0.25


def test_error_bounded_and_channel_isolated():
    # A fully saturated colour against a palette whose nearest is far away
    # exercises the largest possible error.
    rows = [[(255, 0, 0), (0, 255, 0), (0, 0, 255)] * 8] * 8
    for scan in palette.SCAN_MODES:
        out = palette.quantize_diffused(rows, [(0, 0, 0)], scan=scan)
        assert all(i == 0 for row in out for i in row)
    # Working buffer is clamped; an accumulator model would exceed 255 here.
    for value in (-1.0, 0.0, 255.0, 1e9):
        assert 0.0 <= palette._clamp_work(value) <= palette.MAX_WORK_VALUE


def test_spatial_error_bounded():
    # Every pixel's quantisation target stays inside the legal range even
    # after neighbours have dumped their full error onto it.
    rows = [[(255, 255, 255)] * 16 for _ in range(16)]
    out = palette.quantize_diffused(rows, [(0, 0, 0), (255, 255, 255)], scan="forward")
    assert out[0][0] == 1              # white maps to white, error == 0
    assert all(i in (0, 1) for row in out for i in row)


def _reference_fs(rows, pal, scan, clamp):
    """Independent Floyd-Steinberg, used to cross-check the module."""
    h, w = len(rows), len(rows[0])
    buf = [[[float(v) for v in px] for px in row] for row in rows]
    out = []
    for y in range(h):
        row_out = [0] * w
        reverse = scan == "serpentine" and y % 2 == 1
        cols = range(w - 1, -1, -1) if reverse else range(w)
        d = -1 if reverse else 1
        for x in cols:
            px = buf[y][x]
            idx = min(range(len(pal)),
                      key=lambda i: sum((px[c] - pal[i][c]) ** 2 for c in range(3)))
            row_out[x] = idx
            err = [px[c] - pal[idx][c] for c in range(3)]
            for dx, dy, wt in ((1, 0, 7 / 16), (-1, 1, 3 / 16),
                               (0, 1, 5 / 16), (1, 1, 1 / 16)):
                nx, ny = x + d * dx, y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    for c in range(3):
                        buf[ny][nx][c] = clamp(buf[ny][nx][c] + err[c] * wt)
        out.append(row_out)
    return out


def test_matches_reference_floyd_steinberg():
    rows = _gradient_rows()
    for scan in palette.SCAN_MODES:
        got = palette.quantize_diffused(rows, PAL4, scan=scan)
        want = _reference_fs(rows, PAL4, scan, palette._clamp_work)
        assert got == want, "mismatch for scan=%s" % scan


def test_input_not_mutated():
    rows = [[(10, 20, 30), (40, 50, 60)]]
    snapshot = [[tuple(px) for px in row] for row in rows]
    palette.quantize_diffused(rows, PAL4, scan="serpentine")
    assert [[tuple(px) for px in row] for row in rows] == snapshot


def test_usage_entropy_bounds():
    assert palette.usage_entropy([0, 0, 0, 0], 4) == 0.0
    assert abs(palette.usage_entropy([0, 1, 2, 3], 4) - 1.0) < 1e-12
    assert palette.usage_entropy([], 4) == 0.0


if __name__ == "__main__":
    failed = 0
    total = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            total += 1
            try:
                fn()
                print("PASS", name)
            except AssertionError as e:
                failed += 1
                print("FAIL", name, e)
    print("%d checks, %d failures" % (total, failed))
    sys.exit(1 if failed else 0)
