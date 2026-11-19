"""Block-adaptive palette dithering.

The public entry point is :func:`dither_indices`.  It maps RGB pixels to the
nearest palette entry exactly like :func:`palette.map_nearest` when dithering is
disabled, and otherwise runs a deterministic serpentine Floyd-Steinberg error
diffusion whose strength is chosen per source block.

Strength model
--------------
Every ``block_size`` x ``block_size`` source block gets one strength value
computed from its RGB variance:

    variance = mean squared channel deviation from the block mean
    t        = clamp(sqrt(variance / texture_scale), 0, 1)
    s        = strength_max - (strength_max - strength_min) * t

Flat blocks (variance 0) therefore use ``strength_max``; textured blocks use
``strength_min`` once the variance reaches ``texture_scale``.  A banded
gradient has low *local* variance, so it dithers near full strength, while a
glyph edge lives in a block whose variance is one or two orders of magnitude
larger and is therefore suppressed.

With ``strength_min == 0`` high-variance blocks (text edges) are left
byte-for-byte identical to :func:`palette.map_nearest`: error diffusion is
gated by ``min(s_source, s_target)`` so no error crosses from a dithered
gradient block into an undithered text block, and error arriving at a
zero-strength pixel is discarded.

The square root is deliberate: it reaches the floor quickly for real edges but
keeps a gentle ramp on smooth content, so a slightly noisy gradient still gets
partial dithering instead of being treated as text.

All arithmetic is deterministic; no randomness, sets or unordered iteration is
involved, so repeated calls on the same input produce identical indices.
"""

from __future__ import annotations

import math

from palette import map_nearest, nearest_index

DEFAULT_TEXTURE_SCALE = 64.0


def _is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _validate_dimensions(pixels, width, height):
    if not isinstance(width, int) or isinstance(width, bool) or width <= 0:
        raise ValueError("width must be a positive integer")
    if not isinstance(height, int) or isinstance(height, bool) or height <= 0:
        raise ValueError("height must be a positive integer")
    if not hasattr(pixels, "__len__"):
        raise ValueError("pixels must be a sequence")
    if len(pixels) != width * height:
        raise ValueError(
            "pixels length %d does not match %dx%d" % (len(pixels), width, height)
        )


def validate_palette(palette):
    """Validate a palette and return it as a tuple of RGB tuples.

    Rejects ``None``, an empty palette, palettes larger than 256 entries (the
    packer exposes 8 bpp at most) and entries that are not 0..255 integer RGB
    triples.
    """
    if palette is None:
        raise ValueError("palette must not be empty")
    try:
        colors = list(palette)
    except TypeError:
        raise ValueError("palette must be a sequence of RGB triples") from None
    if not colors:
        raise ValueError("palette must not be empty")
    if len(colors) > 256:
        raise ValueError("palette may contain at most 256 colors (8 bpp packer)")
    checked = []
    for i, color in enumerate(colors):
        if not isinstance(color, (tuple, list)) or len(color) != 3:
            raise ValueError("palette[%d] must be an RGB triple" % i)
        rgb = []
        for channel in color:
            if not isinstance(channel, int) or isinstance(channel, bool):
                raise ValueError("palette[%d] components must be integers" % i)
            if channel < 0 or channel > 255:
                raise ValueError("palette[%d] components must be in 0..255" % i)
            rgb.append(channel)
        checked.append(tuple(rgb))
    return tuple(checked)


def validate_params(block_size, strength_min, strength_max,
                    texture_scale=DEFAULT_TEXTURE_SCALE):
    """Validate dithering parameters, raising ``ValueError`` on bad input."""
    if not isinstance(block_size, int) or isinstance(block_size, bool):
        raise ValueError("block_size must be an integer")
    if block_size <= 0:
        raise ValueError("block_size must be > 0")
    for name, value in (("strength_min", strength_min),
                        ("strength_max", strength_max)):
        if not _is_number(value) or not math.isfinite(value):
            raise ValueError("%s must be a finite number" % name)
        if value < 0.0 or value > 1.0:
            raise ValueError("%s must be in [0, 1]" % name)
    if strength_min > strength_max:
        raise ValueError(
            "strength_min (%r) must not exceed strength_max (%r)"
            % (strength_min, strength_max)
        )
    if not _is_number(texture_scale) or not math.isfinite(texture_scale):
        raise ValueError("texture_scale must be a finite number")
    if texture_scale <= 0.0:
        raise ValueError("texture_scale must be > 0")


def _validate_pixels(pixels, width, height):
    for i in range(width * height):
        color = pixels[i]
        if not isinstance(color, (tuple, list)) or len(color) != 3:
            raise ValueError("pixels[%d] must be an RGB triple" % i)
        for channel in color:
            if not _is_number(channel) or not math.isfinite(channel):
                raise ValueError("pixels[%d] components must be finite numbers" % i)
            if channel < 0.0 or channel > 255.0:
                raise ValueError("pixels[%d] components must be in 0..255" % i)


def _block_variance(pixels, width, x0, x1, y0, y1):
    """Mean squared channel deviation from the block mean (mean of channels)."""
    n = (x1 - x0) * (y1 - y0)
    sr = sg = sb = 0.0
    srr = sgg = sbb = 0.0
    for y in range(y0, y1):
        row = y * width
        for x in range(x0, x1):
            r, g, b = pixels[row + x]
            sr += r
            sg += g
            sb += b
            srr += r * r
            sgg += g * g
            sbb += b * b
    mr, mg, mb = sr / n, sg / n, sb / n
    variance = ((srr / n - mr * mr) +
                (sgg / n - mg * mg) +
                (sbb / n - mb * mb)) / 3.0
    return variance if variance > 0.0 else 0.0


def text_mask(pixels, width, height, block_size,
              texture_scale=DEFAULT_TEXTURE_SCALE):
    """Return a bytearray marking pixels in blocks that saturate the ramp.

    A block saturates when its variance reaches ``texture_scale``; there the
    strength has been driven all the way down to ``strength_min``.  With the
    default ``strength_min == 0`` this is exactly the set of pixels that
    :func:`dither_indices` leaves identical to :func:`palette.map_nearest`, so
    it is the mask the tests and the report use to check that text stays clean.
    """
    validate_params(block_size, 0.0, 1.0, texture_scale)
    _validate_dimensions(pixels, width, height)
    _validate_pixels(pixels, width, height)

    blocks_w = (width + block_size - 1) // block_size
    blocks_h = (height + block_size - 1) // block_size
    mask = bytearray(width * height)
    for by in range(blocks_h):
        y0 = by * block_size
        y1 = min(height, y0 + block_size)
        for bx in range(blocks_w):
            x0 = bx * block_size
            x1 = min(width, x0 + block_size)
            if _block_variance(pixels, width, x0, x1, y0, y1) < texture_scale:
                continue
            for y in range(y0, y1):
                row = y * width
                for x in range(x0, x1):
                    mask[row + x] = 1
    return mask


def block_grid_size(width, height, block_size):
    """Number of block columns/rows used to tile a ``width`` x ``height`` image."""
    if not isinstance(block_size, int) or isinstance(block_size, bool):
        raise ValueError("block_size must be an integer")
    if block_size <= 0:
        raise ValueError("block_size must be > 0")
    _validate_dimensions([None] * (width * height), width, height)
    return ((width + block_size - 1) // block_size,
            (height + block_size - 1) // block_size)


def _block_strengths(pixels, width, height, block_size, strength_min,
                     strength_max, texture_scale):
    blocks_w = (width + block_size - 1) // block_size
    blocks_h = (height + block_size - 1) // block_size
    out = []
    for by in range(blocks_h):
        y0 = by * block_size
        y1 = min(height, y0 + block_size)
        for bx in range(blocks_w):
            x0 = bx * block_size
            x1 = min(width, x0 + block_size)
            variance = _block_variance(pixels, width, x0, x1, y0, y1)
            if variance <= 0.0:
                out.append(strength_max)
                continue
            t = math.sqrt(variance / texture_scale)
            if t > 1.0:
                t = 1.0
            out.append(strength_max - (strength_max - strength_min) * t)
    return out


def block_strengths(pixels, width, height, block_size, strength_min,
                    strength_max, texture_scale=DEFAULT_TEXTURE_SCALE):
    """Return per-block dither strengths in row-major block order.

    The returned list has ``blocks_w * blocks_h`` entries where the first
    ``blocks_w`` entries are the top row of blocks.  Partial edge blocks use the
    pixels that actually exist.
    """
    validate_params(block_size, strength_min, strength_max, texture_scale)
    _validate_dimensions(pixels, width, height)
    _validate_pixels(pixels, width, height)
    return _block_strengths(pixels, width, height, block_size, strength_min,
                            strength_max, texture_scale)


def _clamp_round(value):
    if value <= 0.0:
        return 0
    if value >= 255.0:
        return 255
    return int(round(value))


# Floyd-Steinberg neighbours, expressed relative to the scan direction.
# (dx, dy, weight_units); weights are sixteenths.
_FS_FORWARD = ((1, 0, 7), (-1, 1, 3), (0, 1, 5), (1, 1, 1))
_FS_REVERSE = ((-1, 0, 7), (1, 1, 3), (0, 1, 5), (-1, 1, 1))


def dither_indices(pixels, width, height, palette, block_size, strength_min,
                   strength_max, enabled=True, texture_scale=DEFAULT_TEXTURE_SCALE):
    """Map ``pixels`` to palette indices with block-adaptive dithering.

    ``enabled=False`` (or ``strength_min == strength_max == 0``) reproduces
    :func:`palette.map_nearest` pixel for pixel.  Otherwise the result uses only
    indices into ``palette``.

    Raises ``ValueError`` for invalid block sizes, an empty/invalid palette,
    reversed or out-of-range strength bounds, and mismatched dimensions.  All
    parameters are validated even when ``enabled`` is false.
    """
    pal = validate_palette(palette)
    validate_params(block_size, strength_min, strength_max, texture_scale)
    _validate_dimensions(pixels, width, height)
    _validate_pixels(pixels, width, height)

    if not enabled:
        return map_nearest(pixels, pal)

    strengths = _block_strengths(
        pixels, width, height, block_size, strength_min, strength_max,
        texture_scale,
    )
    blocks_w = (width + block_size - 1) // block_size

    # Error rows are float RGB triples offset by one column so that x-1 is valid.
    cur = [[0.0, 0.0, 0.0] for _ in range(width + 2)]
    out = [0] * (width * height)

    for y in range(height):
        nxt = [[0.0, 0.0, 0.0] for _ in range(width + 2)]
        left_to_right = (y & 1) == 0
        neighbours = _FS_FORWARD if left_to_right else _FS_REVERSE
        by = y // block_size
        row_base = y * width
        for step in range(width):
            x = step if left_to_right else width - 1 - step
            err_in = cur[x + 1]
            r, g, b = pixels[row_base + x]
            rv = r + err_in[0]
            gv = g + err_in[1]
            bv = b + err_in[2]
            qr = _clamp_round(rv)
            qg = _clamp_round(gv)
            qb = _clamp_round(bv)
            qi = nearest_index((qr, qg, qb), pal)
            out[row_base + x] = qi
            strength = strengths[by * blocks_w + x // block_size]
            if strength <= 0.0:
                continue
            pr, pg, pb = pal[qi]
            er = rv - pr
            eg = gv - pg
            eb = bv - pb
            if er == 0.0 and eg == 0.0 and eb == 0.0:
                continue
            for dx, dy, units in neighbours:
                tx = x + dx
                if tx < 0 or tx >= width:
                    continue
                if dy == 0:
                    target = cur
                    target_strength = strengths[by * blocks_w + tx // block_size]
                else:
                    if y + 1 >= height:
                        continue
                    target = nxt
                    target_strength = strengths[
                        (y + 1) // block_size * blocks_w + tx // block_size
                    ]
                # Gate diffusion by the weaker endpoint so textured (text)
                # blocks neither receive nor donate error.
                gate = strength if strength < target_strength else target_strength
                if gate <= 0.0:
                    continue
                share = units * gate / 16.0
                target[tx + 1][0] += er * share
                target[tx + 1][1] += eg * share
                target[tx + 1][2] += eb * share
        cur = nxt

    return out
