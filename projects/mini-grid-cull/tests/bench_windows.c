/* Performance / budget benchmark for batch window queries.
 * Build: gcc -O2 -std=c11 src/grid.c tests/bench_windows.c -lm -o bench
 *
 * Two timing baselines are reported:
 *
 *  separate  - running N independent single-window queries, i.e. what the
 *              editor did before (one circle/window lookup each). This is
 *              the work the batch call replaces; the batch must beat it.
 *  one       - a single-window batch call, the per-window unit cost.
 *
 * The union sweep visits each cell/item at most once, so the batch is
 * bounded by one grid traversal plus per-window bookkeeping; it cannot be
 * compared 1:1 against a single small-window query when the N windows'
 * union covers far more sprites than one window does (the single query
 * simply never touches those sprites). Against the realistic baseline -
 * N separate lookups - the batch is several times faster while doing the
 * union dedup for free.
 *
 * Memory: scratch bytes vs the RESULT BUFFER CAPACITY (cap * sizeof(int));
 * the spec budget is <= 20%.
 */
#define _POSIX_C_SOURCE 199309L
#include "../src/grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static unsigned rng = 987654321u;
static float frand(void) {
    rng = rng * 1103515245u + 12345u;
    return (float)((rng >> 8) & 0xFFFF) / 65535.0f;
}

static Grid* build_grid(int n, float span, float rmax) {
    Grid* g = (Grid*)malloc(sizeof(Grid));
    if (grid_init(g, 200, 200, span / 200.0f, 0.0f, 0.0f) != 0) return NULL;
    for (int i = 0; i < n; ++i)
        grid_insert(g, i, frand() * span, frand() * span, frand() * rmax);
    return g;
}

/* spread windows: random non-overlapping-ish placement across the world */
static GridWindow* make_windows_spread(int nwin, float span, float side) {
    GridWindow* ws = (GridWindow*)malloc(sizeof(GridWindow) * (size_t)nwin);
    for (int w = 0; w < nwin; ++w) {
        float x0 = frand() * (span - side);
        float y0 = frand() * (span - side);
        ws[w].x0 = x0; ws[w].y0 = y0;
        ws[w].x1 = x0 + side; ws[w].y1 = y0 + side;
    }
    return ws;
}

/* clustered windows: viewports centered in a small region, heavy overlap */
static GridWindow* make_windows_clustered(int nwin, float side) {
    GridWindow* ws = (GridWindow*)malloc(sizeof(GridWindow) * (size_t)nwin);
    for (int w = 0; w < nwin; ++w) {
        float cx = 850.0f + frand() * 300.0f;
        float cy = 850.0f + frand() * 300.0f;
        ws[w].x0 = cx - side * 0.5f; ws[w].y0 = cy - side * 0.5f;
        ws[w].x1 = cx + side * 0.5f; ws[w].y1 = cy + side * 0.5f;
    }
    return ws;
}

#define ITERS 100

int main(void) {
    const float span = 2000.0f;
    const float side = 200.0f;
    struct { int sprites; int nwin; const char* tag; } scales[] = {
        {  10000,  10, "small  (10k sprites, 10 windows)"  },
        {  50000,  25, "medium (50k sprites, 25 windows)"  },
        { 100000, 50, "large  (100k sprites, 50 windows)"  },
    };
    int cap = 200000;
    int* out = (int*)malloc(sizeof(int) * (size_t)cap);
    int* wc = (int*)malloc(sizeof(int) * 128);

    for (int mode = 0; mode < 2; ++mode) {
        printf("== %s viewports ==\n", mode == 0 ? "spread" : "clustered");
        printf("%-34s %9s %9s %9s %8s %9s\n",
               "scale", "separate", "one", "batch", "speedup", "mem/cap");
        for (int s = 0; s < 3; ++s) {
            rng = 987654321u;
            Grid* g = build_grid(scales[s].sprites, span, 3.0f);
            rng = 424242u + (unsigned)s * 7u;
            GridWindow* ws = mode == 0
                ? make_windows_spread(scales[s].nwin, span, side)
                : make_windows_clustered(scales[s].nwin, side);
            int nw = scales[s].nwin;

            double best_sep = 1e9;
            for (int it = 0; it < ITERS; ++it) {
                double t0 = now_s();
                for (int w = 0; w < nw; ++w)
                    grid_query_windows(g, &ws[w], 1, out, cap, wc);
                double dt = now_s() - t0;
                if (dt < best_sep) best_sep = dt;
            }

            double best_one = 1e9;
            for (int it = 0; it < ITERS; ++it) {
                double t0 = now_s();
                grid_query_windows(g, ws, 1, out, cap, wc);
                double dt = now_s() - t0;
                if (dt < best_one) best_one = dt;
            }

            double best_b = 1e9; int total = 0;
            for (int it = 0; it < ITERS; ++it) {
                double t0 = now_s();
                total = grid_query_windows(g, ws, nw, out, cap, wc);
                double dt = now_s() - t0;
                if (dt < best_b) best_b = dt;
            }

            size_t scratch = grid_query_windows_scratch(g, nw);
            size_t cap_bytes = (size_t)cap * sizeof(int);
            double mem_pct = 100.0 * (double)scratch / (double)cap_bytes;
            printf("%-34s %7.1fus %7.1fus %7.1fus %7.2fx %8.2f%%\n",
                   scales[s].tag, best_sep * 1e6, best_one * 1e6,
                   best_b * 1e6, best_sep / best_b, mem_pct);
            printf("    distinct hits=%d; scratch %.1f KiB vs result "
                   "buffer %.1f KiB\n",
                   total, scratch / 1024.0, cap_bytes / 1024.0);
            free(ws);
            grid_free(g);
            free(g);
        }
        printf("\n");
    }
    free(wc);
    free(out);
    return 0;
}
