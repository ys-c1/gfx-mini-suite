/*
 * downmix -- weighted stereo -> mono downmixer with provable per-frame
 * peak bounding, an energy bound that becomes exact for coherent
 * content when the weights sum to one, and distinct machine-readable
 * results for every rejected input class.
 *
 * Design notes
 * ------------
 *  Legal weights are finite, non-negative and sum to 1 (within
 *  DOWNMIX_WEIGHT_SUM_EPS). Under those constraints, for every frame
 *
 *      out   = wL*L + wR*R
 *      out^2 <= wL*L^2 + wR*R^2              (Jensen, exact arithmetic)
 *      |out| <= wL*|L| + wR*|R| <= max(|L|,|R|)   (triangle)
 *
 *  The first inequality is the energy guarantee: the mixer never creates
 *  energy in any frame. The output sample is rounded to float, so a
 *  real test measures out^2/(wL*L^2 + wR*R^2) <= 1 + a few ulp
 *  (~1e-6); the inequality is exact in real arithmetic and the unit test
 *  uses that slack. A double-precision accumulator lets callers verify
 *  sum(out^2) <= sum(wL*L^2 + wR*R^2).
 *
 *  For wL + wR == 1 there is an exact per-frame energy ledger:
 *
 *      out^2 + wL*wR*(L - R)^2 == wL*L^2 + wR*R^2
 *
 *  (expand the square). Nothing is created or lost: the part of the
 *  weighted input energy that does not reach the mono output is the
 *  side/difference energy wL*wR*(L-R)^2. For coherent channels (L == R)
 *  that term is zero and out == L; with dyadic weights (1/2, 1/4, 0, 1)
 *  every float operation in the map is exact, so sum(out^2) == sum(L^2)
 *  bit-for-bit. With 0.3f/0.7f the two rounded products still differ
 *  from L by <= 1 ulp, which is the only reason "weights summing to one
 *  conserve energy" is not bit-exact there. For uncorrelated channels
 *  the ledger still balances to machine precision; the side energy is
 *  simply not part of the mono programme, so sum(out^2) is smaller.
 *  DownmixStats accumulates both terms, so callers can check
 *  in_energy == out_energy + diff_energy on every run.
 *
 *  Note the mathematical limit: any sum-to-one *linear* mono fold-down
 *  necessarily discards wL*wR*(L-R)^2 of the difference energy, and
 *  0.5/0.5 discards the least. "Conservation" here means the ledger is
 *  exact and coherent material passes through unchanged -- not that
 *  uncorrelated stereo energy survives a fold-down, which is impossible.
 *
 *  The second inequality is the peak guarantee: in-range input can never
 *  produce an out-of-range frame. Note it is the weighted L1 bound, not
 *  max(wL,wR)*max(|L|,|R|); the latter would be wrong when the channels
 *  differ. A saturator exists only as a last resort for out-of-contract
 *  input (|sample| > 1); it is counted in stats.clipped_frames and never
 *  fires for input confined to [-1, 1] with legal weights. It is not part
 *  of the linear path.
 *
 *  The mixer is memoryless and applies one fixed map to every frame, so
 *  it has no block state and no per-block normalisation. Splitting a
 *  signal into arbitrary blocks and concatenating the results is
 *  bit-identical to processing it whole -- the mixer introduces no seam,
 *  no gain ramp and therefore no energy discontinuity of its own. It also
 *  bounds every step: |d_out| <= wL*|dL| + wR*|dR|, so it never amplifies
 *  a transient. Ideal for a streaming device that hands it whatever
 *  buffer size the driver chose.
 *
 *  Mono input (channels_in == 1) is copied sample-for-sample and is
 *  reported as DOWNMIX_OK_MONO. This is a deliberate exception: a single
 *  channel is already the programme material and applying wL to it would
 *  attenuate it. Callers that need a mono file to obey a gain policy
 *  must apply that policy themselves.
 *
 *  Three diagnosable outcomes are distinct by construction:
 *      illegal weights      -> DOWNMIX_E_WEIGHTS  (-2)
 *      unsupported channels -> DOWNMIX_E_CHANNELS (-3)
 *      full-scale input     -> DOWNMIX_OK_FULL_SCALE (2)
 *  and a normal run is DOWNMIX_OK (0), mono is DOWNMIX_OK_MONO (1).
 */
#ifndef DOWNMIX_H
#define DOWNMIX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DOWNMIX_OK              =  0, /* mixed, all frames nominal            */
    DOWNMIX_OK_MONO         =  1, /* mono input copied through unchanged  */
    DOWNMIX_OK_FULL_SCALE   =  2, /* mixed and at least one input frame
                                     reached |x| >= 1 - 1e-6              */
    DOWNMIX_E_NULL          = -1, /* null pointer or frames > PTRDIFF_MAX */
    DOWNMIX_E_WEIGHTS       = -2, /* weights illegal, see validator       */
    DOWNMIX_E_CHANNELS      = -3  /* channels_in not 1 or 2               */
} downmix_status;

/* Tolerance on wL + wR == 1. Admits 0.3f + 0.7f and friends, rejects
 * 1e-3 or 2.0. */
#define DOWNMIX_WEIGHT_SUM_EPS 1e-6f

/* Input samples at or above this magnitude count as full scale. Chosen
 * just below 1.0 so that a value that rounds to 1.0f from the stereo
 * domain is still reported. */
#define DOWNMIX_FULL_SCALE_EPS 1e-6f

/* Reporting threshold for a frame-to-frame amplitude jump: +3 dB in
 * linear terms. This is a measurement convention for the harness, not a
 * limit applied by the mixer (the mixer adds no jump of its own). */
#define DOWNMIX_JUMP_RATIO 1.41253754f

/* Per-call statistics, accumulated; callers reset once with
 * downmix_stats_reset. Counters are in frames (one output sample each). */
typedef struct {
    long   frames;                  /* frames processed                       */
    long   full_scale_frames;       /* frames with max(|L|,|R|) >= 1-eps      */
    long   clipped_frames;          /* frames the safety saturator touched    */
    long   mono_passthrough_frames; /* frames copied unchanged               */
    long   weight_rejects;          /* calls rejected with E_WEIGHTS          */
    long   channel_rejects;         /* calls rejected with E_CHANNELS         */
    double in_energy;               /* sum(wL*L^2 + wR*R^2); mono: sum(x^2)   */
    double out_energy;              /* sum(out^2)                             */
    double diff_energy;             /* sum(wL*wR*(L-R)^2); mono: 0. The exact
                                       ledger is in = out + diff.             */
    double peak_in;                 /* max(|L|,|R|); mono: max|x|             */
    double peak_out;                /* max|out|                               */
    double max_frame_energy_ratio;  /* max over frames of
                                       out^2 / (wL*L^2 + wR*R^2), 0 when the
                                       denominator is zero; <= 1 for legal
                                       weights, in-range input               */
} DownmixStats;

void downmix_stats_reset(DownmixStats* s);

/* 1 if (wl, wr) is legal, 0 otherwise. When why != NULL it receives a
 * static explanation string. */
int downmix_weights_valid(float wl, float wr, const char** why);

/* Number of output frames for a stereo input of n frames (== n). */
size_t downmix_out_frames(size_t in_frames);

/*
 * Downmix `frames` interleaved frames.
 *
 *   channels_in == 2 -> out[i] = wL*in[2i] + wR*in[2i+1]
 *   channels_in == 1 -> out[i] = in[i] (memmove-safe passthrough;
 *                       weights are not consulted, so a mono file is
 *                       never silently attenuated)
 *   other            -> DOWNMIX_E_CHANNELS, no writes
 *
 * For stereo, illegal weights are rejected with DOWNMIX_E_WEIGHTS before
 * any sample is read. For mono, weights are irrelevant and the call
 * succeeds regardless of them; pass (0.5f, 0.5f) or the applied policy
 * for uniform logging.
 *
 * stats may be NULL; when non-NULL it is accumulated into. The function
 * writes exactly out[0 .. frames-1] and reads exactly
 * in[0 .. frames*channels_in-1]. out may alias in when channels_in == 1.
 */
int downmix_frame(const float* in, size_t frames, int channels_in,
                  float wl, float wr, float* out, DownmixStats* stats);

/* Convenience wrapper that only accepts stereo. */
int downmix_stereo_to_mono(const float* in, size_t frames,
                           float wl, float wr, float* out,
                           DownmixStats* stats);

/*
 * Largest symmetric frame-to-frame amplitude step factor
 *     max( |x[n]|/|x[n-1]|, |x[n-1]|/|x[n]| )
 * over consecutive frames whose amplitudes are both above `floor`.
 * Returns 1.0 when there is no measurable step, and fills *at_frame with
 * the frame index or -1. This is a measurement helper: the mixer itself
 * never smooths programme material, it only guarantees it adds no step.
 */
float downmix_max_step_ratio(const float* x, size_t frames, double floor,
                             long* at_frame);

#ifdef __cplusplus
}
#endif

#endif /* DOWNMIX_H */
