/*
 * downmix_report -- end-to-end harness for the downmixer.
 *
 *   downmix_report gen  <dir>   writes the self-made sample material
 *   downmix_report run  <dir>   processes it and writes results
 *
 * Everything is written as text CSV / float32 WAV so the numbers in the
 * report can be re-derived independently.
 */
#include "../src/downmix.h"
#include "wav.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FS 48000
#define SECONDS 2
#define FRAMES (FS * SECONDS)
#define TWO_PI 6.283185307179586476925286766559

/* ------------------------------------------------------------------ */
/* deterministic xorshift noise, so the sample is byte-reproducible    */
static uint32_t rng_state = 0x9E3779B9u;
static double frand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (double)(rng_state & 0xFFFFFFu) / 16777216.0;
}
static double frand_sym(void) { return frand() * 2.0 - 1.0; }

static double soft_limit(double x) { return tanh(x); }

/* A small self-made arrangement: a centred bass, a left-panned lead, a
 * right-panned hat, plus a hard-left-only pad tail. Everything is
 * generated, nothing is sampled from a real recording. */
static void synth(double* l, double* r, int frames, double peak_target) {
    for (int i = 0; i < frames; ++i) {
        double t = (double)i / FS;
        double sec = (double)i / FS;

        /* bass: 55 Hz, always centred, slight decay envelope per beat */
        double bass_env = exp(-4.0 * fmod(sec, 0.5));
        double bass = 0.30 * sin(TWO_PI * 55.0 * t) * bass_env;

        /* lead: 440 Hz with 5 Hz vibrato, constant-power pan 70% left */
        double vib = sin(TWO_PI * 5.0 * t) * 0.004;
        double lead = 0.22 * sin(TWO_PI * 440.0 * t + vib * 40.0);
        double lead_n = 0.07 * sin(TWO_PI * 880.0 * t + vib * 80.0);
        double pan_lead = 0.30;                 /* 0 = hard left */
        double gl_lead = cos(pan_lead * TWO_PI / 4.0);
        double gr_lead = sin(pan_lead * TWO_PI / 4.0);

        /* hat: band-ish noise bursts at 8 Hz, pan 80% right */
        double hat_gate = pow(fmax(0.0, sin(TWO_PI * 8.0 * t)), 12.0);
        double hat = (frand_sym() * 0.5 + 0.12 * sin(TWO_PI * 6300.0 * t)) *
                     hat_gate * 0.16;
        double pan_hat = 0.80;
        double gl_hat = cos(pan_hat * TWO_PI / 4.0);
        double gr_hat = sin(pan_hat * TWO_PI / 4.0);

        /* pad: hard left only in the last 0.5 s, to exercise 0/1 weights */
        double pad = 0.0;
        if (sec > 1.5) {
            double a = 0.10 * (sec - 1.5) / 0.5;
            pad = a * (sin(TWO_PI * 220.0 * t) + 0.5 * sin(TWO_PI * 330.0 * t));
        }

        double L = bass + (lead + lead_n) * gl_lead + hat * gl_hat + pad;
        double R = bass + (lead + lead_n) * gr_lead + hat * gr_hat;

        l[i] = L;
        r[i] = R;
    }

    /* Normalise to the requested peak. Both channels scale together, so
     * the inter-channel balance (and therefore the pan image) is kept. */
    double peak = 0.0;
    for (int i = 0; i < frames; ++i) {
        double a = fabs(l[i]), b = fabs(r[i]);
        if (a > peak) peak = a;
        if (b > peak) peak = b;
    }
    double g = peak > 0.0 ? peak_target / peak : 1.0;
    for (int i = 0; i < frames; ++i) {
        l[i] = soft_limit(l[i] * g);
        r[i] = soft_limit(r[i] * g);
    }
}

static int write_stereo(const char* path, const double* l, const double* r,
                        int frames) {
    WavData w;
    memset(&w, 0, sizeof w);
    w.channels = 2; w.sample_rate = FS; w.bits = 32; w.is_float = 1;
    w.frames = (size_t)frames;
    w.samples = (float*)malloc(sizeof(float) * 2u * (size_t)frames);
    if (!w.samples) return -1;
    for (int i = 0; i < frames; ++i) {
        w.samples[2 * i] = (float)l[i];
        w.samples[2 * i + 1] = (float)r[i];
    }
    const char* err = NULL;
    int rc = wav_write_f32(path, &w, &err);
    if (rc != 0) fprintf(stderr, "wav_write_f32(%s): %s\n", path, err);
    free(w.samples);
    return rc;
}

static int write_mono(const char* path, const double* m, int frames) {
    WavData w;
    memset(&w, 0, sizeof w);
    w.channels = 1; w.sample_rate = FS; w.bits = 32; w.is_float = 1;
    w.frames = (size_t)frames;
    w.samples = (float*)malloc(sizeof(float) * (size_t)frames);
    if (!w.samples) return -1;
    for (int i = 0; i < frames; ++i) w.samples[i] = (float)m[i];
    const char* err = NULL;
    int rc = wav_write_f32(path, &w, &err);
    if (rc != 0) fprintf(stderr, "wav_write_f32(%s): %s\n", path, err);
    free(w.samples);
    return rc;
}

/* ------------------------------------------------------------------ */
static void synthesize_sample(const char* data_dir) {
    double* l = (double*)malloc(sizeof(double) * FRAMES);
    double* r = (double*)malloc(sizeof(double) * FRAMES);
    double* m = (double*)malloc(sizeof(double) * FRAMES);
    double* fl = (double*)malloc(sizeof(double) * FRAMES);
    double* fr = (double*)malloc(sizeof(double) * FRAMES);
    if (!l || !r || !m || !fl || !fr) { fprintf(stderr, "oom\n"); exit(1); }

    synth(l, r, FRAMES, 0.891);          /* -1 dBFS nominal */

    /* Mono rendition of the same arrangement: each source summed once.
     * This is independent material, not a downmix of the stereo file. */
    for (int i = 0; i < FRAMES; ++i) {
        double t = (double)i / FS;
        double sec = (double)i / FS;
        double bass = 0.30 * exp(-4.0 * fmod(sec, 0.5)) * sin(TWO_PI * 55.0 * t);
        double lead = 0.22 * sin(TWO_PI * 440.0 * t) +
                      0.07 * sin(TWO_PI * 880.0 * t);
        double hat_gate = pow(fmax(0.0, sin(TWO_PI * 8.0 * t)), 12.0);
        double hat = (frand_sym() * 0.5 + 0.12 * sin(TWO_PI * 6300.0 * t)) *
                     hat_gate * 0.16;
        double pad = 0.0;
        if (sec > 1.5) {
            double a = 0.10 * (sec - 1.5) / 0.5;
            pad = a * (sin(TWO_PI * 220.0 * t) + 0.5 * sin(TWO_PI * 330.0 * t));
        }
        m[i] = bass + lead + hat + pad;
    }
    double mp = 0.0;
    for (int i = 0; i < FRAMES; ++i) if (fabs(m[i]) > mp) mp = fabs(m[i]);
    for (int i = 0; i < FRAMES; ++i) m[i] = soft_limit(m[i] * 0.85 / mp);

    /* Full-scale rendition: exactly +-1.0 at several frames, so the
     * full-scale path is demonstrable and the peak bound is exercised at
     * the boundary. */
    synth(fl, fr, FRAMES, 1.0);
    for (int i = 0; i < FRAMES; ++i) {
        fl[i] = (float)fl[i];   /* round through float, as stored */
        fr[i] = (float)fr[i];
    }
    fl[1234] = 1.0;  fr[1234] = 1.0;
    fl[4321] = -1.0; fr[4321] = -1.0;
    fl[7777] = 1.0;  fr[7777] = -1.0;

    char path[2048];
    snprintf(path, sizeof path, "%s/sample_stereo.wav", data_dir);
    if (write_stereo(path, l, r, FRAMES) != 0) exit(1);
    snprintf(path, sizeof path, "%s/sample_mono.wav", data_dir);
    if (write_mono(path, m, FRAMES) != 0) exit(1);
    snprintf(path, sizeof path, "%s/sample_fullscale.wav", data_dir);
    if (write_stereo(path, fl, fr, FRAMES) != 0) exit(1);

    printf("wrote %s/sample_{stereo,mono,fullscale}.wav  (%d frames @ %d Hz)\n",
           data_dir, FRAMES, FS);
    free(l); free(r); free(m); free(fl); free(fr);
}

/* ------------------------------------------------------------------ */
typedef struct { double wl, wr; const char* tag; } WS;

static const WS kSets[3] = {
    {0.5, 0.5, "w050_050"},
    {0.3, 0.7, "w030_070"},
    {0.2, 0.8, "w020_080"},
};

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static uint64_t fnv1a(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

static int cmp_double(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

/* Structural per-frame verification of the two hard claims. Returns the
 * number of violations found (must be 0). */
static long verify_frames(const float* in, const float* out, size_t frames,
                          double wl, double wr, double* max_ratio) {
    long bad = 0;
    double worst = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        double l = in[2 * i], r = in[2 * i + 1], o = out[i];
        if (fabs(o) > 1.0) ++bad;
        if (fabs(o) > wl * fabs(l) + wr * fabs(r) + 1e-6) ++bad;
        double den = wl * l * l + wr * r * r;
        if (den > 0.0) {
            double ratio = (o * o) / den;
            if (ratio > worst) worst = ratio;
            if (ratio > 1.0 + 1e-6) ++bad;
        }
    }
    if (max_ratio) *max_ratio = worst;
    return bad;
}

static void run_one(const char* data_dir, const char* wav_dir,
                    const char* stats_dir, const WS* ws, FILE* summary,
                    FILE* per_frame, FILE* blocks, FILE* worst) {
    (void)stats_dir;
    char path[2048];
    WavData src;
    const char* err = NULL;
    snprintf(path, sizeof path, "%s/sample_stereo.wav", data_dir);
    if (wav_read(path, &src, &err) != 0) {
        fprintf(stderr, "read %s: %s\n", path, err);
        exit(1);
    }
    if (src.channels != 2) { fprintf(stderr, "expected stereo\n"); exit(1); }
    size_t n = src.frames;

    float* out = (float*)malloc(sizeof(float) * n);
    if (!out) { fprintf(stderr, "oom\n"); exit(1); }

    /* --- timing: 3 warm-up + 51 timed runs of the mixing loop only --- */
    const int reps = 51;
    double* times = (double*)malloc(sizeof(double) * (size_t)reps);
    if (!times) { fprintf(stderr, "oom\n"); exit(1); }
    for (int k = 0; k < 3; ++k)
        (void)downmix_stereo_to_mono(src.samples, n, (float)ws->wl,
                                     (float)ws->wr, out, NULL);
    for (int k = 0; k < reps; ++k) {
        double t0 = now_sec();
        int rc = downmix_stereo_to_mono(src.samples, n, (float)ws->wl,
                                        (float)ws->wr, out, NULL);
        double t1 = now_sec();
        if (rc != DOWNMIX_OK && rc != DOWNMIX_OK_FULL_SCALE) {
            fprintf(stderr, "mix failed rc=%d\n", rc);
            exit(1);
        }
        times[k] = t1 - t0;
    }
    qsort(times, (size_t)reps, sizeof(double), cmp_double);
    double t_min = times[0], t_med = times[reps / 2];
    double ns_per_frame = t_med * 1e9 / (double)n;

    /* Baseline: the "just take the left channel" shortcut this library
     * replaces. Timed the same way so the cost comparison is honest. */
    for (int k = 0; k < 3; ++k)
        for (size_t i = 0; i < n; ++i) out[i] = src.samples[2 * i];
    for (int k = 0; k < reps; ++k) {
        double t0 = now_sec();
        for (size_t i = 0; i < n; ++i) out[i] = src.samples[2 * i];
        times[k] = now_sec() - t0;
    }
    qsort(times, (size_t)reps, sizeof(double), cmp_double);
    double t_naive_med = times[reps / 2];
    double ns_per_frame_naive = t_naive_med * 1e9 / (double)n;
    double overhead = t_naive_med > 0.0 ? t_med / t_naive_med : 0.0;

    /* --- measured run with a fresh stats record --- */
    DownmixStats st;
    downmix_stats_reset(&st);
    int rc = downmix_stereo_to_mono(src.samples, n, (float)ws->wl,
                                    (float)ws->wr, out, &st);

    double max_ratio = 0.0;
    long violations = verify_frames(src.samples, out, n, ws->wl, ws->wr,
                                    &max_ratio);

    /* --- before / after peak and RMS --- */
    double peak_l = 0, peak_r = 0, peak_comb = 0, peak_out = 0;
    double e_l = 0, e_r = 0, e_out = 0, e_weighted = 0;
    double e_naive = 0;   /* the "take one channel" shortcut, for reference */
    for (size_t i = 0; i < n; ++i) {
        double l = src.samples[2 * i], r = src.samples[2 * i + 1], o = out[i];
        if (fabs(l) > peak_l) peak_l = fabs(l);
        if (fabs(r) > peak_r) peak_r = fabs(r);
        if (fabs(l) > peak_comb) peak_comb = fabs(l);
        if (fabs(r) > peak_comb) peak_comb = fabs(r);
        if (fabs(o) > peak_out) peak_out = fabs(o);
        e_l += l * l; e_r += r * r; e_out += o * o;
        e_weighted += ws->wl * l * l + ws->wr * r * r;
        e_naive += l * l;
    }
    double rms_l = sqrt(e_l / (double)n);
    double rms_r = sqrt(e_r / (double)n);
    double rms_stereo = sqrt((e_l + e_r) / (2.0 * (double)n));
    double rms_weighted = sqrt(e_weighted / (double)n);
    double rms_out = sqrt(e_out / (double)n);
    double rms_naive = sqrt(e_naive / (double)n);   /* left-only */
    double energy_kept_vs_raw = e_out / ((e_l + e_r) / 2.0);
    double ledger_abs = st.in_energy - st.out_energy - st.diff_energy;
    double ledger_rel = ledger_abs / (st.in_energy > 0 ? st.in_energy : 1.0);

    /* --- per-frame trace: output vs. the theoretical convex bound --- */
    if (per_frame) {
        fprintf(per_frame, "frame,out,bound_l1,frame_energy_ratio\n");
        size_t step = n > 5000 ? n / 5000 : 1;   /* keep the file small */
        for (size_t i = 0; i < n; i += step) {
            double l = src.samples[2 * i], r = src.samples[2 * i + 1];
            double den = ws->wl * l * l + ws->wr * r * r;
            fprintf(per_frame, "%zu,%.9g,%.9g,%.9g\n", i, (double)out[i],
                    ws->wl * fabs(l) + ws->wr * fabs(r),
                    den > 0.0 ? ((double)out[i] * out[i]) / den : 0.0);
        }
    }

    /* --- block energy stability ---
     * "No energy jump at a block boundary" is guaranteed structurally:
     * the mixer is memoryless, so a random partition is bit-identical to
     * the whole run (checked here) and the boundary cannot be special.
     * What is also worth measuring is that no block carries more energy
     * than its own input: for every block, sum(out^2) <= sum(wL*L^2 +
     * wR*R^2), and the frame energy ratio never exceeds 1 regardless of
     * where the block boundary falls. The relative block-to-block energy
     * ratio of the *material* is written too, for reference; it is not a
     * pass/fail criterion because a drum hit legitimately raises it. */
    if (blocks) {
        fprintf(blocks, "block_frames,blocks_checked,"
                        "worst_block_energy_ratio,"
                        "max_frame_energy_ratio_in_block,"
                        "partition_bit_identical\n");
        float* chunked = (float*)malloc(sizeof(float) * n);
        if (!chunked) { fprintf(stderr, "oom\n"); exit(1); }
        size_t pos = 0;
        uint32_t seed = 12345u;
        while (pos < n) {
            seed = seed * 1103515245u + 12345u;
            size_t len = 1 + (seed >> 8) % 997u;
            if (len > n - pos) len = n - pos;
            (void)downmix_stereo_to_mono(src.samples + 2 * pos, len,
                                         (float)ws->wl, (float)ws->wr,
                                         chunked + pos, NULL);
            pos += len;
        }
        int identical = memcmp(out, chunked, n * sizeof(float)) == 0;
        free(chunked);

        const size_t sizes[] = {64, 256, 1024, 4096, 16384};
        for (size_t b = 0; b < sizeof sizes / sizeof sizes[0]; ++b) {
            size_t bs = sizes[b];
            if (bs < 2 || bs * 2 > n) continue;
            double worst_block = 0.0, worst_frame = 0.0;
            long nb = 0;
            for (size_t off = 0; off + bs <= n; off += bs) {
                double e_out = 0.0, e_in = 0.0;
                for (size_t i = off; i < off + bs; ++i) {
                    double l = src.samples[2 * i], r = src.samples[2 * i + 1];
                    e_out += (double)out[i] * out[i];
                    e_in += ws->wl * l * l + ws->wr * r * r;
                    if (e_in > 0.0) {
                        double fr = (double)out[i] * out[i] /
                                    (ws->wl * l * l + ws->wr * r * r);
                        if (fr > worst_frame) worst_frame = fr;
                    }
                }
                if (e_in > 0.0) {
                    double br = e_out / e_in;
                    if (br > worst_block) worst_block = br;
                }
                ++nb;
            }
            fprintf(blocks, "%zu,%ld,%.12f,%.12f,%s\n", bs, nb, worst_block,
                    worst_frame, identical ? "yes" : "NO");
            if (worst_block > 1.0 + 1e-6) {
                fprintf(stderr, "block %zu gained energy: %.12f\n", bs,
                        worst_block);
                exit(1);
            }
        }
        if (!identical) {
            fprintf(stderr, "block partition changed the output\n");
            exit(1);
        }
    }

    /* --- frame-to-frame steps, input vs. output ---
     * For a linear map out = wL*L + wR*R, the output step is bounded by
     * the weighted input steps, |d_out| <= wL*|dL| + wR*|dR|. We report
     * the worst ratio |d_out| / (wL*|dL| + wR*|dR|); the mixer must not
     * exceed 1, and the value is the same whether or not the material
     * itself jumps. This isolates the mixer from the programming.
     *
     * The output is stored as float and each product is rounded before
     * the sum, so the measured step may exceed the exact bound by a few
     * ulp of the magnitudes actually involved. The slack below accounts
     * for rounding at both endpoints of the frame (input samples,
     * products, and the stored output), so a correct mixer scores <= 1
     * no matter how small the frame's step happens to be. */
    if (worst) {
        double worst_ratio = 0.0;
        long worst_at = -1;
        double out_step = 0.0, in_step = 0.0;
        for (size_t i = 1; i < n; ++i) {
            double l0 = src.samples[2 * i - 2], r0 = src.samples[2 * i - 1];
            double l1 = src.samples[2 * i], r1 = src.samples[2 * i + 1];
            double dl = fabs(l1 - l0), dr = fabs(r1 - r0);
            double bound = ws->wl * dl + ws->wr * dr;
            double dout = fabs((double)out[i] - (double)out[i - 1]);
            if (dout > out_step) out_step = dout;
            if (bound > in_step) in_step = bound;
            /* Rounding budget: each of the four products is rounded, each
             * sum is rounded, and both stored outputs are rounded. The
             * per-sample magnitude bounds that budget. */
            double mag = fmax(fmax(fabs(l0), fabs(l1)),
                              fmax(fabs(r0), fabs(r1)));
            mag = fmax(mag, fmax(fabs((double)out[i - 1]),
                                 fabs((double)out[i])));
            double slack = bound + 8.0 * 6.0e-8 * (mag + 1e-30);
            double ratio = dout / slack;
            if (ratio > worst_ratio) { worst_ratio = ratio; worst_at = (long)i; }
        }
        if (worst_at < 0) worst_ratio = 0.0;   /* nothing measurable */
        fprintf(worst,
                "weights_tag,max_out_step,max_weighted_in_step,"
                "step_amplification,worst_frame,alarm_threshold\n");
        fprintf(worst, "%s,%.12e,%.12e,%.9f,%ld,%.9f\n", ws->tag, out_step,
                in_step, worst_ratio, worst_at, (double)DOWNMIX_JUMP_RATIO);
        if (worst_ratio > 1.000001) {
            fprintf(stderr, "mixer amplified a step at frame %ld\n", worst_at);
            exit(1);
        }
    }

    /* --- write the mixed WAV --- */
    WavData dst;
    memset(&dst, 0, sizeof dst);
    dst.channels = 1; dst.sample_rate = src.sample_rate;
    dst.bits = 32; dst.is_float = 1; dst.frames = n;
    dst.samples = out;
    snprintf(path, sizeof path, "%s/downmix_%s.wav", wav_dir, ws->tag);
    if (wav_write_f32(path, &dst, &err) != 0) {
        fprintf(stderr, "write %s: %s\n", path, err);
        exit(1);
    }
    uint64_t hash = fnv1a(out, n * sizeof(float));

    if (summary) {
        fprintf(summary,
                "%s,%.6f,%.6f,%zu,%d,"
                "%.9f,%.9f,%.9f,%.9f,"
                "%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,"
                "%.12e,%.12e,%.12e,%.12e,"
                "%.3e,%.9f,%.9f,"
                "%ld,%ld,%ld,"
                "%.6f,%.6f,%.6f,%.6f,%.6f,"
                "%016llx\n",
                ws->tag, ws->wl, ws->wr, n, rc,
                peak_l, peak_r, peak_comb, peak_out,
                rms_l, rms_r, rms_stereo, rms_weighted, rms_out,
                rms_naive, energy_kept_vs_raw,
                e_l, e_r, e_weighted, e_out,
                ledger_rel, max_ratio, st.peak_out,
                st.full_scale_frames, st.clipped_frames, violations,
                t_min * 1e3, t_med * 1e3, ns_per_frame,
                ns_per_frame_naive, overhead,
                (unsigned long long)hash);
    }

    free(times);
    free(out);
    wav_free(&src);
}

/* ------------------------------------------------------------------ */
/* Edge-case matrix: the three diagnosable classes plus mono input.    */
static void edge_cases(const char* data_dir, const char* wav_dir,
                       const char* stats_dir) {
    char path[2048];
    snprintf(path, sizeof path, "%s/edge_cases.csv", stats_dir);
    FILE* csv = fopen(path, "w");
    if (!csv) { perror(path); exit(1); }
    fprintf(csv, "case,channels_in,wl,wr,status_code,status_name,"
                 "frames_processed,peak_out,clipped_frames,"
                 "mono_passthrough_frames,output_untouched\n");

    float buf[512];
    float out[256];
    for (int i = 0; i < 512; ++i) buf[i] = (float)(0.5 * sin(i * 0.017) +
                                                   0.3 * cos(i * 0.031));
    for (int i = 0; i < 256; ++i) out[i] = 0.125f;

    struct {
        const char* name; int ch; float wl, wr;
    } cases[] = {
        {"nominal_stereo", 2, 0.5f, 0.5f},
        {"illegal_weights_sum", 2, 0.5f, 0.6f},
        {"illegal_weights_negative", 2, -0.1f, 1.1f},
        {"illegal_weights_nan", 2, NAN, 0.5f},
        {"unsupported_channels_3", 3, 0.5f, 0.5f},
        {"unsupported_channels_4", 4, 0.5f, 0.5f},
        {"unsupported_channels_0", 0, 0.5f, 0.5f},
        {"mono_passthrough", 1, 0.5f, 0.5f},
        {"mono_ignores_bad_weights", 1, 0.0f, 0.0f},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        for (int k = 0; k < 256; ++k) out[k] = 0.125f;
        DownmixStats st;
        downmix_stats_reset(&st);
        int rc = downmix_frame(buf, 100, cases[i].ch, cases[i].wl,
                               cases[i].wr, out, &st);
        const char* name = "?";
        switch (rc) {
            case DOWNMIX_OK: name = "OK"; break;
            case DOWNMIX_OK_MONO: name = "OK_MONO"; break;
            case DOWNMIX_OK_FULL_SCALE: name = "OK_FULL_SCALE"; break;
            case DOWNMIX_E_NULL: name = "E_NULL"; break;
            case DOWNMIX_E_WEIGHTS: name = "E_WEIGHTS"; break;
            case DOWNMIX_E_CHANNELS: name = "E_CHANNELS"; break;
        }
        int untouched = 1;
        if (rc < 0 || rc == DOWNMIX_OK_MONO) {
            /* rejected calls must not write; mono writes exactly n frames */
            for (int k = rc < 0 ? 0 : 100; k < 256; ++k)
                if (out[k] != 0.125f) untouched = 0;
        }
        fprintf(csv, "%s,%d,%.6g,%.6g,%d,%s,%ld,%.9g,%ld,%ld,%d\n",
                cases[i].name, cases[i].ch, (double)cases[i].wl,
                (double)cases[i].wr, rc, name, st.frames,
                (double)st.peak_out, st.clipped_frames,
                st.mono_passthrough_frames, untouched);
    }

    /* Full-scale input: same samples, status must change class. */
    float fs[8] = { 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f, 0.5f, 0.5f };
    float fsout[4];
    DownmixStats st;
    downmix_stats_reset(&st);
    int rc = downmix_stereo_to_mono(fs, 4, 0.5f, 0.5f, fsout, &st);
    fprintf(csv, "full_scale_stereo,2,0.5,0.5,%d,%s,%ld,%.9g,%ld,%ld,%d\n",
            rc, rc == DOWNMIX_OK_FULL_SCALE ? "OK_FULL_SCALE" : "UNEXPECTED",
            st.frames, (double)st.peak_out, st.clipped_frames,
            st.mono_passthrough_frames, 1);
    fclose(csv);

    /* Mono file: the library must reproduce it bit-for-bit. */
    WavData mono;
    const char* err = NULL;
    snprintf(path, sizeof path, "%s/sample_mono.wav", data_dir);
    if (wav_read(path, &mono, &err) != 0) {
        fprintf(stderr, "read %s: %s\n", path, err);
        exit(1);
    }
    float* mout = (float*)malloc(sizeof(float) * mono.frames);
    if (!mout) { fprintf(stderr, "oom\n"); exit(1); }
    downmix_stats_reset(&st);
    rc = downmix_frame(mono.samples, mono.frames, mono.channels, 0.5f, 0.5f,
                       mout, &st);
    int identical = memcmp(mono.samples, mout, mono.frames * sizeof(float)) == 0;
    printf("mono file: %zu frames, status=%d, bit-identical=%s, "
           "passthrough_frames=%ld\n",
           mono.frames, rc, identical ? "yes" : "NO", st.mono_passthrough_frames);
    if (rc != DOWNMIX_OK_MONO || !identical) {
        fprintf(stderr, "mono passthrough contract violated\n");
        exit(1);
    }
    {
        /* Persist the passthrough so an independent reader can confirm it. */
        char wpath[2048];
        snprintf(wpath, sizeof wpath, "%s/downmix_mono_passthrough.wav",
                 wav_dir);
        WavData dst;
        memset(&dst, 0, sizeof dst);
        dst.channels = 1; dst.sample_rate = mono.sample_rate;
        dst.bits = 32; dst.is_float = 1; dst.frames = mono.frames;
        dst.samples = mout;
        if (wav_write_f32(wpath, &dst, &err) != 0) {
            fprintf(stderr, "write %s: %s\n", wpath, err);
            exit(1);
        }
    }
    free(mout);
    wav_free(&mono);

    printf("wrote %s\n", path);
}

/* ------------------------------------------------------------------ */
/* Full-scale file: all three weight sets, status must be detectable.   */
static void fullscale_run(const char* data_dir, const char* wav_dir,
                          const char* stats_dir) {
    char path[2048];
    WavData src;
    const char* err = NULL;
    snprintf(path, sizeof path, "%s/sample_fullscale.wav", data_dir);
    if (wav_read(path, &src, &err) != 0) {
        fprintf(stderr, "read %s: %s\n", path, err);
        exit(1);
    }
    snprintf(path, sizeof path, "%s/fullscale.csv", stats_dir);
    FILE* f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "weights_tag,wl,wr,status_code,full_scale_frames,"
               "clipped_frames,peak_out,peak_bound_ok,structural_violations,"
               "out_hash_fnv1a\n");
    float* out = (float*)malloc(sizeof(float) * src.frames);
    if (!out) { fprintf(stderr, "oom\n"); exit(1); }
    for (int i = 0; i < 3; ++i) {
        DownmixStats st;
        downmix_stats_reset(&st);
        int rc = downmix_stereo_to_mono(src.samples, src.frames,
                                        (float)kSets[i].wl,
                                        (float)kSets[i].wr, out, &st);
        double max_ratio = 0.0;
        long bad = verify_frames(src.samples, out, src.frames, kSets[i].wl,
                                 kSets[i].wr, &max_ratio);
        int bound_ok = st.peak_out <= 1.0 + 1e-9 && bad == 0;
        fprintf(f, "%s,%.6f,%.6f,%d,%ld,%ld,%.9g,%d,%ld,%016llx\n",
                kSets[i].tag, kSets[i].wl, kSets[i].wr, rc,
                st.full_scale_frames, st.clipped_frames, st.peak_out,
                bound_ok, bad,
                (unsigned long long)fnv1a(out, src.frames * sizeof(float)));

        char wpath[2048];
        snprintf(wpath, sizeof wpath, "%s/downmix_fullscale_%s.wav", wav_dir,
                 kSets[i].tag);
        WavData dst;
        memset(&dst, 0, sizeof dst);
        dst.channels = 1; dst.sample_rate = src.sample_rate;
        dst.bits = 32; dst.is_float = 1; dst.frames = src.frames;
        dst.samples = out;
        if (wav_write_f32(wpath, &dst, &err) != 0) {
            fprintf(stderr, "write %s: %s\n", wpath, err);
            exit(1);
        }
        if (rc != DOWNMIX_OK_FULL_SCALE)
            fprintf(stderr, "full-scale file did not report full scale\n");
    }
    fclose(f);
    free(out);
    wav_free(&src);
    printf("wrote %s/fullscale.csv\n", stats_dir);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s gen|run <root>\n", argv[0]);
        return 2;
    }
    const char* mode = argv[1];
    const char* root = argv[2];
    char data_dir[1024], wav_dir[1024], stats_dir[1024];
    snprintf(data_dir, sizeof data_dir, "%s/data", root);
    snprintf(wav_dir, sizeof wav_dir, "%s/results/wav", root);
    snprintf(stats_dir, sizeof stats_dir, "%s/results/stats", root);

    if (strcmp(mode, "gen") == 0) {
        synthesize_sample(data_dir);
        return 0;
    }
    if (strcmp(mode, "run") != 0) {
        fprintf(stderr, "unknown mode %s\n", mode);
        return 2;
    }

    char path[2048];
    snprintf(path, sizeof path, "%s/summary.csv", stats_dir);
    FILE* summary = fopen(path, "w");
    if (!summary) { perror(path); return 1; }
    fprintf(summary,
            "weights_tag,wl,wr,frames,status_code,"
            "peak_l_in,peak_r_in,peak_combined_in,peak_out,"
            "rms_l_in,rms_r_in,rms_stereo_in,rms_weighted_in,rms_out,"
            "rms_left_only_baseline,energy_kept_vs_raw_mean,"
            "energy_l_in,energy_r_in,energy_weighted_in,energy_out,"
            "energy_ledger_rel,max_frame_energy_ratio,peak_out_stat,"
            "full_scale_frames,clipped_frames,structural_violations,"
            "t_min_ms,t_median_ms,ns_per_frame,"
            "ns_per_frame_naive_baseline,overhead_vs_baseline,"
            "out_hash_fnv1a\n");

    for (int i = 0; i < 3; ++i) {
        char pf[2048], bl[2048], wr[2048];
        snprintf(pf, sizeof pf, "%s/per_frame_%s.csv", stats_dir,
                 kSets[i].tag);
        snprintf(bl, sizeof bl, "%s/blocks_%s.csv", stats_dir, kSets[i].tag);
        snprintf(wr, sizeof wr, "%s/steps_%s.csv", stats_dir, kSets[i].tag);
        FILE* fpf = fopen(pf, "w");
        FILE* fbl = fopen(bl, "w");
        FILE* fwr = fopen(wr, "w");
        if (!fpf || !fbl || !fwr) { perror("stats file"); return 1; }
        run_one(data_dir, wav_dir, stats_dir, &kSets[i], summary, fpf, fbl, fwr);
        fclose(fpf); fclose(fbl); fclose(fwr);
    }
    fclose(summary);
    printf("wrote %s/summary.csv\n", stats_dir);

    fullscale_run(data_dir, wav_dir, stats_dir);
    edge_cases(data_dir, wav_dir, stats_dir);
    return 0;
}
