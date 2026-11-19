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
