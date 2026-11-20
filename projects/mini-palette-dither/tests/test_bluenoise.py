import sys
sys.path.insert(0, "src")

import numpy as np

import bluenoise as bn

PALETTE = [
    (0, 0, 0), (51, 51, 51), (102, 102, 102),
    (153, 153, 153), (204, 204, 204), (255, 255, 255),
    (200, 40, 40), (40, 170, 60), (50, 80, 210), (225, 205, 50),
]
LUM_BOUND = 32.0


def _gradient(size=64):
    y, x = np.mgrid[0:size, 0:size].astype(float)
    g = (x + y) / (2.0 * (size - 1)) * 255.0
    return np.repeat(g[..., None], 3, axis=2)


def test_map_range_and_unique_ranks():
    m = bn.blue_noise_threshold_map(32, seed=1)
    assert m.shape == (32, 32)
    assert m.min() > 0.0 and m.max() < 1.0
    assert len(np.unique(m)) == 32 * 32


def test_map_deterministic():
    a = bn.blue_noise_threshold_map(32, seed=7)
    b = bn.blue_noise_threshold_map(32, seed=7)
    assert np.array_equal(a, b)


def test_output_only_palette_colors():
    tmap = bn.blue_noise_threshold_map(32, seed=2)
    idx = bn.dither(_gradient(), PALETTE, tmap, amp=51.0, lum_bound=LUM_BOUND)
    assert idx.min() >= 0 and idx.max() < len(PALETTE)
    out = np.array(PALETTE, dtype=float)[idx]
    # every output pixel equals some palette entry exactly
    for p in out.reshape(-1, 3):
        assert any(np.array_equal(p, c) for c in PALETTE)


def test_luminance_bound_respected():
    tmap = bn.blue_noise_threshold_map(32, seed=3)
    src = _gradient()
    idx = bn.dither(src, PALETTE, tmap, amp=51.0, lum_bound=LUM_BOUND)
    out = np.array(PALETTE, dtype=float)[idx]
    dev = np.abs(bn.luma(out) - bn.luma(src))
    assert dev.max() <= LUM_BOUND + 1e-9, dev.max()


def test_fallback_when_palette_too_sparse():
    # two-level palette cannot satisfy a tight bound everywhere; the
    # fallback must still return valid indices and minimize luma error.
    tmap = bn.blue_noise_threshold_map(16, seed=4)
    src = _gradient(32)
    idx = bn.dither(src, [(0, 0, 0), (255, 255, 255)], tmap, amp=51.0, lum_bound=8.0)
    assert idx.min() >= 0 and idx.max() < 2


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
