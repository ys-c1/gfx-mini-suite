#include "decim.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DECIM_PI 3.14159265358979323846

/* ------------------------------------------------------------------ */
/* Kaiser design primitives                                            */
/* ------------------------------------------------------------------ */

/* Modified Bessel function of the first kind, order 0, by its power
 * series.  Converges quickly for the beta range used here (0..12). */
static double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    double y = x * x / 4.0;
    for (int k = 1; k < 200; ++k) {
        term *= y / ((double)k * (double)k);
        sum += term;
        if (term < 1e-18 * sum) break;
    }
    return sum;
}

/* Kaiser's window-shape parameter, in dB of design attenuation. */
static double kaiser_beta(double a_db) {
    if (a_db > 50.0) return 0.1102 * (a_db - 8.7);
    if (a_db >= 21.0)
        return 0.5842 * pow(a_db - 21.0, 0.4) + 0.07886 * (a_db - 21.0);
    return 0.0;
}

/* Fold a digital frequency in cycles/sample into [-0.5, 0.5). */
static double fold_to_nyquist(double x) {
    x = x - floor(x);
    if (x >= 0.5) x -= 1.0;
    return x;
}

/* Realized gain of one Type-I FIR at f (cycles per input sample). */
static double stage_abs_gain(const DecimStage *s, double f) {
    double acc = s->h[s->delay];
    for (int i = 1; i <= s->delay; ++i)
        acc += 2.0 * s->h[s->delay - i] * cos(2.0 * DECIM_PI * f * (double)i);
    return fabs(acc);
}

/* |H(f)| of the whole cascade, following the frequency that survives
 * each decimation.  For f below the passband this is the ordinary
 * response; for f in the alias band it is the gain of the image that
 * lands in the output band. */
static double cascade_abs_gain(const Decim *d, double f) {
    double g = 1.0;
    for (int k = 0; k < d->n_stages; ++k) {
        g *= stage_abs_gain(&d->stages[k], f);
        f = fold_to_nyquist(f * (double)d->stages[k].m);
    }
    return g;
}

/* Build a normalized windowed-sinc Type-I filter of odd length n. */
static void build_taps(double *h, int n, double fp, double fs, double beta) {
    int delay = (n - 1) / 2;
    double fc = 0.5 * (fp + fs);
    double i0b = bessel_i0(beta);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        double x = (double)(i - delay);
        double ideal;
        if (fabs(x) < 1e-15) {
            ideal = 2.0 * fc;
        } else {
            ideal = sin(2.0 * DECIM_PI * fc * x) / (DECIM_PI * x);
        }
        double r = (delay > 0) ? x / (double)delay : 0.0;
        double arg = 1.0 - r * r;
        if (arg < 0.0) arg = 0.0;
        double w = bessel_i0(beta * sqrt(arg)) / i0b;
        h[i] = ideal * w;
        sum += h[i];
    }
    /* Normalize DC gain to exactly 1 in double arithmetic. */
    for (int i = 0; i < n; ++i) h[i] /= sum;
}

/* Golden-section maximum of |H| over [lo, hi] (used to refine peaks the
 * uniform sweep may have stepped over). */
static double refine_max_stage(const DecimStage *s, double lo, double hi) {
    const double phi = 0.6180339887498949;
    double a = lo, b = hi;
    double c = b - phi * (b - a), e = a + phi * (b - a);
    double fc_ = stage_abs_gain(s, c), fe = stage_abs_gain(s, e);
    for (int it = 0; it < 80; ++it) {
        if (fc_ > fe) { b = e; e = c; fe = fc_; c = b - phi * (b - a);
                        fc_ = stage_abs_gain(s, c); }
        else          { a = c; c = e; fc_ = fe; e = a + phi * (b - a);
                        fe = stage_abs_gain(s, e); }
    }
    return fc_ > fe ? fc_ : fe;
}

static double refine_max_cascade(const Decim *d, double lo, double hi) {
    const double phi = 0.6180339887498949;
    double a = lo, b = hi;
    double c = b - phi * (b - a), e = a + phi * (b - a);
    double fc_ = cascade_abs_gain(d, c), fe = cascade_abs_gain(d, e);
    for (int it = 0; it < 80; ++it) {
        if (fc_ > fe) { b = e; e = c; fe = fc_; c = b - phi * (b - a);
                        fc_ = cascade_abs_gain(d, c); }
        else          { a = c; c = e; fc_ = fe; e = a + phi * (b - a);
                        fe = cascade_abs_gain(d, e); }
    }
    return fc_ > fe ? fc_ : fe;
}

/* Uniform sweep of [lo,hi] for max and min |H|, refined at the maximum. */
static void sweep_stage(const DecimStage *s, double lo, double hi, int points,
                        double *max_g, double *min_g) {
    double mx = 0.0, mn = 1e300;
    int imax = 0;
    for (int j = 0; j < points; ++j) {
        double f = lo + (hi - lo) * (double)j / (double)(points - 1);
        double g = stage_abs_gain(s, f);
        if (g > mx) { mx = g; imax = j; }
        if (g < mn) mn = g;
    }
    if (imax > 0 && imax < points - 1) {
        double l = lo + (hi - lo) * (double)(imax - 1) / (double)(points - 1);
        double r = lo + (hi - lo) * (double)(imax + 1) / (double)(points - 1);
        double ref = refine_max_stage(s, l, r);
        if (ref > mx) mx = ref;
    }
    *max_g = mx;
    *min_g = mn;
}

/* Design one stage.  Returns 1 on success, 0 if the tap budget could not
 * satisfy the measured bounds.  On success s->h owns its taps.
 *
 * `a_design_db` is Kaiser's A: it places both the window shape and the
 * first candidate length.  The acceptance bound is `a_accept_db`, which
 * the caller sets 3 dB below the advertised requirement, so passing
 * this function means the realized filter is strictly better than the
 * specification. */
static int design_stage(DecimStage *s, int m, double fp, double a_design_db,
                        double a_accept_db, double delta_p, int max_taps) {
    double fs = 0.5 / (double)m;
    if (!(fp > 0.0) || !(fp < fs)) return 0;

    double dw = 2.0 * DECIM_PI * (fs - fp);
    double a_ripple = (delta_p > 0.0) ? -20.0 * log10(delta_p) : 1e9;
    double a = a_design_db;
    if (a_ripple > a) a = a_ripple;

    int n = (int)ceil((a - 8.0) / (2.285 * dw)) + 1;
    if (n < 3) n = 3;
    if ((n & 1) == 0) n += 1;
    if ((max_taps & 1) == 0) max_taps -= 1;

    double beta = kaiser_beta(a);
    double fc = 0.5 * (fp + fs);

    for (; n <= max_taps; n += 2) {
        double *h = (double *)malloc(sizeof(double) * (size_t)n);
        if (!h) return 0;
        build_taps(h, n, fp, fs, beta);

        DecimStage tmp;
        tmp.h = h;
        tmp.delay = (n - 1) / 2;
        tmp.m = m;
        tmp.fp = fp;
        tmp.fs = fs;
        tmp.fc = fc;
        tmp.beta = beta;
        tmp.taps = n;

        /* Grid density follows the tap count: the narrowest lobe of the
         * response is about 1/n cycles/sample wide, so a fixed grid
         * would step over it for long filters.  The maximum found on
         * the grid is then refined by golden-section search. */
        int pp = 4 * n;
        if (pp < 4097) pp = 4097;
        if (pp > 32769) pp = 32769;
        int ps = 6 * n;
        if (ps < 8193) ps = 8193;
        if (ps > 65537) ps = 65537;

        double pmax, pmin, smax, smin;
        sweep_stage(&tmp, 0.0, fp, pp, &pmax, &pmin);
        sweep_stage(&tmp, fs, 0.5, ps, &smax, &smin);

        (void)smin;
        double maxdev = fabs(pmax - 1.0);
        if (fabs(pmin - 1.0) > maxdev) maxdev = fabs(pmin - 1.0);
        double stop_db = (smax > 0.0) ? -20.0 * log10(smax) : 1e9;

        /* Kaiser's estimate errs by a tap or two in either direction.
         * The first length that passes is accepted; each candidate is
         * fully swept, so this search never relaxes the bound. */
        if (maxdev <= delta_p && stop_db >= a_accept_db) {
            s->m = m;
            s->taps = n;
            s->delay = tmp.delay;
            s->beta = beta;
            s->fc = fc;
            s->fp = fp;
            s->fs = fs;
            s->h = h;
            s->measured_ripple_db = 20.0 * log10(pmax / pmin);
            s->measured_stop_db = stop_db;
            return 1;
        }
        free(h);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void decim_config_default(DecimConfig *cfg) {
    if (!cfg) return;
    cfg->passband = 0.8;
    cfg->stop_db = 80.0;
    cfg->ripple_db = 0.01;
    cfg->max_taps = 8191;
}

void decim_free(Decim *d) {
    if (!d) return;
    for (int k = 0; k < DECIM_MAX_STAGES; ++k) {
        free(d->stages[k].h);
        d->stages[k].h = NULL;
    }
    memset(d, 0, sizeof(*d));
}

int decim_init(Decim *d, const int *factors, int n_stages,
               const DecimConfig *cfg_in) {
    if (!d) return DECIM_E_ARG;
    memset(d, 0, sizeof(*d));

    DecimConfig cfg;
    if (cfg_in) cfg = *cfg_in; else decim_config_default(&cfg);
    if (!(cfg.passband > 0.0) || !(cfg.passband < 1.0)) return DECIM_E_ARG;
    if (!(cfg.stop_db >= 40.0)) return DECIM_E_ARG;
    if (!(cfg.ripple_db > 0.0) || !(cfg.ripple_db <= 6.0)) return DECIM_E_ARG;
    if (cfg.max_taps == 0) cfg.max_taps = 8191;
    if (cfg.max_taps < 3) return DECIM_E_ARG;
    if (n_stages < 0 || n_stages > DECIM_MAX_STAGES) return DECIM_E_ARG;
    if (n_stages > 0 && !factors) return DECIM_E_ARG;

    long long total = 1;
    for (int k = 0; k < n_stages; ++k) {
        if (factors[k] < 1) return DECIM_E_ARG;
        total *= (long long)factors[k];
        if (total > 2147483647LL) return DECIM_E_ARG;
    }
    d->cfg = cfg;
    d->total = (int)total;

    /* A total factor of one is a pure passthrough; no filters exist. */
    if (total == 1) return DECIM_OK;

    int active = 0;
    for (int k = 0; k < n_stages; ++k)
        if (factors[k] > 1) ++active;
    if (active > DECIM_MAX_STAGES) return DECIM_E_ARG;

    /* Overall passband edge, cycles per input sample.  (Equivalent to
     * 0 < passband < 1, checked above; stated in the edge units the
     * rest of the file uses.) */
    const double fp_out = cfg.passband * 0.5 / (double)d->total;
    if (!(fp_out > 0.0) || !(fp_out < 0.5 / (double)d->total)) {
        memset(d, 0, sizeof(*d));
        return DECIM_E_ARG;
    }

    /* Ripple budget: the p-p gain deviation of the cascade is at most
     * about the sum of the per-stage linear deviations, so split the
     * total budget equally.  Each stage also designs for stop_db + 6 dB
     * of attenuation and is accepted only if it measures at least
     * stop_db + 3 dB. */
    double ripple_delta = pow(10.0, cfg.ripple_db / 20.0) - 1.0;
    double delta_p = ripple_delta / (2.0 * (double)active);

    /* The default design is A = stop_db + 6 dB and must measure at
     * least stop_db + 3 dB.  If a retry is needed, both move together. */
    for (int attempt = 0; attempt < 6; ++attempt) {
        double a_design = cfg.stop_db + 6.0 + 2.0 * (double)attempt;
        double a_accept = cfg.stop_db + 3.0 + 2.0 * (double)attempt;
        double delta = delta_p * pow(0.5, (double)attempt);
        int P = 1, k = 0, ok = 1;

        for (int j = 0; j < n_stages && ok; ++j) {
            int m = factors[j];
            if (m <= 1) continue;
            double fp = fp_out * (double)P;
            DecimStage stage;
            memset(&stage, 0, sizeof(stage));
            if (!design_stage(&stage, m, fp, a_design, a_accept, delta,
                              cfg.max_taps)) {
                ok = 0;
                break;
            }
            d->stages[k++] = stage;
            P *= m;
        }
        d->n_stages = k;

        if (ok) {
            /* Measure the entire cascade.  The composite response is a
             * product of stage responses, so its narrowest feature is
             * about as wide as the narrowest stage lobe: density the
             * grid from the longest stage, not a fixed count. */
            int longest = 0;
            for (int s = 0; s < d->n_stages; ++s)
                if (d->stages[s].taps > longest) longest = d->stages[s].taps;
            int pts = 8 * longest;
            if (pts < 32769) pts = 32769;
            if (pts > 65537) pts = 65537;
            double pmax = 0.0, pmin = 1e300, smax = 0.0;
            int ismax = 0;
            double phi = fp_out;
            double slo = 0.5 / (double)d->total, shi = 0.5;
            for (int i = 0; i < pts; ++i) {
                double f = phi * (double)i / (double)(pts - 1);
                double g = cascade_abs_gain(d, f);
                if (g > pmax) pmax = g;
                if (g < pmin) pmin = g;
            }
            for (int i = 0; i < pts; ++i) {
                double f = slo + (shi - slo) * (double)i / (double)(pts - 1);
                double g = cascade_abs_gain(d, f);
                if (g > smax) { smax = g; ismax = i; }
            }
            if (ismax > 0 && ismax < pts - 1) {
                double l = slo + (shi - slo) * (double)(ismax - 1) /
                                    (double)(pts - 1);
                double r = slo + (shi - slo) * (double)(ismax + 1) /
                                    (double)(pts - 1);
                double ref = refine_max_cascade(d, l, r);
                if (ref > smax) smax = ref;
            }
            d->measured_ripple_db = 20.0 * log10(pmax / pmin);
            d->measured_alias_db = (smax > 0.0) ? -20.0 * log10(smax) : 1e9;

            /* Support of the composite impulse response, in input
             * samples: sum_k (taps_k - 1) * P_k + 1.  Every stage is
             * causal, so outputs depend only on this prefix. */
            long long span = 1;
            P = 1;
            for (int s = 0; s < d->n_stages; ++s) {
                span += (long long)(d->stages[s].taps - 1) * (long long)P;
                P *= d->stages[s].m;
            }
            d->span_taps = (int)(span > 2147483647LL ? 2147483647LL : span);

            if (d->measured_ripple_db <= cfg.ripple_db &&
                d->measured_alias_db >= cfg.stop_db) {
                return DECIM_OK;
            }
            if (attempt == 5) break;
        }

        /* Retry with a stricter design; free what this attempt built. */
        for (int s = 0; s < d->n_stages; ++s) {
            free(d->stages[s].h);
            memset(&d->stages[s], 0, sizeof(d->stages[s]));
        }
        d->n_stages = 0;
        if (!ok) return DECIM_E_DESIGN;
    }
    memset(d, 0, sizeof(*d));
    return DECIM_E_DESIGN;
}

int decim_total_factor(const Decim *d) { return d ? d->total : 1; }

size_t decim_out_frames(const Decim *d, size_t in_frames) {
    if (!d || d->total <= 1) return in_frames;
    return in_frames / (size_t)d->total;
}

size_t decim_span_input(const Decim *d) {
    return d ? (size_t)d->span_taps : 0u;
}

double decim_stage_gain(const DecimStage *s, double f) {
    return s ? stage_abs_gain(s, f) : 0.0;
}

double decim_response(const Decim *d, double f) {
    return d ? cascade_abs_gain(d, f) : 0.0;
}

const char *decim_status_name(int status) {
    switch (status) {
    case DECIM_OK:        return "OK";
    case DECIM_OK_BYPASS: return "OK_BYPASS";
    case DECIM_OK_TAIL:   return "OK_TAIL";
    case DECIM_OK_SHORT:  return "OK_SHORT";
    case DECIM_E_ARG:     return "E_ARG";
    case DECIM_E_DESIGN:  return "E_DESIGN";
    case DECIM_E_SPACE:   return "E_SPACE";
    case DECIM_E_NOMEM:   return "E_NOMEM";
    default:              return "UNKNOWN";
    }
}

/* Causal FIR + decimation by m.  Zero extension outside [0, n_in).
 *
 * y[j] is the filter output at input index c = j*m, i.e. the full
 * convolution sum over every tap: t = c-(taps-1) .. c maps to
 * h[taps-1] .. h[0].  Both halves of the symmetric response contribute;
 * using only the delay-length neighbourhood would halve the DC gain. */
static void stage_process(const DecimStage *s, const double *x, size_t n_in,
                          double *y, size_t n_out) {
    int m = s->m, delay = s->delay;
    size_t reach = (size_t)delay * 2u;          /* taps - 1 */
    for (size_t j = 0; j < n_out; ++j) {
        size_t c = j * (size_t)m;              /* newest tap index */
        size_t lo = (c >= reach) ? c - reach : 0u;
        size_t hi = (c < n_in) ? c : n_in - 1u;
        double acc = 0.0;
        for (size_t t = lo; t <= hi; ++t)
            acc += s->h[c - t] * x[t];
        y[j] = acc;             /* intermediate stages stay in double    */
    }
}

int decim_run(const Decim *d, const float *in, size_t in_frames,
              float *out, size_t out_cap, DecimStats *stats) {
    DecimStats local;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof(*stats));
    stats->in_frames = in_frames;

    if (!d) return DECIM_E_ARG;
    if (in_frames > 0 && !in) return DECIM_E_ARG;
    if (in_frames > (size_t)-1 / sizeof(double)) return DECIM_E_ARG;

    size_t out_frames = decim_out_frames(d, in_frames);
    if (out_frames > 0 && !out) return DECIM_E_ARG;
    if (out_cap < out_frames) return DECIM_E_SPACE;

    stats->out_frames = out_frames;

    /* Passthrough: bit-identical, in place allowed, no reads beyond n. */
    if (d->total <= 1) {
        stats->bypass = 1;
        if (out_frames > 0 && out != in)
            memmove(out, in, out_frames * sizeof(float));
        double peak = 0.0;
        for (size_t i = 0; i < out_frames; ++i) {
            double a = fabs((double)out[i]);
            if (a > peak) peak = a;
        }
        stats->peak_out = peak;
        return DECIM_OK_BYPASS;
    }

    if (in_frames == 0) return DECIM_OK;

    stats->tail_frames = in_frames % (size_t)d->total;
    stats->partial = stats->tail_frames != 0;
    stats->short_input = in_frames < (size_t)d->span_taps;

    double *a = (double *)malloc(sizeof(double) * in_frames);
    double *b = (double *)malloc(sizeof(double) * in_frames);
    if (!a || !b) {
        free(a);
        free(b);
        return DECIM_E_NOMEM;
    }
    for (size_t i = 0; i < in_frames; ++i) a[i] = (double)in[i];

    size_t n = in_frames;
    for (int k = 0; k < d->n_stages; ++k) {
        const DecimStage *s = &d->stages[k];
        size_t n_out = n / (size_t)s->m;
        stage_process(s, a, n, b, n_out);
        double *t = a; a = b; b = t;
        n = n_out;
    }

    for (size_t i = 0; i < n; ++i) out[i] = (float)a[i];
    double peak = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double mag = fabs((double)out[i]);
        if (mag > peak) peak = mag;
    }
    stats->peak_out = peak;
    free(a);
    free(b);

    if (stats->short_input) return DECIM_OK_SHORT;
    if (stats->partial) return DECIM_OK_TAIL;
    return DECIM_OK;
}
