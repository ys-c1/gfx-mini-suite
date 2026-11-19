"""Tests for block-adaptive palette dithering (``src/dither.py``)."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

import dither
import palette


PAL4 = ((0, 0, 0), (85, 85, 85), (170, 170, 170), (255, 255, 255))
PAL_RGB = ((0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255))


def _expect_value_error(fn, needle=None):
    try:
        fn()
    except ValueError as exc:
        if needle is not None:
            assert needle in str(exc), "message %r lacks %r" % (str(exc), needle)
        return
    raise AssertionError("expected ValueError")


def _grid(width, height, fn):
    return [fn(x, y) for y in range(height) for x in range(width)]


def _ramp(width, height):
    """Smooth grey ramp: 0 at the left edge to 255 at the right edge."""
    return _grid(width, height, lambda x, y: (x * 255 // (width - 1),
                                              x * 255 // (width - 1),
                                              x * 255 // (width - 1)))


def _texty(width, height, block=4):
    """Two flat fields of very different colour, i.e. block-scale edges."""
    return _grid(width, height,
                 lambda x, y: (250, 250, 250) if (x // block + y // block) % 2
                 else (10, 10, 10))


def test_disabled_equals_map_nearest():
    px = [(x * 7 % 256, y * 11 % 256, (x * y) % 256)
          for y in range(9) for x in range(13)]
    for block in (1, 4, 8):
        for smin, smax in ((0.0, 0.0), (0.0, 1.0), (0.3, 0.7)):
            got = dither.dither_indices(px, 13, 9, PAL_RGB, block, smin, smax,
                                        enabled=False)
            assert got == palette.map_nearest(px, PAL_RGB)


def test_disabled_zero_strength_equals_map_nearest():
    px = _ramp(17, 5) + [(250, 250, 250)] * (17 * 4)
    got = dither.dither_indices(px, 17, 9, PAL4, 4, 0.0, 0.0, enabled=True)
    assert got == palette.map_nearest(px, PAL4)


def test_only_palette_indices():
    px = _ramp(32, 32) + _texty(32, 32)
    for block in (1, 3, 8, 32, 64):
        out = dither.dither_indices(px, 32, 64, PAL_RGB, block, 0.0, 1.0)
        assert len(out) == 32 * 64
        assert set(out) <= set(range(len(PAL_RGB)))


def test_deterministic_repeat():
    px = _ramp(24, 24)
    a = dither.dither_indices(px, 24, 24, PAL4, 5, 0.0, 0.9)
    b = dither.dither_indices(px, 24, 24, PAL4, 5, 0.0, 0.9)
    assert a == b
    # Inputs are not mutated.
    assert px == _ramp(24, 24)


def test_strength_ordering_by_variance():
    flat = [(120, 120, 120)] * 64
    noisy = _texty(8, 8)
    s_flat = dither.block_strengths(flat, 8, 8, 8, 0.1, 0.9, 16.0)
    s_noisy = dither.block_strengths(noisy, 8, 8, 8, 0.1, 0.9, 16.0)
    assert abs(s_flat[0] - 0.9) < 1e-12
    assert abs(s_noisy[0] - 0.1) < 1e-12


def test_strength_monotone_and_bounded():
    # Full-contrast checkers whose inked fraction grows left to right: the
    # block variance, and therefore the suppression, rises with the fill.
    fills = (0, 1, 2, 3, 4, 5, 6, 8)
    px = []
    for y in range(8):
        for k in fills:
            for x in range(8):
                on = ((x + y) % 2 == 0) and (x < k)
                v = 235 if on else 20
                px.append((v, v, v))
    s = dither.block_strengths(px, 64, 8, 8, 0.2, 0.8, 16.0)
    assert len(s) == 8
    assert all(0.2 - 1e-9 <= v <= 0.8 + 1e-9 for v in s), s
    assert abs(s[0] - 0.8) < 1e-12
    assert abs(s[-1] - 0.2) < 1e-12


def test_block_size_changes_strength_granularity():
    # A smaller block adapts to local detail more aggressively: the same image
    # gets more distinct strengths with block 4 than with block 16.
    px = []
    for y in range(32):
        for x in range(32):
            band = (x // 4) % 4
            v = 200 - band * 40 + (-3 if (x + y) % 5 == 0 else 0)
            px.append((max(0, v),) * 3)
    small = dither.block_strengths(px, 32, 32, 4, 0.0, 0.9, 16.0)
    large = dither.block_strengths(px, 32, 32, 16, 0.0, 0.9, 16.0)
    assert len(small) == 64 and len(large) == 4
    assert len(set(round(v, 6) for v in small)) > len(set(round(v, 6) for v in large))


def test_block_grid_partial_edges():
    assert dither.block_grid_size(20, 10, 8) == (3, 2)
    assert dither.block_grid_size(8, 8, 8) == (1, 1)
    s = dither.block_strengths([(0, 0, 0)] * 3, 3, 1, 8, 0.0, 1.0, 16.0)
    assert s == [1.0]


def test_gradient_banding_reduced():
    """Dithering must lower per-block mean error on a gentle ramp.

    The ramp spans 40 levels over 64 pixels, which is the kind of slow gradient
    that bands after 4bpp quantization but still has low local variance.
    """
    width, height = 64, 16
    px = _grid(width, height,
               lambda x, y: ((110 + (x * 40) // (width - 1) +
                              (2 if (x + 2 * y) % 3 == 0 else -2)),) * 3)
    px = [(max(0, min(255, p[0])),) * 3 for p in px]
    plain = dither.dither_indices(px, width, height, PAL4, 8, 0.0, 0.0,
                                  enabled=False)
    dith = dither.dither_indices(px, width, height, PAL4, 8, 0.0, 0.9)

    def block_mean_error(idx):
        total = 0.0
        blocks = 0
        for bx in range(0, width, 8):
            src = 0.0
            got = 0.0
            for y in range(height):
                for x in range(bx, bx + 8):
                    i = y * width + x
                    src += px[i][0]
                    got += PAL4[idx[i]][0]
            total += abs(src - got) / (8 * height)
            blocks += 1
        return total / blocks

    assert block_mean_error(dith) < block_mean_error(plain) * 0.75


def test_flat_region_unchanged_when_all_textured():
    """strength_min == strength_max == 0 is a full bypass on any content."""
    px = _texty(16, 16) + _ramp(16, 4)
    want = palette.map_nearest(px, PAL4)
    assert dither.dither_indices(px, 16, 20, PAL4, 4, 0.0, 0.0) == want


def test_texture_mask_flags_edges_only():
    width = 16
    noisy = _texty(width, 16, block=1)  # per-pixel checker: extreme variance
    smooth = []
    for y in range(4):
        for x in range(width):
            v = 120 + (2 if (x + y) % 3 == 0 else -2)
            smooth.append((v, v, v))
    px = noisy + smooth
    mask = dither.text_mask(px, width, 20, 8)
    assert set(mask[:16 * width]) == {1}
    assert set(mask[16 * width:]) == {0}


def test_rejects_bad_block_size():
    px = [(0, 0, 0)] * 16
    for bad in (0, -1, -8, 2.0, 4.5, "4", None, True, False):
        _expect_value_error(
            lambda bad=bad: dither.dither_indices(px, 4, 4, PAL4, bad, 0.0, 1.0),
            "block_size")
        _expect_value_error(
            lambda bad=bad: dither.block_strengths(px, 4, 4, bad, 0.0, 1.0),
            "block_size")
        _expect_value_error(
            lambda bad=bad: dither.block_grid_size(4, 4, bad), "block_size")


def test_rejects_bad_palette():
    px = [(0, 0, 0)] * 4
    for bad in (None, [], (), "rgb", 42, [[0, 0, 0], [255, 255]],
                [[0, 0, 0], [1, 2, 256]], [[0, 0, 0], [1, -1, 2]],
                [[0, 0, 0], [1.0, 2, 3]], [[0, 0, 0], (1, 2, True)]):
        _expect_value_error(
            lambda bad=bad: dither.dither_indices(px, 2, 2, bad, 2, 0.0, 1.0),
            "palette")
    too_many = [(i, i, i) for i in range(257)]
    _expect_value_error(
        lambda: dither.dither_indices(px, 2, 2, too_many, 2, 0.0, 1.0),
        "256")


def test_rejects_inverted_strength_bounds():
    px = [(0, 0, 0)] * 4
    _expect_value_error(
        lambda: dither.dither_indices(px, 2, 2, PAL4, 2, 0.8, 0.2),
        "must not exceed")
    # Validation also runs when dithering is disabled.
    _expect_value_error(
        lambda: dither.dither_indices(px, 2, 2, PAL4, 2, 0.8, 0.2, enabled=False),
        "must not exceed")


def test_rejects_out_of_range_strength():
    px = [(0, 0, 0)] * 4
    for smin, smax in ((-0.1, 0.5), (0.0, 1.1), (0.0, float("nan")),
                       (0.0, float("inf")), ("0", 0.5), (0.0, None)):
        _expect_value_error(
            lambda smin=smin, smax=smax:
            dither.dither_indices(px, 2, 2, PAL4, 2, smin, smax))


def test_rejects_bad_texture_scale():
    px = [(0, 0, 0)] * 4
    for bad in (0.0, -1.0, float("nan"), None, "16"):
        _expect_value_error(
            lambda bad=bad: dither.dither_indices(px, 2, 2, PAL4, 2, 0.0, 1.0,
                                                  texture_scale=bad),
            "texture_scale")


def test_rejects_bad_dimensions_and_pixels():
    _expect_value_error(
        lambda: dither.dither_indices([(0, 0, 0)] * 3, 2, 2, PAL4, 2, 0.0, 1.0),
        "does not match")
    _expect_value_error(
        lambda: dither.dither_indices([(0, 0)] * 4, 2, 2, PAL4, 2, 0.0, 1.0),
        "RGB triple")
    _expect_value_error(
        lambda: dither.dither_indices([(0, 0, 300)] * 4, 2, 2, PAL4, 2, 0.0, 1.0),
        "0..255")
    _expect_value_error(
        lambda: dither.dither_indices([(0, 0, 0)] * 4, 0, 2, PAL4, 2, 0.0, 1.0),
        "width")


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
