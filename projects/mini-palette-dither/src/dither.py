"""Stage 2: serpentine Floyd-Steinberg on a compacted palette.

The ditherer only ever sees the *new* palette from
:class:`palette.PaletteMapping`, so the indices it returns are already in the
compacted numbering.  It never re-derives or applies ``remap``: stage 1
(:func:`palette.remap_indices`) is what translates old index streams.

Error bound
-----------
Diffusion adds a correction ``c`` to a source pixel before quantising.  Each
distributed term is clamped to ``+-S_idx`` per channel, where ``S_idx`` is one
channel's share of the distance from the chosen entry to its nearest *other*
palette colour:

    S_i = cell_i / sqrt(3),
    cell_i = distance from entry i to the nearest other palette colour,
    S_max  = max_i S_i.

Floyd-Steinberg's weights for one pixel sum to 1 (7/16 + 1/16 + 5/16 + 3/16),
so the correction arriving at any pixel obeys ``|c| <= S_max`` per channel,
i.e. ``|c| <= sqrt(3)*S_max`` as an RGB vector.  Clamping to ``cell_i/sqrt(3)``
keeps every correction inside one palette cell, which is what keeps dithering
from overshooting into a wrong-colour neighbourhood.

Let ``x`` be the source pixel, ``D`` its distance to the nearest palette colour
``q``, and ``p`` the colour emitted for ``x + c``.  Since ``p`` is nearest to
``x + c``,

    |x - p| <= |x + c - q| + |c| <= D + 2|c| <= D + 2*sqrt(3)*S_max

Squaring gives the per-pixel bound :func:`dither` reports.  ``error_bound`` is
the largest such bound in the run, ``max_error`` the error actually measured
against the *source* pixels, and ``within_bound`` checks the latter against
the former.

``factor`` scales every distributed term (0 <= factor; 1 is the usual full
strength).  It does not change the clamp, so the bound above holds for every
factor.  Set ``factor=0`` and every term is zero: the output is exactly the
plain nearest-colour map.  That is how "dedupe off" keeps its historical
output.

Arithmetic is integer in units of 1/16 channel step, so repeated runs are
identical; there is no floating-point accumulation and no randomness.
"""

import math

from palette import PaletteError, _check_color, _dist2, validate_mapping

# Default diffusion strength (scales distributed error terms; 1 is full).
OVERSHOOT_FACTOR = 0.5

# Floyd-Steinberg weights for a left-to-right scan: (dx, dy, numerator) with
# denominator 16.  Forward layout:
#         X   7
#     3   5   1
# ``dx`` is mirrored when the scan runs right-to-left.
_FS_WEIGHTS = (
    (1, 0, 7),
    (-1, 1, 3),
    (0, 1, 5),
    (1, 1, 1),
)
_FS_DENOM = 16

# Work buffer and error clamp are in units of 1/UNIT channel steps.
_UNIT = 16


def _nearest(colour, palette):
    """Nearest palette index, first minimum wins (ties to lowest index)."""
    best_i, best_d = 0, None
    for i, c in enumerate(palette):
        d = _dist2(colour, c)
        if best_d is None or d < best_d:
            best_i, best_d = i, d
    return best_i, best_d


def _neighbour_distance(index, palette):
    """Squared distance from ``palette[index]`` to its nearest other colour."""
    me = palette[index]
    best = None
    for j, c in enumerate(palette):
        if j == index:
            continue
        d = _dist2(me, c)
        if best is None or d < best:
            best = d
    return best


def _steps(palette):
    """Per-entry per-channel clamp ``S_i`` in 0..255 units.

    ``S_i`` is one channel's share of the distance from entry *i* to its
    nearest other colour, i.e. ``cell_i / sqrt(3)``.  A correction clamped to
    ``S_i`` per channel therefore never moves a pixel further than one palette
    cell away from the entry it was measured against.  The diffusion factor is
    applied to the distributed terms, not here, so the clamp stays a property
    of the palette alone.
    """
    if len(palette) == 1:
        return [0.0]
    return [
        math.sqrt(_neighbour_distance(i, palette)) / math.sqrt(3.0)
        for i in range(len(palette))
    ]


def _clamp_int(value, limit):
    if value > limit:
        return limit
    if value < -limit:
        return -limit
    return value


def dither(pixels, width, palette, *, factor=OVERSHOOT_FACTOR):
    """Serpentine Floyd-Steinberg mapping of *pixels* onto *palette*.

    ``pixels`` is a flat row-major sequence of ``(r, g, b)`` ints; ``width``
    must divide its length.  The scan direction only affects diffusion, never
    which index wins when the error term is zero, so ``factor=0`` returns
    exactly the plain nearest-colour map.

    Returns ``(indices, stats)``.  ``indices`` is a flat list in the palette's
    own numbering.  ``stats`` holds:

    ``error_bound``      worst squared RGB error per pixel,
                         ``(D + 2*sqrt(3)*S_max)**2``.
    ``max_error``        largest squared error actually observed vs. source.
    ``within_bound``     ``max_error <= error_bound``.
    ``slots``            palette size used.
    ``clamp``            ``S_max``, the per-channel clamp in 0..255 units.
    ``factor``           the diffusion strength used.
    """
    try:
        width = int(width)
    except (TypeError, ValueError):
        raise PaletteError("width must be an int, got %r" % (width,), "width")
    if width <= 0:
        raise PaletteError("width must be >= 1, got %d" % width, "width")
    if isinstance(pixels, (str, bytes)):
        raise PaletteError("pixels must be a sequence of (r, g, b), not str")
    flat = [_check_color(p, "pixels[%d]" % i) for i, p in enumerate(pixels)]
    if not flat:
        raise PaletteError("image has zero pixels", "pixels[0]")
    if len(flat) % width:
        raise PaletteError(
            "image has %d pixels, not a multiple of width %d" % (len(flat), width),
            "pixels[%d]" % len(flat),
        )
    if not palette:
        raise PaletteError("palette has zero colours", "palette[0]")
    entries = [_check_color(c, "palette[%d]" % i) for i, c in enumerate(palette)]
    factor = float(factor)
    if not math.isfinite(factor) or factor < 0:
        raise PaletteError(
            "factor must be a finite non-negative number, got %r" % (factor,)
        )

    steps = _steps(entries)
    s_max = max(steps)
    # Corrections are clamped to S_max per channel and the diffusion weights
    # sum to 1, so a pixel's correction is at most sqrt(3)*S_max as an RGB
    # vector; ``correction`` below is twice that, matching the derivation.
    correction = 2.0 * math.sqrt(3.0) * s_max
    # Clamp in 1/16 units.  ``factor`` scales the terms, so with factor <= 1 the
    # clamp is never reached before scaling; it still bounds the sum.
    step_units = [int(math.floor(step * _UNIT + 1e-9)) for step in steps]

    height = len(flat) // width
    # Work buffers hold 16 * channel + accumulated correction.
    work = [
        [[flat[y * width + x][k] * _UNIT for k in range(3)] for x in range(width)]
        for y in range(height)
    ]
    out = [0] * len(flat)
    max_error = 0
    error_bound = 0.0

    def choose(units):
        """Nearest entry to a 1/16-unit colour, comparing without rounding."""
        best_i, best_d = 0, None
        for i, c in enumerate(entries):
            d = (
                (units[0] - c[0] * _UNIT) ** 2
                + (units[1] - c[1] * _UNIT) ** 2
                + (units[2] - c[2] * _UNIT) ** 2
            )
            if best_d is None or d < best_d:
                best_i, best_d = i, d
        return best_i

    for y in range(height):
        reverse = bool(y % 2)
        xs = range(width - 1, -1, -1) if reverse else range(width)
        for x in xs:
            pos = y * width + x
            units = work[y][x]
            idx = choose(units)
            out[pos] = idx
            chosen = entries[idx]

            source = flat[pos]
            source_sq = _nearest(source, entries)[1]
            # ``correction`` is in 0..255 units, ``source_sq`` is squared.
            if correction:
                bound_here = (math.sqrt(source_sq) + correction) ** 2
            else:
                bound_here = float(source_sq)
            if bound_here > error_bound:
                error_bound = bound_here
            observed = _dist2(source, chosen)
            if observed > max_error:
                max_error = observed

            limit = step_units[idx]
            if limit == 0:
                continue
            # Error in 1/16 units: (work - chosen*16) is the pixel's residual.
            residual = [units[k] - chosen[k] * _UNIT for k in range(3)]
            for dx, dy, num in _FS_WEIGHTS:
                nx = x - dx if reverse else x + dx
                ny = y + dy
                if 0 <= nx < width and ny < height:
                    target = work[ny][nx]
                    for k in range(3):
                        # Truncate towards zero, then clamp: each distributed
                        # term is at most ``limit * num / 16`` units and the
                        # weights sum to 16, so the correction arriving at a
                        # pixel is at most ``limit`` units = S_max even though
                        # the four incoming terms are summed.
                        e = _clamp_int(
                            int(residual[k] * factor * num / _FS_DENOM), limit
                        )
                        target[k] += e

    stats = {
        "slots": len(entries),
        "error_bound": error_bound,
        "max_error": max_error,
        "within_bound": max_error <= error_bound,
        "clamp": s_max,
        "factor": factor,
    }
    return out, stats


def dither_mapping(pixels, width, mapping, *, factor=OVERSHOOT_FACTOR):
    """Dither onto ``mapping.palette``, i.e. the compacted table.

    ``mapping`` is a :class:`palette.PaletteMapping`; it is validated first, so
    a remap table built for a different palette cannot slip through.  The
    return value is ``(new_indices, stats)`` with indices already in the
    compacted numbering.
    """
    validate_mapping(mapping)
    if len(mapping.palette) == 0:
        raise PaletteError("palette has zero colours", "palette[0]")
    indices, stats = dither(pixels, width, mapping.palette, factor=factor)
    stats["enabled"] = mapping.enabled
    return indices, stats
