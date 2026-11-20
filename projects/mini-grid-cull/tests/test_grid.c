#include "../src/grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned rng = 12345u;
static float frand(void) { rng = rng * 1103515245u + 12345u; return (float)((rng >> 8) & 0xFFFF) / 65535.0f; }

/* brute force: circle vs inclusive rect */
static int bf_hit(float x, float y, float r, const GridWindow* w) {
    float nx = x < w->x0 ? w->x0 : (x > w->x1 ? w->x1 : x);
    float ny = y < w->y0 ? w->y0 : (y > w->y1 ? w->y1 : y);
    float dx = x - nx, dy = y - ny;
    return dx * dx + dy * dy <= r * r;
}

static void test_batch_basic(void) {
    Grid g;
    CHECK(grid_init(&g, 8, 8, 10.0f, 0.0f, 0.0f) == 0);
    /* id encodes item index; positions well separated, no boundary ties */
    CHECK(grid_insert(&g, 100, 5.0f, 5.0f, 1.0f) == 0);    /* cell 0,0 */
    CHECK(grid_insert(&g, 101, 15.0f, 5.0f, 1.0f) == 0);
    CHECK(grid_insert(&g, 102, 55.0f, 55.0f, 1.0f) == 0);
    CHECK(grid_insert(&g, 103, 75.0f, 75.0f, 1.0f) == 0);
    CHECK(grid_insert(&g, 104, 30.0f, 30.0f, 0.0f) == 0);  /* r = 0 point */

    GridWindow wins[2] = {
        { 0.0f, 0.0f, 12.0f, 12.0f },    /* hits only 100 */
        { 12.5f, 0.0f, 20.0f, 12.0f },   /* hits only 101; gap from w0 */
    };
    int out[64], wc[2];
    int total = grid_query_windows(&g, wins, 2, out, 64, wc);
    CHECK(total == 2);
    CHECK(out[0] == 100);   /* window 0's segment first (window order) */
    CHECK(out[1] == 101);
    CHECK(wc[0] == 1);
    CHECK(wc[1] == 1);

    /* point item exactly on window boundary: inclusive convention */
    GridWindow edge = { 30.0f, 30.0f, 40.0f, 40.0f };
    int one[8], wone[1] = {0};
    CHECK(grid_query_windows(&g, &edge, 1, one, 8, wone) == 1);
    CHECK(one[0] == 104);
    CHECK(wone[0] == 1);

    /* another insert into w0's cell: within a window items come out in
       insertion order, independent of the head-chain layout */
    CHECK(grid_insert(&g, 105, 6.0f, 6.0f, 0.5f) == 0);
    int out2[8], wc2[2];
    CHECK(grid_query_windows(&g, wins, 2, out2, 8, wc2) == 3);
    CHECK(out2[0] == 100);
    CHECK(out2[1] == 105);
    CHECK(out2[2] == 101);
    CHECK(wc2[0] == 2);
    CHECK(wc2[1] == 1);
    /* the original single-window circle query keeps its own chain order */
    int circ[8];
    int cn = grid_query_circle(&g, 5.0f, 5.0f, 3.0f, circ, 8);
    CHECK(cn == 2);
    CHECK(circ[0] == 105);
    CHECK(circ[1] == 100);

    grid_free(&g);
}

static void test_batch_dedup_order(void) {
    Grid g;
    CHECK(grid_init(&g, 8, 8, 10.0f, 0.0f, 0.0f) == 0);
    for (int i = 0; i < 30; ++i)
        CHECK(grid_insert(&g, 1000 + i, frand() * 40.0f, frand() * 40.0f,
                          0.5f + frand()) == 0);

    /* three nested windows: innermost items appear once, in w0's segment */
    GridWindow nested[3] = {
        { 0.0f, 0.0f, 10.0f, 10.0f },
        { 0.0f, 0.0f, 25.0f, 25.0f },
        { 0.0f, 0.0f, 40.0f, 40.0f },
    };
    int* out = (int*)malloc(sizeof(int) * 64);
    int wc[3];
    int total = grid_query_windows(&g, nested, 3, out, 64, wc);

    /* every id unique in merged output */
    for (int i = 0; i < total; ++i)
        for (int j = i + 1; j < total; ++j)
            CHECK(out[i] != out[j]);

    /* brute force per-window counts and distinct total */
    int ref_wc[3] = {0, 0, 0};
    int distinct = 0;
    for (int i = 0; i < g.count; ++i) {
        int any = 0;
        for (int w = 0; w < 3; ++w)
            if (bf_hit(g.items[i].x, g.items[i].y, g.items[i].r, &nested[w])) {
                ++ref_wc[w]; any = 1;
            }
        if (any) ++distinct;
    }
    CHECK(total == distinct);
    CHECK(wc[0] == ref_wc[0]);
    CHECK(wc[1] == ref_wc[1]);
    CHECK(wc[2] == ref_wc[2]);
    /* every id in segment w must hit window w, and segments concatenate */
    int pos = 0;
    for (int w = 0; w < 3; ++w) {
        /* expected segment size = items whose FIRST hitting window is w */
        int seg = 0;
        for (int i = 0; i < g.count; ++i) {
            int first = -1;
            for (int ww = 0; ww < 3; ++ww)
                if (bf_hit(g.items[i].x, g.items[i].y, g.items[i].r, &nested[ww])) {
                    first = ww; break;
                }
            if (first == w) ++seg;
        }
        for (int k = 0; k < seg; ++k) {
            int id = out[pos + k];
            int item = id - 1000;
            CHECK(bf_hit(g.items[item].x, g.items[item].y, g.items[item].r, &nested[w]));
        }
        pos += seg;
    }
    CHECK(pos == total);
    free(out);
    grid_free(&g);
}

static void test_batch_empty_and_invalid(void) {
    Grid g;
    CHECK(grid_init(&g, 4, 4, 10.0f, 0.0f, 0.0f) == 0);
    CHECK(grid_insert(&g, 1, 5.0f, 5.0f, 1.0f) == 0);

    GridWindow empty = { 50.0f, 50.0f, 60.0f, 60.0f };
    int out[16], wc = -1;
    CHECK(grid_query_windows(&g, &empty, 1, out, 16, &wc) == 0);
    CHECK(wc == 0);

    /* empty grid */
    Grid e;
    CHECK(grid_init(&e, 4, 4, 10.0f, 0.0f, 0.0f) == 0);
    GridWindow w = { 0.0f, 0.0f, 100.0f, 100.0f };
    CHECK(grid_query_windows(&e, &w, 1, out, 16, &wc) == 0);

    /* zero windows: valid, writes nothing */
    CHECK(grid_query_windows(&g, &w, 0, out, 16, &wc) == 0);

    /* invalid arguments */
    CHECK(grid_query_windows(NULL, &w, 1, out, 16, &wc) == -1);
    CHECK(grid_query_windows(&g, NULL, 1, out, 16, &wc) == -1);
    CHECK(grid_query_windows(&g, &w, -1, out, 16, &wc) == -1);
    CHECK(grid_query_windows(&g, &w, 1, out, -1, &wc) == -1);
    GridWindow bad1 = { 10.0f, 0.0f, 5.0f, 10.0f };   /* reversed x */
    CHECK(grid_query_windows(&g, &bad1, 1, out, 16, &wc) == -1);
    GridWindow bad2 = { 0.0f, 10.0f, 5.0f, 5.0f };    /* reversed y */
    CHECK(grid_query_windows(&g, &bad2, 1, out, 16, &wc) == -1);
    GridWindow bad3 = { NAN, 0.0f, 5.0f, 5.0f };
    CHECK(grid_query_windows(&g, &bad3, 1, out, 16, &wc) == -1);
    CHECK(grid_query_windows(&g, &w, 40000, out, 16, &wc) == -1);

    /* zero-area (degenerate but valid) window: only points on the line hit */
    GridWindow line = { 5.0f, 5.0f, 5.0f, 5.0f };
    CHECK(grid_query_windows(&g, &line, 1, out, 16, &wc) == 1);
    CHECK(out[0] == 1);

    grid_free(&e);
    grid_free(&g);
}

static void test_batch_cap(void) {
    Grid g;
    CHECK(grid_init(&g, 10, 10, 10.0f, 0.0f, 0.0f) == 0);
    const int N = 200;
    for (int i = 0; i < N; ++i)
        CHECK(grid_insert(&g, i, frand() * 100.0f, frand() * 100.0f,
                          0.2f + frand() * 2.0f) == 0);
    GridWindow wins[2] = {
        { 0.0f, 0.0f, 60.0f, 60.0f },
        { 40.0f, 40.0f, 100.0f, 100.0f },
    };
    int full[256], small[8], wc1[2], wc2[2];
    int total = grid_query_windows(&g, wins, 2, full, 256, wc1);
    CHECK(total > 8);
    int got = grid_query_windows(&g, wins, 2, small, 8, wc2);
    CHECK(got == total);              /* full count still returned */
    for (int k = 0; k < 8; ++k)
        CHECK(small[k] == full[k]);   /* deterministic prefix */
    CHECK(wc1[0] == wc2[0] && wc1[1] == wc2[1]);

    /* cap = 0: count-only mode, no write */
    int* null_out = NULL;
    int cnt = grid_query_windows(&g, wins, 2, null_out, 0, NULL);
    CHECK(cnt == total);

    /* two repeated calls give identical output (no hidden state) */
    int again[256];
    CHECK(grid_query_windows(&g, wins, 2, again, 256, NULL) == total);
    CHECK(memcmp(full, again, sizeof(int) * (size_t)total) == 0);
    grid_free(&g);
}

static void test_batch_fuzz(int nwin, int N, float span) {
    Grid g;
    CHECK(grid_init(&g, 32, 32, 4.0f, 0.0f, 0.0f) == 0);
    float* xs = (float*)malloc(sizeof(float) * (size_t)N);
    float* ys = (float*)malloc(sizeof(float) * (size_t)N);
    float* rs = (float*)malloc(sizeof(float) * (size_t)N);
    for (int i = 0; i < N; ++i) {
        xs[i] = frand() * span; ys[i] = frand() * span;
        rs[i] = frand() * 3.0f;
        CHECK(grid_insert(&g, i, xs[i], ys[i], rs[i]) == 0);
    }
    GridWindow* wins = (GridWindow*)malloc(sizeof(GridWindow) * (size_t)nwin);
    for (int w = 0; w < nwin; ++w) {
        float x0 = frand() * span, y0 = frand() * span;
        wins[w].x0 = x0; wins[w].y0 = y0;
        wins[w].x1 = x0 + frand() * span * 0.25f;
        wins[w].y1 = y0 + frand() * span * 0.25f;
    }
    int* out = (int*)malloc(sizeof(int) * (size_t)(N + 1));
    int* wc = (int*)malloc(sizeof(int) * (size_t)nwin);
    int total = grid_query_windows(&g, wins, nwin, out, N + 1, wc);
    CHECK(total <= N);

    /* reference counts */
    int* ref_wc = (int*)calloc((size_t)nwin, sizeof(int));
    int* first = (int*)malloc(sizeof(int) * (size_t)N);
    int ref_total = 0;
    for (int i = 0; i < N; ++i) {
        first[i] = -1;
        for (int w = 0; w < nwin; ++w)
            if (bf_hit(xs[i], ys[i], rs[i], &wins[w])) {
                if (first[i] < 0) first[i] = w;
                ++ref_wc[w];
            }
        if (first[i] >= 0) ++ref_total;
    }
    CHECK(total == ref_total);
    for (int w = 0; w < nwin; ++w) CHECK(wc[w] == ref_wc[w]);

    /* merged output: unique, window-major, each id hits its segment window,
       and within each window ids ascend by insertion index */
    for (int i = 0; i < total; ++i)
        for (int j = i + 1; j < total; ++j)
            CHECK(out[i] != out[j]);
    int pos = 0;
    for (int w = 0; w < nwin; ++w) {
        int prev_item = -1;
        for (int i = 0; i < N; ++i) {
            if (first[i] != w) continue;
            CHECK(pos < total);
            CHECK(out[pos] == i);
            CHECK(i > prev_item);   /* insertion order: ascending index */
            prev_item = i;
            ++pos;
        }
    }
    CHECK(pos == total);

    /* scratch size matches the documented allocation formula exactly:
       packed stamps + 4 cell boxes + histogram (+2 lists over 64 windows) */
    size_t scratch = grid_query_windows_scratch(&g, nwin);
    int b = 1; while (b < 16 && (1 << b) < nwin + 1) ++b;
    size_t stamp_bytes = ((size_t)N * (size_t)b + 31) / 32 * 4 + 4;
    size_t expect = stamp_bytes
                  + sizeof(int) * (size_t)nwin * 5
                  + (nwin > 64 ? sizeof(int) * (size_t)nwin * 2 : 0);
    CHECK(scratch == expect);

    free(first); free(ref_wc); free(wc); free(out); free(wins);
    free(rs); free(ys); free(xs);
    grid_free(&g);
}

int main(void) {
    Grid g;
    CHECK(grid_init(&g, 16, 16, 8.0f, 0.0f, 0.0f) == 0);
    int N = 400;
    float xs[400], ys[400], rs[400];
    for (int i = 0; i < N; ++i) {
        xs[i] = frand() * 128.0f; ys[i] = frand() * 128.0f; rs[i] = frand() * 2.0f;
        CHECK(grid_insert(&g, i, xs[i], ys[i], rs[i]) == 0);
    }
    CHECK(grid_count(&g) == N);
    CHECK(grid_cells_used(&g) > 0);

    for (int q = 0; q < 20; ++q) {
        float cx = frand() * 128.0f, cy = frand() * 128.0f, rad = 4.0f + frand() * 12.0f;
        int out[512];
        int got = grid_query_circle(&g, cx, cy, rad, out, 512);
        int brute = 0;
        for (int i = 0; i < N; ++i) {
            float dx = xs[i] - cx, dy = ys[i] - cy;
            float rr = rad + rs[i];
            if (dx * dx + dy * dy <= rr * rr) ++brute;
        }
        CHECK(got == brute);
        CHECK(got <= 512);
    }

    int bad = grid_query_circle(&g, 0, 0, -1.0f, NULL, 0);
    CHECK(bad == -1);

    grid_clear(&g);
    CHECK(grid_count(&g) == 0);
    grid_free(&g);

    /* ---- batch window query tests ---- */
    test_batch_basic();
    test_batch_dedup_order();
    test_batch_empty_and_invalid();
    test_batch_cap();
    test_batch_fuzz(1, 500, 128.0f);
    test_batch_fuzz(3, 500, 128.0f);
    test_batch_fuzz(10, 1000, 128.0f);
    test_batch_fuzz(50, 2000, 128.0f);      /* fast mask path */
    test_batch_fuzz(64, 1000, 128.0f);
    test_batch_fuzz(65, 1000, 128.0f);      /* generic fallback path */
    test_batch_fuzz(120, 1500, 128.0f);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
