"""Measure block-adaptive dithering on the generated gradient+text samples.

Run from ``projects/mini-palette-dither`` (``make bench`` does this)::

    python3 tools/bench_dither.py [--json out.json]

For every sample and every block size the report compares

  * the plain nearest-colour mapping (dithering disabled) and
  * the block-adaptive dithering result,

giving the mean colour difference against the source, the luminance deviation
upper bound, the packed size after the repository's own C packer
(``mini-bitplane-pack``) and wall time.  The run fails if any invariant is
broken:

  * indices stay within the palette,
  * two runs on the same input are byte-identical,
  * packed size is at most 1.15x the undithered packed size,
  * pixels in textured (text) blocks are unchanged, because
    ``strength_min == 0`` there.

Note on the 1.15x budget: ``mini-bitplane-pack`` is a *fixed-rate* row packer --
its output is exactly ``w*h*bpp`` bits for both the dithered and the undithered
index plane.  At 2bpp (four colours) the ratio is therefore 1.000 by
construction and the budget is always met.  The report also prints the deflate
size of the packed stream: that is where dithering's extra entropy shows up, and
it is well above 1.15.  If this pipeline ever moves from the fixed-rate packer to
an entropy-coded one, this is the number that matters.
"""

import json
import os
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "src"))
sys.path.insert(0, HERE)

import dither  # noqa: E402
import sample_gen  # noqa: E402

CLI_BIN = os.path.join(ROOT, "build", "bitpack_cli")

BLOCK_SIZES = (6, 12)
STRENGTH_MIN = 0.0
STRENGTH_MAX = 0.75
TEXTURE_SCALE = 64.0
SIZE_LIMIT = 1.15


def _mean_color_error(src_rgb, idx, pal):
    total = 0
    for i, qi in enumerate(idx):
        pr, pg, pb = pal[qi]
        total += (abs(src_rgb[i * 3] - pr) +
                  abs(src_rgb[i * 3 + 1] - pg) +
                  abs(src_rgb[i * 3 + 2] - pb))
    return total / (len(idx) * 3.0)


def _luma(r, g, b):
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def _luma_stats(src_rgb, idx, pal):
    """Signed mean luminance bias and worst per-pixel |dLuma|."""
    total = 0.0
    worst = 0.0
    for i, qi in enumerate(idx):
        pr, pg, pb = pal[qi]
        d = (_luma(pr, pg, pb) -
             _luma(src_rgb[i * 3], src_rgb[i * 3 + 1], src_rgb[i * 3 + 2]))
        total += d
        if abs(d) > worst:
            worst = abs(d)
    return total / len(idx), worst


def _tile_error(src_rgb, idx, pal, width, height, tile, luma_only=False):
    """Per-tile error of the tile's mean colour (banding) or mean luma."""
    total = 0.0
    worst = 0.0
    tiles = 0
    for by in range(0, height, tile):
        for bx in range(0, width, tile):
            sr = sg = sb = er = eg = eb = 0.0
            count = 0
            for y in range(by, min(height, by + tile)):
                for x in range(bx, min(width, bx + tile)):
                    i = y * width + x
                    q = pal[idx[i]]
                    sr += src_rgb[i * 3]
                    sg += src_rgb[i * 3 + 1]
                    sb += src_rgb[i * 3 + 2]
                    er += q[0]
                    eg += q[1]
                    eb += q[2]
                    count += 1
            if luma_only:
                err = abs(_luma(sr, sg, sb) - _luma(er, eg, eb)) / count
            else:
                err = (abs(sr - er) + abs(sg - eg) + abs(sb - eb)) / (3.0 * count)
            total += err
            if err > worst:
                worst = err
            tiles += 1
    return total / tiles, worst


def _pack_size(index_bytes, width, height, bpp):
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "idx.raw")
        out = os.path.join(tmp, "idx.pack")
        with open(raw, "wb") as fh:
            fh.write(bytes(index_bytes))
        result = subprocess.run(
            [CLI_BIN, str(width), str(height), str(bpp), raw, out],
            capture_output=True, text=True, check=False,
        )
        if result.returncode != 0:
            raise RuntimeError("bitpack_cli failed: " + result.stderr)
        packed_len, used = result.stdout.split()
        with open(out, "rb") as fh:
            packed = fh.read()
        return int(packed_len), int(used), packed


def _deflate(data):
    return len(zlib.compress(bytes(data), 9))


def _required_bpp(index_bytes):
    """Smallest bpp (1..8) the packer can use that still holds every value."""
    top = max(index_bytes)
    bpp = 1
    while (1 << bpp) - 1 < top:
        bpp += 1
    return bpp


def _text_stats(mask, dithered_idx, plain_idx):
    inked = 0
    changed = 0
    for i, m in enumerate(mask):
        if m:
            inked += 1
            if dithered_idx[i] != plain_idx[i]:
                changed += 1
    return inked, changed


def _timed(fn):
    t0 = time.perf_counter()
    result = fn()
    return result, time.perf_counter() - t0


def run_sample(sample, block_size):
    width, height, rgb = sample_gen.render_pixels(sample)
    pixels = sample_gen.to_rgb_list(rgb)
    pal = dither.validate_palette(sample.palette)
    bpp = 2  # four colours

    plain_idx, t_plain = _timed(
        lambda: dither.dither_indices(
            pixels, width, height, pal, block_size,
            STRENGTH_MIN, STRENGTH_MAX,
            enabled=False, texture_scale=TEXTURE_SCALE))
    dithered_idx, t_dither = _timed(
        lambda: dither.dither_indices(
            pixels, width, height, pal, block_size,
            STRENGTH_MIN, STRENGTH_MAX,
            enabled=True, texture_scale=TEXTURE_SCALE))
    repeat_idx = dither.dither_indices(
        pixels, width, height, pal, block_size,
        STRENGTH_MIN, STRENGTH_MAX,
        enabled=True, texture_scale=TEXTURE_SCALE)
    deterministic = repeat_idx == dithered_idx

    if not all(0 <= i < len(pal) for i in dithered_idx + plain_idx):
        raise AssertionError("index outside palette")

    plain_pack, plain_used, plain_bytes = _pack_size(plain_idx, width, height, bpp)
    dith_pack, dith_used, dith_bytes = _pack_size(dithered_idx, width, height, bpp)
    ratio = dith_pack / plain_pack if plain_pack else float("inf")
    # Context: fixed-rate packing hides the entropy dithering adds, so report
    # what a downstream compressor sees.  Not a pass/fail criterion.
    plain_deflate = _deflate(plain_bytes)
    dith_deflate = _deflate(dith_bytes)
    deflate_ratio = dith_deflate / plain_deflate if plain_deflate else float("inf")
    # A palette that does not use the top index needs fewer bits per pixel than
    # four colours imply; show whether dithering forces a wider frame.
    plain_bpp = _required_bpp(plain_idx)
    dith_bpp = _required_bpp(dithered_idx)

    mask = dither.text_mask(pixels, width, height, block_size)
    inked, text_changed = _text_stats(mask, dithered_idx, plain_idx)

    plain_bias, plain_bound = _luma_stats(rgb, plain_idx, pal)
    dith_bias, dith_bound = _luma_stats(rgb, dithered_idx, pal)
    plain_tile, plain_tile_worst = _tile_error(
        rgb, plain_idx, pal, width, height, 8, luma_only=True)
    dith_tile, dith_tile_worst = _tile_error(
        rgb, dithered_idx, pal, width, height, 8, luma_only=True)
    plain_rgb_tile, _ = _tile_error(rgb, plain_idx, pal, width, height, 8)
    dith_rgb_tile, _ = _tile_error(rgb, dithered_idx, pal, width, height, 8)

    return {
        "sample": sample.name,
        "description": sample.description,
        "palette": list(pal),
        "block_size": block_size,
        "size": [width, height],
        "bpp": bpp,
        "mean_color_error_plain": _mean_color_error(rgb, plain_idx, pal),
        "mean_color_error_dither": _mean_color_error(rgb, dithered_idx, pal),
        "luma_bias_plain": plain_bias,
        "luma_bias_dither": dith_bias,
        "luma_error_bound_plain": plain_bound,
        "luma_error_bound_dither": dith_bound,
        "tile_luma_error_plain": plain_tile,
        "tile_luma_error_dither": dith_tile,
        "tile_luma_error_worst_plain": plain_tile_worst,
        "tile_luma_error_worst_dither": dith_tile_worst,
        "tile_rgb_error_plain": plain_rgb_tile,
        "tile_rgb_error_dither": dith_rgb_tile,
        "packed_bytes_plain": plain_pack,
        "packed_bytes_dither": dith_pack,
        "packed_ratio": ratio,
        "packed_deflate_bytes_plain": plain_deflate,
        "packed_deflate_bytes_dither": dith_deflate,
        "packed_deflate_ratio": deflate_ratio,
        "required_bpp_plain": plain_bpp,
        "required_bpp_dither": dith_bpp,
        "zlib_bytes_plain": len(zlib.compress(bytes(plain_idx), 9)),
        "zlib_bytes_dither": len(zlib.compress(bytes(dithered_idx), 9)),
        "zlib_ratio": (len(zlib.compress(bytes(dithered_idx), 9)) /
                       len(zlib.compress(bytes(plain_idx), 9))),
        "used_values_plain": plain_used,
        "used_values_dither": dith_used,
        "ms_plain": t_plain * 1000.0,
        "ms_dither": t_dither * 1000.0,
        "deterministic": deterministic,
        "suppressed_pixels": inked,
        "suppressed_pixels_changed": text_changed,
        "within_size_limit": ratio <= SIZE_LIMIT,
    }


def print_report(rows):
    header = ("sample", "block", "mean err plain", "mean err dith",
              "tile dL plain", "tile dL dith", "tile worst plain",
              "tile worst dith", "bias plain", "bias dith", "luma bound",
              "pack B", "pack ratio", "deflate ratio", "bpp p/d", "ms",
              "text chg")
    widths = (7, 5, 14, 13, 13, 12, 15, 14, 10, 9, 10, 6, 10, 13, 7, 6, 9)
    print("  ".join(h.ljust(w) for h, w in zip(header, widths)))
    print("  ".join("-" * w for w in widths))
    for r in rows:
        cells = (
            r["sample"], r["block_size"],
            "%.2f" % r["mean_color_error_plain"],
            "%.2f" % r["mean_color_error_dither"],
            "%.2f" % r["tile_luma_error_plain"],
            "%.2f" % r["tile_luma_error_dither"],
            "%.2f" % r["tile_luma_error_worst_plain"],
            "%.2f" % r["tile_luma_error_worst_dither"],
            "%+.2f" % r["luma_bias_plain"],
            "%+.2f" % r["luma_bias_dither"],
            "%.2f" % r["luma_error_bound_dither"],
            r["packed_bytes_dither"],
            "%.3f" % r["packed_ratio"],
            "%.2f" % r["packed_deflate_ratio"],
            "%d/%d" % (r["required_bpp_plain"], r["required_bpp_dither"]),
            "%.0f" % r["ms_dither"],
            "%d/%d" % (r["suppressed_pixels_changed"],
                       r["suppressed_pixels"]),
        )
        print("  ".join(str(c).ljust(w) for c, w in zip(cells, widths)))
    print()
    print("strength_min=%.2f strength_max=%.2f texture_scale=%.1f "
          "size_limit=%.2f (2bpp / four colours)" % (
              STRENGTH_MIN, STRENGTH_MAX, TEXTURE_SCALE, SIZE_LIMIT))
    print("'tile dL' / 'tile worst' = mean and worst per-8x8-tile |dLuma| of "
          "the tile mean (banding).")
    print("'bias' = signed mean luminance bias over all pixels.")
    print("'luma bound' = worst per-pixel |dLuma|.")
    print("'pack ratio' = bytes from mini-bitplane-pack, dithered / plain.")
    print("               That packer is fixed-rate, so this is 1.000 for "
          "every row;")
    print("               the 1.15 budget is met structurally, not by luck.")
    print("'deflate ratio' = zlib of the packed stream, dithered / plain: the "
          "entropy")
    print("               view, and where dithering's real cost shows up.")
    print("'bpp p/d' = bits per pixel the two index planes actually need; a "
          "wider")
    print("               dithered value would push the fixed-rate frame up.")
    print("'text chg' = pixels in saturated (text) blocks whose index differs "
          "from the undithered mapping; must be 0.")


def main(argv):
    if not os.path.isfile(CLI_BIN):
        print("missing %s; run `make bench` from the project root" % CLI_BIN,
              file=sys.stderr)
        return 2
    rows = []
    for sample in sample_gen.SAMPLES:
        for block_size in BLOCK_SIZES:
            rows.append(run_sample(sample, block_size))
    print_report(rows)

    failures = []
    for r in rows:
        label = "%s/%d" % (r["sample"], r["block_size"])
        if not r["deterministic"]:
            failures.append(label + " not deterministic")
        if not r["within_size_limit"]:
            failures.append("%s size ratio %.3f > %.2f"
                            % (label, r["packed_ratio"], SIZE_LIMIT))
        if r["suppressed_pixels_changed"] != 0:
            failures.append("%s changed %d suppressed pixels"
                            % (label, r["suppressed_pixels_changed"]))
        if r["used_values_dither"] > 4:
            failures.append("%s used %d values" % (label, r["used_values_dither"]))
    if failures:
        for f in failures:
            print("FAIL", f)
        return 1
    print("all invariants hold")

    if "--json" in argv:
        out_path = argv[argv.index("--json") + 1]
        with open(out_path, "w") as fh:
            json.dump({
                "block_sizes": list(BLOCK_SIZES),
                "strength_min": STRENGTH_MIN,
                "strength_max": STRENGTH_MAX,
                "texture_scale": TEXTURE_SCALE,
                "size_limit": SIZE_LIMIT,
                "rows": rows,
            }, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print("wrote", out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
