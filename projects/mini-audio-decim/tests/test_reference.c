/* Cross-check decim_run against a direct convolution with the composite
 * kernel, for random plans and lengths.
 *
 * The cascade is linear and, for an input shift of D samples, the
 * output shifts by one: it is representable as a single D-phase
 * filter,
 *
 *     y[j] = sum_p x[j*D - p] * G[p]
 *
 * where G is built stage by stage.  Starting with G = h1, adding a
 * stage with factor m and taps h gives
 *
 *     G_new[D_old*r + p] += G[p] * h[r]        D_old = product so far
 *
 * i.e. upsample G by D_old and convolve with h.  Note an impulse at
 * input 0 only reveals G at multiples of D; the other phases need
 * impulses at other offsets, which is why a naive impulse-response test
 * alone would miss them.
 */
#include "../src/decim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; \
    printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned rng = 12345u;
static unsigned rnd(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}

static double *build_kernel(const Decim *d, size_t *len_out) {
    /* Expand the cascade algebraically.  With P_1 = 1, P_k =
     * m_1...m_(k-1), the net is
     *
     *     y[j] = sum_t x[j*D - t] * G[t],
     *     G[t] = sum over (r_1..r_K) with t = sum_k r_k*P_k
     *            of prod_k h_k[r_k].
     *
     * Build G directly by iterating stages and, for each existing tap,
     * placing h_k[r_k] at t + r_k*P_k. */
    int P = 1;
    size_t cap = (size_t)d->span_taps + 8;
    double *g = (double *)calloc(cap, sizeof(double));
    g[0] = 1.0;
    size_t glen = 1;

    for (int k = 0; k < d->n_stages; ++k) {
        const DecimStage *s = &d->stages[k];
        size_t nlen = glen + (size_t)(s->taps - 1) * (size_t)P;
        double *ng = (double *)calloc(nlen, sizeof(double));
        for (size_t p = 0; p < glen; ++p) {
            if (g[p] == 0.0) continue;
            for (int r = 0; r < s->taps; ++r)
                ng[p + (size_t)r * (size_t)P] += g[p] * s->h[r];
        }
        free(g);
        g = ng;
        glen = nlen;
        P *= s->m;
    }
    *len_out = glen;
    return g;
}

int main(void) {
    int plans[][DECIM_MAX_STAGES] = {
        {2}, {3}, {4}, {5}, {6}, {3, 2}, {2, 2}, {4, 3}, {5, 2},
        {3, 2, 2}, {2, 2, 2}, {4, 3, 2}, {3, 3}, {6, 5}, {2, 3, 2},
        {2, 2, 2, 2},
    };
    int ns[] = {1,1,1,1,1,2,2,2,2,3,3,3,2,2,3,4};

    for (int p = 0; p < (int)(sizeof(ns) / sizeof(ns[0])); ++p) {
        Decim d;
        int rc = decim_init(&d, plans[p], ns[p], NULL);
        if (rc != DECIM_OK) { printf("skip plan %d rc=%d\n", p, rc); continue; }

        size_t glen = 0;
        double *g = build_kernel(&d, &glen);
        int D = d.total;
        CHECK(glen <= (size_t)d.span_taps);

        double worst_all = 0.0;
        for (int trial = 0; trial < 8; ++trial) {
            size_t n = 50 + (rnd() % 3000);
            float *in = (float *)malloc(sizeof(float) * n);
            for (size_t i = 0; i < n; ++i) {
                int v = (int)(rnd() % 2001u) - 1000;   /* signed! */
                in[i] = (float)v / 1000.0f;
            }
            size_t cap = decim_out_frames(&d, n);
            float *out = (float *)malloc(sizeof(float) * (cap ? cap : 1));
            int r = decim_run(&d, in, n, out, cap, NULL);
            CHECK(r == DECIM_OK || r == DECIM_OK_TAIL ||
                  r == DECIM_OK_SHORT);
            CHECK(cap == n / (size_t)D);

            double worst = 0.0;
            for (size_t j = 0; j < cap; ++j) {
                double acc = 0.0;
                for (size_t t = 0; t < glen; ++t) {
                    long idx = (long)(j * (size_t)D) - (long)t;
                    if (idx >= 0 && (size_t)idx < n)
                        acc += g[t] * (double)in[idx];
                }
                double e = fabs(acc - (double)out[j]);
                if (e > worst) worst = e;
            }
            if (worst > worst_all) worst_all = worst;
            free(in); free(out);
        }
        /* double accumulation vs double accumulation: agreement should
         * be at float-storage level, ~1e-7 for unit-scale signals. */
        CHECK(worst_all <= 1e-5);
        if (worst_all > 1e-5)
            printf("  plan %d D=%d worst=%.3e kernels=%zu\n", p, D,
                   worst_all, glen);
        free(g);
        decim_free(&d);
    }
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
