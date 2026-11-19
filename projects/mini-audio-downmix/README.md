# mini-audio-downmix

Weighted stereo→mono downmixer (C11, no dependencies) for devices whose
output is mono. It replaces "just take the left channel", which halves the
energy and drags the image to one side.

```
make test          # build + run the unit tests (6.7M checks)
make report        # regenerate data/ and results/, then verify them
make regression    # prove the five other projects are unchanged
make test-sanitize # unit tests under ASan + UBSan
```

Everything below is produced by those commands; the numbers are in
`results/stats/` and the audio in `results/wav/`. `make regression`
extracts the committed tree into a scratch directory, rebuilds the five
pre-existing projects there, and compares both the source hashes and the
full `make test` output against the working tree. Result
(`results/regression/summary.txt`): `UNCHANGED`, every file byte-identical
and identical stdout (54 / 102 / 445 / 4 / 264 checks, 0 failures). The
only edit outside this directory is one row added to the suite README's
project table.

---

## 1. The API and the three diagnosable results

```c
int downmix_frame(const float* in, size_t frames, int channels_in,
                  float wl, float wr, float* out, DownmixStats* stats);
```

| situation | result | status code |
|---|---|---|
| mixed normally | `DOWNMIX_OK` | `0` |
| mono input, copied through | `DOWNMIX_OK_MONO` | `1` |
| at least one input frame at ±full scale | `DOWNMIX_OK_FULL_SCALE` | `2` |
| weights illegal (negative / non-finite / not summing to 1) | `DOWNMIX_E_WEIGHTS` | `-2` |
| `channels_in` not 1 or 2 | `DOWNMIX_E_CHANNELS` | `-3` |
| null pointer | `DOWNMIX_E_NULL` | `-1` |

The three cases the requirement calls out are **distinguishable both by
return code and by side effect**: a rejected call writes nothing to
`out`, a successful one writes exactly `frames` samples. See
`results/stats/edge_cases.csv`, where `output_untouched` is 1 for every
rejected call:

```
case                      ch  wl    wr   code  name            frames  untouched
nominal_stereo            2   0.5   0.5   0    OK              100     1
illegal_weights_sum       2   0.5   0.6  -2    E_WEIGHTS         0     1
illegal_weights_negative  2  -0.1   1.1  -2    E_WEIGHTS         0     1
illegal_weights_nan       2   nan   0.5  -2    E_WEIGHTS         0     1
unsupported_channels_3    3   0.5   0.5  -3    E_CHANNELS        0     1
unsupported_channels_4    4   0.5   0.5  -3    E_CHANNELS        0     1
unsupported_channels_0    0   0.5   0.5  -3    E_CHANNELS        0     1
mono_passthrough          1   0.5   0.5   1    OK_MONO         100     1
mono_ignores_bad_weights  1   0     0     1    OK_MONO         100     1
full_scale_stereo         2   0.5   0.5   2    OK_FULL_SCALE     4     1
```

## 2. Mono input

`channels_in == 1` is copied sample-for-sample (`memmove`-safe, and
`out == in` is allowed). Weights are **not consulted** for mono: a single
channel is already the programme material, and applying `wL` to it would
attenuate a mono file. The call reports `DOWNMIX_OK_MONO` and increments
`stats.mono_passthrough_frames`, so a caller can see that this path ran.

The requirement's precedence is therefore documented and tested: mono
wins over weight validation (the `mono_ignores_bad_weights` row above),
and over full-scale reporting (there is no full-scale counter on the mono
path, because scaling is not happening at all). `results/wav/
downmix_mono_passthrough.wav` is bit-identical to `data/sample_mono.wav`;
the unit test and `tools/verify_results.py` both compare bytes.

## 3. Weights and energy

Weights are legal iff both are finite, non-negative, and sum to 1 within
`1e-6` (`downmix_weights_valid` returns a reason string). For every frame

```
out     = wL*L + wR*R
out^2  <= wL*L^2 + wR*R^2                 -- never creates energy
|out|  <= wL*|L| + wR*|R| <= max(|L|,|R|) -- never exceeds the input envelope
```

**What "energy conserved when the weights sum to one" means precisely.**
The exact per-frame ledger is

```
out^2 + wL*wR*(L-R)^2 == wL*L^2 + wR*R^2
```

so nothing is lost: the weighted input energy is split into the mono
programme plus the side/difference energy `wL*wR*(L-R)^2`. For **coherent**
channels (`L == R`) the side term is zero and `out == L`:

| weights | coherent energy ratio | mix error vs. L |
|---|---|---|
| 0.5/0.5 | 1.000000000000 | 0 |
| 0.3/0.7 | 1.000000006750 | 8.3e-8 |
| 0.25/0.75 | 1.000000000000 | 0 |
| 0/1, 1/0 | 1.000000000000 | 0 |

Conservation for coherent material is **bit-exact for dyadic weights**
(½, ¼, 0, 1); 0.3f+0.7f is not exactly 1 in binary, so the residual is
float rounding, not the mixer. For **uncorrelated** material the ledger
still balances — but a mono fold-down cannot carry the side energy, so
`sum(out²) < sum(weighted in²)`. That is physics, not a bug; the mix ratio
in the table below is the honest measure.

For a linear, memoryless map this is the only possible behaviour: any
sum-to-one mono combination loses the difference energy, and 0.5/0.5 is
the minimum-loss point. The header states this rather than pretending
otherwise.

## 4. Peaks and steps

**Per-frame peak.** With legal weights and in-range input, `|out|` cannot
exceed `max(|L|,|R|) ≤ 1`. `structural_violations` in `summary.csv` is 0
for all runs and all 96 000 frames; `tools/verify_results.py` recomputes
the same check in Python from the WAV files and also finds 0. A saturator
exists only for out-of-contract input (`|sample| > 1`) and is counted in
`stats.clipped_frames`; it is **never reached** for legal input
(`clipped_frames = 0` everywhere). With full-scale input the output peeks
to exactly 1.0, still inside the bound (`results/stats/fullscale.csv`).

**No energy jumps.** The mixer is memoryless with no per-block gain, so
(a) a random partition of the signal is **bit-identical** to processing it
whole, and (b) the output step is bounded by the weighted input step,
`|Δout| ≤ wL|ΔL| + wR|ΔR|`. Both are measured:

```
block_frames  blocks  worst_block_energy_ratio  partition_bit_identical
64            1500    0.995444                   yes
256            375    0.987287                   yes
1024            93    0.981179                   yes
4096            23    0.973693                   yes
16384            5    0.957352                   yes
```

and `steps_*.csv` gives the worst step amplification with a rounding
budget (input rounding + product rounding + output storage): 0.9999997,
0.9999997, 0.9999998 for the three weight sets. No frame amplifies a step,
and no block gains energy. Material that jumps on its own still jumps; the
mixer never adds a jump and never smooths one away.

## 5. Peak / RMS before and after, with timing

Self-made 2 s, 48 kHz stereo clip (`data/sample_stereo.wav`): centred bass,
70 %-left lead, 80 %-right hats, hard-left pad in the tail. All numbers are
for the full 96 000 frames; the C values were independently recomputed in
Python (`tools/verify_results.py`, 0 failures).

| weights | peak in | peak out | RMS in (weighted) | RMS out | RMS, left-only | ledger rel. | max frame E ratio | median time | ns/frame |
|---|---|---|---|---|---|---|---|---|---|
| 0.5 / 0.5 | 0.71189 | 0.68861 | 0.25434 | 0.24724 | 0.29005 | 2.2e-10 | 1.000000075 | 0.184 ms | 1.92 |
| 0.3 / 0.7 | 0.71189 | 0.68016 | 0.23856 | 0.23221 | 0.29005 | -2.7e-09 | 1.000000108 | 0.185 ms | 1.92 |
| 0.2 / 0.8 | 0.71189 | 0.67594 | 0.23027 | 0.22526 | 0.29005 | -2.2e-08 | 1.000000190 | 0.184 ms | 1.92 |

Timing is a 51-run median of the mixing loop only, measured with
`CLOCK_MONOTONIC` after 3 warm-up runs (WSL2, 16 cores, gcc 15.2, `-O2`).
The left-channel shortcut costs ~0.17 ms (0.17 ns/frame); the real mixer
costs ~0.185 ms (1.92 ns/frame), about **11×** the naive copy — still
0.009 % of the 2 s of real time the clip represents, i.e. ~10 800× faster
than real time. Exact per-run values: `results/stats/summary.csv`.

The "left-only" column is the thing being replaced: its RMS (0.290) is
above both the correct weighted RMS (0.230–0.254) and the stereo average
(0.254), because taking one channel of correlated material does not
attenuate, but it drops everything that lived on the right. The
downmixer keeps 78–94 % of the mean stereo energy depending on the
weights (`energy_kept_vs_raw_mean`); the missing part is the side
information, exactly as the ledger in §3 predicts.

## 6. Full-scale input

`data/sample_fullscale.wav` contains frames deliberately set to ±1.0
(`(+1,+1)`, `(-1,-1)`, `(+1,-1)`) so the boundary is exercised. All three
weight sets return `DOWNMIX_OK_FULL_SCALE` (code `2`), report
`full_scale_frames = 3`, `clipped_frames = 0`, `peak_out = 1.0` and
`peak_bound_ok = 1`. Note the mixed `(+1,-1)` frame produces
`wL - wR`, which is *inside* the range — the peak guarantee holds at the
boundary rather than being violated by it. `results/stats/fullscale.csv`.

## 7. Files

```
src/downmix.h, src/downmix.c   library (single translation unit)
tests/test_downmix.c           unit tests, 6.7M checks
tools/downmix_report.c         WAV harness: gen data, run matrix, timing
tools/wav.c, tools/wav.h       minimal PCM16 / float32 WAV I/O
tools/verify_results.py        independent (stdlib-only) recomputation
data/sample_stereo.wav         2 s stereo test clip (self-made)
data/sample_mono.wav           mono rendition of the same arrangement
data/sample_fullscale.wav      stereo clip with ±1.0 frames
results/wav/                   mixed outputs (mono float32)
results/stats/summary.csv      before/after peak, RMS, energy, timing
results/stats/blocks_*.csv     block-energy stability + partition check
results/stats/steps_*.csv      step amplification per weight set
results/stats/per_frame_*.csv  output vs. the convex bound, sampled
results/stats/fullscale.csv    full-scale run
results/stats/edge_cases.csv   status-code matrix
results/regression/            base-vs-work hashes and test output
scripts/check_regression.sh    the `make regression` implementation
```

`make report` is deterministic: the sample generator uses a fixed-seed
xorshift PRNG and the same float rounding as storage, so the WAVs and the
hash column in `summary.csv` reproduce byte-for-byte on the same
toolchain.
