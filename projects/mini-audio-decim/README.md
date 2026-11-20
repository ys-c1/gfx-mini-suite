# mini-audio-decim

Multistage decimator for mono float audio (C11, no dependencies). It
replaces "skip every D-th sample", which folds everything between the
output Nyquist and the input Nyquist straight into the programme.

```
make test           # unit tests: 794 + 288 checks
make report         # regenerate results/ and verify it independently
make test-sanitize  # unit tests under ASan + UBSan
```

Everything below is produced by those commands. Numbers quoted here are
in `results/*.csv`; `make report` also runs `tools/verify_results.py`,
which re-derives the whole design from the exported coefficients with a
stdlib-only Python implementation and re-measures it (1889 checks, 0
failures).

---

## 1. API and configurable cascade

```c
void decim_config_default(DecimConfig *cfg);   /* 0.8, 80 dB, 0.01 dB */
int  decim_init(Decim *d, const int *factors, int n_stages,
                const DecimConfig *cfg);
int  decim_run(const Decim *d, const float *in, size_t in_frames,
               float *out, size_t out_cap, DecimStats *stats);
```

`factors[]` is the stage list. `[6]` is one stage by 6; `[3,2]` is 3 then
2; `[3,2,2,2]` is 24 in four stages. The total factor is the product:
`decim_total_factor()` returns it, and `decim_out_frames(d, n)` is
`floor(n/D)`. `DECIM_MAX_STAGES` is 8.

| field | default | meaning |
|---|---|---|
| `passband` | 0.8 | passband edge as a fraction of the **output** Nyquist |
| `stop_db` | 80 | required alias rejection; must be ≥ 40 |
| `ripple_db` | 0.01 | maximum total passband ripple, dB |
| `max_taps` | 8191 | per-stage tap budget |

With the default the overall passband edge is `0.4/D` cycles per input
sample, i.e. 80 % of the output Nyquist. `decim_init` fails with
`DECIM_E_DESIGN` rather than hand back a filter that misses the spec.

## 2. Filter design basis

No coefficient tables, no fixed taps. Every stage is a Type-I
(odd-length, symmetric) linear-phase FIR designed when `decim_init` runs:

```
ideal lowpass   h_d[n] = 2*fc * sinc(2*fc*(n-M))     M = (N-1)/2
Kaiser window   w[n]   = I0(beta*sqrt(1-((n-M)/M)^2)) / I0(beta)
taps            h[n]   = h_d[n]*w[n] / sum(h_d*w)    (DC gain exactly 1)
cutoff          fc     = (fp + fs)/2
```

`I0` is the zeroth-order modified Bessel function, evaluated from its
power series. `beta` and `N` come from Kaiser's empirical design
equations (Kaiser, "Nonrecursive digital filter design using the
I0-sinh window function", Proc. IEEE ISCAS 1974; Oppenheim & Schafer,
*Discrete-Time Signal Processing*, 3rd ed., §7.3):

```
beta = 0.1102*(A-8.7)                        A > 50
       0.5842*(A-21)^0.4 + 0.07886*(A-21)    21 <= A <= 50
       0                                      A < 21
N    = ceil((A-8) / (2.285*dw)) + 1          dw = 2*pi*(fs-fp)
A    = max(stop_db + 6, -20*log10(delta_p))
```

`A` is the design attenuation. It is derived from the requested
stopband plus 6 dB of margin, or from the per-stage passband deviation
`delta_p` when that is stricter; `delta_p` is a split of the requested
total ripple across the stages.

The coefficients are not trusted because a formula produced them. For
every stage the library evaluates the realized `|H(f)|` on a grid whose
density follows the tap count (the narrowest lobe is about `1/N` wide),
refines the maximum by golden-section search, and accepts the stage only
if

```
passband  [0, fp]    max | |H(f)| - 1 |  <= delta_p
stopband  [fs, 0.5]  max |H(f)|           <= 10^(-(stop_db+3)/20)
```

The acceptance bound is 3 dB tighter than the advertised `stop_db`, so
a filter that passes can never sit exactly on the requirement.
Otherwise the length grows by two and the stage is rebuilt and re-swept;
if the tap budget runs out `decim_init` returns `DECIM_E_DESIGN`. The
whole cascade is then measured again on `[0, 0.4/D]` (passband) and
`[0.5/D, 0.5]` (alias band); those measured values are what the harness
reports. `results/stages_<plan>.csv` lists `fp, fs, fc, beta, taps,
sum(h)`, and `results/coeffs_<plan>.csv` has every coefficient, so the
design can be re-derived without this program — `verify_results.py`
does exactly that and requires agreement to better than `1e-12` per tap.
`tests/test_reference.c` additionally expands the whole cascade into
one D-phase kernel (`G[t] = sum over r_k with t = sum r_k*P_k of
prod h_k[r_k]`) and requires `decim_run` to match a direct convolution
with it on 16 stage plans and random lengths.

### Stage band edges

With `fp_out = passband * 0.5 / D` the edge in input-sample cycles, and
`P_k = m_1 * ... * m_(k-1)`:

```
fp_k = fp_out * P_k          fs_k = 0.5 / m_k
```

so every stage's stopband starts at its own output Nyquist and
`fs_k > fp_k` for any `passband < 1`.

### Why the cascade aliases by less than any single stage

For every frequency `f >= 0.5/D` (exactly the ones that fold in the
final D-fold decimation): suppose no stage rejects it, i.e. `f*P_k <
0.5/m_k` for every k. Then no stage aliases it, so the last stage sees
`f*D/m_last >= 0.5/m_last` — a contradiction. Therefore there is a
first stage k with `f*P_k >= 0.5/m_k`; all earlier stages did not alias,
so the tone arrives at stage k unchanged and sits in its stopband. With
later stages no worse than `1+delta_p` each,

```
composite gain <= (1+delta_p)^(K-1) * 10^(-(stop_db+3)/20)
```

which is below −80 dB for the defaults. The harness checks this
numerically for every alias frequency on a dense grid
(`proof_worst_stage_rejection_db`, `proof_ok` in `summary.csv`: every
row is `-85…-86 dB` and `1`).

## 3. Three ratios, measured

`results/summary.csv`, input 96 000 frames (2 s at 48 kHz), gcc 15.2
`-O2` on WSL2. "alias" is the worst cascade gain on `[0.5/D, 0.5]`;
"share" is the fraction of output power that alias-band input
contributes for white input over the whole band; "path" is an
independent time-domain check that feeds tones which fold onto one
output bin and reads the unwanted image out of the output spectrum.

| plan | D | stages | taps | pass ripple | alias | share | path | median ms / 2 s |
|---|---|---|---|---|---|---|---|---|
| D6_single | 6 | 6 | 327 | 0.00084 dB | −85.4 dB | −97.2 dB | −95.2 dB | 2.26 |
| D6_multi | 6 | 3×2 | 168 | 0.00128 dB | −86.3 dB | −98.0 dB | −97.4 dB | 1.16 |
| D12_single | 12 | 12 | 653 | 0.00085 dB | −85.5 dB | −96.9 dB | −94.6 dB | 2.36 |
| D12_multi | 12 | 3×2×2 | 193 | 0.00232 dB | −86.3 dB | −96.5 dB | −97.4 dB | 0.96 |
| D12_alt | 12 | 4×3 | 226 | 0.00112 dB | −85.6 dB | −98.0 dB | −97.4 dB | 0.99 |
| D24_single | 24 | 24 | 1305 | 0.00085 dB | −85.6 dB | −96.7 dB | −94.3 dB | 2.38 |
| D24_multi | 24 | 3×2×2×2 | 222 | 0.00226 dB | −86.3 dB | −98.1 dB | −97.4 dB | 0.89 |
| D24_alt | 24 | 4×3×2 | 221 | 0.00154 dB | −86.3 dB | −97.6 dB | −97.4 dB | 0.72 |

All three ratios clear the −40 dB requirement by about 45 dB in the
worst image and by about 56 dB in total alias power, and the passband
stays inside the 0.01 dB bound.

Work per input sample (`taps_k` multiplications per stage output,
`1/P_(k+1)` outputs per input sample) and the equivalent single stage:

| plan | MACs/input sample | single-stage MACs | ratio |
|---|---|---|---|
| D6_single | 54.50 | 54.50 | 1.000 |
| D6_multi | 37.50 | 54.50 | 0.688 |
| D12_single | 54.42 | 54.42 | 1.000 |
| D12_multi | 30.08 | 54.42 | 0.553 |
| D12_alt | 29.00 | 54.42 | 0.533 |
| D24_single | 54.38 | 54.38 | 1.000 |
| D24_multi | 26.71 | 54.38 | 0.491 |
| D24_alt | 22.62 | 54.38 | 0.416 |

The multi-stage plans do 31–58 % less arithmetic than a single stage at
the same factor, and the measured wall time follows (e.g. D24 drops from
2.38 ms to 0.72–0.89 ms for 2 s of audio; still ~2700× faster than real
time). Per-run timings are in `results/timing.csv` (31 runs each, after
3 warm-up runs, `CLOCK_MONOTONIC`); min/median/max are also in
`summary.csv`. Timings are the only non-deterministic column.

`results/freqresp_<plan>.csv` has the full response from 0 to 0.5
cycles/input-sample with the passband / transition / alias band labelled
and each stage's own response alongside; `alias_path_<plan>.csv` has one
row per probe bin and interferer for the time-domain check.

## 4. The three edge inputs

Documented precedence: argument errors, then bypass, then empty input,
then short input, then a non-divisible tail. `results/edge_cases.csv`
is generated by `make report`; `verify_results.py` re-checks it.

```
case                 D   in     out  status      tail short partial bypass
factor_one_empty     1    100    100  OK_BYPASS    0    0     0       1
factor_one_single    1    100    100  OK_BYPASS    0    0     0       1
factor_one_list      1    100    100  OK_BYPASS    0    0     0       1
nondivisible_1001   12   1001     83  OK_TAIL      5    0     1       0
nondivisible_5_of_6  6      5      0  OK_SHORT     5    1     1       0
divisible_12000     12  12000   1000  OK           0    0     0       0
short_3_of_span     12      3      0  OK_SHORT     3    1     1       0
short_span_minus_1  12    842     70  OK_SHORT     2    1     1       0
```

**Factor one.** An empty factor list, `[1]`, or `[1,1,1]` all give a
total factor of 1 and a pure passthrough: `out[i] == in[i]` byte for
byte. `results/bypass.csv` runs 96 000 samples through each form,
including a quiet NaN with a non-canonical payload, a negative NaN,
`+inf`, `-inf`, `-0.0` and the smallest subnormal, and compares the
bits: `copy_bit_identical = 1`, `distinct_bit_patterns = 5/5` for all
three. In-place operation is allowed on this path (it is a `memmove`).
The status is `DECIM_OK_BYPASS`.

**Input not divisible by the total factor.** Exactly `floor(n/D)`
frames come out. Each stage emits `floor(n_k/m_k)` and drops the
`n_k mod m_k` trailing frames; nested floors compose to `floor(n/D)`,
and because the filters are causal the dropped tail cannot change an
emitted sample. The unit test runs the same prefix with and without the
tail and finds the emitted frames `memcmp`-identical; it also checks the
`OA[j]` frame against a change at input `(j+1)*D` and requires equality.
Status `DECIM_OK_TAIL`, `stats.tail_frames = n mod D`,
`stats.partial = 1`. With `n < D` nothing is emitted (out_frames = 0)
and the same status applies unless the input is also short.

**Input shorter than the filter.** `decim_span_input()` is the length of
the composite impulse response,
`sum_k (taps_k - 1) * P_k + 1`; below it the input lies entirely inside
the transient. The documented behaviour is zero initial state and zero
extension: the output is the causal convolution of the zero-padded
input, `floor(n/D)` frames (possibly none). The unit test checks the
result frame by frame against a direct convolution over the public
stage taps. Status `DECIM_OK_SHORT`, `stats.short_input = 1`; a short
input that is also non-divisible reports `OK_SHORT` with
`stats.partial` still set, so neither fact is hidden.

In every case a rejected call (`DECIM_E_ARG`, `DECIM_E_SPACE`,
`DECIM_E_DESIGN`) writes nothing to `out`, and `out_cap` is checked
before the first write. The bypass path is the only one where `out` may
alias `in`.

## 5. Original passthrough unchanged

This project is new; it does not touch any existing file. `make
regression` extracts the committed tree (commit `da546e9`) into a
scratch directory, builds and runs the five pre-existing projects
there, and compares their full `make test` output and source hashes
against the working tree. Only this directory and one README row should
differ. The result is in `results/regression/summary.txt`.

## 6. Arithmetic and block size

Taps are designed and stored in double; every stage accumulates in
double and intermediate stage outputs are stored in double. The only
float rounding is the final store to `out`. This keeps the arithmetic
floor of the measured alias images at about −95…−97 dB
(`alias_path_*.csv`), well below the −40 dB requirement and consistent with the
−85…−86 dB design bound from the response formula.

`decim_run` is a whole-buffer call. Internally it needs two double
buffers of the input length (`2*n*sizeof(double)`), because each stage
writes into the other buffer; this is independent of any block size the
caller chose, and there is no hidden global state. Splitting a stream
into blocks and concatenating the outputs is deterministic, but each
call restarts from zero history, so a block seam shows the transient.
For continuous streaming either feed whole buffers or use overlap-save
with `decim_span_input()` samples of overlap; the causality test in the
unit suite is what makes that valid. The zero-history rule is also what
defines the short-input and non-divisible-tail behaviour in §4.

## 7. Files

```
src/decim.h, src/decim.c     library (single translation unit)
tests/test_decim.c           unit tests, 794 checks
tests/test_reference.c       independent D-phase convolution check, 288 checks
tools/decim_report.c         measurement harness
tools/verify_results.py      independent stdlib-only re-derivation
scripts/check_regression.sh  the `make regression` implementation
results/summary.csv          the three ratios: taps, ripple, alias, work, time
results/stages_*.csv         per-stage design parameters and realized values
results/coeffs_*.csv         every filter coefficient
results/freqresp_*.csv       response curves, band-labelled
results/alias_path_*.csv     time-domain alias check per probe bin
results/alias_path_worst.csv worst measured image per plan
results/bypass.csv           the factor-1 path
results/edge_cases.csv       the three edge inputs
results/timing.csv           per-run timings
results/regression/          base-vs-work hashes and test output
```

`make report` is deterministic except for the timing columns: the input
signals are fixed-seed xorshift sequences.
