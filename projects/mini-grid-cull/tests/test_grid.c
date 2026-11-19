#include "../src/grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned rng = 12345u;
static float frand(void) { rng = rng * 1103515245u + 12345u; return (float)((rng >> 8) & 0xFFFF) / 65535.0f; }

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

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
