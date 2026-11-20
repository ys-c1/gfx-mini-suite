"""Three deterministic test images plus a PNG writer (stdlib + numpy only).

Each image is generated from fixed parameters and a fixed RNG seed, so the
pixels are identical on every run and every machine.  They differ in what
they demand from a palette:

    bands       smooth interleaved sin/cos bands, all 256 source colors used
    scene       sky, sun, hills with mild grain; heavy dark/blue mass
    confetti    hard-edged color patches with strong grain; near-uniform use

`write_png` is a minimal non-interlaced RGB PNG encoder, so the demo can leave
viewable files behind without depending on Pillow.
"""

import os
import struct
import zlib

import numpy as np


def _clip(img):
    return np.clip(np.rint(img), 0, 255).astype(np.uint8)


def bands_image(h=160, w=224):
    """Interleaved sine bands on all three axes, plus fine grain.

    The frequencies are chosen so the image visits the whole 6x6x6 cube and
    the gray ramp; that spreads pixel mass across all 256 source colors.
    """
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    u = x / (w - 1)
    v = y / (h - 1)
    r = 255 * (0.5 + 0.5 * np.sin(11.0 * u + 4.0 * v))
    g = 255 * (0.5 + 0.5 * np.sin(13.0 * u - 6.0 * v + 1.0))
    b = 255 * (0.5 + 0.5 * np.sin(9.0 * u + 7.0 * v + 2.0))
    rng = np.random.default_rng(11)
    grain = rng.normal(0.0, 30.0, (h, w, 1))
    return _clip(np.stack([r, g, b], axis=-1) + grain)


def scene_image(h=160, w=224, seed=5):
    """Sky, sun and hills, with barn/tree/road objects and film grain.

    Most pixels land on a handful of source colors (this is what a real photo
    looks like after 6-level quantization), so the selection problem is
    sparse; the grain is what gives the small remaining colors their weight.
    """
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    u = x / (w - 1)
    v = y / (h - 1)
    r = 90 + 110 * (1 - v)
    g = 140 + 80 * (1 - v)
    b = 200 + 45 * (1 - v)
    sun = np.exp(-(((u - 0.78) ** 2 + (v - 0.22) ** 2) / 0.004))
    r = r + 165 * sun
    g = g + 120 * sun
    b = b + 40 * sun
    ridge = 0.60 + 0.14 * np.sin(7.0 * u) + 0.06 * np.sin(23.0 * u + 1.0)
    ground = v > ridge
    r = np.where(ground, 40 + 70 * (v - ridge), r)
    g = np.where(ground, 90 + 80 * (v - ridge), g)
    b = np.where(ground, 35 + 30 * (v - ridge), b)
    barn = (np.abs(u - 0.30) < 0.09) & (np.abs(v - 0.66) < 0.07)
    trees = (np.abs(u - 0.62) < 0.10) & (np.abs(v - 0.72) < 0.10)
    road = np.abs(v - 0.90) < 0.05
    r = np.where(barn, 170, r)
    g = np.where(barn, 60, g)
    b = np.where(barn, 45, b)
    r = np.where(trees, 30, r)
    g = np.where(trees, 80, g)
    b = np.where(trees, 40, b)
    r = np.where(road, 60, r)
    g = np.where(road, 60, g)
    b = np.where(road, 65, b)
    rng = np.random.default_rng(seed)
    noise = rng.normal(0.0, 14.0, (h, w, 1))
    return _clip(np.stack([r, g, b], axis=-1) + noise)


def confetti_image(h=160, w=224, seed=7):
    """Hard-edged 24px color patches with saturated secondaries, heavy grain."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    u = x / (w - 1)
    v = y / (h - 1)
    r = 255 * (0.5 + 0.5 * np.sin(17.0 * u + 5.0 * v))
    g = 255 * (0.5 + 0.5 * np.cos(13.0 * u - 9.0 * v + 0.7))
    b = 255 * (0.5 + 0.5 * np.sin(7.0 * u + 15.0 * v + 2.1))
    patch = ((x // 24) + (y // 24)) % 3
    # three hard patch tones; keeps mass near saturated corners and mid grays
    r = np.where(patch == 0, 245 - 30 * v, r)
    g = np.where(patch == 1, 235 - 20 * u, g)
    b = np.where(patch == 2, 250 - 40 * (u * v), b)
    rng = np.random.default_rng(seed)
    grain = rng.normal(0.0, 34.0, (h, w, 1))
    return _clip(np.stack([r, g, b], axis=-1) + grain)


IMAGES = (
    ("bands", bands_image),
    ("scene", scene_image),
    ("confetti", confetti_image),
)


def build_images():
    """Return [(name, (h, w, 3) uint8 array)] in a fixed order."""
    return [(name, fn()) for name, fn in IMAGES]


def write_png(path, img):
    """Write an (h, w, 3) uint8 array as a non-interlaced 8-bit RGB PNG."""
    img = np.asarray(img, dtype=np.uint8)
    if img.ndim != 3 or img.shape[2] != 3:
        raise ValueError("expected an (h, w, 3) RGB array")
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[row].tobytes() for row in range(h))

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(png)
    return path
