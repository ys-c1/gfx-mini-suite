"""Stage 1 tests: compaction, remapping and the error contract."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

import palette
from palette import PaletteError


def _raised(fn, position):
    try:
        fn()
    except PaletteError as exc:
        assert exc.position == position, (exc.position, position, str(exc))
        assert position in str(exc), (str(exc), position)
        return exc
    raise AssertionError("expected PaletteError at %s" % position)


def test_empty_palette_is_an_error():
    _raised(lambda: palette.dedupe_palette([]), "palette[0]")
    _raised(lambda: palette.map_nearest([(0, 0, 0)], []), "palette[0]")
    _raised(lambda: palette.palette_usage([], 0), "palette[0]")


def test_index_out_of_range_is_not_clamped():
    _raised(lambda: palette.palette_usage([0, 3], 3), "indices[1]")
    _raised(lambda: palette.palette_usage([-1], 3), "indices[0]")
    _raised(lambda: palette.palette_usage([0.0], 3), "indices[0]")
    _raised(lambda: palette.palette_usage([True], 3), "indices[0]")


def test_bad_colour_names_slot():
    _raised(lambda: palette.dedupe_palette([(0, 0, 0), (1, 2)]), "palette[1]")
    _raised(lambda: palette.dedupe_palette([(0, 0, 256)]), "palette[0]")
    _raised(
        lambda: palette.map_nearest([(0, 0, 0), (1, 1)], [(0, 0, 0)]),
        "pixels[1]",
    )


def test_usage_length_mismatch_names_slot():
    pal = [(0, 0, 0), (1, 1, 1), (2, 2, 2)]
    _raised(lambda: palette.dedupe_palette(pal, [5, 1]), "usage[2]")
    _raised(lambda: palette.dedupe_palette(pal, [5, 1, 2, 9]), "usage[3]")
    _raised(lambda: palette.dedupe_palette(pal, [5, -1, 2]), "usage[1]")


def test_dedupe_removes_duplicates_and_unused():
    pal = [(0, 0, 0), (9, 9, 9), (0, 0, 0), (255, 255, 255), (9, 9, 9)]
    counts = [4, 0, 2, 1, 3]
    m = palette.dedupe_palette(pal, counts)
    # (9,9,9) at slot 1 is unused, but slot 4 is used: keep one copy of it.
    assert m.palette == [(0, 0, 0), (255, 255, 255), (9, 9, 9)]
    assert m.remap == [0, -1, 0, 1, 2]
    assert m.usage == [6, 1, 3]
    assert m.saved_slots == 2
    assert m.enabled and len(m) == 3


def test_duplicate_collapses_even_when_first_copy_is_unused():
    pal = [(5, 5, 5), (5, 5, 5)]
    m = palette.dedupe_palette(pal, [0, 7])
    assert m.palette == [(5, 5, 5)]
    assert m.remap == [-1, 0]
    assert m.usage == [7]


def test_unused_but_unique_colours_can_be_kept():
    pal = [(0, 0, 0), (1, 1, 1), (0, 0, 0)]
    m = palette.dedupe_palette(pal, [2, 0, 0], remove_unused=False)
    assert m.palette == [(0, 0, 0), (1, 1, 1)]
    assert m.remap == [0, 1, 0]


def test_remap_indices():
    pal = [(0, 0, 0), (9, 9, 9), (0, 0, 0), (255, 255, 255), (9, 9, 9)]
    m = palette.dedupe_palette(pal, [4, 0, 2, 1, 3])
    assert palette.remap_indices([0, 2, 3], m) == [0, 0, 1]
    # index 1 and 4 point at dropped slots: an error, not a clamp
    _raised(lambda: palette.remap_indices([0, 1], m), "indices[1]")
    _raised(lambda: palette.remap_indices([9], m), "indices[0]")


def test_remap_length_mismatch_names_position():
    pal = [(0, 0, 0), (1, 1, 1)]
    plain = palette.dedupe_palette(pal)
    bad = palette.PaletteMapping(plain.original, plain.palette, [0], None, True)
    _raised(lambda: palette.validate_mapping(bad), "remap[1]")

    long_table = palette.PaletteMapping(
        plain.original, plain.palette, [0, 1, 2], None, True
    )
    _raised(lambda: palette.validate_mapping(long_table), "remap[2]")


def test_remap_pointing_at_wrong_colour_is_an_error():
    original = [(0, 0, 0), (10, 10, 10)]
    wrong = [(0, 0, 0), (20, 20, 20)]
    bad = palette.PaletteMapping(original, wrong, [0, 1], None, True)
    _raised(lambda: palette.validate_mapping(bad), "remap[1]")


def test_remap_target_past_palette_is_an_error():
    original = [(0, 0, 0), (10, 10, 10)]
    bad = palette.PaletteMapping(original, [(0, 0, 0)], [0, 1], None, True)
    _raised(lambda: palette.validate_mapping(bad), "remap[1]")


def test_all_unused_palette_is_an_error():
    _raised(
        lambda: palette.dedupe_palette([(0, 0, 0), (1, 1, 1)], [0, 0]),
        "palette[0]",
    )


def test_disabled_dedupe_is_an_identity_layout():
    pal = [(0, 0, 0), (0, 0, 0), (7, 7, 7)]
    m = palette.dedupe_palette(pal, [1, 2, 0], enabled=False)
    assert m.palette == pal
    assert m.remap == [0, 1, 2]
    assert not m.enabled
    assert m.saved_slots == 0
    assert palette.remap_indices([2, 0, 1], m) == [2, 0, 1]


def test_original_helpers_still_behave():
    pal = [(0, 0, 0), (255, 255, 255), (255, 0, 0)]
    assert palette.map_nearest([(1, 1, 1), (250, 250, 250), (250, 10, 10)], pal) == [
        0,
        1,
        2,
    ]
    assert palette.palette_usage([0, 0, 1, 2, 2, 2], 3) == [2, 1, 3]
    assert palette.quantize_uniform([0, 64, 128, 200, 255], 4)[0] == 0
