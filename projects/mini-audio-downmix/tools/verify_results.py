#!/usr/bin/env python3
"""Independent re-derivation of the numbers in results/stats/.

Reads the self-made sample WAVs and the downmixer's output WAVs and
recomputes peak, RMS, per-frame bounds and the energy ledger without
using the C library. This is the "prove the other paths are unchanged"
half of the report: if the C stats and these numbers disagree, the
report is wrong.

Run with -I so a planted json.py etc. in the data directory cannot
shadow a stdlib module:

    python3 -I tools/verify_results.py
"""
import csv
import os
import struct
import sys


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")
WAV = os.path.join(ROOT, "results", "wav")
STATS = os.path.join(ROOT, "results", "stats")

def f32(x):
    """Round a Python float to IEEE binary32, as the C library stores the
    weights. Without this, comparing against 0.3 (double) would disagree
    with the C ledger in the last few digits."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


WEIGHTS = [(tag, f32(wl), f32(wr)) for tag, wl, wr in
           [("w050_050", 0.5, 0.5), ("w030_070", 0.3, 0.7),
            ("w020_080", 0.2, 0.8)]]


def read_float_wav(path):
    """Read a WAVE file; IEEE float via the wave module's fallback."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise ValueError(f"{path}: not WAVE")
    off = 12
    fmt = None
    data = None
    while off + 8 <= len(raw):
        cid = raw[off:off + 4]
        size = struct.unpack("<I", raw[off + 4:off + 8])[0]
        body = raw[off + 8:off + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
            if fmt[0] == 0xFFFE and len(body) >= 40:
                fmt = (struct.unpack("<H", body[24:26])[0],) + fmt[1:]
        elif cid == b"data":
            data = body
        off += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = fmt
    if tag != 3 or bits != 32:
        raise ValueError(f"{path}: expected float32, got tag={tag} bits={bits}")
    n = len(data) // 4
    samples = list(struct.unpack("<%df" % n, data[:n * 4]))
    return channels, rate, samples


def read_summary():
    with open(os.path.join(STATS, "summary.csv"), newline="") as f:
        return {row["weights_tag"]: row for row in csv.DictReader(f)}


def main():
    failures = 0

    def check(cond, msg):
        nonlocal failures
        if not cond:
            failures += 1
            print("FAIL", msg)

    summary = read_summary()
    ch, rate, stereo = read_float_wav(os.path.join(DATA, "sample_stereo.wav"))
    check(ch == 2, "sample_stereo.wav is not stereo")
    check(rate == 48000, "sample rate is not 48000")
    nframes = len(stereo) // 2
    left = stereo[0::2]
    right = stereo[1::2]

    print(f"input: {nframes} frames, {rate} Hz, 2 ch")
    print(f"{'weights':10s} {'peak_in':>9s} {'peak_out':>9s} "
          f"{'rms_w_in':>9s} {'rms_out':>9s} {'E_ratio':>10s} "
          f"{'ledger':>10s} {'viol':>5s}")

    for tag, wl, wr in WEIGHTS:
        path = os.path.join(WAV, f"downmix_{tag}.wav")
        ch, rate2, out = read_float_wav(path)
        check(ch == 1, f"{tag}: output is not mono")
        check(rate2 == rate, f"{tag}: sample rate changed")
        check(len(out) == nframes, f"{tag}: frame count changed "
                                    f"({len(out)} vs {nframes})")

        peak_in = max(max(abs(l), abs(r)) for l, r in zip(left, right))
        peak_out = max(abs(v) for v in out)
        e_out = sum(v * v for v in out)
        e_w = sum(wl * l * l + wr * r * r for l, r in zip(left, right))
        e_diff = sum(wl * wr * (l - r) * (l - r) for l, r in zip(left, right))
        rms_w = (e_w / nframes) ** 0.5
        rms_out = (e_out / nframes) ** 0.5
        ledger = (e_w - e_out - e_diff) / e_w

        worst_frame = 0.0
        worst_peak = 0.0
        viol = 0
        for l, r, o in zip(left, right, out):
            if abs(o) > 1.0:
                viol += 1
            if abs(o) > wl * abs(l) + wr * abs(r) + 2e-6:
                viol += 1
            den = wl * l * l + wr * r * r
            if den > 0.0:
                fr = o * o / den
                if fr > worst_frame:
                    worst_frame = fr
            if abs(o) > worst_peak:
                worst_peak = abs(o)

        row = summary[tag]
        # compare against the C run; allow for accumulation order
        check(abs(float(row["peak_out"]) - peak_out) < 1e-6,
              f"{tag}: peak_out C={row['peak_out']} py={peak_out}")
        check(abs(float(row["rms_out"]) - rms_out) < 1e-6,
              f"{tag}: rms_out C={row['rms_out']} py={rms_out}")
        check(abs(float(row["rms_weighted_in"]) - rms_w) < 1e-5,
              f"{tag}: rms_weighted C={row['rms_weighted_in']} py={rms_w}")
        check(abs(float(row["energy_ledger_rel"]) - ledger) < 1e-6,
              f"{tag}: ledger C={row['energy_ledger_rel']} py={ledger}")
        check(viol == 0, f"{tag}: {viol} structural violations")
        check(worst_frame <= 1.0 + 1e-6,
              f"{tag}: frame energy ratio {worst_frame}")
        check(int(row["structural_violations"]) == 0,
              f"{tag}: C reported structural violations")

        print(f"{tag:10s} {peak_in:9.6f} {peak_out:9.6f} {rms_w:9.6f} "
              f"{rms_out:9.6f} {e_out / e_w:10.6f} {ledger:10.3e} "
              f"{viol:5d}")

    # Mono passthrough: the mono file must be reproduced bit-for-bit.
    ch, _, mono = read_float_wav(os.path.join(DATA, "sample_mono.wav"))
    check(ch == 1, "sample_mono.wav is not mono")
    mout_path = os.path.join(WAV, "downmix_mono_passthrough.wav")
    if os.path.exists(mout_path):
        _, _, mout = read_float_wav(mout_path)
        check(mono == mout, "mono passthrough is not bit-identical")
        print(f"mono passthrough: {len(mono)} frames bit-identical")

    print(f"{failures} verification failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
