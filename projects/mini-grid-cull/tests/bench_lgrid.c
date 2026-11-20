/* Benchmark: brute force vs flat single-cell-width grid vs density-adaptive
 * two-layer grid, at 10k and 100k sprites with a clustered (wide density
 * span) distribution. Metrics: broadphase candidates, grid rebuilds,
 * query time. */
#define _POSIX_C_SOURCE 199309L /* clock_gettime with -std=c11 */
#include "../src/lgrid.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define WORLD 4000.0f
#define NCLUST 5
#define QUERIES 1000
#define BATCHES 4

static unsigned rng = 20260611u;
static float frand(void) { rng = rng * 1103515245u + 12345u; return (float)((rng >> 8) & 0xFFFF) / 65535.0f; }

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

typedef struct { float x, y, r; } Item;

/* 70% of items in NCLUST tight clusters, 30% uniform: density spans orders
 * of magnitude, the case a single cell width cannot serve well. */
static void gen_items(Item* it, int n, float* clx, float* cly) {
    for (int c = 0; c < NCLUST; ++c) {
        clx[c] = 200.0f + frand() * (WORLD - 400.0f);
        cly[c] = 200.0f + frand() * (WORLD - 400.0f);
    }
    for (int i = 0; i < n; ++i) {
        if (frand() < 0.7f) {
            int c = i % NCLUST;
            it[i].x = clx[c] + (frand() - 0.5f) * 400.0f;
            it[i].y = cly[c] + (frand() - 0.5f) * 400.0f;
        } else {
            it[i].x = frand() * WORLD;
            it[i].y = frand() * WORLD;
        }
        it[i].r = 1.0f + frand() * 2.0f;
    }
}

static void gen_queries(float* qx, float* qy, float* qr, const float* clx, const float* cly) {
    for (int q = 0; q < QUERIES; ++q) {
        if (q % 5 < 3) { /* 60% of queries land in dense clusters */
            int c = q % NCLUST;
            qx[q] = clx[c] + (frand() - 0.5f) * 300.0f;
            qy[q] = cly[c] + (frand() - 0.5f) * 300.0f;
        } else {
            qx[q] = frand() * WORLD;
            qy[q] = frand() * WORLD;
        }
        qr[q] = 15.0f + frand() * 25.0f;
    }
}

static int brute_query(const Item* it, int n, float cx, float cy, float rad) {
    int hits = 0;
    for (int i = 0; i < n; ++i) {
        float dx = it[i].x - cx, dy = it[i].y - cy;
        float rr = rad + it[i].r;
        if (dx * dx + dy * dy <= rr * rr) ++hits;
    }
    return hits;
}

static void run_scale(int n, const float* clx, const float* cly) {
    Item* items = (Item*)malloc(sizeof(Item) * (size_t)n);
    float* qx = (float*)malloc(sizeof(float) * QUERIES);
    float* qy = (float*)malloc(sizeof(float) * QUERIES);
    float* qr = (float*)malloc(sizeof(float) * QUERIES);
    int* out = (int*)malloc(sizeof(int) * (size_t)n);
    gen_items(items, n, (float*)clx, (float*)cly);
    gen_queries(qx, qy, qr, clx, cly);

    printf("\n=== %d sprites, %d queries (world %.0fx%.0f) ===\n", n, QUERIES, WORLD, WORLD);
    printf("%-22s %14s %10s %12s %10s\n", "method", "cand/query", "rebuilds", "time(ms)", "speedup");

    /* baseline: brute force */
    double t0 = now_ms();
    long bhits = 0;
    for (int q = 0; q < QUERIES; ++q) bhits += brute_query(items, n, qx[q], qy[q], qr[q]);
    double t_brute = now_ms() - t0;
    printf("%-22s %14d %10d %12.1f %10.1f\n", "brute force", n, 0, t_brute, 1.0);

    /* flat grid: one fixed cell width for everything */
    char err[256];
    LGrid flat;
    if (lgrid_init(&flat, 1, 64.0f, 0.0f, 0, 0, WORLD, WORLD, err, sizeof err) != 0) {
        printf("flat init failed: %s\n", err);
        free(items); free(qx); free(qy); free(qr); free(out);
        return;
    }
    for (int i = 0; i < n; ++i) lgrid_insert(&flat, i, items[i].x, items[i].y, items[i].r);
    long cand = 0, hits = 0;
    t0 = now_ms();
    for (int q = 0; q < QUERIES; ++q) {
        hits += lgrid_query_circle(&flat, qx[q], qy[q], qr[q], out, n);
        cand += lgrid_last_candidates(&flat);
    }
    double t_flat = now_ms() - t0;
    printf("%-22s %14.1f %10lu %12.1f %9.1fx\n", "flat cell=64 (1 layer)",
           (double)cand / QUERIES, lgrid_rebuilds(&flat), t_flat, t_brute / t_flat);
    if (hits != bhits) printf("  MISMATCH: flat hits %ld != brute %ld\n", hits, bhits);

    /* two-layer: cell width follows density; inserted in batches so the
     * density updates trigger logged cell-width switches and rebuilds */
    LGrid lg;
    if (lgrid_init(&lg, 2, 64.0f, 0.0f, 0, 0, WORLD, WORLD, err, sizeof err) != 0) {
        printf("lgrid init failed: %s\n", err);
        lgrid_free(&flat);
        free(items); free(qx); free(qy); free(qr); free(out);
        return;
    }
    double t_build0 = now_ms();
    for (int b = 0; b < BATCHES; ++b) {
        int lo = n * b / BATCHES, hi = n * (b + 1) / BATCHES;
        for (int i = lo; i < hi; ++i) lgrid_insert(&lg, i, items[i].x, items[i].y, items[i].r);
        lgrid_set_density(&lg, (double)hi / (WORLD * WORLD), err, sizeof err);
    }
    double t_build = now_ms() - t_build0;
    cand = 0; hits = 0;
    t0 = now_ms();
    for (int q = 0; q < QUERIES; ++q) {
        hits += lgrid_query_circle(&lg, qx[q], qy[q], qr[q], out, n);
        cand += lgrid_last_candidates(&lg);
    }
    double t_layered = now_ms() - t0;
    printf("%-22s %14.1f %10lu %12.1f %9.1fx\n", "two-layer adaptive",
           (double)cand / QUERIES, lgrid_rebuilds(&lg), t_layered, t_brute / t_layered);
    printf("  final cell width %.2f (flat used 64), build+rebuilds %.1f ms\n",
           lgrid_cell(&lg), t_build);
    const LGridSwitch* sw = lgrid_switches(&lg);
    for (int s = 0; s < lgrid_switch_count(&lg); ++s)
        printf("  switch #%lu: cell %.2f -> %.2f at density %.6f (%d items)\n",
               sw[s].seq, sw[s].old_cell, sw[s].new_cell, sw[s].density, sw[s].items);
    if (hits != bhits) printf("  MISMATCH: layered hits %ld != brute %ld\n", hits, bhits);

    lgrid_free(&flat);
    lgrid_free(&lg);
    free(items); free(qx); free(qy); free(qr); free(out);
}

int main(void) {
    float clx[NCLUST] = {0}, cly[NCLUST] = {0};
    run_scale(10000, clx, cly);
    run_scale(100000, clx, cly);
    return 0;
}
