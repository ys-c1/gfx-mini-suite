"""Deterministic self-check: the old nearest-colour output must survive.

Run with ``make test``; prints one line per check and exits non-zero on any
mismatch.  This is the "dedupe off == today's output" guard, kept separate
from the unit tests so it is easy to run on its own.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

import dither
import palette


def _pixels(width, height):
    out = []
    for y in range(height):
        for x in range(width):
            out.append((x * 3 % 256, y * 7 % 256, (x + y) * 5 % 256))
    return out


def main():
    cases = [
        [(0, 0, 0), (255, 255, 255)],
        [(0, 0, 0), (0, 0, 0), (128, 128, 128), (255, 255, 255), (255, 255, 255)],
        [(10, 0, 20), (200, 30, 40), (10, 0, 20), (5, 5, 5)],
    ]
    failed = 0
    width, height = 11, 6
    pixels = _pixels(width, height)
    for n, pal in enumerate(cases):
        usage = [1 + (i % 3) for i in range(len(pal))]
        mapping = palette.dedupe_palette(pal, usage, enabled=False)
        expected = palette.map_nearest(pixels, pal)
        got, stats = dither.dither(pixels, width, mapping.palette, factor=0.0)
        ok = got == expected and mapping.palette == pal and mapping.remap == list(
            range(len(pal))
        )
        print("%s case %d: %d indices" % ("PASS" if ok else "FAIL", n, len(got)))
        failed += 0 if ok else 1
    print("%d cases, %d failures" % (len(cases), failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
