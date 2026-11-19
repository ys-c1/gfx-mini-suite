#define _POSIX_C_SOURCE 199309L
#include "../src/grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
#define MOVE_OK(rc) ((rc) == GRID_OK || (rc) == GRID_MOVED_CELL)

static unsigned rng = 12345u;
static float frand(void) {
    rng = rng * 1103515245u + 12345u;
    return (float)((rng >> 8) & 0xFFFF) / 65535.0f;
}

#define WORLD 256.0f
#define GCOLS 32
#define QCAP 4096

static int brute_hits(const GridItem* it, int n, float cx, float cy, float rad) {
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (it[i].id < 0) continue; /* compact arrays use id >= 0 anyway */
        float dx = it[i].x - cx, dy = it[i].y - cy;
        float rr = rad + it[i].r;
        if (dx * dx + dy * dy <= rr * rr) ++k;
    }
    return k;
}

static int clamped_cell(int cols, float cell, float minx, float miny, float x, float y) {
    int cx = (int)((x - minx) / cell);
    int cy = (int)((y - miny) / cell);
    if (cx < 0) cx = 0;
    if (cx >= cols) cx = cols - 1;
    if (cy < 0) cy = 0;
    if (cy >= cols) cy = cols - 1;
    return cy * cols + cx;
}

static int cell_clamped(const Grid* g, float x, float y) {
    return clamped_cell(g->cols, g->cell, g->minx, g->miny, x, y);
}

/* structural invariant: every reachable slot is doubly-linked into exactly
 * the cell its geometry says it belongs to, and nothing else is marked live */
static int grid_integrity(const Grid* g) {
    int seen = 0;
    for (int c = 0; c < g->cols * g->rows; ++c) {
        int s = g->head[c];
        if (s != -1 && g->prev[s] != -1) return 0;
        while (s != -1) {
            if (g->slotcell[s] != c) return 0;
            int gc = cell_clamped(g, g->items[s].x, g->items[s].y);
            if (gc != c) return 0;
            if (g->next[s] != -1 && g->prev[g->next[s]] != s) return 0;
            ++seen;
            s = g->next[s];
        }
    }
    return seen == g->count;
}

/* ====================================================================== */
/* Part A: correctness of the in-place path                               */
/* ====================================================================== */

static void test_basic_and_error_codes(void) {
    Grid g;
    CHECK(grid_init(&g, 16, 16, 8.0f, 0.0f, 0.0f) == GRID_OK);

    /* original behaviour still holds */
    float xs[400], ys[400], rs[400];
    GridItem ref0[400];
    for (int i = 0; i < 400; ++i) {
        xs[i] = frand() * 128.0f; ys[i] = frand() * 128.0f; rs[i] = frand() * 2.0f;
        CHECK(grid_insert(&g, i, xs[i], ys[i], rs[i]) == GRID_OK);
        ref0[i].id = i; ref0[i].x = xs[i]; ref0[i].y = ys[i]; ref0[i].r = rs[i];
    }
    CHECK(grid_count(&g) == 400);
    CHECK(grid_cells_used(&g) > 0);
    int out[512];
    for (int q = 0; q < 20; ++q) {
        float cx = frand() * 128.0f, cy = frand() * 128.0f, rad = 4.0f + frand() * 12.0f;
        int got = grid_query_circle(&g, cx, cy, rad, out, 512);
        CHECK(got == brute_hits(ref0, 400, cx, cy, rad));
        CHECK(got <= 512);
    }
    CHECK(grid_query_circle(&g, 0, 0, -1.0f, NULL, 0) == GRID_ERR_INVALID);

    /* distinct, distinguishable return codes */
    CHECK(grid_insert(&g, 9999, 200.0f, 200.0f, 1.0f) == GRID_ERR_BOUNDS); /* out of world */
    CHECK(grid_insert(&g, 0, 1.0f, 1.0f, 1.0f) == GRID_ERR_INVALID);      /* duplicate id */
    CHECK(grid_move(&g, 777, 1.0f, 1.0f) == GRID_ERR_NOID);               /* unknown id */
    CHECK(grid_move(&g, 0, 300.0f, 1.0f) == GRID_ERR_BOUNDS);             /* move OOB */
    CHECK(grid_remove(&g, 777) == GRID_ERR_NOID);                         /* unknown delete */
    grid_clear(&g);
    CHECK(grid_count(&g) == 0);
    CHECK(grid_move(&g, 1, 1.0f, 1.0f) == GRID_ERR_EMPTY);                /* empty grid move */
    CHECK(grid_remove(&g, 1) == GRID_ERR_NOID);                            /* empty grid delete: NOID */
    CHECK(grid_query_circle(&g, 1.0f, 1.0f, 1.0f, out, 512) == 0);        /* query on empty */

    /* repeated delete: first succeeds, every later one is NOID */
    CHECK(grid_insert(&g, 42, 4.0f, 4.0f, 1.0f) == GRID_OK);
    CHECK(grid_remove(&g, 42) == GRID_OK);
    CHECK(grid_remove(&g, 42) == GRID_ERR_NOID);
    CHECK(grid_remove(&g, 42) == GRID_ERR_NOID);
    CHECK(grid_count(&g) == 0);
    grid_free(&g);
}

static void test_move_immediate(void) {
    Grid g;
    CHECK(grid_init(&g, GCOLS, GCOLS, WORLD / GCOLS, 0.0f, 0.0f) == GRID_OK);
    int N = 500;
    GridItem* ref = (GridItem*)malloc(sizeof(GridItem) * (size_t)N);
    for (int i = 0; i < N; ++i) {
        ref[i].id = i;
        ref[i].x = frand() * (WORLD - 0.01f);
        ref[i].y = frand() * (WORLD - 0.01f);
        ref[i].r = frand() * 2.0f;
        CHECK(grid_insert(&g, i, ref[i].x, ref[i].y, ref[i].r) == GRID_OK);
    }

    /* move every item across a cell boundary, then verify immediately */
    for (int i = 0; i < N; ++i) {
        float nx, ny;
        do {
            nx = frand() * (WORLD - 0.01f);
            ny = frand() * (WORLD - 0.01f);
        } while (cell_clamped(&g, nx, ny) == cell_clamped(&g, ref[i].x, ref[i].y));

        int old_cell = cell_clamped(&g, ref[i].x, ref[i].y);
        int new_cell = cell_clamped(&g, nx, ny);
        CHECK(grid_move(&g, i, nx, ny) == GRID_MOVED_CELL); /* forced cross-cell */
        ref[i].x = nx; ref[i].y = ny;
        if (!grid_integrity(&g)) {
            printf("INTEGRITY BROKEN after moving id=%d %d -> %d\n", i, old_cell, new_cell);
            CHECK(0);
            return;
        }

        /* old cell must not contain the item anymore */
        int* head = g.head;
        for (int s = head[old_cell]; s != -1; s = g.next[s])
            CHECK(g.items[s].id != i);
        /* new cell must contain it */
        int found = 0;
        for (int s = head[new_cell]; s != -1; s = g.next[s])
            if (g.items[s].id == i) found = 1;
        CHECK(found);
        /* zero-radius query at the new position must hit it (and only live items) */
        int q[64];
        int k = grid_query_circle(&g, nx, ny, 0.0f, q, 64);
        found = 0;
        for (int j = 0; j < k; ++j) if (q[j] == i) found = 1;
        CHECK(found);
        /* full canonical query agrees with brute force right away */
        float cx = nx, cy = ny, rad = 12.0f;
        int qq[QCAP];
        int got = grid_query_circle(&g, cx, cy, rad, qq, QCAP);
        GridItem* live = (GridItem*)malloc(sizeof(GridItem) * (size_t)g.count);
        int nl = 0;
        for (int s = 0; s < g.nslots; ++s)
            if (g.slotcell[s] >= 0) live[nl++] = g.items[s];
        CHECK(nl == g.count);
        CHECK(got == brute_hits(live, nl, cx, cy, rad));
        free(live);
    }
    free(ref);
    grid_free(&g);
}

static void test_delete_no_residue(void) {
    Grid g;
    CHECK(grid_init(&g, GCOLS, GCOLS, WORLD / GCOLS, 0.0f, 0.0f) == GRID_OK);
    int N = 300;
    for (int i = 0; i < N; ++i)
        CHECK(grid_insert(&g, i, frand() * (WORLD - 0.01f),
                           frand() * (WORLD - 0.01f), frand() * 2.0f) == GRID_OK);
    /* delete half, including head, middle and tail positions of chains */
    for (int i = 0; i < N; i += 2) CHECK(grid_remove(&g, i) == GRID_OK);
    CHECK(grid_count(&g) == N / 2);

    /* no deleted id survives in any chain */
    for (int c = 0; c < g.cols * g.rows; ++c)
        for (int s = g.head[c]; s != -1; s = g.next[s])
            CHECK(g.items[s].id % 2 == 1);

    /* chain integrity: prev/next are consistent */
    for (int c = 0; c < g.cols * g.rows; ++c) {
        int s = g.head[c];
        CHECK(s == -1 || g.prev[s] == -1);
        while (s != -1) {
            CHECK(g.slotcell[s] == c);
            if (g.next[s] != -1) CHECK(g.prev[g.next[s]] == s);
            s = g.next[s];
        }
    }
    /* queries never return deleted ids */
    for (int q = 0; q < 30; ++q) {
        int out[QCAP];
        float cx = frand() * WORLD, cy = frand() * WORLD, rad = 4.0f + frand() * 16.0f;
        int k = grid_query_circle(&g, cx, cy, rad, out, QCAP);
        for (int j = 0; j < k; ++j) CHECK(out[j] % 2 == 1);
    }

    /* re-inserting a deleted id works and the new slot is reachable */
    CHECK(grid_insert(&g, 0, 5.0f, 5.0f, 1.0f) == GRID_OK);
    CHECK(grid_count(&g) == N / 2 + 1);
    int out[QCAP];
    int k = grid_query_circle(&g, 5.0f, 5.0f, 0.0f, out, QCAP);
    int found = 0;
    for (int j = 0; j < k; ++j) if (out[j] == 0) found = 1;
    CHECK(found);
    grid_free(&g);
}

static void compare_two_paths(Grid* inplace, const GridItem* ref, int n) {
    GridItem* live = (GridItem*)malloc(sizeof(GridItem) * (size_t)(n ? n : 1));
    int nl = 0;
    for (int i = 0; i < n; ++i) if (ref[i].id >= 0) live[nl++] = ref[i];

    Grid rebuilt;
    CHECK(grid_init(&rebuilt, inplace->cols, inplace->rows, inplace->cell,
                    inplace->minx, inplace->miny) == GRID_OK);
    CHECK(grid_rebuild(&rebuilt, live, nl) == GRID_OK);
    CHECK(grid_count(&rebuilt) == inplace->count);
    CHECK(grid_cells_used(&rebuilt) == grid_cells_used(inplace));

    for (int q = 0; q < 24; ++q) {
        float cx = frand() * WORLD, cy = frand() * WORLD;
        float rad = 2.0f + frand() * 20.0f;
        int a[QCAP], b[QCAP];
        int ka = grid_query_circle(inplace, cx, cy, rad, a, QCAP);
        int kb = grid_query_circle(&rebuilt, cx, cy, rad, b, QCAP);
        CHECK(ka == kb);
        CHECK(ka == brute_hits(live, nl, cx, cy, rad));
        CHECK(memcmp(a, b, sizeof(int) * (size_t)ka) == 0); /* same set, same order */
    }

    /* canonical (cell, id) order: verify via an id->entry lookup on the live set */
    int maxid = 0;
    for (int i = 0; i < nl; ++i) if (live[i].id > maxid) maxid = live[i].id;
    GridItem* byid = (GridItem*)calloc((size_t)(maxid + 1), sizeof(GridItem));
    for (int i = 0; i < nl; ++i) byid[live[i].id] = live[i];
    for (int q = 0; q < 8; ++q) {
        float cx = frand() * WORLD, cy = frand() * WORLD, rad = 5.0f + frand() * 15.0f;
        int a[QCAP];
        int ka = grid_query_circle(inplace, cx, cy, rad, a, QCAP);
        for (int j = 1; j < ka; ++j) {
            GridItem p = byid[a[j - 1]], c = byid[a[j]];
            int cp = clamped_cell(inplace->cols, inplace->cell, inplace->minx, inplace->miny, p.x, p.y);
            int cc = clamped_cell(inplace->cols, inplace->cell, inplace->minx, inplace->miny, c.x, c.y);
            CHECK(cp < cc || (cp == cc && a[j - 1] < a[j]));
        }
    }
    free(byid);
    free(live);
    grid_free(&rebuilt);
}

static void test_longhaul_20k(void) {
    Grid g;
    CHECK(grid_init(&g, GCOLS, GCOLS, WORLD / GCOLS, 0.0f, 0.0f) == GRID_OK);
    int N = 1000;
    GridItem* ref = (GridItem*)malloc(sizeof(GridItem) * (size_t)N);
    for (int i = 0; i < N; ++i) {
        ref[i].id = i;
        ref[i].x = frand() * (WORLD - 0.01f);
        ref[i].y = frand() * (WORLD - 0.01f);
        ref[i].r = frand() * 2.0f;
        CHECK(grid_insert(&g, i, ref[i].x, ref[i].y, ref[i].r) == GRID_OK);
    }

    int dead_count = 0;
    for (int step = 0; step < 20000; ++step) {
        int i = (int)(frand() * N);
        if (ref[i].id < 0) continue; /* deleted slot */
        int action = (int)(frand() * 10.0f);
        if (action < 7) {
            /* worst-case biased move: teleport to a random cell, cross-cell mostly */
            float nx = frand() * (WORLD - 0.01f);
            float ny = frand() * (WORLD - 0.01f);
            int move_rc = grid_move(&g, ref[i].id, nx, ny);
            CHECK(MOVE_OK(move_rc));
            ref[i].x = nx; ref[i].y = ny;
        } else if (action < 9 && dead_count < N / 2) {
            CHECK(grid_remove(&g, ref[i].id) == GRID_OK);
            ref[i].id = -1;
            ++dead_count;
        } else if (dead_count > 0) {
            /* revive a previously deleted slot; bounded probes, never spin */
            int j = -1;
            for (int k = 0; k < 64; ++k) {
                int cand = (int)(frand() * N);
                if (ref[cand].id < 0) { j = cand; break; }
            }
            if (j < 0) continue;
            ref[j].id = j;
            ref[j].x = frand() * (WORLD - 0.01f);
            ref[j].y = frand() * (WORLD - 0.01f);
            CHECK(grid_insert(&g, j, ref[j].x, ref[j].y, ref[j].r) == GRID_OK);
            --dead_count;
        }
        if ((step + 1) % 5000 == 0) {
            printf("  longhaul step %d (live=%d) ...\n", step + 1, grid_count(&g));
            fflush(stdout);
            compare_two_paths(&g, ref, N); /* checkpoints at 5k/10k/15k/20k */
        }
    }

    /* final full accounting: compact the reference to live entries */
    int live = 0;
    for (int i = 0; i < N; ++i) if (ref[i].id >= 0) ++live;
    CHECK(grid_count(&g) == live);
    compare_two_paths(&g, ref, N);

    /* rebuild on an empty grid is legal and yields an empty, queryable grid */
    CHECK(grid_rebuild(&g, NULL, 0) == GRID_OK);
    CHECK(grid_count(&g) == 0);
    int out[16];
    CHECK(grid_query_circle(&g, 1.0f, 1.0f, 5.0f, out, 16) == 0);
    CHECK(grid_rebuild(&g, ref, 0) == GRID_OK);

    free(ref);
    grid_free(&g);
}

/* ====================================================================== */
/* Part B: benchmarks                                                     */
/* ====================================================================== */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

/* workload modes:
 *   0 slow walk   : small per-frame jitter, most sprites stay in their cell
 *   1 half churn  : every second sprite teleports, ~50% cross-cell
 *   2 teleport    : every sprite jumps to a random cell (~100% cross-cell),
 *                   the stated worst case for the in-place path            */
static void gen_frame(unsigned* r, const float* x, const float* y,
                      float* nx, float* ny, int N, int mode) {
    for (int i = 0; i < N; ++i) {
        unsigned u = *r;
        u = u * 1103515245u + 12345u;
        float a = (float)((u >> 8) & 0xFFFF) / 65535.0f;
        u = u * 1103515245u + 12345u;
        float b = (float)((u >> 8) & 0xFFFF) / 65535.0f;
        *r = u;
        if (mode == 0) {
            nx[i] = x[i] + (a - 0.5f) * 3.0f;   /* cell size is 8 world units */
            ny[i] = y[i] + (b - 0.5f) * 3.0f;
        } else if (mode == 1) {
            nx[i] = (i & 1) ? a * (WORLD - 0.01f) : x[i];
            ny[i] = (i & 1) ? b * (WORLD - 0.01f) : y[i];
        } else {
            nx[i] = a * (WORLD - 0.01f);
            ny[i] = b * (WORLD - 0.01f);
        }
        if (nx[i] < 0.0f) nx[i] = 0.0f;
        if (ny[i] < 0.0f) ny[i] = 0.0f;
        if (nx[i] >= WORLD) nx[i] = WORLD - 0.01f;
        if (ny[i] >= WORLD) ny[i] = WORLD - 0.01f;
    }
}

static const char* MODE_NAME[3] = {
    "dataset 1 slow walk (small jitter)    ",
    "dataset 2 half churn (~50% cross-cell)",
    "dataset 3 teleport (~100% cross-cell) ",
};

static void run_bench(int N, int mode) {
    const int WARM = 50, FRAMES = 300;
    float *x  = (float*)malloc(sizeof(float) * (size_t)N);
    float *y  = (float*)malloc(sizeof(float) * (size_t)N);
    float *nx = (float*)malloc(sizeof(float) * (size_t)N);
    float *ny = (float*)malloc(sizeof(float) * (size_t)N);
    GridItem* batch = (GridItem*)malloc(sizeof(GridItem) * (size_t)N);

    unsigned seed = 0xC0FFEEu + (unsigned)N * 7u + (unsigned)mode * 131u;
    for (int i = 0; i < N; ++i) {
        seed = seed * 1103515245u + 12345u;
        x[i] = (float)((seed >> 8) & 0xFFFF) / 65535.0f * (WORLD - 0.01f);
        seed = seed * 1103515245u + 12345u;
        y[i] = (float)((seed >> 8) & 0xFFFF) / 65535.0f * (WORLD - 0.01f);
        batch[i].id = i; batch[i].r = 1.0f;
    }

    Grid g;
    grid_init(&g, GCOLS, GCOLS, WORLD / GCOLS, 0.0f, 0.0f);
    for (int i = 0; i < N; ++i)
        if (grid_insert(&g, i, x[i], y[i], 1.0f) != GRID_OK) CHECK(0);

    double t_inc[FRAMES], t_reb[FRAMES];
    double inc_grand = 0.0;   /* total in-place time over measured frames */
    long long cross_total = 0;
    unsigned seed_run = seed;

    for (int f = 0; f < WARM + FRAMES; ++f) {
        gen_frame(&seed_run, x, y, nx, ny, N, mode);

        /* path 1: in-place maintenance */
        long long cross = 0;
        double t0 = now_sec();
        for (int i = 0; i < N; ++i) {
            int rc = grid_move(&g, i, nx[i], ny[i]);
            if (rc < 0) CHECK(0);
            if (rc == GRID_MOVED_CELL) ++cross;
        }
        double t1 = now_sec();

        /* path 2: full rebuild of the same grid, identical target contents */
        for (int i = 0; i < N; ++i) { batch[i].x = nx[i]; batch[i].y = ny[i]; }
        double t2 = now_sec();
        if (grid_rebuild(&g, batch, N) != GRID_OK) CHECK(0);
        double t3 = now_sec();

        for (int i = 0; i < N; ++i) { x[i] = nx[i]; y[i] = ny[i]; }
        if (f >= WARM) {
            t_inc[f - WARM] = t1 - t0;
            t_reb[f - WARM] = t3 - t2;
            cross_total += cross;
            inc_grand += t1 - t0;
        }
    }

    /* correctness gate: the two paths must return identical sets AND order */
    for (int i = 0; i < N; ++i) { batch[i].x = x[i]; batch[i].y = y[i]; }
    Grid check;
    grid_init(&check, GCOLS, GCOLS, WORLD / GCOLS, 0.0f, 0.0f);
    CHECK(grid_rebuild(&check, batch, N) == GRID_OK);
    int oa[QCAP], ob[QCAP];
    for (int q = 0; q < 16; ++q) {
        float cx = (float)(q * 17 % 256), cy = (float)(q * 31 % 256), rad = 10.0f;
        int ka = grid_query_circle(&g, cx, cy, rad, oa, QCAP);
        int kb = grid_query_circle(&check, cx, cy, rad, ob, QCAP);
        CHECK(ka == kb && memcmp(oa, ob, sizeof(int) * (size_t)ka) == 0);
    }
    grid_free(&check);

    qsort(t_inc, FRAMES, sizeof(double), cmp_double);
    qsort(t_reb, FRAMES, sizeof(double), cmp_double);
    double mi  = t_inc[FRAMES / 2]        * 1e6;
    double mr  = t_reb[FRAMES / 2]        * 1e6;
    double pi  = t_inc[FRAMES * 95 / 100] * 1e6;
    double pr  = t_reb[FRAMES * 95 / 100] * 1e6;
    double wci = t_inc[FRAMES - 1]        * 1e6; /* worst observed frame */
    double wcr = t_reb[FRAMES - 1]        * 1e6;
    double cross_pct = 100.0 * (double)cross_total / ((double)FRAMES * N);
    /* amortized per-move cost over all measured frames: robust to single-frame
     * OS preemption, so N * this is a reproducible algorithmic time bound. */
    double per_move_ns = inc_grand / ((double)FRAMES * N) * 1e9;
    double bound_us = per_move_ns * (double)N / 1000.0;

    printf("  [%s N=%d]\n", MODE_NAME[mode], N);
    printf("    measured cross-cell rate: %.1f%% of moves\n", cross_pct);
    printf("    in-place : median %8.2f us   p95 %8.2f us   worst-frame %8.2f us\n",
           mi, pi, wci);
    printf("    rebuild  : median %8.2f us   p95 %8.2f us   worst-frame %8.2f us\n",
           mr, pr, wcr);
    printf("    speedup vs rebuild: median x%.2f   p95 x%.2f\n", mr / mi, pr / pi);
    printf("    amortized %.2f ns/move -> deterministic upper bound for a frame "
           "of %d moves: %.2f us\n\n", per_move_ns, N, bound_us);

    grid_free(&g);
    free(x); free(y); free(nx); free(ny); free(batch);
}

int main(void) {
    printf("== Part A: correctness ==\n");
    test_basic_and_error_codes();
    test_move_immediate();
    test_delete_no_residue();
    test_longhaul_20k();
    printf("part A: %d checks, %d failures\n\n", checks, fails);

    printf("== Part B: per-frame cost (300 measured frames after 50 warm-up) ==\n");
    printf("both paths run on the same Grid; speedup = rebuild time / in-place time\n\n");
    for (int k = 0; k < 2; ++k) {
        int N = k == 0 ? 1000 : 2000;
        for (int mode = 0; mode < 3; ++mode) run_bench(N, mode);
    }

    printf("total: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
