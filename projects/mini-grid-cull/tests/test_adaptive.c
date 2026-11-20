#include "../src/grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned long long rng_state = 0x9E3779B97F4A7C15ull;
static double rnd(void) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (double)(rng_state & 0xFFFFFF) / 16777215.0;
}

typedef struct { float x, y, r; int id; } Rec;

/* Density generator: N items in one of three regimes.
 *   sparse / medium / dense differ by the spacing scale. */
static void gen_items(Rec* a, int n, int scen) {
    float scale = scen == 0 ? 400.0f : scen == 1 ? 120.0f : 30.0f;
    float rad_scale = scen == 0 ? 3.0f : scen == 1 ? 2.0f : 1.0f;
    for (int i = 0; i < n; ++i) {
        a[i].x = (float)(rnd() * scale);
        a[i].y = (float)(rnd() * scale);
        a[i].r = (float)(rnd() * rad_scale);
        a[i].id = i;
    }
}

static int brute(const Rec* a, int n, float cx, float cy, float rad) {
    int c = 0;
    for (int i = 0; i < n; ++i) {
        float dx = a[i].x - cx, dy = a[i].y - cy, rr = rad + a[i].r;
        if (dx * dx + dy * dy <= rr * rr) ++c;
    }
    return c;
}

int main(void) {
    /* --- correctness: adaptive output must equal brute force and the fixed grid --- */
    for (int scen = 0; scen < 3; ++scen) {
        enum { N = 3000 };
        Rec a[N];
        gen_items(a, N, scen);
        float lo = scen == 0 ? 400.0f : scen == 1 ? 120.0f : 30.0f;

        Grid ad, fx;
        CHECK(grid_init_adaptive(&ad, 0, 0, lo, lo, 0.0f) == 0);
        CHECK(grid_init(&fx, 64, 64, lo / 64.0f, 0, 0) == 0);
        for (int i = 0; i < N; ++i) {
            CHECK(grid_insert(&ad, a[i].id, a[i].x, a[i].y, a[i].r) == 0);
            CHECK(grid_insert(&fx, a[i].id, a[i].x, a[i].y, a[i].r) == 0);
        }
        CHECK(grid_count(&ad) == N);
        CHECK(grid_cells(&ad) <= GRID_MAX_CELLS);
        for (int q = 0; q < 200; ++q) {
            float cx = (float)(rnd() * lo), cy = (float)(rnd() * lo), rad = (float)(rnd() * lo * 0.1);
            int oa[4096], of[4096];
            int na = grid_query_circle(&ad, cx, cy, rad, oa, 4096);
            int nf = grid_query_circle(&fx, cx, cy, rad, of, 4096);
            CHECK(na == nf);
            CHECK(na == brute(a, N, cx, cy, rad));
            /* same set of ids, not just the same count */
            int seen[4096] = {0};
            for (int k = 0; k < na; ++k) seen[oa[k]] = 1;
            for (int k = 0; k < nf; ++k) CHECK(seen[of[k]]);
        }
        grid_free(&ad);
        grid_free(&fx);
    }
    printf("correctness: %d checks, %d failures\n", checks, fails);

    /* --- hysteresis: inserts within a regime should not rebuild on every insert --- */
    {
        enum { N = 20000 };
        Rec a[N];
        gen_items(a, N, 1);
        Grid g;
        grid_init_adaptive(&g, 0, 0, 120.0f, 120.0f, 0.0f);
        for (int i = 0; i < N; ++i) grid_insert(&g, i, a[i].x, a[i].y, a[i].r);
        printf("hysteresis: %d rebuilds for %d inserts (%.2f%% of inserts)\n",
               grid_rebuilds(&g), N, 100.0 * grid_rebuilds(&g) / N);
        CHECK(grid_rebuilds(&g) < N / 10);
        grid_free(&g);
    }

    /* --- grid_cells_used > 0 sanity --- */
    {
        Grid g;
        grid_init_adaptive(&g, 0, 0, 100, 100, 0);
        for (int i = 0; i < 500; ++i) grid_insert(&g, i, (float)(rnd() * 100), (float)(rnd() * 100), 0.5f);
        CHECK(grid_cells_used(&g) > 0);
        grid_free(&g);
    }

    /* --- arena growth: items outside the declared arena are not lost --- */
    {
        Grid g;
        grid_init_adaptive(&g, 0, 0, 10, 10, 0);
        CHECK(grid_insert(&g, 1, -25.0f, 4.0f, 0.5f) == 0);
        CHECK(grid_insert(&g, 2, 40.0f, 4.0f, 0.5f) == 0);
        CHECK(grid_insert(&g, 3, 4.0f, -30.0f, 0.5f) == 0);
        CHECK(grid_insert(&g, 4, 4.0f, 55.0f, 0.5f) == 0);
        CHECK(grid_insert(&g, 5, 5.0f, 5.0f, 0.5f) == 0);
        int out[8];
        CHECK(grid_query_circle(&g, -25.0f, 4.0f, 0.6f, out, 8) == 1);
        CHECK(grid_query_circle(&g, 40.0f, 4.0f, 0.6f, out, 8) == 1);
        CHECK(grid_query_circle(&g, 4.0f, 55.0f, 0.6f, out, 8) == 1);
        CHECK(grid_query_circle(&g, 0.0f, 0.0f, 1000.0f, out, 8) == 5);
        grid_free(&g);
    }

    /* --- NaN/inf rejection --- */
    {
        Grid g;
        grid_init_adaptive(&g, 0, 0, 10, 10, 0);
        float nan_ = 0.0f / 0.0f, inf_ = 1.0f / 0.0f;
        CHECK(grid_insert(&g, 1, nan_, 1.0f, 0.5f) == -1);
        CHECK(grid_insert(&g, 2, 1.0f, inf_, 0.5f) == -1);
        CHECK(grid_insert(&g, 3, 1.0f, 1.0f, nan_) == -1);
        int out[4];
        CHECK(grid_query_circle(&g, nan_, 0.0f, 1.0f, out, 4) == -1);
        CHECK(grid_query_circle(&g, 0.0f, 0.0f, inf_, out, 4) == -1);
        CHECK(grid_count(&g) == 0);
        grid_free(&g);
    }

    /* --- rebuild budget is respected, results stay exact on the stale layout --- */
    {
        enum { N = 50000 };
        Rec* a = (Rec*)malloc(sizeof(Rec) * (size_t)N);
        gen_items(a, N, 1);
        Grid g;
        grid_init_adaptive(&g, 0, 0, 120, 120, 0);
        grid_set_rebuild_budget(&g, 3);
        for (int i = 0; i < N; ++i) grid_insert(&g, i, a[i].x, a[i].y, a[i].r);
        CHECK(grid_rebuilds(&g) == 3);
        int out[256];
        for (int q = 0; q < 50; ++q) {
            float cx = (float)(rnd() * 120.0), cy = (float)(rnd() * 120.0);
            CHECK(grid_query_circle(&g, cx, cy, 3.0f, out, 256) == brute(a, N, cx, cy, 3.0f));
        }
        grid_free(&g);
        free(a);
    }

    /* --- arbitrary cap: returns the true count, truncates the output --- */
    {
        Grid g;
        grid_init(&g, 4, 4, 10, 0, 0);
        for (int i = 0; i < 10; ++i) grid_insert(&g, i, 5.0f + (float)i * 0.1f, 5.0f, 0.0f);
        int out[3];
        CHECK(grid_query_circle(&g, 5.0f, 5.0f, 5.0f, out, 3) == 10);
        grid_free(&g);
    }

    /* --- a large sprite whose centre is outside the query radius still counts;
     * this used to be missed because the old query only visited cells within
     * the query radius of the centre --- */
    {
        Grid g;
        grid_init(&g, 16, 16, 8.0f, 0, 0);
        CHECK(grid_insert(&g, 0, 11.0f, 0.0f, 10.0f) == 0); /* dist 11 == rad 1 + r 10 */
        int out[4];
        CHECK(grid_query_circle(&g, 0.0f, 0.0f, 1.0f, out, 4) == 1);
        grid_free(&g);
    }

    if (fails) return 1;
    return 0;
}
