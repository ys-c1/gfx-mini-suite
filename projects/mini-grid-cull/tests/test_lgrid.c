#include "../src/lgrid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned rng = 777u;
static float frand(void) { rng = rng * 1103515245u + 12345u; return (float)((rng >> 8) & 0xFFFF) / 65535.0f; }

static int cmp_int(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }

static void test_errors(void) {
    char err[1024];
    LGrid lg;

    /* each violation reported on its own, with a reason */
    CHECK(lgrid_init(&lg, 2, 0.0f, 1.0f, 0, 0, 128, 128, err, sizeof err) == -1);
    CHECK(strstr(err, "cell width") != NULL);

    CHECK(lgrid_init(&lg, 2, 8.0f, -1.0f, 0, 0, 128, 128, err, sizeof err) == -1);
    CHECK(strstr(err, "density") != NULL);

    CHECK(lgrid_init(&lg, LGRID_MAX_LAYERS + 1, 8.0f, 1.0f, 0, 0, 128, 128, err, sizeof err) == -1);
    CHECK(strstr(err, "layers") != NULL);

    /* all three at once: every problem must appear in the report */
    CHECK(lgrid_init(&lg, LGRID_MAX_LAYERS + 1, 0.0f, -2.0f, 0, 0, 128, 128, err, sizeof err) == -1);
    CHECK(strstr(err, "cell width") != NULL);
    CHECK(strstr(err, "density") != NULL);
    CHECK(strstr(err, "layers") != NULL);

    /* valid init still succeeds */
    CHECK(lgrid_init(&lg, 2, 8.0f, 0.0f, 0, 0, 128, 128, err, sizeof err) == 0);
    lgrid_free(&lg);

    /* negative density on a live grid */
    CHECK(lgrid_init(&lg, 2, 8.0f, 0.0f, 0, 0, 128, 128, err, sizeof err) == 0);
    CHECK(lgrid_set_density(&lg, -0.5f, err, sizeof err) == -1);
    CHECK(strstr(err, "density") != NULL);
    lgrid_free(&lg);
}

static void test_equivalence(void) {
    /* layering off: results must match the existing plain Grid exactly */
    Grid g;
    LGrid lg;
    char err[256];
    CHECK(grid_init(&g, 16, 16, 8.0f, 0.0f, 0.0f) == 0);
    CHECK(lgrid_init(&lg, 1, 8.0f, 0.0f, 0, 0, 128, 128, err, sizeof err) == 0);
    CHECK(lg.layered == 0);

    int N = 400;
    float xs[400], ys[400], rs[400];
    for (int i = 0; i < N; ++i) {
        xs[i] = frand() * 128.0f; ys[i] = frand() * 128.0f; rs[i] = frand() * 2.0f;
        CHECK(grid_insert(&g, i, xs[i], ys[i], rs[i]) == 0);
        CHECK(lgrid_insert(&lg, i, xs[i], ys[i], rs[i]) == 0);
    }

    for (int q = 0; q < 50; ++q) {
        float cx = frand() * 128.0f, cy = frand() * 128.0f, rad = 4.0f + frand() * 12.0f;
        int oa[512], ob[512];
        int na = grid_query_circle(&g, cx, cy, rad, oa, 512);
        int nb = lgrid_query_circle(&lg, cx, cy, rad, ob, 512);
        CHECK(na == nb);
        qsort(oa, na < 512 ? na : 512, sizeof(int), cmp_int);
        qsort(ob, nb < 512 ? nb : 512, sizeof(int), cmp_int);
        CHECK(memcmp(oa, ob, sizeof(int) * (size_t)(na < 512 ? na : 512)) == 0);
    }

    /* enabling layering must not change query results either */
    lgrid_set_layered(&lg, 0); /* layers==1: stays flat, no-op */
    lgrid_free(&lg);

    LGrid lg2;
    CHECK(lgrid_init(&lg2, 2, 8.0f, 0.0f, 0, 0, 128, 128, err, sizeof err) == 0);
    for (int i = 0; i < N; ++i) CHECK(lgrid_insert(&lg2, i, xs[i], ys[i], rs[i]) == 0);
    CHECK(lgrid_set_density(&lg2, (float)N / (128.0f * 128.0f), err, sizeof err) == 0);
    for (int q = 0; q < 50; ++q) {
        float cx = frand() * 128.0f, cy = frand() * 128.0f, rad = 4.0f + frand() * 12.0f;
        int oa[512], ob[512];
        int na = grid_query_circle(&g, cx, cy, rad, oa, 512);
        int nb = lgrid_query_circle(&lg2, cx, cy, rad, ob, 512);
        CHECK(na == nb);
        qsort(oa, na < 512 ? na : 512, sizeof(int), cmp_int);
        qsort(ob, nb < 512 ? nb : 512, sizeof(int), cmp_int);
        CHECK(memcmp(oa, ob, sizeof(int) * (size_t)(na < 512 ? na : 512)) == 0);
    }
    lgrid_free(&lg2);
    grid_free(&g);
}

static void test_switch_log(void) {
    char err[256];
    LGrid lg;
    CHECK(lgrid_init(&lg, 2, 8.0f, 0.0f, 0, 0, 1024, 1024, err, sizeof err) == 0);
    CHECK(lgrid_switch_count(&lg) == 0);
    CHECK(lgrid_rebuilds(&lg) == 0);
    float cell0 = lgrid_cell(&lg);
    CHECK(cell0 == 8.0f); /* density 0 -> base cell */

    for (int i = 0; i < 200; ++i)
        CHECK(lgrid_insert(&lg, i, frand() * 1024.0f, frand() * 1024.0f, 1.0f) == 0);

    /* higher density -> smaller cell, one logged switch, one rebuild */
    CHECK(lgrid_set_density(&lg, 0.5f, err, sizeof err) == 0);
    CHECK(lgrid_switch_count(&lg) == 1);
    CHECK(lgrid_rebuilds(&lg) == 1);
    float cell1 = lgrid_cell(&lg);
    CHECK(cell1 < cell0);
    const LGridSwitch* s = lgrid_switches(&lg);
    CHECK(s != NULL);
    CHECK(s[0].seq == 1);
    CHECK(s[0].old_cell == cell0);
    CHECK(s[0].new_cell == cell1);
    CHECK(s[0].density == 0.5f);
    CHECK(s[0].items == 200);

    /* lower density -> larger cell, second switch logged */
    CHECK(lgrid_set_density(&lg, 0.001f, err, sizeof err) == 0);
    CHECK(lgrid_switch_count(&lg) == 2);
    CHECK(lgrid_rebuilds(&lg) == 2);
    CHECK(lgrid_cell(&lg) > cell1);

    /* tiny density change below the 5% threshold: no rebuild */
    float d = 0.001f;
    CHECK(lgrid_set_density(&lg, d * 1.01f, err, sizeof err) == 0);
    CHECK(lgrid_rebuilds(&lg) == 2);

    /* switching layering off returns to the base cell and is logged */
    lgrid_set_layered(&lg, 0);
    CHECK(lgrid_cell(&lg) == 8.0f);
    CHECK(lgrid_switch_count(&lg) == 3);

    /* items survive every rebuild */
    CHECK(lgrid_count(&lg) == 200);
    lgrid_free(&lg);
}

int main(void) {
    test_errors();
    test_equivalence();
    test_switch_log();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
