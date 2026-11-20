/* Unit tests for mini-audio-decim.
 *
 * The tests cover four things the requirement calls out explicitly:
 *   1. the designed cascade meets the advertised alias / ripple bounds,
 *      measured independently of the library's own acceptance sweep;
 *   2. the three edge inputs (factor 1, non-divisible length, input
 *      shorter than the filter) have the documented result;
 *   3. the bypass path is bit-identical sample for sample, including
 *      NaN payloads and signed zero;
 *   4. the decimator is causal: output frames do not depend on input
 *      beyond their own history, so appending a tail changes nothing.
 */
#include "../src/decim.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; \
    printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const double PI = 3.14159265358979323846;

/* Deterministic xorshift32 noise in [-1, 1). */
static uint32_t rng_state = 0x9E3779B9u;
static uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x;
    return x;
}
static float rng_float(void) {
    return (float)((double)rng_next() / 2147483648.0 - 1.0);
}

static void run_double(const Decim *d, const double *x, size_t n,
                       double *y, size_t cap, int *rc) {
    float *fin = (float *)malloc(sizeof(float) * (n ? n : 1));
    float *fout = (float *)malloc(sizeof(float) * (cap ? cap : 1));
    for (size_t i = 0; i < n; ++i) fin[i] = (float)x[i];
    *rc = decim_run(d, fin, n, fout, cap, NULL);
    for (size_t i = 0; i < cap; ++i) y[i] = (double)fout[i];
    free(fin);
    free(fout);
}

/* ------------------------------------------------------------------ */
/* 1. Design acceptance                                                 */
/* ------------------------------------------------------------------ */

static void test_design_bounds(void) {
    struct { const char *name; int f[4]; int n; int want_better_than; } cases[] = {
        { "D2  [2]",     {2, 0, 0, 0}, 1, 1 },
        { "D3  [3]",     {3, 0, 0, 0}, 1, 1 },
        { "D6  [3,2]",   {3, 2, 0, 0}, 2, 1 },
        { "D12 [3,2,2]", {3, 2, 2, 0}, 3, 1 },
        { "D24 [3,2,2,2]", {3, 2, 2, 2}, 4, 1 },
        { "D12 [4,3]",   {4, 3, 0, 0}, 2, 1 },
        { "D8  [2,2,2]", {2, 2, 2, 0}, 3, 1 },
        /* Arbitrary factors, including primes and a repeated factor:
         * this is the "configurable" claim, not a fixed plan table. */
        { "D7  [7]",     {7, 0, 0, 0}, 1, 1 },
        { "D13 [13]",    {13, 0, 0, 0}, 1, 1 },
        { "D25 [5,5]",   {5, 5, 0, 0}, 2, 1 },
        { "D21 [3,7]",   {3, 7, 0, 0}, 2, 1 },
        { "D24 [2,2,2,3]", {2, 2, 2, 3}, 4, 1 },
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        Decim d;
        int rc = decim_init(&d, cases[c].f, cases[c].n, NULL);
        if (rc != DECIM_OK) {
            printf("FAIL %s init rc=%d %s\n", cases[c].name, rc,
                   decim_status_name(rc));
            ++fails;
            continue;
        }
        CHECK(d.total > 1);
        CHECK(d.n_stages == cases[c].n);
        int prod = 1;
        for (int k = 0; k < d.n_stages; ++k) {
            CHECK(d.stages[k].taps % 2 == 1);
            CHECK(d.stages[k].taps <= 8191);
            CHECK(d.stages[k].fs > d.stages[k].fp);
            prod *= d.stages[k].m;
        }
        CHECK(prod == d.total);

        /* Recomputed independently from the public response API, on a
         * grid that does not coincide with the library's acceptance
         * grid. */
        double fp = 0.4 / (double)d.total;   /* passband 0.8 * 0.5/D */
        int G = 12001;
        double pmax = 0.0, pmin = 1e300, smax = 0.0;
        for (int i = 0; i < G; ++i) {
            double f = fp * (double)i / (double)(G - 1);
            double g = decim_response(&d, f);
            if (g > pmax) pmax = g;
            if (g < pmin) pmin = g;
        }
        for (int i = 0; i < G; ++i) {
            double f = 0.5 / (double)d.total +
                       0.5 * (double)i / (double)(G - 1);
            double g = decim_response(&d, f);
            if (g > smax) smax = g;
        }
        double ripple = 20.0 * log10(pmax / pmin);
        double alias = (smax > 0.0) ? -20.0 * log10(smax) : 1e9;
        printf("  %-12s D=%2d stages=%d taps=", cases[c].name, d.total,
               d.n_stages);
        for (int k = 0; k < d.n_stages; ++k)
            printf("%d%s", d.stages[k].taps,
                   k + 1 < d.n_stages ? "+" : "");
        printf("  ripple=%.4f dB  alias=%.1f dB\n", ripple, alias);
        CHECK(ripple <= d.cfg.ripple_db);
        CHECK(alias >= d.cfg.stop_db);
        /* The requirement's floor, not just the configured target. */
        CHECK(alias >= 40.0);
        decim_free(&d);
    }
}

static void test_design_errors(void) {
    Decim d;
    int bad_factor[] = {3, 0};
    int bad_hi[] = {3, -1};
    DecimConfig cfg;
    int f2[] = {2};
    int f6[] = {3, 2};

    CHECK(decim_init(NULL, f2, 1, NULL) == DECIM_E_ARG);
    CHECK(decim_init(&d, f2, 1, NULL) == DECIM_OK);
    decim_free(&d);

    CHECK(decim_init(&d, bad_factor, 2, NULL) == DECIM_E_ARG);
    CHECK(d.n_stages == 0 && d.total == 0);
    CHECK(decim_init(&d, bad_hi, 2, NULL) == DECIM_E_ARG);

    decim_config_default(&cfg);
    cfg.passband = 0.0;
    CHECK(decim_init(&d, f2, 1, &cfg) == DECIM_E_ARG);
    decim_config_default(&cfg);
    cfg.passband = 1.0;
    CHECK(decim_init(&d, f2, 1, &cfg) == DECIM_E_ARG);
    decim_config_default(&cfg);
    cfg.stop_db = 39.0;
    CHECK(decim_init(&d, f2, 1, &cfg) == DECIM_E_ARG);
    decim_config_default(&cfg);
    cfg.ripple_db = 0.0;
    CHECK(decim_init(&d, f2, 1, &cfg) == DECIM_E_ARG);
    decim_config_default(&cfg);
    cfg.ripple_db = 7.0;
    CHECK(decim_init(&d, f2, 1, &cfg) == DECIM_E_ARG);

    /* A tap budget too small for the request must fail *loudly*, not
     * return an under-designed filter. */
    decim_config_default(&cfg);
    cfg.max_taps = 9;
    CHECK(decim_init(&d, f2, 1, &cfg) == DECIM_E_DESIGN);
    CHECK(d.n_stages == 0 && d.stages[0].h == NULL);
    cfg.max_taps = 15;
    CHECK(decim_init(&d, f6, 2, &cfg) == DECIM_E_DESIGN);
    decim_free(&d);
}

/* The user-facing requirement is 40 dB; a 40 dB request must be met by
 * the measured cascade too, and the design should not silently need a
 * huge filter for it. */
static void test_design_40db(void) {
    DecimConfig cfg;
    decim_config_default(&cfg);
    cfg.stop_db = 40.0;
    cfg.ripple_db = 0.1;
    int f[] = {15, 4};
    Decim d;
    CHECK(decim_init(&d, f, 2, &cfg) == DECIM_OK);
    CHECK(d.total == 60);
    CHECK(d.measured_alias_db >= 40.0);
    CHECK(d.measured_ripple_db <= 0.1);
    printf("  D60 [15,4] @40dB: taps=");
    for (int k = 0; k < d.n_stages; ++k)
        printf("%d%s", d.stages[k].taps, k + 1 < d.n_stages ? "+" : "");
    printf(" alias=%.1f dB ripple=%.4f dB\n", d.measured_alias_db,
           d.measured_ripple_db);
    decim_free(&d);
}

/* ------------------------------------------------------------------ */
/* 2. The three edge inputs                                             */
/* ------------------------------------------------------------------ */

static void test_factor_one(void) {
    int one[] = {1};
    Decim d;
    CHECK(decim_init(&d, NULL, 0, NULL) == DECIM_OK);
    CHECK(decim_total_factor(&d) == 1);
    CHECK(d.n_stages == 0);
    decim_free(&d);
    CHECK(decim_init(&d, one, 1, NULL) == DECIM_OK);
    CHECK(decim_total_factor(&d) == 1);
    CHECK(d.n_stages == 0);
    decim_free(&d);

    int all_one[] = {1, 1, 1};
    CHECK(decim_init(&d, all_one, 3, NULL) == DECIM_OK);
    CHECK(decim_total_factor(&d) == 1);
    CHECK(decim_span_input(&d) == 0);

    /* Bit-identical, including NaN payloads, infinities and -0.0. */
    union { uint32_t u; float f; } vals[8];
    vals[0].u = 0x7FC12345u;          /* quiet NaN, payload preserved */
    vals[1].u = 0xFFC00001u;          /* negative NaN                  */
    vals[2].u = 0x7F800000u;          /* +inf                          */
    vals[3].u = 0xFF800000u;          /* -inf                          */
    vals[4].u = 0x80000000u;          /* -0.0                          */
    vals[5].u = 0x00000001u;          /* smallest subnormal            */
    vals[6].u = 0x3F800000u;          /* 1.0                           */
    vals[7].u = 0xBF000000u;          /* -0.5                          */
    float in[8], out[8];
    for (int i = 0; i < 8; ++i) { in[i] = vals[i].f; out[i] = 123.0f; }
    DecimStats st;
    CHECK(decim_run(&d, in, 8, out, 8, &st) == DECIM_OK_BYPASS);
    CHECK(st.bypass == 1);
    CHECK(st.out_frames == 8);
    CHECK(st.tail_frames == 0);
    CHECK(memcmp(in, out, sizeof(in)) == 0);
    for (int i = 0; i < 8; ++i) {
        union { uint32_t u; float f; } u; u.f = out[i];
        CHECK(u.u == vals[i].u);
    }
    /* In-place is allowed on the bypass path. */
    double *dbl = NULL; (void)dbl;
    CHECK(decim_run(&d, in, 8, in, 8, &st) == DECIM_OK_BYPASS);
    for (int i = 0; i < 8; ++i) {
        union { uint32_t u; float f; } u; u.f = in[i];
        CHECK(u.u == vals[i].u);
    }
    /* Capacity is checked before any write. */
    float tiny[4] = {1, 2, 3, 4};
    CHECK(decim_run(&d, in, 8, tiny, 4, &st) == DECIM_E_SPACE);
    CHECK(tiny[0] == 1 && tiny[3] == 4);
    decim_free(&d);
}

static void test_nondivisible(void) {
    int f[] = {3, 2};
    Decim d;
    CHECK(decim_init(&d, f, 2, NULL) == DECIM_OK);
    CHECK(d.total == 6);
    for (size_t n = 1; n < 400; n += 7) {
        CHECK(decim_out_frames(&d, n) == n / 6);
    }

    size_t n = 2400 + 3;
    size_t big_n = 2408;             /* every residue class up to n+5 */
    float *big = (float *)malloc(sizeof(float) * big_n);
    float *out = (float *)malloc(sizeof(float) * big_n);
    for (size_t i = 0; i < big_n; ++i) big[i] = (float)sin(0.01 * (double)i);
    DecimStats st;

    /* Every residue class, at a length beyond the filter span. */
    for (size_t r = 0; r < 6; ++r) {
        size_t nn = 2400 + r;
        size_t want = nn / 6;
        int rc = decim_run(&d, big, nn, out, want, &st);
        CHECK(rc == (r ? DECIM_OK_TAIL : DECIM_OK));
        CHECK(st.out_frames == want);
        CHECK(st.tail_frames == r);
        CHECK(st.partial == (int)(r != 0));
        CHECK(st.short_input == 0);
    }

    int rc = decim_run(&d, big, n, out, n / 6, &st);
    CHECK(rc == DECIM_OK_TAIL);
    CHECK(st.partial == 1);
    CHECK(st.tail_frames == 3);
    CHECK(st.in_frames == n);
    CHECK(st.out_frames == n / 6);

    /* Same prefix with and without the dropped tail must give the same
     * emitted frames: the tail cannot influence them (causality). */
    size_t m = n - 3;
    float *out2 = (float *)malloc(sizeof(float) * (m / 6 + 1));
    CHECK(decim_run(&d, big, m, out2, m / 6, &st) == DECIM_OK);
    CHECK(st.partial == 0);
    CHECK(st.tail_frames == 0);
    CHECK(memcmp(out, out2, sizeof(float) * (m / 6)) == 0);

    /* One sample short of a frame: nothing is emitted.  The input is
     * also shorter than the filter span, and the documented precedence
     * makes SHORT win over TAIL; both facts are reported. */
    DecimStats s0;
    CHECK(decim_run(&d, big, 5, out, 0, &s0) == DECIM_OK_SHORT);
    CHECK(s0.out_frames == 0);
    CHECK(s0.tail_frames == 5);
    CHECK(s0.partial == 1);
    CHECK(s0.short_input == 1);

    /* n == 0: no reads, no writes, capacity 0 accepted. */
    CHECK(decim_run(&d, NULL, 0, NULL, 0, &s0) == DECIM_OK);
    CHECK(s0.out_frames == 0);
    CHECK(s0.partial == 0);
    CHECK(s0.bypass == 0);

    /* out_cap too small: E_SPACE and no writes. */
    for (size_t i = 0; i < n / 6; ++i) out[i] = 77.0f;
    CHECK(decim_run(&d, big, n, out, n / 6 - 1, &st) == DECIM_E_SPACE);
    for (size_t i = 0; i < n / 6; ++i) CHECK(out[i] == 77.0f);
    CHECK(decim_run(&d, big, n, NULL, n / 6, &st) == DECIM_E_ARG);
    CHECK(decim_run(&d, NULL, 10, out, n / 6, &st) == DECIM_E_ARG);

    free(out); free(out2); free(big);
    decim_free(&d);
}

static void test_short_input(void) {
    int f[] = {2, 2};
    Decim d;
    CHECK(decim_init(&d, f, 2, NULL) == DECIM_OK);
    size_t span = decim_span_input(&d);
    CHECK(span > 1);

    DecimStats st;
    size_t n = 32;
    CHECK(n < span);
    float *in = (float *)malloc(sizeof(float) * n);
    for (size_t i = 0; i < n; ++i) in[i] = (float)(i + 1);
    size_t want = decim_out_frames(&d, n);   /* 8 frames */

    float *out = (float *)malloc(sizeof(float) * (want ? want : 1));
    int rc = decim_run(&d, in, n, out, want, &st);
    CHECK(rc == DECIM_OK_SHORT);
    CHECK(st.short_input == 1);
    CHECK(st.in_frames == n);
    CHECK(st.out_frames == want);
    CHECK(st.tail_frames == n % 4);

    /* "Zero extension" is exactly what the implementation does, so the
     * output must equal the causal convolution of the padded input.
     * Check that against an independent direct evaluation of the
     * cascade using the public per-stage taps, in float storage like
     * the library. */
    size_t cap = n + 8;              /* must hold the input itself */
    double *a = (double *)calloc(cap, sizeof(double));
    double *b = (double *)calloc(cap, sizeof(double));
    for (size_t i = 0; i < n; ++i) a[i] = (double)in[i];
    size_t m = n;
    int cur = 0;
    for (int k = 0; k < d.n_stages; ++k) {
        const DecimStage *s = &d.stages[k];
        size_t outn = m / (size_t)s->m;
        double *src = cur ? b : a;
        double *dst = cur ? a : b;
        for (size_t j = 0; j < outn; ++j) {
            size_t c = j * (size_t)s->m;
            double acc = 0.0;
            for (int t = 0; t < s->taps; ++t) {
                long idx = (long)c - t;
                if (idx >= 0 && (size_t)idx < m) acc += s->h[t] * src[idx];
            }
            dst[j] = acc;
        }
        cur = !cur;
        m = outn;
    }
    const double *ref = cur ? b : a;
    for (size_t i = 0; i < m; ++i)
        CHECK(fabs((double)out[i] - ref[i]) <= 1e-6);

    /* Exactly at the span threshold the status is not SHORT. */
    {
        size_t ns = span;
        float *ib = (float *)malloc(sizeof(float) * ns);
        for (size_t i = 0; i < ns; ++i) ib[i] = (float)(i + 1);
        size_t w = decim_out_frames(&d, ns);
        float *o2 = (float *)malloc(sizeof(float) * (w ? w : 1));
        int rc2 = decim_run(&d, ib, ns, o2, w, &st);
        CHECK(rc2 == DECIM_OK || rc2 == DECIM_OK_TAIL);
        CHECK(st.short_input == 0);
        free(ib); free(o2);
    }

    free(in); free(out); free(a); free(b);
    decim_free(&d);
}

/* ------------------------------------------------------------------ */
/* 3. Signal behaviour: passband, aliasing, causality, gain            */
/* ------------------------------------------------------------------ */

/* Measure the gain the decimator applies to a tone at normalized input
 * frequency f.  Uses a long enough signal that the filter transient is
 * negligible, a Hann taper on the measured window, and a least-squares
 * projection of the output onto sin/cos at the folded frequency.
 * Returns NAN when f lands on the output DC or Nyquist bin, where a
 * sine projects ambiguously and the quadrature is singular. */
static double tone_gain_measured(const Decim *d, double f) {
    size_t n = 120000;
    double *x = (double *)malloc(sizeof(double) * n);
    for (size_t i = 0; i < n; ++i) x[i] = 0.5 * sin(2.0 * PI * f * (double)i);

    size_t cap = decim_out_frames(d, n);
    double *y = (double *)malloc(sizeof(double) * (cap ? cap : 1));
    int rc;
    run_double(d, x, n, y, cap, &rc);

    /* Steady-state region: skip the first few output samples (transient
     * from zero initial state) and stop well before the end. */
    size_t lo = 64, hi = cap;
    if (hi <= lo + 64) { free(x); free(y); return NAN; }

    /* Output frequency of the surviving image. */
    double fo = f * (double)d->total;
    fo = fo - floor(fo);
    if (fo >= 0.5) fo -= 1.0;
    if (fabs(fo) < 0.01 || fabs(fabs(fo) - 0.5) < 0.01) {
        free(x); free(y);
        return NAN;
    }

    double ss = 0, cc = 0, sc = 0, sy = 0, cy = 0;
    for (size_t i = lo; i < hi; ++i) {
        double w = 0.5 - 0.5 * cos(2.0 * PI * (double)(i - lo) /
                                   (double)(hi - lo - 1));
        double s = sin(2.0 * PI * fo * (double)i);
        double c = cos(2.0 * PI * fo * (double)i);
        ss += w * s * s; cc += w * c * c; sc += w * s * c;
        sy += w * s * y[i]; cy += w * c * y[i];
    }
    double det = ss * cc - sc * sc;
    if (fabs(det) < 1e-12) { free(x); free(y); return NAN; }
    double A = (sy * cc - cy * sc) / det;
    double B = (cy * ss - sy * sc) / det;
    double amp = sqrt(A * A + B * B);
    free(x); free(y);
    /* Input amplitude is 0.5, so the linear gain is amp / 0.5. */
    return amp / 0.5;
}

static void test_passband_and_alias(void) {
    struct { int f[3]; int n; } plan[] = {
        { {3, 2, 0}, 2 },
        { {3, 2, 2}, 3 },
    };
    for (size_t c = 0; c < 2; ++c) {
        Decim d;
        CHECK(decim_init(&d, plan[c].f, plan[c].n, NULL) == DECIM_OK);
        int D = d.total;
        double fp = 0.4 / (double)D;

        /* Passband: gain flat within the advertised ripple, and the
         * passband edge lands at least 0.99. */
        double gmin = 1e9, gmax = 0;
        for (int i = 0; i <= 7; ++i) {
            double f = fp * (double)i / 7.0;
            double g = tone_gain_measured(&d, f);
            if (!isnan(g)) {
                if (g < gmin) gmin = g;
                if (g > gmax) gmax = g;
            }
        }
        double rip_db = 20.0 * log10(gmax / gmin);
        printf("  D%-2d passband measured gain %.5f..%.5f (%.4f dB)\n",
               D, gmin, gmax, rip_db);
        CHECK(gmin > 0.99);
        CHECK(rip_db <= 0.05);

        /* Alias: tones that must fold must be attenuated by >= 40 dB,
         * measured through the actual sample path, not the response
         * formula.  Sample the alias band densely enough to catch the
         * worst point without taking forever. */
        double worst = 0.0;
        int ng = 41;
        for (int i = 0; i < ng; ++i) {
            double f = 0.5 / (double)D +
                       0.5 * (double)i / (double)(ng - 1);
            if (f >= 0.5) f = 0.4999;
            double g = tone_gain_measured(&d, f);
            if (!isnan(g) && g > worst) worst = g;
        }
        double alias_db = 20.0 * log10(worst);
        printf("  D%-2d worst measured alias gain %.6f (%.1f dB)\n",
               D, worst, alias_db);
        CHECK(alias_db <= -40.0);
        decim_free(&d);
    }
}

static void test_passthrough_energy(void) {
    /* A constant is in the passband, so once the zero-state transient
     * (the filter delay, at most span-1 input samples) has passed, the
     * output must be the same constant: the filter's DC gain is
     * exactly 1. */
    int f[] = {4, 3};
    Decim d;
    CHECK(decim_init(&d, f, 2, NULL) == DECIM_OK);
    size_t n = 8000;
    float *in = (float *)malloc(sizeof(float) * n);
    float *out = (float *)malloc(sizeof(float) * n);
    for (size_t i = 0; i < n; ++i) in[i] = 0.25f;
    CHECK(decim_run(&d, in, n, out, n / 12, NULL) == DECIM_OK_TAIL);
    size_t skip = decim_span_input(&d) / 12 + 8;
    double worst = 0.0;
    for (size_t i = skip; i < n / 12; ++i) {
        double e = fabs((double)out[i] - 0.25);
        if (e > worst) worst = e;
    }
    printf("  D12 constant: max |out-0.25| after transient = %.2e\n", worst);
    CHECK(worst <= 1e-6);
    decim_free(&d);
    free(in); free(out);
}

/* A frame emitted at index j must not depend on input at or beyond
 * (j+1)*D: that is what lets the decimator drop a non-divisible tail. */
static void test_causality(void) {
    int f[] = {3, 2, 2};
    Decim d;
    CHECK(decim_init(&d, f, 3, NULL) == DECIM_OK);
    size_t n = 1200;                 /* divisible by D, full-length output */
    float *a = (float *)malloc(sizeof(float) * n);
    float *b = (float *)malloc(sizeof(float) * n);
    for (size_t i = 0; i < n; ++i) { a[i] = rng_float(); b[i] = a[i]; }
    /* Change one sample beyond the filter's reach for frame j, but
     * before the filter's reach for later frames. */
    size_t j = 50, cut = (j + 1) * (size_t)d.total;
    b[cut] += 10.0f;

    float *oa = (float *)malloc(sizeof(float) * n);
    float *ob = (float *)malloc(sizeof(float) * n);
    size_t nout = decim_out_frames(&d, n);
    CHECK(decim_run(&d, a, n, oa, nout, NULL) == DECIM_OK);
    CHECK(decim_run(&d, b, n, ob, nout, NULL) == DECIM_OK);
    for (size_t i = 0; i <= j; ++i) CHECK(oa[i] == ob[i]);
    /* The cut is observable once the filter reaches it, i.e. at
     * output indices ceil((cut - reach_j)/D); check the last frame. */
    CHECK(oa[nout - 1] != ob[nout - 1]);
    free(a); free(b); free(oa); free(ob);
    decim_free(&d);
}

int main(void) {
    printf("design bounds\n");
    test_design_bounds();
    printf("design errors\n");
    test_design_errors();
    printf("40 dB design\n");
    test_design_40db();
    printf("factor one\n");
    test_factor_one();
    printf("non-divisible input\n");
    test_nondivisible();
    printf("short input\n");
    test_short_input();
    printf("passband and alias, measured through the sample path\n");
    test_passband_and_alias();
    printf("constant passthrough\n");
    test_passthrough_energy();
    printf("causality\n");
    test_causality();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
