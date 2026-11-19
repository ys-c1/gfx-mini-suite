"""Tests for the dependency-free sample tooling (tools/pngio.py)."""

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

from pngio import read_png, write_png  # noqa: E402


def _roundtrip(path, width, height, rgb):
    write_png(path, width, height, rgb)
    w, h, back = read_png(path)
    assert (w, h) == (width, height)
    assert bytes(back) == bytes(rgb)


def test_rgb_png_round_trip():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "a.png")
        width, height = 7, 5
        rgb = bytearray((x * 13 + y * 7) % 256 for y in range(height)
                        for x in range(width) for _ in range(3))
        _roundtrip(path, width, height, rgb)


def test_palette_png_round_trip():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "p.png")
        width, height = 6, 4
        pal = [(10, 20, 30), (200, 100, 50), (0, 0, 0), (255, 255, 255)]
        idx = [(x + y) % 4 for y in range(height) for x in range(width)]
        write_png(path, width, height, None, palette=pal, indices=idx)
        w, h, rgb = read_png(path)
        assert (w, h) == (width, height)
        want = bytearray()
        for i in idx:
            want.extend(bytes(pal[i]))
        assert bytes(rgb) == bytes(want)


def test_palette_requires_indices():
    try:
        write_png("unused.png", 2, 2, None, palette=[(0, 0, 0)])
    except ValueError:
        return
    raise AssertionError("expected ValueError")


def test_rejects_bad_lengths():
    cases = (
        (None, dict(palette=[(0, 0, 0)], indices=[0])),
        (bytearray(3), dict(palette=None, indices=None)),
    )
    for rgb, kwargs in cases:
        try:
            write_png("unused.png", 2, 2, rgb, **kwargs)
        except ValueError:
            continue
        raise AssertionError("expected ValueError")


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
