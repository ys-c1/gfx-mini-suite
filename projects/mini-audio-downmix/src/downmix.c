#include "downmix.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* A saturator is needed only for input outside [-1, 1]; with legal
 * weights and in-range input it is unreachable (proof in the header).
 * Keeping it here makes the "peak never leaves the range" claim total
 * rather than conditional on the caller behaving. */
static float clamp_unit(float v) {
    if (v > 1.0f) return 1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

void downmix_stats_reset(DownmixStats* s) {
    if (s) memset(s, 0, sizeof(*s));
}

int downmix_weights_valid(float wl, float wr, const char** why) {
    const char* msg = NULL;
    if (!isfinite((double)wl) || !isfinite((double)wr)) {
        msg = "weight is not finite";
    } else if (wl < 0.0f || wr < 0.0f) {
        msg = "weight is negative";
    } else if (fabsf((wl + wr) - 1.0f) > DOWNMIX_WEIGHT_SUM_EPS) {
        msg = "weights do not sum to 1";
    }
    if (why) *why = msg ? msg : "ok";
    return msg ? 0 : 1;
}

size_t downmix_out_frames(size_t in_frames) { return in_frames; }

static void record_frame_stats(DownmixStats* s, float l, float r, float out,
                               double wl, double wr, int mono) {
    double den = mono ? (double)l * (double)l
                      : wl * (double)l * (double)l + wr * (double)r * (double)r;
    double num = (double)out * (double)out;
    double pin = mono ? fabs((double)l) : fmax(fabs((double)l), fabs((double)r));
    double pout = fabs((double)out);

    ++s->frames;
    s->in_energy += den;
    s->out_energy += num;
    if (!mono) {
        /* Exact ledger term: out^2 + wL*wR*(L-R)^2 == wL*L^2 + wR*R^2. */
        double d = (double)l - (double)r;
        s->diff_energy += wl * wr * d * d;
    }
    if (pin > s->peak_in) s->peak_in = pin;
    if (pout > s->peak_out) s->peak_out = pout;
    if (!mono && pin >= 1.0 - (double)DOWNMIX_FULL_SCALE_EPS) ++s->full_scale_frames;
    if (mono) ++s->mono_passthrough_frames;
    if (den > 0.0) {
        double ratio = num / den;
        if (ratio > s->max_frame_energy_ratio) s->max_frame_energy_ratio = ratio;
    }
}

int downmix_frame(const float* in, size_t frames, int channels_in,
                  float wl, float wr, float* out, DownmixStats* stats) {
    if (!in || !out) return DOWNMIX_E_NULL;
    if (frames > (size_t)PTRDIFF_MAX) return DOWNMIX_E_NULL;
    if (channels_in != 1 && channels_in != 2) {
        if (stats) ++stats->channel_rejects;
        return DOWNMIX_E_CHANNELS;
    }
    /* Order matters for diagnosability: a mono call with junk weights is
     * still a mono passthrough (weights are not part of that contract),
     * whereas a stereo call must be told its weights are wrong before it
     * touches a single sample. */
    if (channels_in == 2 && !downmix_weights_valid(wl, wr, NULL)) {
        if (stats) ++stats->weight_rejects;
        return DOWNMIX_E_WEIGHTS;
    }

    /* Mono: the input is the programme material; copying it is the only
     * choice that neither attenuates nor double-counts it. out == in is
     * legal. */
    if (channels_in == 1) {
        if (out != in) memmove(out, in, frames * sizeof(float));
        if (stats) {
            for (size_t i = 0; i < frames; ++i)
                record_frame_stats(stats, in[i], 0.0f, out[i],
                                   (double)wl, (double)wr, 1);
        }
        return DOWNMIX_OK_MONO;
    }

    const float fwl = wl, fwr = wr;
    int full_scale = 0;
    for (size_t i = 0; i < frames; ++i) {
        float l = in[2 * i], r = in[2 * i + 1];
        /* Plain float mul+add: the compiler may contract it to an FMA and
         * may vectorise the loop. The result is within ~1 ulp of the
         * exact weighted sum; the clamp makes the peak contract total
         * even when the caller supplies out-of-range samples. */
        float mixed = fwl * l + fwr * r;
        float v = clamp_unit(mixed);
        if (v != mixed) {
            if (stats) ++stats->clipped_frames;
        }
        out[i] = v;
        if (fmaxf(fabsf(l), fabsf(r)) >= 1.0f - DOWNMIX_FULL_SCALE_EPS)
            full_scale = 1;
        if (stats)
            record_frame_stats(stats, l, r, v, (double)wl, (double)wr, 0);
    }
    return full_scale ? DOWNMIX_OK_FULL_SCALE : DOWNMIX_OK;
}

int downmix_stereo_to_mono(const float* in, size_t frames,
                           float wl, float wr, float* out,
                           DownmixStats* stats) {
    return downmix_frame(in, frames, 2, wl, wr, out, stats);
}

float downmix_max_step_ratio(const float* x, size_t frames, double floor,
                             long* at_frame) {
    if (at_frame) *at_frame = -1;
    if (!x || frames < 2) return 1.0f;
    double worst = 1.0;
    long where = -1;
    for (size_t i = 1; i < frames; ++i) {
        double a = fabs((double)x[i - 1]), b = fabs((double)x[i]);
        if (a <= floor || b <= floor) continue;
        double hi = a > b ? a : b, lo = a > b ? b : a;
        double ratio = hi / lo;
        if (ratio > worst) { worst = ratio; where = (long)i; }
    }
    if (at_frame) *at_frame = where;
    if (!isfinite(worst) || worst > 1e30) return 1e30f;
    return (float)worst;
}
