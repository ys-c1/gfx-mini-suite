"""Palette helpers: nearest-colour mapping and palette compaction.

This module is stage 1 of the palette pipeline:

* :func:`dedupe_palette` removes duplicate and unused entries and returns a
  :class:`PaletteMapping` holding the new palette plus the old -> new index
  table (``remap``).
* :func:`remap_indices` applies that table to stored index streams.

Stage 2 (:mod:`dither`) receives the compacted palette and produces indices in
the new numbering, so it never sees the removed slots.

Validation never clamps.  An empty palette, an index outside the palette, a
usage histogram of the wrong length and a remap table whose length or targets
do not line up with the palette all raise :class:`PaletteError`, which names
the offending position (``indices[7]``, ``usage[64]``, ``remap[64]``,
``palette[0]``, ...) both as ``exc.position`` and in the message.

Compacting a palette is stage 1; dithering on the compacted table is stage 2
in :mod:`dither`.  Deduplication can be switched off with ``enabled=False``,
which yields an identity mapping so stage 2 reproduces the historical output
byte for byte.
"""

# Position markers are part of the public error contract: tests and callers
# use them to point at the bad element.  Keep them stable.

# A colour is an (r, g, b) triple of ints in 0..255.
_COLOR_LEN = 3
_COLOR_MAX = 255


class PaletteError(IndexError, ValueError):
    """A palette, index or remap table is invalid.

    Subclasses both ``IndexError`` (an index is outside the palette) and
    ``ValueError`` (a size or shape is wrong), so either ``except`` clause
    catches it.

    ``position`` is a short string such as ``"indices[7]"`` or ``"remap[64]"``.
    It is always also appended to the message, so ``str(exc)`` alone is enough
    to locate the problem.
    """

    def __init__(self, message, position=None):
        self.message = str(message)
        self.position = position
        if position is not None:
            message = "%s (at %s)" % (message, position)
        super().__init__(message)


def _dist2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def _fail(message, position=None):
    raise PaletteError(message, position)


# ---------------------------------------------------------------------------
# validation
# ---------------------------------------------------------------------------

def _check_color(color, position):
    try:
        channels = tuple(color)
    except TypeError:
        _fail("expected an (r, g, b) triple, got %r" % (color,), position)
    if len(channels) != _COLOR_LEN:
        _fail(
            "expected an (r, g, b) triple, got %d channel(s) in %r"
            % (len(channels), color),
            position,
        )
    for value in channels:
        if isinstance(value, bool) or not isinstance(value, int):
            _fail("colour channel %r is not an int in %r" % (value, color), position)
        if not 0 <= value <= _COLOR_MAX:
            _fail(
                "colour channel %d is outside 0..255 in %r" % (value, color),
                position,
            )
    return channels


def validate_palette(palette):
    """Return *palette* as a list of validated ``(r, g, b)`` tuples.

    An empty palette is an error; the position is reported as ``palette[0]``
    because that is the missing entry the caller has to supply.
    """
    if isinstance(palette, (str, bytes)):
        _fail("palette must be a sequence of (r, g, b) triples")
    try:
        entries = list(palette)
    except TypeError:
        _fail("palette must be a sequence of (r, g, b) triples")
    if not entries:
        _fail("palette has zero colours; at least one is required", "palette[0]")
    return [
        _check_color(color, "palette[%d]" % i) for i, color in enumerate(entries)
    ]


def _check_counts(counts, n, name):
    if counts is None:
        return None
    try:
        values = list(counts)
    except TypeError:
        _fail("%s must be a sequence of ints" % name)

    if len(values) != n:
        _fail(
            "%s has length %d but palette has %d entries" % (name, len(values), n),
            "%s[%d]" % (name, min(len(values), n)),
        )
    for i, value in enumerate(values):
        if isinstance(value, bool) or not isinstance(value, int):
            _fail("%s[%d] = %r is not an int" % (name, i, value), "%s[%d]" % (name, i))
        if value < 0:
            _fail("%s[%d] = %d is negative" % (name, i, value), "%s[%d]" % (name, i))
    return values


def _check_index(index, n, position):
    if isinstance(index, bool) or not isinstance(index, int):
        _fail("index %r is not an int" % (index,), position)
    if not 0 <= index < n:
        _fail("index %d is outside the palette's %d entries" % (index, n), position)
    return index


def _check_enabled(value):
    if not isinstance(value, bool):
        _fail("enabled must be True or False, got %r" % (value,), "enabled")
    return value


def _check_index_stream(indices, n, name="indices"):
    if isinstance(indices, (str, bytes)):
        _fail("%s must be a sequence of ints, not str" % name)
    try:
        values = list(indices)
    except TypeError:
        _fail("%s must be a sequence of ints" % name)
    return [
        _check_index(value, n, "%s[%d]" % (name, i))
        for i, value in enumerate(values)
    ]


# ---------------------------------------------------------------------------
# original (unchanged) public helpers
# ---------------------------------------------------------------------------

def _nearest(pixel, entries):
    """Nearest entry of an already-validated palette; ties to lowest index."""
    best_i, best_d = 0, None
    for i, c in enumerate(entries):
        d = _dist2(pixel, c)
        if best_d is None or d < best_d:
            best_i, best_d = i, d
    return best_i


def nearest_index(pixel, palette):
    """Index of the palette entry nearest to *pixel* (squared RGB distance).

    Ties resolve to the lowest index, i.e. the first entry in palette order.
    """
    p = _check_color(pixel, "pixel")
    return _nearest(p, validate_palette(palette))


def map_nearest(pixels, palette):
    if isinstance(pixels, (str, bytes)):
        _fail("pixels must be a sequence of (r, g, b), not str")
    entries = validate_palette(palette)
    return [
        _nearest(_check_color(p, "pixels[%d]" % i), entries)
        for i, p in enumerate(pixels)
    ]


def quantize_uniform(values, levels):
    if levels < 2:
        raise ValueError("levels must be >= 2")
    step = 255.0 / (levels - 1)
    return [min(255, max(0, int(round(v / step)) * int(round(step)))) for v in values]


def palette_usage(indices, n_colors):
    """Usage histogram of *indices* for a palette of *n_colors* entries.

    Indices outside ``0..n_colors-1`` are an error and are never clamped.
    """
    if isinstance(n_colors, bool) or not isinstance(n_colors, int):
        _fail("n_colors must be an int, got %r" % (n_colors,), "n_colors")
    if n_colors == 0:
        _fail("palette has zero colours; at least one is required", "palette[0]")
    if n_colors < 0:
        _fail("n_colors must be >= 1, got %d" % n_colors, "n_colors")
    counts = [0] * n_colors
    for i in _check_index_stream(indices, n_colors):
        counts[i] += 1
    return counts


# ---------------------------------------------------------------------------
# stage 1: compaction
# ---------------------------------------------------------------------------

class PaletteMapping:
    """A compacted palette and the table that produced it.

    Attributes
    ----------
    original : list of (r, g, b)
        The input palette, unchanged.
    palette : list of (r, g, b)
        The compacted palette.  Entry order is the original order with dropped
        entries removed, so the kept entries keep their relative order.
    remap : list of int
        ``remap[old]`` is the new index of old entry *old*, or ``-1`` if that
        entry was dropped.
    usage : list of int or None
        Per-entry usage counts for ``palette`` (not for ``original``).  ``None``
        when the caller did not supply usage information.
    enabled : bool
        False for the identity mapping returned with deduplication switched off.
    """

    __slots__ = ("original", "palette", "remap", "usage", "enabled")

    def __init__(self, original, palette, remap, usage=None, enabled=True):
        self.original = original
        self.palette = palette
        self.remap = remap
        self.usage = usage
        self.enabled = enabled

    @property
    def original_size(self):
        return len(self.original)

    @property
    def saved_slots(self):
        return len(self.original) - len(self.palette)

    def __len__(self):
        return len(self.palette)

    def __repr__(self):
        return (
            "PaletteMapping(original=%d, palette=%d, saved=%d, enabled=%r)"
            % (
                len(self.original),
                len(self.palette),
                self.saved_slots,
                self.enabled,
            )
        )


def dedupe_palette(palette, usage=None, *, remove_unused=True, enabled=True):
    """Remove duplicate and unused palette entries.

    Duplicate colours collapse onto the first *kept* occurrence, so the old ->
    new table is monotone on the entries it keeps.  An entry is unused when
    its own usage count is zero; that is only known when *usage* is supplied,
    and ``remove_unused=False`` turns that rule off while still collapsing
    duplicates.

    With ``enabled=False`` this is a no-op for the layout: every entry is kept
    in order and ``remap = range(n)``, so stage 2 reproduces exactly the output
    the project had before deduplication existed.

    Raises
    ------
    PaletteError
        Empty input palette, malformed colour, usage length mismatch, or a
        result that would contain zero colours because every entry is unused.
    """
    _check_enabled(enabled)
    original = validate_palette(palette)
    n = len(original)
    counts = _check_counts(usage, n, "usage")

    if not enabled:
        return PaletteMapping(
            original,
            list(original),
            list(range(n)),
            None if counts is None else list(counts),
            enabled=False,
        )

    new_palette = []
    new_usage = [] if counts is not None else None
    seen = {}
    remap = [-1] * n

    for old, color in enumerate(original):
        if counts is not None and remove_unused and counts[old] == 0:
            # Dropped here; a later copy of the same colour may still be kept
            # when its own turn comes.
            continue
        if color in seen:
            new_index = seen[color]
            remap[old] = new_index
            if new_usage is not None:
                new_usage[new_index] += counts[old]
        else:
            new_index = len(new_palette)
            seen[color] = new_index
            remap[old] = new_index
            new_palette.append(color)
            if new_usage is not None:
                new_usage.append(counts[old])

    if not new_palette:
        _fail(
            "deduplication removed every colour; the compacted palette would "
            "have zero entries",
            "palette[0]",
        )

    mapping = PaletteMapping(original, new_palette, remap, new_usage, enabled=True)
    validate_mapping(mapping)
    return mapping


def validate_mapping(mapping):
    """Check a :class:`PaletteMapping`; return it.

    This is where a remap table built for another palette is rejected.  The
    error position names the first bad table slot:

    * ``len(remap) != len(original)`` -> ``remap[<first mismatch>]``
    * ``remap[old]`` outside ``-1 .. len(palette)-1`` -> ``remap[old]``
    * ``palette[remap[old]] != original[old]`` -> ``remap[old]``
    * usage length not equal to ``len(palette)`` -> ``usage[<first mismatch>]``
    """
    original = validate_palette(mapping.original)
    palette = validate_palette(mapping.palette)
    try:
        remap = list(mapping.remap)
    except TypeError:
        _fail("remap must be a sequence of ints", "remap")

    if len(remap) != len(original):
        _fail(
            "remap has length %d but palette has %d entries"
            % (len(remap), len(original)),
            "remap[%d]" % min(len(remap), len(original)),
        )

    for old, new in enumerate(remap):
        if isinstance(new, bool) or not isinstance(new, int):
            _fail("remap[%d] = %r is not an int" % (old, new), "remap[%d]" % old)
        if new < -1:
            _fail("remap[%d] = %d is below -1" % (old, new), "remap[%d]" % old)
        if new >= len(palette):
            _fail(
                "remap[%d] = %d points past the %d-colour palette"
                % (old, new, len(palette)),
                "remap[%d]" % old,
            )
        if new >= 0 and palette[new] != original[old]:
            _fail(
                "remap[%d] = %d but palette[%d] = %r differs from "
                "original[%d] = %r"
                % (old, new, new, palette[new], old, original[old]),
                "remap[%d]" % old,
            )

    if mapping.usage is not None:
        _check_counts(mapping.usage, len(palette), "usage")
    return mapping


def remap_indices(indices, mapping):
    """Translate stored indices through ``mapping.remap``.

    An index pointing at a dropped entry (``remap[old] == -1``) is an error,
    not a silent -1 or clamp: the caller is mixing an index stream with a
    palette that no longer contains what the stream refers to.
    """
    validate_mapping(mapping)
    old_n = len(mapping.original)
    new_n = len(mapping.palette)
    out = []
    for position, old in enumerate(_check_index_stream(indices, old_n)):
        new = mapping.remap[old]
        if new < 0:
            _fail(
                "index %d refers to an entry removed by deduplication "
                "(remap[%d] = -1)" % (old, old),
                "indices[%d]" % position,
            )
        # validate_mapping already checked new < new_n; keep the bound here in
        # case a caller mutates the mapping between calls.
        if not 0 <= new < new_n:
            _fail(
                "remap[%d] = %d points outside the %d-colour palette"
                % (old, new, new_n),
                "remap[%d]" % old,
            )
        out.append(new)
    return out
