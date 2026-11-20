"""Generate four gradient test images, dither them with a blue-noise
threshold map (and a Bayer map as baseline), and report uniformity and
color-difference metrics. Outputs PNGs to ./out/.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "src"))

import numpy as np
from PIL import Image

import bluenoise as bn

SIZE = 256
AMP = 51.0        # threshold modulation amplitude (one gray step)
LUM_BOUND = 32.0  # per-pixel |luma(out) - luma(in)| upper bound (0..255)
BLOCK = 16        # block size for the uniformity metric

# 6 gray levels (step 51, so the bound 32 is always satisfiable) + 4 chroma.
PALETTE = [
    (0, 0, 0), (51, 51, 51), (102, 102, 102),
    (153, 153, 153), (204, 204, 204), (255, 255, 255),
    (200, 40, 40), (40, 170, 60), (50, 80, 210), (225, 205, 50),
]


def make_gradients(size):
    y, x = np.mgrid[0:size, 0:size].astype(float)
    s = size - 1.0
    horiz = x / s
    vert = y / s
    diag = (x + y) / (2.0 * s)
    d = np.sqrt((x - s / 2) ** 2 + (y - s / 2) ** 2)
    radial = 1.0 - np.clip(d / (s / np.sqrt(2.0)), 0.0, 1.0)
    grads = {"horizontal": horiz, "vertical": vert, "diagonal": diag, "radial": radial}
    return {name: np.repeat((g * 255.0)[..., None], 3, axis=2) for name, g in grads.items()}


def report(name, method, src, out_idx):
    out = np.array(PALETTE, dtype=float)[out_idx]
    de = bn.delta_e76(src, out)
    dev = np.abs(bn.luma(out) - bn.luma(src))
    rms, mx = bn.block_uniformity(bn.luma(out), bn.luma(src), BLOCK)
    peak, lowf = bn.spectral_uniformity(bn.luma(out), bn.luma(src))
    print("%-11s %-9s  dE76 mean %5.2f  max %5.2f | lum dev max %5.2f | "
          "block RMS %5.2f  max %5.2f | spectral peak %6.1fx  low-freq %5.1f%%"
          % (name, method, de.mean(), de.max(), dev.max(), rms, mx,
             peak, lowf * 100.0))


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "out")
    os.makedirs(out_dir, exist_ok=True)
    tmap_bn = bn.blue_noise_threshold_map(64, seed=0)
    tmap_bayer = bn.bayer_threshold_map(8)

    for name, src in make_gradients(SIZE).items():
        Image.fromarray(src.astype(np.uint8)).save(os.path.join(out_dir, name + "_input.png"))
        for method, tmap in (("bayer", tmap_bayer), ("bluenoise", tmap_bn)):
            idx = bn.dither(src, PALETTE, tmap, amp=AMP, lum_bound=LUM_BOUND)
            out = np.array(PALETTE, dtype=np.uint8)[idx]
            Image.fromarray(out).save(os.path.join(out_dir, name + "_" + method + ".png"))
            report(name, method, src, idx)
    print("\nPNGs written to", out_dir)


if __name__ == "__main__":
    main()
