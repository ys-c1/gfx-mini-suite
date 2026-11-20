"""Blue-noise threshold maps and bounded-luminance palette dithering.

The threshold map is built with Ulichney's void-and-cluster method on a
toroidal grid, so it tiles seamlessly and its spectrum is isotropic
(no visible repeated pattern, unlike a Bayer matrix).
"""

import numpy as np

# Rec.709 luma weights, used for the luminance deviation bound.
LUMA = (0.2126, 0.7152, 0.0722)


def _gauss_kernel(size, sigma):
    ax = np.arange(size) - size // 2
    xx, yy = np.meshgrid(ax, ax)
    k = np.exp(-(xx ** 2 + yy ** 2) / (2.0 * sigma ** 2))
    return k / k.sum()


def blue_noise_threshold_map(size=64, sigma=1.9, seed=0, init_density=0.1):
    """Void-and-cluster threshold map, values in (0, 1), all ranks distinct."""
    rng = np.random.default_rng(seed)
    pat = rng.random((size, size)) < init_density
    kc = _gauss_kernel(size, sigma)  # peak at (size//2, size//2)
    kf = np.fft.fft2(np.fft.ifftshift(kc))
    energy = np.real(np.fft.ifft2(np.fft.fft2(pat.astype(float)) * kf))
    rank = np.empty((size, size), dtype=np.int64)

    # Phase 1: remove minority pixels, tightest cluster first.
    for r in range(int(pat.sum()) - 1, -1, -1):
        e = np.where(pat, energy, -np.inf)
        y, x = np.unravel_index(np.argmax(e), e.shape)
        pat[y, x] = False
        energy -= np.roll(kc, (y - size // 2, x - size // 2), axis=(0, 1))
        rank[y, x] = r

    # Phases 2+3: fill remaining ranks into the largest voids.
    total = size * size
    count = 0
    while count < total:
        e = np.where(pat, np.inf, energy)
        y, x = np.unravel_index(np.argmin(e), e.shape)
        pat[y, x] = True
        energy += np.roll(kc, (y - size // 2, x - size // 2), axis=(0, 1))
        rank[y, x] = count
        count += 1

    return (rank + 0.5) / total


def bayer_threshold_map(n=8):
    """Classic ordered (Bayer) map, kept as a baseline for comparison."""
    m = np.array([[0, 2], [3, 1]], dtype=np.int64)
    size = 2
    while size < n:
        m = np.block([[4 * m, 4 * m + 2], [4 * m + 3, 4 * m + 1]])
        size *= 2
    return (m + 0.5) / (n * n)


def dither(img, palette, tmap, amp=51.0, lum_bound=None):
    """Ordered-style dither with an arbitrary threshold map.

    img: HxWx3 (0..255). palette: P x 3. Returns HxW palette indices.
    Every output pixel is exactly a palette color. When lum_bound is set,
    only palette colors whose luma is within +-lum_bound of the *input*
    pixel luma are eligible; if none qualifies (palette too sparse), the
    closest-luma color is used as a fallback.
    """
    img = np.asarray(img, dtype=float)
    pal = np.asarray(palette, dtype=float)
    h, w = img.shape[:2]
    s = tmap.shape[0]
    t = np.tile(tmap, (h // s + 1, w // s + 1))[:h, :w]
    adj = np.clip(img + (t[..., None] - 0.5) * amp, 0.0, 255.0)
    d2 = ((adj[..., None, :] - pal) ** 2).sum(-1)
    if lum_bound is None:
        return d2.argmin(-1)
    lw = np.asarray(LUMA)
    in_lum = img @ lw
    pal_lum = pal @ lw
    dev = np.abs(pal_lum[None, None, :] - in_lum[..., None])
    allowed = dev <= lum_bound
    idx = np.where(allowed, d2, np.inf).argmin(-1)
    bad = ~allowed.any(-1)
    if bad.any():
        idx[bad] = dev[bad].argmin(-1)
    return idx


# --- metrics ---------------------------------------------------------------

def srgb_to_lab(rgb):
    """Vectorized sRGB (0..255) -> CIE L*a*b* (D65)."""
    x = np.asarray(rgb, dtype=float) / 255.0
    lin = np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)
    m = np.array([
        [0.4124564, 0.3575761, 0.1804375],
        [0.2126729, 0.7151522, 0.0721750],
        [0.0193339, 0.1191920, 0.9503041],
    ])
    xyz = lin @ m.T
    white = np.array([0.95047, 1.0, 1.08883])
    t = xyz / white
    f = np.where(t > 0.008856, np.cbrt(t), 7.787 * t + 16.0 / 116.0)
    return np.stack([
        116.0 * f[..., 1] - 16.0,
        500.0 * (f[..., 0] - f[..., 1]),
        200.0 * (f[..., 1] - f[..., 2]),
    ], axis=-1)


def delta_e76(rgb_a, rgb_b):
    """CIE76 color difference between two RGB images/colors."""
    lab_a = srgb_to_lab(rgb_a)
    lab_b = srgb_to_lab(rgb_b)
    return np.sqrt(((lab_a - lab_b) ** 2).sum(-1))


def luma(img):
    return np.asarray(img, dtype=float) @ np.asarray(LUMA)


def block_uniformity(out_lum, in_lum, block=16):
    """RMS and max of per-block mean luminance residual (lower = more uniform)."""
    h, w = in_lum.shape
    res = out_lum - in_lum
    hb, wb = h // block, w // block
    blocks = res[:hb * block, :wb * block].reshape(hb, block, wb, block).mean((1, 3))
    return float(np.sqrt((blocks ** 2).mean())), float(np.abs(blocks).max())


def spectral_uniformity(out_lum, in_lum, low_cut=0.05):
    """Pattern-visibility metrics from the residual power spectrum.

    The residual component that is explained by the tone level itself
    (quantization banding, identical for any dither method) is removed
    first, so what remains is the spatial dither pattern.

    Returns (peak_ratio, low_freq_ratio):
    - peak_ratio: strongest non-DC spectral peak as a multiple of the mean
      power. Ordered maps (Bayer) spike here; blue noise stays flat.
    - low_freq_ratio: fraction of pattern power below low_cut cycles/pixel.
      Blue noise pushes error to high frequencies, so this is small.
    """
    res = out_lum - in_lum
    lvl = np.clip(np.round(in_lum).astype(np.int64), 0, 255)
    counts = np.bincount(lvl.ravel(), None, 256)
    sums = np.bincount(lvl.ravel(), res.ravel(), 256)
    tone_mean = sums / np.maximum(counts, 1)
    pat = res - tone_mean[lvl]
    h, w = pat.shape
    win = np.hanning(h)[:, None] * np.hanning(w)[None, :]
    power = np.abs(np.fft.fft2(pat * win)) ** 2
    power[0, 0] = 0.0
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    radial = np.sqrt(fx ** 2 + fy ** 2)
    total = power.sum()
    peak_ratio = float(power.max() / (total / power.size)) if total > 0 else 0.0
    low_freq = float(power[radial < low_cut].sum() / total) if total > 0 else 0.0
    return peak_ratio, low_freq

