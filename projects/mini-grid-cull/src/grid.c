#include "grid.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Adaptive layout
 * ---------------
 * Cell size tracks the current density: the target edge is
 * sqrt(arena_area * TARGET_ITEMS_PER_CELL / n), so a cell holds about
 * TARGET_ITEMS_PER_CELL items on average. A relayout ("rebuild") happens only
 * when the current edge leaves [HYST_LO, HYST_HI] x the target (2x hysteresis).
 * Rebuild cost is bounded three ways:
 *   1. never more than GRID_MAX_CELLS cells, so one relayout is
 *      O(count + GRID_MAX_CELLS) worst case;
 *   2. hysteresis means the number of relayouts is O(log2(n)) for a growing
 *      item set, not O(n);
 *   3. the caller can cap relayouts per epoch (grid_set_rebuild_budget).
 * Queries stay exact whatever the cell size: candidates are tested with a
 * circle-circle overlap, so results do not depend on where cells fall.
 */
#define TARGET_ITEMS_PER_CELL 4.0f
#define HYST_LO 0.5f   /* rebuild when target < cell * HYST_LO  (too dense)  */
#define HYST_HI 2.0f   /* rebuild when target > cell * HYST_HI  (too sparse) */

static float arena_area(const Grid* g) {
    float w = g->maxx - g->minx, h = g->maxy - g->miny;
    if (!(w > 0.0f)) w = 1.0f;
    if (!(h > 0.0f)) h = 1.0f;
    return w * h;
}

/* Desired edge length for the current item count and arena. */
static float target_cell(const Grid* g) {
    float n = (float)g->count;
    if (n <= 0.0f) return g->cell;
    float edge = sqrtf(arena_area(g) * TARGET_ITEMS_PER_CELL / n);
    if (edge < GRID_MIN_CELL) edge = GRID_MIN_CELL;
    if (edge > GRID_MAX_CELL) edge = GRID_MAX_CELL;
    return edge;
}

/* Fit cols x rows for `edge` over the arena without exceeding GRID_MAX_CELLS.
 * If the arena would need too many cells, the edge is grown (never the grid
 * shrunk below the arena) so the grid always covers [min, max]. */
static void layout_for(const Grid* g, float edge, int* cols, int* rows, float* out_cell) {
    float w = g->maxx - g->minx, h = g->maxy - g->miny;
    if (!(w > 0.0f)) w = 1.0f;
    if (!(h > 0.0f)) h = 1.0f;
    if (edge < GRID_MIN_CELL) edge = GRID_MIN_CELL;
    if (edge > GRID_MAX_CELL) edge = GRID_MAX_CELL;
    int c = (int)ceilf(w / edge);
    int r = (int)ceilf(h / edge);
    if (c < 1) c = 1;
    if (r < 1) r = 1;
    while ((double)c * (double)r > (double)GRID_MAX_CELLS) {
        edge *= 1.25f;              /* grow the cell, then refit both axes */
        c = (int)ceilf(w / edge);
        r = (int)ceilf(h / edge);
        if (c < 1) c = 1;
        if (r < 1) r = 1;
    }
    /* the grid must cover the arena: edge may need rounding up to w/c, h/r */
    float need = w / (float)c;
    if (h / (float)r > need) need = h / (float)r;
    if (need > edge) edge = need;
    *cols = c;
    *rows = r;
    *out_cell = edge;
}

static int cell_index(const Grid* g, float x, float y) {
    int cx = (int)floorf((x - g->minx) / g->cell);
    int cy = (int)floorf((y - g->miny) / g->cell);
    if (cx < 0) cx = 0;
    if (cx >= g->cols) cx = g->cols - 1;
    if (cy < 0) cy = 0;
    if (cy >= g->rows) cy = g->rows - 1;
    return cy * g->cols + cx;
}

/* Allocate cols x rows head array, reset count, re-bin every stored item.
 * Items are kept in insertion order in g->items; only the chains change. */
static int relayout(Grid* g, int cols, int rows, float cell) {
    int* head = (int*)malloc(sizeof(int) * (size_t)cols * (size_t)rows);
    if (!head) return -1;
    free(g->head);
    g->head = head;
    g->cols = cols;
    g->rows = rows;
    g->cell = cell;
    int n = cols * rows;
    for (int i = 0; i < n; ++i) head[i] = -1;
    for (int i = g->count - 1; i >= 0; --i) {  /* reverse keeps query order stable-ish */
        int c = cell_index(g, g->items[i].x, g->items[i].y);
        g->next[i] = head[c];
        head[c] = i;
    }
    g->relinked += g->count;
    ++g->rebuilds;
    ++g->epoch_rebuilds;
    return 0;
}

/* Compute the edge the current count wants; relayout if it is outside the
 * hysteresis band and the budget allows. Returns 0 on success, -1 on alloc
 * failure; a budget stop is not an error. */
static int maybe_rebuild(Grid* g) {
    if (!g->adaptive) return 0;
    float want = target_cell(g);
    if (want <= 0.0f) return 0;
    int too_dense = want < g->cell * HYST_LO;
    int too_sparse = want > g->cell * HYST_HI;
    if (!too_dense && !too_sparse) return 0;
    if (g->budget >= 0 && g->epoch_rebuilds >= g->budget) return 0;
    int cols, rows;
    float cell;
    layout_for(g, want, &cols, &rows, &cell);
    if (cols == g->cols && rows == g->rows && cell == g->cell) return 0;
    return relayout(g, cols, rows, cell);
}

/* Grow the arena by powers of two until (x,y) is inside. Growing invalidates
 * the old grid, so this always counts as a rebuild. */
static int ensure_inside(Grid* g, float x, float y) {
    if (!g->adaptive) return 0;
    int grew = 0;
    while (x < g->minx) { float w = g->maxx - g->minx; g->minx -= w > 0 ? w : 1.0f; grew = 1; }
    while (y < g->miny) { float h = g->maxy - g->miny; g->miny -= h > 0 ? h : 1.0f; grew = 1; }
    while (x > g->maxx) { float w = g->maxx - g->minx; g->maxx += w > 0 ? w : 1.0f; grew = 1; }
    while (y > g->maxy) { float h = g->maxy - g->miny; g->maxy += h > 0 ? h : 1.0f; grew = 1; }
    if (!grew) return 0;
    /* arena change: relayout at the size the new arena wants */
    float want = target_cell(g);
    if (want <= 0.0f) want = g->cell;
    int cols, rows;
    float cell;
    layout_for(g, want, &cols, &rows, &cell);
    return relayout(g, cols, rows, cell);
}

static int grow_items(Grid* g) {
    if (g->count < g->cap) return 0;
    int ncap = g->cap ? g->cap * 2 : 64;
    int* nn = (int*)realloc(g->next, sizeof(int) * (size_t)ncap);
    if (!nn) return -1;
    g->next = nn;
    GridItem* ni = (GridItem*)realloc(g->items, sizeof(GridItem) * (size_t)ncap);
    if (!ni) return -1;
    g->items = ni;
    g->cap = ncap;
    return 0;
}

/* Restart layout state shared by init paths. */
static int init_common(Grid* g) {
    g->count = 0;
    g->cap = 64;
    g->maxr = 0.0f;
    g->rebuilds = 0;
    g->epoch_rebuilds = 0;
    g->budget = GRID_DEFAULT_REBUILD_BUDGET;
    g->relinked = 0;
    g->head = NULL;
    g->next = (int*)malloc(sizeof(int) * (size_t)g->cap);
    g->items = (GridItem*)malloc(sizeof(GridItem) * (size_t)g->cap);
    if (!g->next || !g->items) { grid_free(g); return -1; }
    return 0;
}

int grid_init(Grid* g, int cols, int rows, float cell, float minx, float miny) {
    if (!g || cols <= 0 || rows <= 0 || cell <= 0.0f) return -1;
    if (init_common(g) != 0) return -1;
    g->adaptive = 0;
    g->minx = minx; g->miny = miny;
    g->maxx = minx + cell * (float)cols;
    g->maxy = miny + cell * (float)rows;
    if (relayout(g, cols, rows, cell) != 0) return -1;
    g->rebuilds = 0; g->epoch_rebuilds = 0; g->relinked = 0;
    return 0;
}

int grid_init_adaptive(Grid* g, float minx, float miny, float maxx, float maxy,
                       float cell_hint) {
    if (!g || !(maxx > minx) || !(maxy > miny)) return -1;
    if (init_common(g) != 0) return -1;
    g->adaptive = 1;
    g->minx = minx; g->miny = miny; g->maxx = maxx; g->maxy = maxy;
    int cols, rows;
    float cell;
    if (cell_hint > 0.0f) {
        layout_for(g, cell_hint, &cols, &rows, &cell);
    } else {
        /* single cell; the first inserts will size it */
        cols = rows = 1;
        cell = maxx - minx > maxy - miny ? maxx - minx : maxy - miny;
    }
    if (relayout(g, cols, rows, cell) != 0) return -1;
    g->rebuilds = 0; g->epoch_rebuilds = 0; g->relinked = 0;
    return 0;
}

void grid_free(Grid* g) {
    if (!g) return;
    free(g->head); free(g->next); free(g->items);
    g->head = NULL; g->next = NULL; g->items = NULL;
    g->count = 0; g->cap = 0; g->cols = g->rows = 0;
}

int grid_insert(Grid* g, int id, float x, float y, float r) {
    if (!g || !isfinite(x) || !isfinite(y) || !isfinite(r) || r < 0.0f) return -1;
    if (grow_items(g) != 0) return -1;
    if (ensure_inside(g, x, y) != 0) return -1;
    int idx = g->count++;
    g->items[idx].id = id; g->items[idx].x = x; g->items[idx].y = y; g->items[idx].r = r;
    if (r > g->maxr) g->maxr = r;
    int c = cell_index(g, x, y);
    g->next[idx] = g->head[c];
    g->head[c] = idx;
    return maybe_rebuild(g);
}

int grid_query_circle(const Grid* g, float cx, float cy, float rad, int* out, int cap) {
    if (!g || !out || !isfinite(cx) || !isfinite(cy) || !isfinite(rad) || rad < 0.0f)
        return -1;
    /* Items are binned by centre only, so a match's centre lies within
     * rad + its radius <= rad + maxr of the query centre, whatever the layout.
     * Which cells this covers depends on the cell size; which items match does
     * not, so adaptive and fixed grids return the same set. */
    float reach = rad + g->maxr;
    int c0x = (int)floorf((cx - reach - g->minx) / g->cell);
    int c1x = (int)floorf((cx + reach - g->minx) / g->cell);
    int c0y = (int)floorf((cy - reach - g->miny) / g->cell);
    int c1y = (int)floorf((cy + reach - g->miny) / g->cell);
    if (c0x < 0) c0x = 0;
    if (c0y < 0) c0y = 0;
    if (c1x >= g->cols) c1x = g->cols - 1;
    if (c1y >= g->rows) c1y = g->rows - 1;
    int n = 0;
    for (int y = c0y; y <= c1y; ++y)
        for (int x = c0x; x <= c1x; ++x)
            for (int i = g->head[y * g->cols + x]; i != -1; i = g->next[i]) {
                float dx = g->items[i].x - cx, dy = g->items[i].y - cy;
                float rr = rad + g->items[i].r;
                if (dx * dx + dy * dy <= rr * rr) {
                    if (n < cap) out[n] = g->items[i].id;
                    ++n;
                }
            }
    return n;
}

int grid_count(const Grid* g) { return g ? g->count : 0; }

int grid_cells_used(const Grid* g) {
    if (!g) return 0;
    int used = 0, n = g->cols * g->rows;
    for (int i = 0; i < n; ++i) if (g->head[i] != -1) ++used;
    return used;
}

void grid_clear(Grid* g) {
    if (!g) return;
    int n = g->cols * g->rows;
    for (int i = 0; i < n; ++i) g->head[i] = -1;
    g->count = 0;
    g->maxr = 0.0f;
    g->rebuilds = 0;
    g->epoch_rebuilds = 0;
    g->relinked = 0;
}

int grid_cells(const Grid* g) { return g ? g->cols * g->rows : 0; }
float grid_cell_size(const Grid* g) { return g ? g->cell : 0.0f; }
int grid_rebuilds(const Grid* g) { return g ? g->rebuilds : 0; }
int grid_relinked(const Grid* g) { return g ? g->relinked : 0; }

void grid_set_rebuild_budget(Grid* g, int per_epoch) {
    if (g) g->budget = per_epoch;
}

void grid_new_epoch(Grid* g) {
    if (g) g->epoch_rebuilds = 0;
}
