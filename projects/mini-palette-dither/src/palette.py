"""Small palette helpers used by the demo script."""

import math

SCAN_MODES = ("forward", "serpentine")

#: Upper bound for every accumulated working value.  Values are clamped to
#: [0, MAX_WORK_VALUE] independently per channel before every neighbour write,
#: so the propagated error can never overflow into another channel or grow
#: without bound.
MAX_WORK_VALUE = 255.0

# Floyd-Steinberg kernel: (dx, dy, weight).  Weights sum to 1.
_KERNEL = (
    (1, 0, 7.0 / 16.0),
    (-1, 1, 3.0 / 16.0),
    (0, 1, 5.0 / 16.0),
    (1, 1, 1.0 / 16.0),
)


def _clamp_work(value):
    if value < 0.0:
        return 0.0
    if value > MAX_WORK_VALUE:
        return MAX_WORK_VALUE
    return float(value)


def _resolve_scan(scan):
    if not isinstance(scan, str) or scan not in SCAN_MODES:
        raise ValueError(
            "unsupported scan mode %r (expected one of: %s)"
            % (scan, ", ".join(SCAN_MODES))
        )
    return scan


def nearest_index(pixel, palette):
    best_i, best_d = 0, None
    for i, c in enumerate(palette):
        d = (pixel[0] - c[0]) ** 2 + (pixel[1] - c[1]) ** 2 + (pixel[2] - c[2]) ** 2
        if best_d is None or d < best_d:
            best_i, best_d = i, d
    return best_i


def map_nearest(pixels, palette):
    return [nearest_index(p, palette) for p in pixels]


def quantize_uniform(values, levels):
    if levels < 2:
        raise ValueError("levels must be >= 2")
    step = 255.0 / (levels - 1)
    return [min(255, max(0, int(round(v / step)) * int(round(step)))) for v in values]


def palette_usage(indices, n_colors):
    counts = [0] * n_colors
    for i in indices:
        counts[i] += 1
    return counts


def _check_palette(palette):
    if not palette:
        raise ValueError("palette must not be empty")
    for c in palette:
        if len(c) != 3:
            raise ValueError("palette entries must be (r, g, b)")


def _check_rows(rows):
    width = None
    for row in rows:
        if width is None:
            width = len(row)
        elif len(row) != width:
            raise ValueError("all rows must have the same width")
        for px in row:
            if len(px) != 3:
                raise ValueError("pixels must be (r, g, b)")
    return 0 if width is None else width


def quantize_diffused(rows, palette, scan="serpentine"):
    """Map RGB pixels to palette indices with Floyd-Steinberg error diffusion.

    ``rows`` is a sequence of rows, each row a sequence of ``(r, g, b)``
    pixels.  ``scan`` selects the traversal order:

    * ``"forward"``     -- every row left-to-right;
    * ``"serpentine"``  -- even rows left-to-right, odd rows right-to-left.

    Any other value raises :class:`ValueError`.

    The accumulated error is kept per channel in a floating-point working
    buffer and clamped to ``[0, MAX_WORK_VALUE]`` on every neighbour write, so
    a pixel can never absorb more than that bound.  Channels are separate
    scalars, hence error propagated in one channel cannot leak into another.
    The caller's pixels are not mutated.
    """
    _resolve_scan(scan)
    _check_palette(palette)
    width = _check_rows(rows)

    work = [[[float(px[c]) for c in range(3)] for px in row] for row in rows]
    height = len(work)
    if height == 0 or width == 0:
        return [[] for _ in rows]

    out = []
    for y in range(height):
        row_out = [0] * width
        reverse = scan == "serpentine" and (y % 2 == 1)
        direction = -1.0 if reverse else 1.0
        columns = range(width - 1, -1, -1) if reverse else range(width)
        for x in columns:
            current = work[y][x]
            idx = nearest_index(current, palette)
            row_out[x] = idx
            chosen = palette[idx]
            error = [current[c] - chosen[c] for c in range(3)]
            for dx, dy, weight in _KERNEL:
                nx = x + int(direction) * dx
                ny = y + dy
                if 0 <= nx < width and 0 <= ny < height:
                    target = work[ny][nx]
                    for c in range(3):
                        target[c] = _clamp_work(target[c] + error[c] * weight)
        out.append(row_out)
    return out


def map_diffused(pixels, palette, width, scan="serpentine"):
    """Flat-buffer wrapper around :func:`quantize_diffused`.

    ``pixels`` is a flat sequence of ``(r, g, b)`` and ``width`` is the row
    length.  Returns a flat list of palette indices with the same length.
    """
    if width <= 0:
        raise ValueError("width must be positive")
    if len(pixels) % width != 0:
        raise ValueError("pixels length must be a multiple of width")
    rows = [list(pixels[y * width:(y + 1) * width])
            for y in range(len(pixels) // width)]
    return [i for row in quantize_diffused(rows, palette, scan) for i in row]


def quantization_error(pixels, indices, palette):
    """Sum of squared per-channel errors for a mapped image."""
    if len(pixels) != len(indices):
        raise ValueError("pixels and indices must have the same length")
    total = 0.0
    for pixel, idx in zip(pixels, indices):
        chosen = palette[idx]
        for c in range(3):
            d = pixel[c] - chosen[c]
            total += d * d
    return total


def usage_entropy(indices, n_colors):
    """Shannon entropy of palette usage, normalised to [0, 1].

    ``1.0`` means every palette colour is used equally often; ``0.0`` means a
    single colour took every pixel.  Empty input returns ``0.0``.
    """
    counts = palette_usage(indices, n_colors)
    total = sum(counts)
    if total == 0:
        return 0.0
    entropy = 0.0
    for count in counts:
        if count:
            p = count / total
            entropy -= p * math.log(p, 2)
    if n_colors < 2:
        return 0.0
    return entropy / math.log(n_colors, 2)

