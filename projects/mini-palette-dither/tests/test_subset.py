import os
import subprocess
import sys
import time

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

import demo_images
import palette
import subset


def source_palette():
    cube = [(r * 51, g * 51, b * 51)
            for r in range(6) for g in range(6) for b in range(6)]
    grays = [(5 * k, 5 * k, 5 * k) for k in range(1, 41)]
    return cube + grays


PAL = source_palette()
K = 32


def histograms():
    """The three demo images, plus a few synthetic weight vectors."""
    out = {}
    for name, img in demo_images.build_images():
        out[name] = subset.histogram(img.reshape(-1, 3), PAL)
    rng = np.random.default_rng(0)
    out["random"] = rng.integers(0, 1000, len(PAL))
    out["uniform"] = np.ones(len(PAL), dtype=np.int64)
    sparse = np.zeros(len(PAL), dtype=np.int64)
    sparse[[1, 5, 27, 60, 90, 140, 200, 255]] = 7
    out["sparse"] = sparse
    return out


# --- input contract --------------------------------------------------------

def test_empty_and_zero_count():
    assert subset.select_palette([], 0) == []
    assert subset.select_palette([], 0, []) == []
    try:
        subset.select_palette([], 1)
    except ValueError:
        pass
    else:
        raise AssertionError("empty palette with k > 0 must raise")


def test_invalid_counts():
    for bad in (-1, len(PAL) + 1, 300):
        try:
            subset.select_palette(PAL, bad)
        except ValueError:
            continue
        raise AssertionError("k=%r must raise ValueError" % (bad,))
    try:
        subset.select_palette(PAL, 1.5)
    except ValueError:
        pass
    else:
        raise AssertionError("non-integer k must raise")


def test_full_palette_is_identity():
    assert subset.select_palette(PAL, len(PAL)) == list(range(len(PAL)))
    # With the whole table selected the mapping error is exactly zero.
    colors = subset.normalize_palette(PAL)
    dist = subset.distance_matrix(colors)
    weights = np.ones(len(PAL), dtype=np.int64)
    assert subset.error_of(dist, weights, list(range(len(PAL)))) == 0


def test_flat_and_bad_palette():
    flat = [v for c in PAL[:4] for v in c]
    colors = subset.normalize_palette(flat)
    assert colors.shape == (4, 3)
    for bad in ([1, 2], [(1, 2, 3), (4, 5)], [(1, 2, 256)],
                [(1.0, 2, 3), (0, 0, 0)], [(-1, 0, 0)]):
        try:
            subset.normalize_palette(bad)
        except ValueError:
            continue
        raise AssertionError("palette %r must be rejected" % (bad,))


def test_bad_weights():
    for bad in ([1, 2, 3], [0] * (len(PAL) + 1), [-1] * len(PAL)):
        try:
            subset.select_palette(PAL, K, bad)
        except ValueError:
            continue
        raise AssertionError("weights %r must be rejected" % (bad,))
    # Zero weights are allowed: they just mean "this color is never used".
    zero = [0] * len(PAL)
    zero[17] = 5
    picked = subset.select_palette(PAL, K, zero)
    assert len(picked) == K and len(set(picked)) == K


# --- selection invariants --------------------------------------------------

def test_selection_is_a_distinct_subset_of_the_table():
    seen = {tuple(c) for c in PAL}
    for name, w in histograms().items():
        picked = subset.select_palette(PAL, K, w)
        assert len(picked) == K, name
        assert len(set(picked)) == K, name + ": duplicate indices"
        for i in picked:
            assert 0 <= i < len(PAL), name
            assert tuple(PAL[i]) in seen, name


def test_duplicate_table_rows_stay_distinct_indices():
    table = [(0, 0, 0), (0, 0, 0), (255, 0, 0), (255, 0, 0), (0, 255, 0)]
    # k is 4, so with five rows exactly one row - a duplicate of another -
    # is left out; whichever it is, no index may repeat.
    picked = subset.select_palette(table, 4, [3, 2, 4, 1, 5])
    assert len(picked) == 4 and len(set(picked)) == 4, picked
    assert set(picked) < set(range(5)), picked


# --- quality contract ------------------------------------------------------

def test_never_worse_than_either_baseline():
    for name, w in histograms().items():
        r = subset.compare_methods(PAL, K, w)
        best = min(r["greedy"]["error"], r["median-cut"]["error"])
        assert r["ours"]["error"] <= best, (
            name, r["ours"]["error"], r["greedy"]["error"], r["median-cut"]["error"])
        # The hard contract: at most 1.05x the better baseline.  Because both
        # baselines are starting points of our search, 1.0x actually holds.
        assert r["ours"]["error"] <= 1.05 * best or best == 0
        assert r["ratio"] <= 1.05


def test_error_matches_independent_objective():
    """compare_methods' error must equal a direct O(n*k) computation."""
    for name, w in histograms().items():
        r = subset.compare_methods(PAL, K, w)
        for method in subset.METHODS:
            idx = r[method]["indices"]
            total = 0
            for c_pos, color in enumerate(PAL):
                nearest = min((color[0] - PAL[i][0]) ** 2
                              + (color[1] - PAL[i][1]) ** 2
                              + (color[2] - PAL[i][2]) ** 2 for i in idx)
                total += int(w[c_pos]) * nearest
            assert total == r[method]["error"], (name, method, total, r[method]["error"])


def test_local_search_reaches_one_swap_optimum():
    """On a small table, no single swap may improve the final selection."""
    small = [(0, 0, 0), (40, 0, 0), (80, 20, 0), (120, 40, 20), (160, 90, 40),
             (200, 150, 90), (255, 200, 140), (10, 60, 160), (40, 120, 220),
             (90, 200, 250), (200, 40, 200), (255, 255, 255)]
    rng = np.random.default_rng(42)
    w = rng.integers(1, 100, len(small))
    colors = subset.normalize_palette(small)
    dist = subset.distance_matrix(colors)
    picked = subset.select_palette(small, 4, w)
    base = subset.error_of(dist, w, picked)
    free = [c for c in range(len(small)) if c not in picked]
    for pos in range(4):
        for c in free:
            trial = list(picked)
            trial[pos] = c
            assert subset.error_of(dist, w, trial) >= base


def test_baselines_are_plain_and_deterministic():
    w = histograms()["random"]
    r1 = subset.compare_methods(PAL, K, w)
    r2 = subset.compare_methods(PAL, K, w)
    assert r1 == r2
    colors = subset.normalize_palette(PAL)
    dist = subset.distance_matrix(colors)
    g = subset.greedy_select(dist, w, K)
    m = subset.median_cut_select(colors, w, K)
    assert r1["greedy"]["indices"] == sorted(g)
    assert r1["median-cut"]["indices"] == sorted(m)


# --- determinism -----------------------------------------------------------

def test_repeated_calls_are_identical():
    for name, w in histograms().items():
        first = subset.select_palette(PAL, K, w)
        for _ in range(3):
            assert subset.select_palette(PAL, K, w) == first, name


def test_identical_across_processes_and_hash_seeds():
    code = (
        "import sys, json;"
        "sys.path.insert(0, %r);"
        "import subset;"
        "w = [((i * 37) %% 101) + (i < 40) * 300 for i in range(27)];"
        "print(json.dumps(subset.select_palette([(r, g, b) for r in (0, 128, 255) for g in (0, 128, 255) for b in (0, 128, 255)], 12, w[:27])))"
        % os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src")
    )
    outputs = []
    for seed in ("0", "1", "12345"):
        env = dict(os.environ, PYTHONHASHSEED=seed)
        out = subprocess.run([sys.executable, "-c", code], env=env,
                             capture_output=True, text=True, check=True)
        outputs.append(out.stdout.strip())
    assert len(set(outputs)) == 1, outputs


# --- mapping is unchanged --------------------------------------------------

def test_full_table_mapping_is_unchanged():
    img = demo_images.build_images()[1][1]  # scene, fixed pixels
    pixels = img.reshape(-1, 3)
    ref = palette.map_nearest([tuple(int(v) for v in p) for p in pixels], PAL)
    got = subset.map_nearest_rows(pixels, PAL).tolist()
    assert got == ref
    # And the histogram is exactly the distribution of that mapping.
    counts = subset.histogram(pixels, PAL)
    assert counts.tolist() == np.bincount(np.asarray(ref), minlength=len(PAL)).tolist()


def test_selecting_whole_table_leaves_mapping_output_untouched():
    img = demo_images.build_images()[0][1]
    pixels = img.reshape(-1, 3)
    full = subset.select_palette(PAL, len(PAL))
    assert subset.map_nearest_rows(pixels, [PAL[i] for i in full]).tolist() == \
        palette.map_nearest([tuple(int(v) for v in p) for p in pixels], PAL)


# --- budget ----------------------------------------------------------------

def test_search_time_under_200ms():
    for name, w in histograms().items():
        t0 = time.perf_counter()
        subset.select_palette(PAL, K, w)
        elapsed_ms = (time.perf_counter() - t0) * 1000.0
        assert elapsed_ms <= 200.0, (name, elapsed_ms)


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
