import random
import sys
sys.path.insert(0, "src")

import palette


def test_nearest_identity():
    pal = [(0, 0, 0), (255, 255, 255), (255, 0, 0)]
    assert palette.map_nearest([(1, 1, 1), (250, 250, 250), (250, 10, 10)], pal) == [0, 1, 2]


def test_uniform_levels():
    out = palette.quantize_uniform([0, 64, 128, 200, 255], 4)
    assert len(out) == 5
    assert out[0] == 0 and out[-1] == 255
    assert all(0 <= v <= 255 for v in out)


def test_usage_counts():
    assert palette.palette_usage([0, 0, 1, 2, 2, 2], 3) == [2, 1, 3]


def test_invalid_levels():
    try:
        palette.quantize_uniform([1, 2], 1)
    except ValueError:
        return
    raise AssertionError("expected ValueError")


def test_index_empty_palette():
    # Must match nearest_index on an empty palette: both return 0.
    idx = palette.PaletteIndex([])
    assert idx.query((10, 20, 30)) == 0
    assert palette.nearest_index((10, 20, 30), []) == 0


def test_index_single_color():
    idx = palette.PaletteIndex([(40, 80, 160)])
    assert idx.query((0, 0, 0)) == 0
    assert idx.query((255, 255, 255)) == 0
    assert idx.query((40, 80, 160)) == 0
    assert len(idx) == 1


def test_index_ties_pick_smallest_index():
    # (0,0,0) and (2,0,0) are both distance 1 from (1,0,0).
    pal = [(0, 0, 0), (2, 0, 0), (-2, 0, 0)]
    idx = palette.PaletteIndex(pal)
    assert idx.query((1, 0, 0)) == palette.nearest_index((1, 0, 0), pal) == 0
    # Three-way tie at distance 4 from (0,0,0).
    pal2 = [(0, 2, 0), (2, 0, 0), (0, 0, 2)]
    idx2 = palette.PaletteIndex(pal2)
    assert idx2.query((0, 0, 0)) == palette.nearest_index((0, 0, 0), pal2) == 0


def test_index_matches_brute_force_fixed():
    pal = [(0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 255, 0),
           (0, 0, 255), (128, 128, 0), (64, 64, 64), (200, 100, 50)]
    idx = palette.PaletteIndex(pal)
    probes = [(r, g, b) for r in range(0, 256, 17)
              for g in range(0, 256, 19) for b in range(0, 256, 23)]
    for p in probes:
        assert idx.query(p) == palette.nearest_index(p, pal), p


def test_index_matches_brute_force_random():
    rng = random.Random(20261120)
    for n in (2, 3, 5, 16, 64, 200, 500):
        pal = [(rng.randrange(256), rng.randrange(256), rng.randrange(256))
               for _ in range(n)]
        idx = palette.PaletteIndex(pal)
        for _ in range(300):
            p = (rng.randrange(256), rng.randrange(256), rng.randrange(256))
            assert idx.query(p) == palette.nearest_index(p, pal), (n, p)


def test_index_matches_brute_force_small_domain():
    # Tiny colour domain forces many exact-distance ties and duplicates.
    rng = random.Random(7)
    pal = [(rng.randrange(4), rng.randrange(4), rng.randrange(4))
           for _ in range(40)]
    idx = palette.PaletteIndex(pal)
    for r in range(4):
        for g in range(4):
            for b in range(4):
                p = (r, g, b)
                assert idx.query(p) == palette.nearest_index(p, pal), p


def test_index_incremental_add_matches_brute_force():
    rng = random.Random(99)
    pal = [(rng.randrange(256), rng.randrange(256), rng.randrange(256))
           for _ in range(8)]
    idx = palette.PaletteIndex(pal)
    working = list(pal)
    for _ in range(64):
        c = (rng.randrange(256), rng.randrange(256), rng.randrange(256))
        new_i = idx.add(c)
        working.append(c)
        assert new_i == len(working) - 1
        for _ in range(20):
            p = (rng.randrange(256), rng.randrange(256), rng.randrange(256))
            assert idx.query(p) == palette.nearest_index(p, working), p
    assert list(idx.palette) == working


def test_index_rebuild_and_snapshot():
    pal = [(1, 2, 3), (4, 5, 6)]
    idx = palette.PaletteIndex(pal)
    pal.append((7, 8, 9))  # mutating the caller's list must not affect the index
    assert len(idx) == 2
    idx.rebuild(pal)
    assert len(idx) == 3
    assert idx.query((7, 8, 9)) == 2
    assert idx.query((0, 0, 0)) == palette.nearest_index((0, 0, 0), pal)


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
