"""Procedural sample images: smooth gradients with text drawn on top.

Every sample is generated from a seeded integer PRNG so the pixels are
reproducible on any platform.  Three samples are provided, matching the three
report groups, each with its own four-colour output palette (the "4-colour
output" the customer complained about):

``vgrad``  sky-like vertical gradient, 3x text
``hgrad``  warm horizontal gradient, smaller text
``radial`` blue/green radial gradient with a bright hotspot, mixed text
"""

import math

WIDTH = 192
HEIGHT = 128

# 5x7 glyphs, one string per row.  Kept tiny on purpose: the sample text is
# large enough to form textured blocks but small enough to embed here.
_GLYPHS = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("01110", "00100", "00100", "00100", "00100", "00100", "01110"),
    "J": ("00111", "00010", "00010", "00010", "00010", "10010", "01100"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11111", "00010", "00100", "00010", "00001", "10001", "01110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "11110", "00001", "00001", "10001", "01110"),
    "6": ("00110", "01000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00010", "01100"),
    " ": ("00000", "00000", "00000", "00000", "00000", "00000", "00000"),
    ".": ("00000", "00000", "00000", "00000", "00000", "01100", "01100"),
    "-": ("00000", "00000", "00000", "11111", "00000", "00000", "00000"),
    ":": ("00000", "01100", "01100", "00000", "01100", "01100", "00000"),
}

_GLYPH_W = 5
_GLYPH_H = 7
_TRACKING = 1


class Sample:
    def __init__(self, name, description, background, text, text_color,
                 text_scale, palette):
        self.name = name
        self.description = description
        self.background = background   # callable (x, y) -> (r, g, b) floats
        self.text = text
        self.text_color = text_color
        self.text_scale = text_scale
        self.palette = palette         # the four output colours

    def __repr__(self):
        return "Sample(%r)" % self.name


def _lerp(a, b, t):
    return a + (b - a) * t


def _text_mask(sample):
    """Return a bytearray mask: 1 where a glyph pixel is inked."""
    w, h = WIDTH, HEIGHT
    scale = sample.text_scale
    gw = (_GLYPH_W + _TRACKING) * scale
    text_w = max(0, len(sample.text) * gw - _TRACKING * scale)
    x0 = max(0, (w - text_w) // 2)
    y0 = (h - _GLYPH_H * scale) // 2
    mask = bytearray(w * h)
    for ci, ch in enumerate(sample.text):
        glyph = _GLYPHS.get(ch)
        if glyph is None:
            raise KeyError("no glyph for %r" % ch)
        gx = x0 + ci * gw
        for gy in range(_GLYPH_H):
            bits = glyph[gy]
            for gxx in range(_GLYPH_W):
                if bits[gxx] != "1":
                    continue
                for sy in range(scale):
                    py = y0 + gy * scale + sy
                    if py < 0 or py >= h:
                        continue
                    row = py * w
                    for sx in range(scale):
                        px = gx + gxx * scale + sx
                        if 0 <= px < w:
                            mask[row + px] = 1
    return mask


def _render(sample, textured=False):
    """Render one sample.

    ``textured=True`` turns on per-pixel noise and a diagonal hatch so that the
    report can contrast a flat-sky gradient with a busy one.
    """
    w, h = WIDTH, HEIGHT
    mask = _text_mask(sample)
    rng = _LCG(0x5EED + sum(map(ord, sample.name)))
    out = bytearray(w * h * 3)
    tr, tg, tb = sample.text_color
    for y in range(h):
        row = y * w
        for x in range(w):
            i = row + x
            r, g, b = sample.background(x, y)
            if textured:
                # Deterministic 1-bit hatch + small noise: a stand-in for photo
                # texture that has fine detail everywhere.
                if ((x // 2 + y // 2) & 1) == 0:
                    r += 6.0
                    g += 6.0
                    b += 6.0
                n = rng.next_range(-7, 7)
                r += n
                g += n
                b += n
            if mask[i]:
                r, g, b = tr, tg, tb
            out[i * 3] = _clamp_byte(r)
            out[i * 3 + 1] = _clamp_byte(g)
            out[i * 3 + 2] = _clamp_byte(b)
    return out


class _LCG:
    """Deterministic 32-bit linear congruential generator."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF

    def next_u32(self):
        self.state = (1664525 * self.state + 1013904223) & 0xFFFFFFFF
        return self.state

    def next_range(self, lo, hi):
        return lo + self.next_u32() % (hi - lo + 1)


def _clamp_byte(v):
    if v <= 0.0:
        return 0
    if v >= 255.0:
        return 255
    return int(round(v))


def _bg_vgrad(x, y):
    t = y / (HEIGHT - 1)
    return (_lerp(14, 150, t), _lerp(32, 200, t), _lerp(70, 245, t))


def _bg_hgrad(x, y):
    t = x / (WIDTH - 1)
    return (_lerp(30, 255, t), _lerp(14, 225, t), _lerp(10, 180, t))


def _bg_radial(x, y):
    cx, cy = WIDTH * 0.62, HEIGHT * 0.38
    d = math.hypot((x - cx) / WIDTH, (y - cy) / HEIGHT) / 0.72
    d = min(1.0, d)
    base = (_lerp(24, 8, d), _lerp(108, 22, d), _lerp(132, 64, d))
    # Hotspot adds a second, smaller-scale gradient.
    hotspot = max(0.0, 1.0 - math.hypot(x - cx * 1.3, y - cy * 0.8) / 34.0)
    hotspot = hotspot * hotspot
    return tuple(_clamp_byte(c + 150.0 * hotspot) for c in base)


SAMPLES = (
    Sample(
        "vgrad",
        "vertical sky gradient with text",
        _bg_vgrad,
        "SKY 01",
        (250, 250, 250),
        3,
        ((8, 16, 40), (40, 96, 160), (120, 184, 232), (250, 250, 250)),
    ),
    Sample(
        "hgrad",
        "horizontal warm gradient with text",
        _bg_hgrad,
        "WARM 42",
        (18, 10, 6),
        2,
        ((16, 8, 6), (150, 52, 28), (236, 148, 72), (255, 244, 214)),
    ),
    Sample(
        "radial",
        "radial gradient with hotspot and text",
        _bg_radial,
        "RADIAL 7",
        (255, 255, 250),
        2,
        ((6, 18, 58), (24, 108, 132), (72, 196, 148), (255, 255, 250)),
    ),
)


def render_pixels(sample):
    """Return ``(width, height, bytearray)`` RGB bytes for ``sample``."""
    return WIDTH, HEIGHT, _render(sample)


def render_pixels_textured(sample):
    """Textured variant used to show why strength follows block variance."""
    return WIDTH, HEIGHT, _render(sample, textured=True)


def to_rgb_list(rgb):
    """Convert raw RGB bytes to the library's ``[(r, g, b), ...]`` form."""
    return [(rgb[i], rgb[i + 1], rgb[i + 2]) for i in range(0, len(rgb), 3)]
