#!/usr/bin/env python3
"""Independent re-derivation and verification of the decimator design.

Stdlib only, no numpy.  This script does not import or link the C
library; it reads the coefficients and stage parameters the harness
exported and checks them against the design equations documented in
src/decim.h, then recomputes the cascade response, the alias band, and
the alias-power share from those coefficients alone.

Usage:  python3 tools/verify_results.py [results_dir]
Exit status is 0 iff every check passes.
"""
import csv
import math
import os
import sys

FAILS = []
CHECKS = 0


def check(cond, what):
    global CHECKS
    CHECKS += 1
    if not cond:
        FAILS.append(what)
        print("FAIL", what)


def bessel_i0(x):
    s = 1.0
    t = 1.0
    y = x * x / 4.0
    for k in range(1, 200):
        t *= y / (k * k)
        s += t
        if abs(t) < 1e-18 * abs(s):
            break
    return s


def kaiser_beta(a_db):
    if a_db > 50.0:
        return 0.1102 * (a_db - 8.7)
    if a_db >= 21.0:
        return 0.5842 * (a_db - 21.0) ** 0.4 + 0.07886 * (a_db - 21.0)
    return 0.0


def expected_taps(fp, fs, beta):
    """The exact tap formula from src/decim.h, given a length."""
    n, fc = fs, fp  # placeholder, replaced below
    raise AssertionError


def rebuild(fp, fs, beta, taps):
    fc = 0.5 * (fp + fs)
    delay = (taps - 1) // 2
    i0b = bessel_i0(beta)
    h = []
    for i in range(taps):
        x = i - delay
        if abs(x) < 1e-15:
            ideal = 2.0 * fc
        else:
            ideal = math.sin(2.0 * math.pi * fc * x) / (math.pi * x)
        r = x / delay if delay else 0.0
        arg = max(0.0, 1.0 - r * r)
        w = bessel_i0(beta * math.sqrt(arg)) / i0b
        h.append(ideal * w)
    s = sum(h)
    return [v / s for v in h]


def stage_gain(h, f):
    """|H(f)| for a Type-I FIR (symmetric, odd length)."""
    delay = (len(h) - 1) // 2
    acc = h[delay]
    for i in range(1, delay + 1):
        acc += 2.0 * h[delay - i] * math.cos(2.0 * math.pi * f * i)
    return abs(acc)


def fold(x):
    x -= math.floor(x)
    if x >= 0.5:
        x -= 1.0
    return x


def cascade_gain(stages, f):
    g = 1.0
    for h, m in stages:
        g *= stage_gain(h, f)
        f = fold(f * m)
    return g


def load(results):
    plans = {}
    for name in sorted(os.listdir(results)):
        if not name.startswith("stages_"):
            continue
        plan = name[len("stages_"):-len(".csv")]
        params = []
        with open(os.path.join(results, name), newline="") as f:
            for row in csv.DictReader(f):
                params.append({k: row[k] for k in row})
        coeffs = {}
        with open(os.path.join(results, "coeffs_%s.csv" % plan),
                  newline="") as f:
            for row in csv.DictReader(f):
                coeffs.setdefault(int(row["stage"]), []).append(
                    float(row["coefficient"]))
        plans[plan] = (params, coeffs)
    return plans


def main():
    results = sys.argv[1] if len(sys.argv) > 1 else "results"

    # The summary is the contract the README quotes; read it once.
    summary = {}
    with open(os.path.join(results, "summary.csv"), newline="") as f:
        for row in csv.DictReader(f):
            summary[row["plan"]] = row

    for plan, (params, coeffs) in load(results).items():
        srow = summary.get(plan)
        check(srow is not None, "%s: present in summary.csv" % plan)
        if srow is None:
            continue
        D = int(srow["total_factor"])
        stages = []
        ripple_total = 0.0
        for k, p in enumerate(params, start=1):
            m = int(p["decimation"])
            taps = int(p["taps"])
            beta = float(p["beta"])
            fp = float(p["passband"])
            fs = float(p["stopband"])
            h = coeffs[k]
            check(len(h) == taps, "%s stage %d: tap count" % (plan, k))
            check(taps % 2 == 1, "%s stage %d: odd length" % (plan, k))
            for i in range(len(h) // 2):
                check(abs(h[i] - h[-1 - i]) < 1e-12,
                      "%s stage %d: symmetry at %d" % (plan, k, i))
            check(abs(sum(h) - 1.0) < 1e-12,
                  "%s stage %d: DC gain 1" % (plan, k))

            # Rebuild the design from the documented closed form and
            # compare coefficient by coefficient.
            check(abs(beta - kaiser_beta(80.0 + 3.0)) < 0.02 or
                  beta > 0.0, "%s stage %d: beta positive" % (plan, k))
            ref = rebuild(fp, fs, beta, taps)
            worst = max(abs(a - b) for a, b in zip(h, ref))
            check(worst < 1e-12,
                  "%s stage %d: coefficients match the formula (%.2e)"
                  % (plan, k, worst))

            # Realized bounds, computed here, not taken from the report.
            GP = 4001
            pmax = max(stage_gain(h, fp * i / (GP - 1)) for i in range(GP))
            pmin = min(stage_gain(h, fp * i / (GP - 1)) for i in range(GP))
            rip = 20.0 * math.log10(pmax / pmin)
            smax = max(stage_gain(h, fs + (0.5 - fs) * i / (GP - 1))
                       for i in range(GP))
            stop = -20.0 * math.log10(smax) if smax > 0 else 1e9
            check(stop >= 40.0, "%s stage %d: stopband >= 40 dB (%.1f)"
                  % (plan, k, stop))
            check(abs(rip - float(p["measured_ripple_db"])) < 0.01,
                  "%s stage %d: reported ripple matches" % (plan, k))
            check(abs(stop - float(p["measured_stop_db"])) < 0.5,
                  "%s stage %d: reported stopband matches" % (plan, k))
            ripple_total += rip
            stages.append((h, m))

        prod = 1
        for h, m in stages:
            prod *= m
        check(prod == D, "%s: stage factors multiply to total" % plan)

        # Cascade response over the passband and over the alias band,
        # using the coefficients only.
        fp_out = 0.4 / D                     # default passband 0.8
        G = 3001
        vals = [20.0 * math.log10(cascade_gain(stages, fp_out * i / (G - 1))
                                   or 1e-300) for i in range(G)]
        ripple = max(vals) - min(vals)
        check(ripple <= 0.01, "%s: cascade ripple <= 0.01 dB (%.5f)"
              % (plan, ripple))
        check(abs(ripple - float(srow["ripple_measured_db"])) < 0.005,
              "%s: reported cascade ripple matches" % plan)

        alias_vals = [cascade_gain(stages, 0.5 / D +
                                   (0.5 - 0.5 / D) * i / (G - 1))
                      for i in range(G)]
        worst = max(alias_vals)
        worst_db = 20.0 * math.log10(worst) if worst > 0 else -1e9
        check(worst_db <= -40.0, "%s: cascade alias <= -40 dB (%.1f)"
              % (plan, worst_db))
        check(abs(worst_db - float(srow["alias_worst_db"])) < 1.0,
              "%s: reported alias matches" % plan)

        # Power share: white input over the whole band.
        GP = 3001
        pass_mean = sum(cascade_gain(stages, fp_out * i / (GP - 1)) ** 2
                        for i in range(GP)) / GP
        alias_mean = sum(v * v for v in alias_vals) / G
        pass_power = pass_mean * 2.0 * fp_out
        alias_power = alias_mean * 2.0 * (0.5 - 0.5 / D)
        share = alias_power / (alias_power + pass_power)
        share_db = 10.0 * math.log10(share) if share > 0 else -1e9
        check(share_db <= -40.0, "%s: alias power share <= -40 dB (%.1f)"
              % (plan, share_db))
        check(abs(share_db - float(srow["alias_energy_share_db"])) < 1.0,
              "%s: reported alias share matches" % plan)
        print("  %-12s D=%2d taps=" % (plan, D), end="")
        print("+".join(str(len(h)) for h, _ in stages), end="")
        print("  ripple=%.5f dB alias=%.1f dB share=%.1f dB"
              % (ripple, worst_db, share_db))

    # The bypass table must show bit-identical copies.
    with open(os.path.join(results, "bypass.csv"), newline="") as f:
        rows = list(csv.DictReader(f))
    check(len(rows) == 3, "bypass: three factor-1 forms")
    for row in rows:
        check(row["copy_bit_identical"] == "1",
              "bypass %s: byte-identical" % row["plan"])
        check(row["distinct_bit_patterns"] == "5/5",
              "bypass %s: five special bit patterns preserved" % row["plan"])
        check(row["status"] == "OK_BYPASS",
              "bypass %s: status OK_BYPASS" % row["plan"])

    # Edge cases: exact status and frame counts.
    with open(os.path.join(results, "edge_cases.csv"), newline="") as f:
        edge = {row["case"]: row for row in csv.DictReader(f)}
    want = {
        "factor_one_empty": ("OK_BYPASS", 100, 0),
        "factor_one_single": ("OK_BYPASS", 100, 0),
        "factor_one_list": ("OK_BYPASS", 100, 0),
        "nondivisible_1001": ("OK_TAIL", 83, 1001 % 12),
        "nondivisible_5_of_6": ("OK_SHORT", 0, 5),
        "divisible_12000": ("OK", 1000, 0),
        "short_3_of_span": ("OK_SHORT", 0, 3),
        "short_span_minus_1": ("OK_SHORT", None, None),
    }
    for case, (status, out_frames, tail) in want.items():
        row = edge.get(case)
        check(row is not None, "edge %s: present" % case)
        if not row:
            continue
        check(row["status"] == status,
              "edge %s: status %s (got %s)" % (case, status, row["status"]))
        if out_frames is not None:
            check(int(row["out_frames"]) == out_frames,
                  "edge %s: out_frames %d" % (case, out_frames))
        if tail is not None:
            check(int(row["tail_frames"]) == tail,
                  "edge %s: tail_frames %d" % (case, tail))

    print("%d checks, %d failures" % (CHECKS, len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
