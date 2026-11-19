#include "../src/downmix.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_MSG(c, ...) do { ++checks; if (!(c)) { ++fails; \
    printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned rng = 0x12345678u;
static float frand_unit(void) {          /* [-1, 1) */
    rng = rng * 1664525u + 1013904223u;
    return (float)((int32_t)(rng >> 8) - (int32_t)0x800000) / 8388608.0f;
}

typedef struct { float wl, wr; const char* name; } WeightSet;

static const WeightSet kWeights[] = {
    {0.5f, 0.5f, "equal 0.5/0.5"},
    {0.3f, 0.7f, "0.3/0.7"},
    {0.2f, 0.8f, "0.2/0.8"},
    {0.0f, 1.0f, "0.0/1.0"},
    {1.0f, 0.0f, "1.0/0.0"},
    {0.25f, 0.75f, "0.25/0.75"},
};
#define NWEIGHTS (sizeof(kWeights) / sizeof(kWeights[0]))

/* ------------------------------------------------------------------ */
static void test_weight_validation(void) {
    const char* why = NULL;
    for (size_t i = 0; i < NWEIGHTS; ++i) {
        CHECK_MSG(downmix_weights_valid(kWeights[i].wl, kWeights[i].wr, &why),
                  "expected legal: %s (%s)", kWeights[i].name, why);
        CHECK(why != NULL && strcmp(why, "ok") == 0);
    }

    struct { float wl, wr; const char* what; } bad[] = {
        {-0.1f, 1.1f, "negative left"},
        {1.1f, -0.1f, "negative right"},
        {0.5f, 0.6f, "sum 1.1"},
        {0.5f, 0.4f, "sum 0.9"},
        {0.0f, 0.0f, "sum 0"},
        {NAN, 0.5f, "nan left"},
        {0.5f, NAN, "nan right"},
        {INFINITY, 0.5f, "inf left"},
        {0.5f, -INFINITY, "inf right"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        why = NULL;
        CHECK_MSG(!downmix_weights_valid(bad[i].wl, bad[i].wr, &why),
                  "expected illegal: %s", bad[i].what);
        CHECK_MSG(why != NULL && strcmp(why, "ok") != 0,
                  "missing reason for %s", bad[i].what);
    }
    /* The 1e-6 tolerance must not reject ordinary float pairs and must
     * not accept a 1e-3 error. */
    CHECK(downmix_weights_valid(0.30000001f, 0.69999999f, NULL));
    CHECK(!downmix_weights_valid(0.5005f, 0.5f, NULL));
}

/* ------------------------------------------------------------------ */
static void test_status_codes_distinct(void) {
    /* The three diagnosable classes must not collide, and none may look
     * like success. */
    CHECK(DOWNMIX_OK == 0);
    CHECK(DOWNMIX_OK_MONO != DOWNMIX_OK);
    CHECK(DOWNMIX_OK_FULL_SCALE != DOWNMIX_OK);
    CHECK(DOWNMIX_E_WEIGHTS != DOWNMIX_E_CHANNELS);
    CHECK(DOWNMIX_E_WEIGHTS < 0 && DOWNMIX_E_CHANNELS < 0);
    CHECK(DOWNMIX_OK_FULL_SCALE > 0);
    CHECK(DOWNMIX_E_NULL < 0);
}

/* ------------------------------------------------------------------ */
static void test_mono_passthrough(void) {
    float in[257], out[257], sentinel_out[260];
    for (int i = 0; i < 257; ++i) in[i] = frand_unit() * 0.8f;

    DownmixStats st;
    downmix_stats_reset(&st);
    /* Weights are irrelevant for mono -- even ones that a stereo call
     * would reject must not attenuate or reject a mono file. */
    int rc = downmix_frame(in, 257, 1, 0.0f, 0.0f, out, &st);
    CHECK(rc == DOWNMIX_OK_MONO);
    CHECK(memcmp(in, out, sizeof in) == 0);
    CHECK(st.frames == 257);
    CHECK(st.mono_passthrough_frames == 257);
    CHECK(st.weight_rejects == 0 && st.channel_rejects == 0);
    CHECK(st.peak_in == st.peak_out);
    CHECK(st.in_energy == st.out_energy);

    /* In-place passthrough is legal and stays exact. */
    memcpy(out, in, sizeof in);
    rc = downmix_frame(out, 257, 1, 0.3f, 0.7f, out, NULL);
    CHECK(rc == DOWNMIX_OK_MONO);
    CHECK(memcmp(out, in, sizeof in) == 0);

    /* Exact bounds: nothing written outside out[0..n-1]. */
    for (int i = 0; i < 260; ++i) sentinel_out[i] = 123.5f;
    rc = downmix_frame(in, 10, 1, 0.5f, 0.5f, sentinel_out, NULL);
    CHECK(rc == DOWNMIX_OK_MONO);
    for (int i = 10; i < 260; ++i) CHECK(sentinel_out[i] == 123.5f);
    CHECK(sentinel_out[0] == in[0]);

    /* Mono full-scale is still mono passthrough; documented precedence. */
    float fs[4] = {1.0f, -1.0f, 0.0f, 0.999999f};
    rc = downmix_frame(fs, 4, 1, 0.5f, 0.5f, out, NULL);
    CHECK(rc == DOWNMIX_OK_MONO);
    CHECK(memcmp(fs, out, sizeof fs) == 0);
}

/* ------------------------------------------------------------------ */
static void test_channel_rejection(void) {
    float in[16] = {0}, out[16];
    int chans[] = {0, 3, 4, -1, 100};
    for (size_t i = 0; i < sizeof(chans) / sizeof(chans[0]); ++i) {
        for (int k = 0; k < 16; ++k) out[k] = 42.0f;
        DownmixStats st;
        downmix_stats_reset(&st);
        int rc = downmix_frame(in, 8, chans[i], 0.5f, 0.5f, out, &st);
        CHECK_MSG(rc == DOWNMIX_E_CHANNELS, "channels=%d rc=%d", chans[i], rc);
        CHECK(st.channel_rejects == 1);
        for (int k = 0; k < 16; ++k) CHECK(out[k] == 42.0f);
    }
    /* An unsupported channel count wins over bad weights: the caller is
     * told the more fundamental problem. */
    DownmixStats st;
    downmix_stats_reset(&st);
    CHECK(downmix_frame(in, 8, 3, 0.0f, 0.0f, out, &st) == DOWNMIX_E_CHANNELS);
    CHECK(st.channel_rejects == 1 && st.weight_rejects == 0);
}

/* ------------------------------------------------------------------ */
static void test_weight_rejection_untouched(void) {
    float in[32], out[32];
    for (int i = 0; i < 32; ++i) in[i] = frand_unit();
    for (int k = 0; k < 32; ++k) out[k] = -7.25f;
    DownmixStats st;
    downmix_stats_reset(&st);
    int rc = downmix_stereo_to_mono(in, 16, 0.5f, 0.6f, out, &st);
    CHECK(rc == DOWNMIX_E_WEIGHTS);
    CHECK(st.weight_rejects == 1);
    CHECK(st.frames == 0);
    for (int k = 0; k < 32; ++k) CHECK(out[k] == -7.25f);

    /* NULL handling is its own class and does not touch stats. */
    downmix_stats_reset(&st);
    CHECK(downmix_frame(NULL, 4, 2, 0.5f, 0.5f, out, &st) == DOWNMIX_E_NULL);
    CHECK(downmix_frame(in, 4, 2, 0.5f, 0.5f, NULL, &st) == DOWNMIX_E_NULL);
    CHECK(st.weight_rejects == 0 && st.channel_rejects == 0);
}

/* ------------------------------------------------------------------ */
static void test_full_scale(void) {
    float in[] = { 1.0f, 1.0f,  1.0f, -1.0f,  -1.0f, -1.0f,
                   1.0f, 0.0f,  -1.0f, 0.0f,   0.5f, 0.5f };
    const size_t frames = sizeof in / (2 * sizeof in[0]);
    float out[frames];
    for (int i = 0; i < 3; ++i) {           /* repeat to test detection */
        DownmixStats st;
        downmix_stats_reset(&st);
        int rc = downmix_stereo_to_mono(in, frames, kWeights[i].wl,
                                        kWeights[i].wr, out, &st);
        CHECK_MSG(rc == DOWNMIX_OK_FULL_SCALE, "weights %s rc=%d",
                  kWeights[i].name, rc);
        CHECK(st.full_scale_frames == (long)frames - 1); /* last frame 0.5 */
        CHECK(st.clipped_frames == 0);
        for (size_t k = 0; k < frames; ++k) {
            CHECK(fabs((double)out[k]) <= 1.0);
            /* Weighted L1 triangle bound, evaluated on the input frame. */
            double bound = kWeights[i].wl * fabs((double)in[2 * k]) +
                           kWeights[i].wr * fabs((double)in[2 * k + 1]);
            CHECK(fabs((double)out[k]) <= bound + 1e-6);
        }
        CHECK(st.peak_out <= 1.0);
    }

    /* Distinguishability against a nominal run on the same buffer. */
    float nominal[] = {0.5f, 0.5f, 0.25f, -0.25f};
    float nomout[2];
    DownmixStats st;
    downmix_stats_reset(&st);
    int rc = downmix_stereo_to_mono(nominal, 2, 0.5f, 0.5f, nomout, &st);
    CHECK(rc == DOWNMIX_OK);
    CHECK(st.full_scale_frames == 0);
}

/* ------------------------------------------------------------------ */
static void test_peak_and_energy_bounds(void) {
    enum { N = 200000 };
    float* in = (float*)malloc(sizeof(float) * 2 * N);
    float* out = (float*)malloc(sizeof(float) * N);
    CHECK(in && out);
    if (!in || !out) { free(in); free(out); return; }

    for (int i = 0; i < 2 * N; ++i) in[i] = frand_unit() * 0.999f;

    for (size_t w = 0; w < NWEIGHTS; ++w) {
        double wl = kWeights[w].wl, wr = kWeights[w].wr;
        DownmixStats st;
        downmix_stats_reset(&st);
        int rc = downmix_stereo_to_mono(in, N, (float)wl, (float)wr, out, &st);
        CHECK_MSG(rc == DOWNMIX_OK, "weights %s rc=%d", kWeights[w].name, rc);
        CHECK(st.clipped_frames == 0);

        double raw_in_energy = 0.0, weighted_in_energy = 0.0, out_energy = 0.0;
        for (int i = 0; i < N; ++i) {
            double l = in[2 * i], r = in[2 * i + 1], o = out[i];
            raw_in_energy += l * l + r * r;
            weighted_in_energy += wl * l * l + wr * r * r;
            out_energy += o * o;
            CHECK_MSG(fabs(o) <= 1.0, "%s frame %d |out|=%.9f",
                      kWeights[w].name, i, fabs(o));
            /* The reference must be the same float expression the library
             * uses, so a compiler contraction choice cannot make the test
             * disagree with the code. */
            CHECK_MSG(out[i] == in[2 * i] * (float)wl + in[2 * i + 1] * (float)wr,
                      "%s frame %d is not the documented linear map",
                      kWeights[w].name, i);
            CHECK_MSG(fabs(o - (wl * l + wr * r)) <=
                      2e-7 * (1.0 + fabs(o)),
                      "%s frame %d differs from the exact map by %.3e",
                      kWeights[w].name, i, fabs(o - (wl * l + wr * r)));
            /* Triangle bound with the weights as L1 coefficients; a more
             * permissive max(wL,wR) bound would be wrong here. */
            CHECK_MSG(fabs(o) <= wl * fabs(l) + wr * fabs(r) + 1e-6,
                      "%s frame %d peak bound violated", kWeights[w].name, i);
            CHECK_MSG(fabs(o) <= fmax(fabs(l), fabs(r)) + 1e-6,
                      "%s frame %d exceeds input envelope", kWeights[w].name, i);
        }
        CHECK_MSG(out_energy <= weighted_in_energy * (1.0 + 1e-12),
                  "%s created energy: out=%.12f weighted_in=%.12f",
                  kWeights[w].name, out_energy, weighted_in_energy);
        CHECK_MSG(st.max_frame_energy_ratio <= 1.0 + 1e-6,
                  "%s per-frame energy ratio %.12f",
                  kWeights[w].name, st.max_frame_energy_ratio);
        CHECK(fabs(st.out_energy - out_energy) <= 1e-9 * (1.0 + out_energy));
        CHECK(fabs(st.in_energy - weighted_in_energy) <=
              1e-9 * (1.0 + weighted_in_energy));
        /* Exact ledger: nothing is destroyed, the part that leaves the
         * mono programme is the side energy wL*wR*(L-R)^2. The identity
         * is exact in real arithmetic; the output is stored as float, so
         * the measured residual is the energy of the output rounding
         * (~1e-7 relative) and the test allows for it. */
        CHECK_MSG(fabs(st.in_energy - st.out_energy - st.diff_energy) <=
                  1e-6 * (1.0 + st.in_energy),
                  "%s ledger unbalanced: in=%.12f out=%.12f diff=%.12f",
                  kWeights[w].name, st.in_energy, st.out_energy,
                  st.diff_energy);
        printf("  [bounds] %-15s raw-in=%.4f weighted-in=%.4f out=%.4f "
               "ratio=%.6f peak_out=%.6f\n",
               kWeights[w].name, 0.5 * raw_in_energy, weighted_in_energy,
               out_energy, out_energy / weighted_in_energy, st.peak_out);
    }
    free(in); free(out);
}

/* ------------------------------------------------------------------ */
static void test_coherent_energy_conservation(void) {
    enum { N = 50000 };
    float in[2 * N], out[N];
    for (int i = 0; i < N; ++i) {
        float l = 0.7f * sinf((float)i * 0.0013f) + 0.2f * sinf((float)i * 0.01f);
        in[2 * i] = l; in[2 * i + 1] = l;   /* perfectly coherent */
    }

    /* Only sets whose float sum is exactly 1 in double conserve exactly;
     * all legal sets must at least satisfy the bound. */
    for (size_t w = 0; w < NWEIGHTS; ++w) {
        double wl = kWeights[w].wl, wr = kWeights[w].wr;
        DownmixStats st;
        downmix_stats_reset(&st);
        int rc = downmix_stereo_to_mono(in, N, (float)wl, (float)wr, out, &st);
        CHECK(rc == DOWNMIX_OK);
        double raw = 0.0, oute = 0.0, worst = 0.0;
        for (int i = 0; i < N; ++i) {
            double l = in[2 * i], o = out[i];
            raw += l * l;
            oute += o * o;
            double d = fabs(o - l) / (fabs(l) > 1e-6 ? fabs(l) : 1.0);
            if (d > worst) worst = d;
        }
        double rel = fabs(oute - raw) / raw;
        double wl_ = kWeights[w].wl, wr_ = kWeights[w].wr;
        /* Conservation out == L for L == R is exact in real arithmetic
         * whenever wL + wR == 1. In float each product rounds, so it is
         * bit-exact only for dyadic weights (1/2, 1/4, 0, 1) where every
         * scaling and sum is exact; with 0.3f/0.7f the sum of the two
         * rounded products still differs from L by <= 1 ulp. We assert
         * exactness only where float can deliver it, and float precision
         * everywhere else. */
        int dyadic = (wl_ == 0.5 && wr_ == 0.5) ||
                     (wl_ == 0.25 && wr_ == 0.75) ||
                     (wl_ == 0.0 && wr_ == 1.0) ||
                     (wl_ == 1.0 && wr_ == 0.0);
        printf("  [coherent] %-15s energy ratio=%.12f worst rel mix err=%.3e "
               "diff=%.3e%s\n",
               kWeights[w].name, oute / raw, worst, st.diff_energy,
               dyadic ? "  (dyadic: bit-exact expected)" : "");
        CHECK_MSG(rel <= 1e-6, "%s coherent energy ratio off by %.3e",
                  kWeights[w].name, rel);
        CHECK_MSG(worst <= 1e-6, "%s coherent mix error %.3e",
                  kWeights[w].name, worst);
        if (dyadic) {
            CHECK_MSG(rel <= 1e-9,
                      "%s should conserve coherent energy exactly (rel=%.3e)",
                      kWeights[w].name, rel);
            CHECK_MSG(worst <= 1e-9,
                      "%s dyadic mix should be bit-exact (worst=%.3e)",
                      kWeights[w].name, worst);
            CHECK_MSG(st.diff_energy <= 1e-9 * (1.0 + st.in_energy),
                      "%s coherent side energy should be zero, got %.3e",
                      kWeights[w].name, st.diff_energy);
            CHECK_MSG(fabs(st.in_energy - st.out_energy) <=
                      1e-9 * (1.0 + st.in_energy),
                      "%s ledger: in=%.12f out=%.12f", kWeights[w].name,
                      st.in_energy, st.out_energy);
        }
        CHECK(fabs(st.in_energy - st.out_energy - st.diff_energy) <=
              1e-6 * (1.0 + st.in_energy));
    }
}

/* ------------------------------------------------------------------ */
static void test_step_never_amplified(void) {
    enum { N = 100000 };
    float in[2 * N], out[N];
    for (int i = 0; i < N; ++i) {
        float t = (float)i / 48000.0f;
        float l = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * t)
                + 0.15f * sinf(2.0f * 3.14159265f * 3000.0f * t + 0.7f);
        float r = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * t + 0.01f)
                - 0.15f * sinf(2.0f * 3.14159265f * 3000.0f * t + 2.1f);
        in[2 * i] = l; in[2 * i + 1] = r;
    }
    for (size_t w = 0; w < NWEIGHTS; ++w) {
        double wl = kWeights[w].wl, wr = kWeights[w].wr;
        downmix_stereo_to_mono(in, N, (float)wl, (float)wr, out, NULL);
        double max_d_out = 0.0, bound = 0.0;
        for (int i = 1; i < N; ++i) {
            double dl = fabs((double)in[2 * i] - in[2 * i - 2]);
            double dr = fabs((double)in[2 * i + 1] - in[2 * i - 1]);
            double dout = fabs((double)out[i] - out[i - 1]);
            double b = wl * dl + wr * dr;
            if (dout > max_d_out) max_d_out = dout;
            if (b > bound) bound = b;
            CHECK_MSG(dout <= b + 1e-6,
                      "%s step amplified at %d: %.9f > %.9f",
                      kWeights[w].name, i, dout, b);
        }
        printf("  [steps]    %-15s max|dout|=%.9f convex bound=%.9f\n",
               kWeights[w].name, max_d_out, bound);
    }

    /* Coherent input: output must be the input itself, so its step
     * profile is literally unchanged. */
    for (int i = 0; i < N; ++i) in[2 * i + 1] = in[2 * i];
    downmix_stereo_to_mono(in, N, 0.3f, 0.7f, out, NULL);
    for (int i = 0; i < N; ++i)
        CHECK_MSG(fabs((double)out[i] - in[2 * i]) <= 1e-6,
                  "coherent frame %d changed: %.9f vs %.9f",
                  i, out[i], in[2 * i]);
}

/* ------------------------------------------------------------------ */
static void test_block_partition_invariance(void) {
    enum { N = 40001 };
    static float in[2 * N], whole[N], split[N];
    for (int i = 0; i < 2 * N; ++i) in[i] = frand_unit() * 0.95f;

    downmix_stereo_to_mono(in, N, 0.3f, 0.7f, whole, NULL);

    /* A memoryless fixed map has no block state; arbitrary chunking must
     * be bit-identical, so the mixer adds no block-boundary energy jump. */
    size_t pos = 0;
    int chunk = 1;
    while (pos < N) {
        size_t n = (size_t)(1 + (chunk * 37) % 977);
        if (n > N - pos) n = N - pos;
        int rc = downmix_stereo_to_mono(in + 2 * pos, n, 0.3f, 0.7f,
                                        split + pos, NULL);
        CHECK(rc == DOWNMIX_OK);
        pos += n; ++chunk;
    }
    CHECK(memcmp(whole, split, sizeof whole) == 0);
}

/* ------------------------------------------------------------------ */
static void test_helper_functions(void) {
    CHECK(downmix_out_frames(1234) == 1234);
    CHECK(downmix_out_frames(0) == 0);

    float x[] = {0.0f, 0.25f, 0.5f, 0.5f, 0.25f, 0.0f};
    long at = 0;
    float r = downmix_max_step_ratio(x, 6, 1e-6, &at);
    CHECK(fabsf(r - 2.0f) < 1e-6f);
    CHECK(at == 2 || at == 4);
    CHECK(downmix_max_step_ratio(x, 0, 1e-6, &at) == 1.0f && at == -1);
    CHECK(downmix_max_step_ratio(NULL, 6, 1e-6, &at) == 1.0f);
}

/* ------------------------------------------------------------------ */
int main(void) {
    printf("mini-audio-downmix unit tests\n");
    test_status_codes_distinct();
    test_weight_validation();
    test_mono_passthrough();
    test_channel_rejection();
    test_weight_rejection_untouched();
    test_full_scale();
    test_peak_and_energy_bounds();
    test_coherent_energy_conservation();
    test_step_never_amplified();
    test_block_partition_invariance();
    test_helper_functions();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
