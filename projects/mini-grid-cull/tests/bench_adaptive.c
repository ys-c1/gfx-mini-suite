#define _POSIX_C_SOURCE 199309L
#include "../src/grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

/* Three sprite densities in the same 1000x1000 world, 10x apart.
 * Compares one density-adaptive grid against two fixed-cell grids:
 *   coarse (cell 20) and fine (cell 4), each of which is a good choice for
 *   exactly one of the scenes and a poor one for the others.
 * Reports rebuild counts, rebuild work, and per-query cost.
 *
 * Sprite radii shrink with density (world is the same, sprites get smaller),
 * and the query radius shrinks with them, so every scene answers the same
 * number of hits (~0.7-1.0% of items) and the timings are comparable. */

#define WORLD 1000.0f
#define NQ 2000
#define QCAP 4096

static unsigned long long s = 0x9E3779B97F4A7C15ull;
static float frand(void) {
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return (float)(s & 0xFFFFFF) / 16777215.0f;
}

typedef struct { float x, y, r; int id; } Rec;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static int brute(const Rec* a, int n, float cx, float cy, float rad) {
    int c = 0;
    for (int i = 0; i < n; ++i) {
        float dx = a[i].x - cx, dy = a[i].y - cy, rr = rad + a[i].r;
        if (dx * dx + dy * dy <= rr * rr) ++c;
    }
    return c;
}

static void gen(Rec* a, int n, float r0, float r1) {
    for (int i = 0; i < n; ++i) {
        a[i].x = frand() * WORLD;
        a[i].y = frand() * WORLD;
        a[i].r = r0 + frand() * (r1 - r0);
        a[i].id = i;
    }
}

typedef struct {
    int cells;
    int rebuilds;
    int relinked;
    int max_rebuild_relinked;
    double insert_us;
    double query_us;
    int matches;
    int sample_mismatch;
} Stats;

static Stats measure(Grid* g, const Rec* a, int n,
                     const float* qx, const float* qy, float qrad) {
    Stats st;
    st.cells = grid_cells(g);
    st.rebuilds = 0;
    st.relinked = 0;
    st.max_rebuild_relinked = 0;
    st.sample_mismatch = 0;

    double t0 = now_ms();
    int last_rebuilds = 0, last_relinked = 0;
    for (int i = 0; i < n; ++i) {
        grid_insert(g, a[i].id, a[i].x, a[i].y, a[i].r);
        if (grid_rebuilds(g) != last_rebuilds) {
            int d = grid_relinked(g) - last_relinked;
            if (d > st.max_rebuild_relinked) st.max_rebuild_relinked = d;
            last_rebuilds = grid_rebuilds(g);
            last_relinked = grid_relinked(g);
        }
    }
    st.insert_us = (now_ms() - t0) / (double)n * 1000.0;
    st.cells = grid_cells(g);
    st.rebuilds = grid_rebuilds(g);
    st.relinked = grid_relinked(g);

    int* out = (int*)malloc(sizeof(int) * QCAP);
    int total = 0;
    volatile int sink = 0;
    t0 = now_ms();
    for (int k = 0; k < NQ; ++k) {
        int got = grid_query_circle(g, qx[k], qy[k], qrad, out, QCAP);
        total += got;
        if (got) sink ^= out[0];
    }
    st.query_us = (now_ms() - t0) / (double)NQ * 1000.0;
    st.matches = total;
    (void)sink;

    /* exactness sample vs brute force */
    for (int k = 0; k < 200; ++k) {
        if (grid_query_circle(g, qx[k], qy[k], qrad, out, QCAP) !=
            brute(a, n, qx[k], qy[k], qrad)) {
            ++st.sample_mismatch;
        }
    }
    free(out);
    return st;
}

static void scene(int n, float r0, float r1, float qrad) {
    Rec* a = (Rec*)malloc(sizeof(Rec) * (size_t)n);
    gen(a, n, r0, r1);
    float* qx = (float*)malloc(sizeof(float) * NQ);
    float* qy = (float*)malloc(sizeof(float) * NQ);
    for (int k = 0; k < NQ; ++k) { qx[k] = frand() * WORLD; qy[k] = frand() * WORLD; }

    double density = (double)n / (WORLD * WORLD);

    Grid ad, coarse, fine;
    grid_init_adaptive(&ad, 0, 0, WORLD, WORLD, 0);
    grid_init(&coarse, (int)(WORLD / 20.0f), (int)(WORLD / 20.0f), 20.0f, 0, 0);
    grid_init(&fine, (int)(WORLD / 4.0f), (int)(WORLD / 4.0f), 4.0f, 0, 0);

    Stats sa = measure(&ad, a, n, qx, qy, qrad);
    Stats sc = measure(&coarse, a, n, qx, qy, qrad);
    Stats sf = measure(&fine, a, n, qx, qy, qrad);

    printf("### %d sprites (%.3f per unit^2, 10^%.0f of the sparse scene)\n\n", n,
           density, log10(density / 0.01));
    printf("| grid | cells | rebuilds | relinked | max rebuild | insert us | query us | hits | exact |\n");
    printf("|------|-------|----------|----------|-------------|-----------|----------|------|-------|\n");
    printf("| adaptive | %d | **%d** | %d | %d items | %.4f | **%.3f** | %d | %s |\n",
           sa.cells, sa.rebuilds, sa.relinked, sa.max_rebuild_relinked,
           sa.insert_us, sa.query_us, sa.matches,
           sa.sample_mismatch ? "NO" : "yes");
    printf("| fixed cell 20 | %d | 0 | 0 | - | %.4f | %.3f | %d | %s |\n",
           sc.cells, sc.insert_us, sc.query_us, sc.matches,
           sc.sample_mismatch ? "NO" : "yes");
    printf("| fixed cell 4 | %d | 0 | 0 | - | %.4f | %.3f | %d | %s |\n\n",
           sf.cells, sf.insert_us, sf.query_us, sf.matches,
           sf.sample_mismatch ? "NO" : "yes");

    printf("RESULT n=%d density=%.3f rebuilds=%d relinked=%d max_rebuild=%d cells=%d "
           "cell=%.3f query_us=%.4f hits=%d mismatch=%d "
           "coarse_us=%.4f coarse_cells=%d fine_us=%.4f fine_cells=%d\n\n",
           n, density, sa.rebuilds, sa.relinked, sa.max_rebuild_relinked, sa.cells,
           grid_cell_size(&ad), sa.query_us, sa.matches,
           sa.sample_mismatch + sc.sample_mismatch + sf.sample_mismatch,
           sc.query_us, sc.cells, sf.query_us, sf.cells);

    grid_free(&ad); grid_free(&coarse); grid_free(&fine);
    free(qx); free(qy); free(a);
}

int main(void) {
    printf("Adaptive cell size vs fixed cell sizes, 1000x1000 world.\n");
    printf("Query radius shrinks with sprite size so hit counts stay comparable.\n\n");
    scene(10000, 0.5f, 1.0f, 8.0f);
    scene(100000, 0.1f, 0.3f, 2.5f);
    scene(1000000, 0.02f, 0.06f, 0.8f);

    /* Rebuild budget: hard cap on relayouts per epoch. When exhausted the grid
     * keeps serving exact queries from a stale (too coarse or too fine) layout. */
    printf("### rebuild budget\n\n");
    enum { N = 300000 };
    Rec* a = (Rec*)malloc(sizeof(Rec) * (size_t)N);
    gen(a, N, 0.1f, 0.3f);
    int out[QCAP], bad = 0;
    Grid g;
    grid_init_adaptive(&g, 0, 0, WORLD, WORLD, 0);
    grid_set_rebuild_budget(&g, 4);
    for (int i = 0; i < N; ++i) grid_insert(&g, i, a[i].x, a[i].y, a[i].r);
    int r1 = grid_rebuilds(&g), c1 = grid_cells(&g);
    for (int k = 0; k < 200; ++k) {
        float cx = frand() * WORLD, cy = frand() * WORLD;
        if (grid_query_circle(&g, cx, cy, 2.5f, out, QCAP) != brute(a, N, cx, cy, 2.5f)) ++bad;
    }
    printf("budget 4/epoch, %d inserts: %d rebuilds, %d cells, %d/200 query mismatches\n",
           N, r1, c1, bad);
    /* same run with an unbounded budget, for comparison */
    grid_new_epoch(&g);
    grid_set_rebuild_budget(&g, -1);
    grid_clear(&g);
    for (int i = 0; i < N; ++i) grid_insert(&g, i, a[i].x, a[i].y, a[i].r);
    printf("           unbounded budget: %d rebuilds, %d cells (layout catches up)\n\n",
           grid_rebuilds(&g), grid_cells(&g));
    grid_free(&g);

    for (int epoch = 0; epoch < 4; ++epoch) {
        /* an epoch = one full re-stream of the sprite set into a fresh grid */
        Grid e;
        grid_init_adaptive(&e, 0, 0, WORLD, WORLD, 0);
        grid_set_rebuild_budget(&e, 4);
        for (int i = 0; i < N; ++i) grid_insert(&e, i, a[i].x, a[i].y, a[i].r);
        printf("streamed epoch %d: %d rebuilds, %d cells\n", epoch + 1,
               grid_rebuilds(&e), grid_cells(&e));
        grid_free(&e);
    }
    free(a);
    return 0;
}
