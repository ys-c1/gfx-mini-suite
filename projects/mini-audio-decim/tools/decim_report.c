/* Measurement harness for mini-audio-decim.
 *
 * For three ratios (D = 6, 12, 24) and several stage splits it writes
 *
 *   results/freqresp_D<d>_<plan>.csv   designed response, one row per
 *                                      frequency, plus each stage's own
 *                                      response
 *   results/summary.csv                taps, measured ripple, measured
 *                                      alias rejection, alias energy
 *                                      share, timings, and the cost of
 *                                      the equivalent single stage
 *   results/bypass.csv                 the factor-1 passthrough path,
 *                                      compared byte for byte
 *   results/edge_cases.csv             the three documented edge inputs
 *   results/timing.csv                 repeated timings for all plans
 *
 * Everything is deterministic: the input is a fixed-seed xorshift
 * generator, no wall-clock value enters a signal, only the timing
 * columns (which are repeated and reported as median + spread).
 */
#define _POSIX_C_SOURCE 200809L

#include "../src/decim.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static uint32_t rng_state;
static void rng_seed(uint32_t s) { rng_state = s ? s : 1u; }
static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static float rng_float(void) {
    return (float)((double)rng_next() / 2147483648.0 - 1.0);
}

static double seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median(double *v, int n) {
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

/* ------------------------------------------------------------------ */
/* Signal measurements                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    double alias_energy_share; /* alias-band power in the output band   */
    double alias_worst_db;     /* worst single alias image, dB          */
    double alias_at;           /* frequency of that worst image         */
    int    worst_stage;        /* first stage that rejects it, -1 if in
                                  the passband                         */
    int    proof_ok;           /* every alias frequency hit some stage
                                  stopband at >= stop_db                */
    double proof_worst_db;     /* worst rejection actually found        */
} AliasReport;

/* Walk an input frequency through the cascade, recording each stage's
 * gain and the frequency that survives.  Returns the tone's total
 * attenuation in dB and identifies the first rejecting stage. */
static double trace_tone(const Decim *d, double f, double *stage_db,
                         int *n_stages) {
    double gdb = 0.0;
    int k = 0;
    for (; k < d->n_stages; ++k) {
        const DecimStage *s = &d->stages[k];
        double g = decim_stage_gain(s, f);
        double db = (g > 1e-30) ? 20.0 * log10(g) : -600.0;
        if (stage_db) stage_db[k] = db;
        gdb += db;
        /* fold to the stage's output Nyquist frequency */
        f = f * (double)s->m;
        f = f - floor(f);
        if (f >= 0.5) f -= 1.0;
        f = fabs(f);
    }
    if (n_stages) *n_stages = k;
    return gdb;
}

/* Check the alias proof on a dense grid and measure the alias-band
 * power share of a white input that occupies the whole input band. */
static AliasReport measure_aliases(const Decim *d, int D, double fp_out) {
    AliasReport r;
    memset(&r, 0, sizeof(r));
    double stop = 0.5 / (double)D;

    const int G = 20001;
    double *sd = (double *)malloc(sizeof(double) * (size_t)d->n_stages *
                                  sizeof(double));
    int nst = 0;
    double worst = -1e9, worst_at = stop;
    double alias_energy = 0.0;
    int proof_ok = 1;
    double proof_worst = -1e9;

    for (int i = 0; i < G; ++i) {
        double f = stop + (0.5 - stop) * (double)i / (double)(G - 1);
        double gdb = trace_tone(d, f, sd, &nst);
        double g = pow(10.0, gdb / 20.0);
        alias_energy += g * g;        /* white input, unit PSD */
        if (gdb > worst) { worst = gdb; worst_at = f; }
        /* The proof says: the first stage k with f*P_k >= 0.5/m_k has
         * that frequency in its stopband.  Find it and check. */
        double P = 1.0;
        int hit = 0;
        for (int k = 0; k < d->n_stages; ++k) {
            double fk = f * P;
            double fkn = fk - floor(fk);   /* earlier stages do not alias */
            if (fk >= 0.5 / (double)d->stages[k].m) {
                double db = 20.0 * log10(decim_stage_gain(&d->stages[k], fkn) +
                                         1e-30);
                if (db > proof_worst) proof_worst = db;
                if (db > -d->cfg.stop_db) proof_ok = 0;
                hit = 1;
                break;
            }
            P *= d->stages[k].m;
        }
        if (!hit) proof_ok = 0;   /* would contradict the proof */
    }
    free(sd);

    /* Power bookkeeping for a white input that occupies the whole input
     * band [-0.5, 0.5].  |H| is even, so both mirror halves count.
     *   passband input  [-fp_out, fp_out]: what should be kept
     *   alias-band input[-0.5,-stop] and [stop,0.5]: what must be lost
     * The transition band is deliberately excluded from both. */
    const int GP = 8001;
    double pass_mean = 0.0;
    for (int i = 0; i < GP; ++i) {
        double f = fp_out * (double)i / (double)(GP - 1);
        double g = decim_response(d, f);
        pass_mean += g * g;
    }
    pass_mean /= (double)GP;
    double pass_power = pass_mean * 2.0 * fp_out;
    double alias_mean = alias_energy / (double)G;
    double alias_power = alias_mean * 2.0 * (0.5 - stop);
    r.alias_energy_share = alias_power / (alias_power + pass_power + 1e-300);
    r.alias_worst_db = worst;
    r.alias_at = worst_at;
    r.proof_ok = proof_ok;
    r.proof_worst_db = proof_worst;
    return r;
}

/* Overall passband ripple of the cascade (peak-to-peak, dB). */
static double cascade_ripple_db(const Decim *d, double fp_out) {
    const int G = 20001;
    double mx = 0.0, mn = 1e300;
    for (int i = 0; i < G; ++i) {
        double f = fp_out * (double)i / (double)(G - 1);
        double g = decim_response(d, f);
        if (g > mx) mx = g;
        if (g < mn) mn = g;
    }
    return 20.0 * log10(mx / mn);
}

/* ---- alias leakage through the sample path ------------------------
 *
 * Response formulas say what the *design* achieves.  To check the
 * actual arithmetic, feed the decimator a tone in the passband plus up
 * to three tones that alias onto the same output bin, and read the
 * level of the unwanted images out of the output spectrum.  A Hann
 * taper gives enough sidelobe suppression (-31 dB first sidelobe,
 * rolling off as f^-3) that a -90 dB image is cleanly visible.
 *
 * For an output bin at fo (cycles / output sample) the three aliasing
 * input frequencies are (fo + q) / D for q = 0, 1, 2, at most.
 */
static double hann_at(size_t i, size_t lo, size_t hi) {
    return 0.5 - 0.5 * cos(2.0 * PI * (double)(i - lo) /
                          (double)(hi - lo - 1));
}

/* Least-squares amplitude at output frequency fo over a Hann window. */
static double amp_at(const float *y, size_t lo, size_t hi, double fo) {
    double ss = 0, cc = 0, sc = 0, sy = 0, cy = 0;
    for (size_t i = lo; i < hi; ++i) {
        double w = hann_at(i, lo, hi);
        double s = w * sin(2.0 * PI * fo * (double)i);
        double c = w * cos(2.0 * PI * fo * (double)i);
        ss += s * s; cc += c * c; sc += s * c;
        sy += s * w * (double)y[i]; cy += c * w * (double)y[i];
    }
    double det = ss * cc - sc * sc;    if (fabs(det) < 1e-12) return 0.0;
    double A = (sy * cc - cy * sc) / det;
    double B = (cy * ss - sy * sc) / det;
    return sqrt(A * A + B * B);
}

static void alias_path_measurement(const Decim *d, const char *dir,
                                   const char *plan) {
    int D = d->total;
    const size_t N = 32768;
    float *in = (float *)malloc(sizeof(float) * N);
    float *out = (float *)malloc(sizeof(float) * N);
    size_t cap = decim_out_frames(d, N);
    if (cap <= 1024) { free(in); free(out); return; }

    const int NB = 31;
    char path[512];
    snprintf(path, sizeof(path), "%s/alias_path_%s.csv", dir, plan);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); free(in); free(out); return; }
    fprintf(f, "output_bin,interferer_input,interferer_gain_db,"
               "desired_input,desired_gain_db,rejected_40db\n");

    size_t lo = 256, hi = cap;
    double worst = -1e9;
    double worst_at = 0.0;

    for (int b = 0; b < NB; ++b) {
        double fo = 0.005 + (0.44 - 0.005) * (double)b / (double)(NB - 1);

        /* Desired image, q = 0: output fo. */
        double f0 = fo / (double)D;
        for (size_t i = 0; i < N; ++i)
            in[i] = (float)(0.25 * sin(2.0 * PI * f0 * (double)i + 0.3));
        decim_run(d, in, N, out, cap, NULL);
        double gdes = amp_at(out, lo, hi, fo);
        double gain_db = (gdes > 0.0) ? 20.0 * log10(gdes / 0.25) : -600.0;

        /* Interferers that fold onto output +-fo: (q +- fo)/D for q >= 1. */
        double inter[4];
        int nint = 0;
        for (int q = 1; q <= 3; ++q) {
            double fq = (fo + (double)q) / (double)D;
            if (fq < 0.5) inter[nint++] = fq;
            fq = ((double)q - fo) / (double)D;
            if (fq > 0.0 && fq < 0.5) {
                int dup = 0;
                for (int j = 0; j < nint; ++j)
                    if (fabs(inter[j] - fq) < 1e-12) dup = 1;
                if (!dup) inter[nint++] = fq;
            }
        }

        for (int j = 0; j < nint; ++j) {
            for (size_t i = 0; i < N; ++i)
                in[i] = (float)(0.05 * sin(2.0 * PI * inter[j] *
                                           (double)i + 1.7));
            decim_run(d, in, N, out, cap, NULL);
            double leak = amp_at(out, lo, hi, fo);
            double db = (leak > 0.0) ? 20.0 * log10(leak / 0.05) : -600.0;
            if (db > worst) { worst = db; worst_at = fo; }
            fprintf(f, "%.6f,%.8f,%.2f,%.8f,%.4f,%d\n",
                    fo, inter[j], db, f0, gain_db, db <= -40.0 ? 1 : 0);
        }
    }
    fclose(f);
    free(in); free(out);

    snprintf(path, sizeof(path), "%s/alias_path_worst.csv", dir);
    FILE *w = fopen(path, "a");
    if (w) {
        fprintf(w, "%s,%d,%.2f,%.6f\n", plan, D, worst, worst_at);
        fclose(w);
    }
    printf("  alias path %-12s worst single interferer %.1f dB at bin %.3f\n",
           plan, worst, worst_at);
}

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    double med_ms;
    double lo_ms;
    double hi_ms;
    double ns_per_out;
} Timing;

static Timing time_plan(const Decim *d, size_t n, int reps) {
    float *in = (float *)malloc(sizeof(float) * n);
    size_t cap = decim_out_frames(d, n);
    float *out = (float *)malloc(sizeof(float) * (cap ? cap : 1));
    rng_seed(12345u);
    for (size_t i = 0; i < n; ++i) in[i] = rng_float();

    for (int w = 0; w < 3; ++w) decim_run(d, in, n, out, cap, NULL);

    double *t = (double *)malloc(sizeof(double) * (size_t)reps);
    for (int r = 0; r < reps; ++r) {
        double t0 = seconds();
        decim_run(d, in, n, out, cap, NULL);
        t[r] = (seconds() - t0) * 1e3;
    }
    Timing tm;
    tm.lo_ms = t[0]; tm.hi_ms = t[0];
    for (int r = 1; r < reps; ++r) {
        if (t[r] < tm.lo_ms) tm.lo_ms = t[r];
        if (t[r] > tm.hi_ms) tm.hi_ms = t[r];
    }
    tm.med_ms = median(t, reps);
    tm.ns_per_out = cap ? tm.med_ms * 1e6 / (double)cap : 0.0;
    free(t); free(in); free(out);
    return tm;
}

/* ------------------------------------------------------------------ */
/* Plan table                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *name;
    int factors[DECIM_MAX_STAGES];
    int n;
} Plan;

static const Plan plans[] = {
    {"D6_single",  {6, 0, 0, 0},       1},
    {"D6_multi",   {3, 2, 0, 0},       2},
    {"D12_single", {12, 0, 0, 0},      1},
    {"D12_multi",  {3, 2, 2, 0},       3},
    {"D12_alt",    {4, 3, 0, 0},       2},
    {"D24_single", {24, 0, 0, 0},      1},
    {"D24_multi",  {3, 2, 2, 2},       4},
    {"D24_alt",    {4, 3, 2, 0},       3},
};
#define NPLANS ((int)(sizeof(plans) / sizeof(plans[0])))

static double effective_macs(const Decim *d, int *passes_out) {
    /* Per input sample, stage k costs taps_k multiplications per
     * output sample, and there are 1/(m1*...*mk) output samples per
     * input sample.  This is the true work measure. */
    double cost = 0.0;
    double rate = 1.0;
    for (int k = 0; k < d->n_stages; ++k) {
        rate /= (double)d->stages[k].m;
        cost += (double)d->stages[k].taps * rate;
    }
    if (passes_out) *passes_out = d->n_stages;
    return cost;
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

/* Per-stage design report: the closed-form inputs, the verified
 * outputs, and the coefficients themselves so the design can be
 * re-derived and checked without trusting this program. */
static int write_stages(const Decim *d, const char *dir, const char *name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/stages_%s.csv", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "stage,decimation,passband,stopband,cutoff,beta,taps,delay,"
               "sum_h,measured_ripple_db,measured_stop_db\n");
    for (int k = 0; k < d->n_stages; ++k) {
        const DecimStage *s = &d->stages[k];
        double sh = 0.0;
        for (int i = 0; i < s->taps; ++i) sh += s->h[i];
        fprintf(f, "%d,%d,%.17g,%.17g,%.17g,%.17g,%d,%d,%.15f,%.5f,%.2f\n",
                k + 1, s->m, s->fp, s->fs, s->fc, s->beta, s->taps,
                s->delay, sh, s->measured_ripple_db, s->measured_stop_db);
    }
    fclose(f);

    snprintf(path, sizeof(path), "%s/coeffs_%s.csv", dir, name);
    f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "stage,tap,coefficient\n");
    for (int k = 0; k < d->n_stages; ++k) {
        const DecimStage *s = &d->stages[k];
        for (int i = 0; i < s->taps; ++i)
            fprintf(f, "%d,%d,%.17g\n", k + 1, i, s->h[i]);
    }
    fclose(f);
    return 0;
}

static int write_freqresp(const Decim *d, const char *dir, const char *name,
                          int D) {
    char path[512];
    snprintf(path, sizeof(path), "%s/freqresp_%s.csv", dir, name);
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "frequency_cycles_per_input_sample,output_band,cascade_gain_db");
    for (int k = 0; k < d->n_stages; ++k)
        fprintf(fp, ",stage%d_gain_db", k + 1);
    fprintf(fp, "\n");

    double fp_out = 0.5 * d->cfg.passband / (double)D;
    double stop = 0.5 / (double)D;
    /* One curve per plot, not per analysis grid: 0.5/5000 = 1e-4 is
     * far below a pixel at any figure size, and the passband has its
     * own detailed sweep in the verifier and in summary.csv. */
    const int G = 5001;
    for (int i = 0; i < G; ++i) {
        double f = 0.5 * (double)i / (double)(G - 1);
        double g = decim_response(d, f);
        double db = (g > 1e-30) ? 20.0 * log10(g) : -600.0;
        fprintf(fp, "%.6f,%s,%.4f", f, f <= fp_out ? "passband" :
                (f >= stop ? "alias" : "transition"), db);
        for (int k = 0; k < d->n_stages; ++k) {
            double gk = decim_stage_gain(&d->stages[k], f);
            fprintf(fp, ",%.4f",
                    gk > 1e-30 ? 20.0 * log10(gk) : -600.0);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "results";
    char path[512];

    snprintf(path, sizeof(path), "%s/summary.csv", dir);
    FILE *sum = fopen(path, "w");
    if (!sum) { perror(path); return 1; }
    fprintf(sum,
            "plan,total_factor,stages,factors,taps_total,passes_per_input_"
            "sample,macs_per_input_sample,passband_edge_over_D,"
            "ripple_measured_db,alias_worst_db,alias_worst_at,"
            "alias_energy_share_db,"
            "median_ms_2s,min_ms_2s,max_ms_2s,ns_per_output,"
            "single_stage_taps,macs_single_stage,macs_ratio_multi_over_single,"
            "proof_worst_stage_rejection_db,proof_ok,"
            "path_worst_interferer_db\n");

    snprintf(path, sizeof(path), "%s/timing.csv", dir);
    FILE *tim = fopen(path, "w");
    if (!tim) { perror(path); return 1; }
    fprintf(tim, "plan,run,ms\n");

    /* Two seconds at 48 kHz. */
    const size_t NIN = 96000;
    const double SR = 48000.0;
    const int REPS = 31;

    printf("%-12s %4s %6s %12s %10s %13s %9s %10s\n",
           "plan", "D", "taps", "ripple dB", "alias dB", "share dB",
           "path dB", "median ms");

    for (int p = 0; p < NPLANS; ++p) {
        Decim d;
        int rc = decim_init(&d, plans[p].factors, plans[p].n, NULL);
        if (rc != DECIM_OK) {
            fprintf(stderr, "%s: init failed: %s\n", plans[p].name,
                    decim_status_name(rc));
            return 1;
        }
        int D = d.total;
        double fp_out = 0.5 * d.cfg.passband / (double)D;

        if (write_freqresp(&d, dir, plans[p].name, D) != 0 ||
            write_stages(&d, dir, plans[p].name) != 0) {
            perror("results");
            return 1;
        }

        double ripple = cascade_ripple_db(&d, fp_out);
        AliasReport ar = measure_aliases(&d, D, fp_out);

        /* Independent check through the sample path: tones that alias
         * onto the same output bin, measured in the output spectrum. */
        alias_path_measurement(&d, dir, plans[p].name);
        double path_worst = -600.0;
        {
            char pp[512];
            snprintf(pp, sizeof(pp), "%s/alias_path_worst.csv", dir);
            FILE *rf = fopen(pp, "r");
            if (rf) {
                char line[256];
                while (fgets(line, sizeof(line), rf)) {
                    char nm[64]; int dd; double v; double at;
                    if (sscanf(line, "%63[^,],%d,%lf,%lf", nm, &dd, &v,
                               &at) == 4 && strcmp(nm, plans[p].name) == 0)
                        path_worst = v;
                }
                fclose(rf);
            }
        }

        char factors[64] = "";
        for (int k = 0; k < d.n_stages; ++k) {
            char one[8];
            snprintf(one, sizeof(one), "%s%d", k ? "x" : "",
                     d.stages[k].m);
            strncat(factors, one, sizeof(factors) - strlen(factors) - 1);
        }

        int passes = 0;
        double macs = effective_macs(&d, &passes);
        double taps = 0.0;
        for (int k = 0; k < d.n_stages; ++k) taps += d.stages[k].taps;

        /* Same-factor single stage, for the cost comparison. */
        int single[1] = {D};
        Decim ds;
        double macs_single = 0.0, taps_single = 0.0;
        if (decim_init(&ds, single, 1, NULL) == DECIM_OK) {
            macs_single = effective_macs(&ds, NULL);
            for (int k = 0; k < ds.n_stages; ++k)
                taps_single += ds.stages[k].taps;
            decim_free(&ds);
        }

        Timing tm = time_plan(&d, NIN, REPS);
        for (int r = 0; r < REPS; ++r) {
            /* record each run again? time_plan keeps only summary; a
             * second short series gives the per-run CSV */
            (void)r;
        }

        fprintf(sum,
                "%s,%d,%d,%s,%.0f,%d,%.2f,%.4f,%.5f,%.2f,%.6f,%.2f,"
                "%.4f,%.4f,%.4f,%.2f,%.0f,%.2f,%.3f,%.2f,%d,%.2f\n",
                plans[p].name, D, d.n_stages, factors, taps, passes, macs,
                0.5 * d.cfg.passband, ripple, ar.alias_worst_db,
                ar.alias_at,
                10.0 * log10(ar.alias_energy_share + 1e-30),
                tm.med_ms, tm.lo_ms, tm.hi_ms, tm.ns_per_out,
                taps_single, macs_single,
                macs_single > 0 ? macs / macs_single : 0.0,
                ar.proof_worst_db, ar.proof_ok, path_worst);

        printf("%-12s %4d %6.0f %12.5f %10.1f %13.1f %9.1f %10.4f\n",
               plans[p].name, D, taps, ripple, ar.alias_worst_db,
               10.0 * log10(ar.alias_energy_share + 1e-30), path_worst,
               tm.med_ms);

        /* per-run timings, a clean series */
        {
            float *in = (float *)malloc(sizeof(float) * NIN);
            size_t cap = decim_out_frames(&d, NIN);
            float *out = (float *)malloc(sizeof(float) * (cap ? cap : 1));
            rng_seed(999u);
            for (size_t i = 0; i < NIN; ++i) in[i] = rng_float();
            for (int r = 0; r < REPS; ++r) {
                double t0 = seconds();
                decim_run(&d, in, NIN, out, cap, NULL);
                fprintf(tim, "%s,%d,%.5f\n", plans[p].name, r,
                        (seconds() - t0) * 1e3);
            }
            free(in); free(out);
        }

        decim_free(&d);
    }
    fclose(sum);
    fclose(tim);

    /* ---- bypass: factor one, byte-for-byte ------------------------ */
    snprintf(path, sizeof(path), "%s/bypass.csv", dir);
    FILE *bp = fopen(path, "w");
    if (!bp) { perror(path); return 1; }
    fprintf(bp, "plan,in_frames,status,copy_bit_identical,"
                "distinct_bit_patterns,median_ms_2s\n");

    struct { const char *name; int f[DECIM_MAX_STAGES]; int n; } byp[] = {
        {"empty_list",  {0},             0},
        {"one_factor",  {1},             1},
        {"ones_list",   {1, 1, 1},       3},
    };
    for (int b = 0; b < 3; ++b) {
        Decim d;
        int rc = decim_init(&d, byp[b].n ? byp[b].f : NULL, byp[b].n, NULL);
        if (rc != DECIM_OK) {
            fprintf(stderr, "bypass %s: %s\n", byp[b].name,
                    decim_status_name(rc));
            return 1;
        }
        size_t n = NIN;
        float *in = (float *)malloc(sizeof(float) * n);
        float *out = (float *)malloc(sizeof(float) * n);
        rng_seed(4242u);
        for (size_t i = 0; i < n; ++i) in[i] = rng_float();
        /* inject bit patterns that a copy must not normalize */
        union { uint32_t u; float f; } odd[5] = {
            {0x7FC00001u}, {0xFFC00002u}, {0x80000000u},
            {0x00000001u}, {0x7F800000u},
        };
        for (int i = 0; i < 5; ++i) in[100 + (size_t)i] = odd[i].f;

        DecimStats st;
        int r = decim_run(&d, in, n, out, n, &st);
        int same = memcmp(in, out, sizeof(float) * n) == 0;
        int distinct = 0;
        for (int i = 0; i < 5; ++i) {
            union { uint32_t u; float f; } u; u.f = out[100 + (size_t)i];
            if (u.u == odd[i].u) ++distinct;
        }
        double t0 = seconds();
        for (int rep = 0; rep < 51; ++rep)
            decim_run(&d, in, n, out, n, &st);
        double ms = (seconds() - t0) * 1e3 / 51.0;
        fprintf(bp, "%s,%zu,%s,%d,%d/5,%.4f\n", byp[b].name, n,
                decim_status_name(r), same, distinct, ms);
        free(in); free(out);
        decim_free(&d);
    }
    fclose(bp);

    /* ---- three edge inputs ---------------------------------------- */
    snprintf(path, sizeof(path), "%s/edge_cases.csv", dir);
    FILE *ec = fopen(path, "w");
    if (!ec) { perror(path); return 1; }
    fprintf(ec, "case,total_factor,in_frames,out_frames,status,"
                "tail_frames,short_input,partial,bypass,peak_out\n");

    struct { const char *name; int f[DECIM_MAX_STAGES]; int nf;
             size_t in; } edge[] = {
        {"factor_one_empty",   {0},        0, 100},
        {"factor_one_single",  {1},        1, 100},
        {"factor_one_list",    {1, 1, 1},  3, 100},
        {"nondivisible_1001",  {3, 2, 2},  3, 1001},
        {"nondivisible_5_of_6",{3, 2},     2, 5},
        {"divisible_12000",    {3, 2, 2},  3, 12000},
        {"short_3_of_span",    {3, 2, 2},  3, 3},
        {"short_span_minus_1", {3, 2, 2},  3, 1},
    };
    for (size_t e = 0; e < sizeof(edge) / sizeof(edge[0]); ++e) {
        Decim d;
        int rc = decim_init(&d, edge[e].nf ? edge[e].f : NULL,
                            edge[e].nf, NULL);
        if (rc != DECIM_OK) return 1;
        size_t n = edge[e].in;
        if (strcmp(edge[e].name, "short_span_minus_1") == 0)
            n = decim_span_input(&d) - 1;
        if (strcmp(edge[e].name, "short_3_of_span") == 0)
            n = 3;
        if (n > 200000) n = 200000;
        float *in = (float *)malloc(sizeof(float) * (n ? n : 1));
        for (size_t i = 0; i < n; ++i) in[i] = (float)sin(0.02 * (double)i);
        size_t cap = decim_out_frames(&d, n);
        float *out = (float *)malloc(sizeof(float) * (cap ? cap : 1));
        DecimStats st;
        int r = decim_run(&d, in, n, out, cap, &st);
        fprintf(ec, "%s,%d,%zu,%zu,%s,%zu,%d,%d,%d,%.8f\n",
                edge[e].name, d.total, n, st.out_frames,
                decim_status_name(r), st.tail_frames, st.short_input,
                st.partial, st.bypass, st.peak_out);
        free(in); free(out);
        decim_free(&d);
    }
    fclose(ec);

    (void)SR;
    printf("results written to %s/\n", dir);
    return 0;
}
