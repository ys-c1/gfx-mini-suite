"""Stage 2 tests: dithering on the compacted palette and its error bound."""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

import dither
import palette
from palette import PaletteError, _dist2


def _gradient(width, height, lo=0, hi=255):
    """Horizontal grey gradient, flat row-major (r, g, b) list."""
    out = []
    span = max(1, width - 1)
    for y in range(height):
        for x in range(width):
            v = lo + (hi - lo) * x // span
            out.append((v, v, v))
    return out


def _grey_palette(levels):
    step = 255 // (levels - 1)
    return [(v, v, v) for v in range(0, 255 + 1, step)][:levels]


def _deduped(pal, counts):
    return palette.dedupe_palette(pal, counts)


def test_factor_zero_matches_plain_nearest():
    pal = [(0, 0, 0), (128, 128, 128), (255, 255, 255)]
    px = _gradient(17, 5)
    plain = palette.map_nearest(px, pal)
    dithered, stats = dither.dither(px, 17, pal, factor=0.0)
    assert dithered == plain
    assert stats["max_error"] <= stats["error_bound"]


def test_dedupe_off_output_is_identical():
    """dedupe disabled -> exact old result, including with duplicates present."""
    pal = [(0, 0, 0), (0, 0, 0), (128, 128, 128), (255, 255, 255), (255, 255, 255)]
    px = _gradient(13, 7)
    m = palette.dedupe_palette(pal, enabled=False)
    old = palette.map_nearest(px, pal)
    out, stats = dither.dither(px, 13, m.palette, factor=0.0)
    assert out == old
    # The mapping is the identity, so remapped output is the same too.
    assert palette.remap_indices(out, m) == old


def test_compacted_palette_quantises_to_new_table():
    pal = [(0, 0, 0), (9, 9, 9), (0, 0, 0), (255, 255, 255), (9, 9, 9)]
    m = _deduped(pal, [5, 0, 3, 4, 2])
    assert m.palette == [(0, 0, 0), (255, 255, 255), (9, 9, 9)]
    px = _gradient(32, 4)
    out, stats = dither.dither(px, 32, m.palette)
    assert max(out) < len(m.palette)
    assert stats["within_bound"]


def test_error_bound_holds_on_random_images():
    pal = [(0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 0, 255)]
    # deterministic pseudo-random pixels without importing random state
    px = []
    state = 12345
    for _ in range(40 * 9):
        state = (1103515245 * state + 12345) & 0x7FFFFFFF
        px.append((state & 0xFF, (state >> 8) & 0xFF, (state >> 16) & 0xFF))
    out, stats = dither.dither(px, 40, pal, factor=0.5)
    assert stats["within_bound"], stats
    assert stats["max_error"] <= stats["error_bound"]


def test_bound_matches_the_derived_formula():
    pal = _grey_palette(32)
    px = _gradient(64, 8)
    _, stats = dither.dither(px, 64, pal)
    # Per-entry clamp is one channel's share of the nearest-other distance:
    # neighbouring greys differ by 8 per channel, so the cell is sqrt(3)*8 and
    # the clamp is cell/sqrt(3) = 8.
    clamp = 8.0
    assert abs(stats["clamp"] - clamp) < 1e-9, stats
    # D is each source pixel's own nearest-palette distance; the reported bound
    # is the largest (sqrt(D) + 2*sqrt(3)*S_max)^2 over the image.
    worst_d = min(_dist2(p, c) for c in pal for p in [px[0]])
    for p in px:
        worst_d = max(worst_d, min(_dist2(p, c) for c in pal))
    expected = (math.sqrt(worst_d) + 2 * 3 ** 0.5 * clamp) ** 2
    assert abs(stats["error_bound"] - expected) < 1e-6, (stats, expected)


def test_single_colour_palette():
    out, stats = dither.dither([(10, 20, 30), (200, 0, 0)], 2, [(0, 0, 0)])
    assert out == [0, 0]
    assert stats["within_bound"]


def test_palette_errors_name_positions():
    def raised(fn, position):
        try:
            fn()
        except PaletteError as exc:
            assert exc.position == position, (exc.position, position, str(exc))
            return
        raise AssertionError("expected PaletteError at %s" % position)

    raised(lambda: dither.dither([], 1, [(0, 0, 0)]), "pixels[0]")
    raised(lambda: dither.dither([(0, 0, 0)], 0, [(0, 0, 0)]), "width")
    raised(lambda: dither.dither([(0, 0, 0)] * 3, 2, [(0, 0, 0)]), "pixels[3]")
    raised(lambda: dither.dither([(0, 0, 0)], 1, []), "palette[0]")
    raised(lambda: dither.dither([(0, 0, 300)], 1, [(0, 0, 0)]), "pixels[0]")
