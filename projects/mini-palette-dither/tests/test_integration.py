"""End-to-end checks against the repository's C packer.

These tests exercise the requirement that dithering does not inflate the
packer's output beyond 1.15x the undithered size, that the packer round-trips,
and that two separate processes produce byte-identical index planes for the same
input (no dependence on hash seeds, dictionary order or floating point
iteration order).
"""

import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "src"))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import dither  # noqa: E402
import sample_gen  # noqa: E402

CLI_BIN = os.path.join(ROOT, "build", "bitpack_cli")
SIZE_LIMIT = 1.15
BLOCK_SIZES = (6, 12)
STRENGTH_MIN = 0.0
STRENGTH_MAX = 0.75
TEXTURE_SCALE = 64.0
BPP = 2

_CROSS_PROCESS = r"""
import hashlib, sys
sys.path.insert(0, %(src)r)
sys.path.insert(0, %(tools)r)
import dither, sample_gen
sample = [s for s in sample_gen.SAMPLES if s.name == %(name)r][0]
w, h, rgb = sample_gen.render_pixels(sample)
px = sample_gen.to_rgb_list(rgb)
pal = dither.validate_palette(sample.palette)
idx = dither.dither_indices(px, w, h, pal, %(block)d,
                            %(smin)r, %(smax)r, texture_scale=%(scale)r)
print(hashlib.sha256(bytes(idx)).hexdigest())
"""


def _pack(indices, width, height):
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "idx.raw")
        out = os.path.join(tmp, "idx.pack")
        with open(raw, "wb") as fh:
            fh.write(bytes(indices))
        result = subprocess.run(
            [CLI_BIN, str(width), str(height), str(BPP), raw, out],
            capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise RuntimeError(result.stderr)
        return os.path.getsize(out), result.stdout.strip()


def _run_sample(sample, block):
    width, height, rgb = sample_gen.render_pixels(sample)
    pixels = sample_gen.to_rgb_list(rgb)
    pal = dither.validate_palette(sample.palette)
    plain = dither.dither_indices(pixels, width, height, pal, block,
                                  STRENGTH_MIN, STRENGTH_MAX, enabled=False,
                                  texture_scale=TEXTURE_SCALE)
    dith = dither.dither_indices(pixels, width, height, pal, block,
                                 STRENGTH_MIN, STRENGTH_MAX,
                                 texture_scale=TEXTURE_SCALE)
    return plain, dith, width, height


def test_packer_size_within_budget():
    assert os.path.isfile(CLI_BIN), "build/bitpack_cli missing; run make test"
    for sample in sample_gen.SAMPLES:
        for block in BLOCK_SIZES:
            plain, dith, width, height = _run_sample(sample, block)
            plain_len, plain_stats = _pack(plain, width, height)
            dith_len, dith_stats = _pack(dith, width, height)
            assert dith_len <= plain_len * SIZE_LIMIT, (
                "%s/%d: %d > %.2f * %d" % (sample.name, block, dith_len,
                                           SIZE_LIMIT, plain_len))
            # Fixed-rate packer: both must be the same 2bpp frame size.
            assert dith_len == (width * height * BPP + 7) // 8
            assert dith_stats.split()[0] == str(dith_len)
            assert int(dith_stats.split()[1]) <= 4


def test_index_plane_is_reproducible_across_processes():
    assert os.path.isfile(CLI_BIN), "build/bitpack_cli missing; run make test"
    script = _CROSS_PROCESS % {
        "src": os.path.join(ROOT, "src"),
        "tools": os.path.join(ROOT, "tools"),
        "name": "vgrad",
        "block": 6,
        "smin": STRENGTH_MIN,
        "smax": STRENGTH_MAX,
        "scale": TEXTURE_SCALE,
    }
    digests = []
    for seed in ("0", "1", "123456"):
        env = dict(os.environ, PYTHONHASHSEED=seed)
        result = subprocess.run([sys.executable, "-c", script],
                                capture_output=True, text=True, env=env,
                                check=False)
        assert result.returncode == 0, result.stderr
        digests.append(result.stdout.strip())
    assert len(set(digests)) == 1, digests


def test_repeat_call_byte_identical():
    sample = sample_gen.SAMPLES[2]
    _, first, width, height = _run_sample(sample, 12)
    _, second, _, _ = _run_sample(sample, 12)
    assert hashlib.sha256(bytes(first)).digest() == \
        hashlib.sha256(bytes(second)).digest()


def test_packed_round_trip():
    sample = sample_gen.SAMPLES[1]
    _, dith, width, height = _run_sample(sample, 6)
    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, "idx.raw")
        with open(raw, "wb") as fh:
            fh.write(bytes(dith))
        out = os.path.join(tmp, "idx.pack")
        subprocess.run([CLI_BIN, str(width), str(height), str(BPP), raw, out],
                       check=True, capture_output=True)
        with open(out, "rb") as fh:
            data = fh.read()
    # Unpack with the Python mirror of bitpack.c and compare.
    recovered = bytearray(width * height)
    bit = 0
    for i in range(width * height):
        value = 0
        for _ in range(BPP):
            byte = data[bit >> 3]
            value = (value << 1) | ((byte >> (7 - (bit & 7))) & 1)
            bit += 1
        recovered[i] = value
    assert recovered == bytes(dith)


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
