#include "grid.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* ====================================================================== */
/* internals                                                              */
/* ====================================================================== */

#define IDMAP_BLOCK 256 /* ids per second-level block                       */
#define IDMAP_TOP   256 /* blocks across the full unsigned 32-bit id space */
#define NO_SLOT     (-1)

/* strict cell lookup: returns GRID_ERR_BOUNDS instead of edge-clamping, so
 * callers can distinguish a position outside the world. */
static int cell_strict(const Grid* g, float x, float y, int* out) {
    float fx = (x - g->minx) / g->cell;
    float fy = (y - g->miny) / g->cell;
    if (isnan(fx) || isnan(fy)) return GRID_ERR_BOUNDS;
    int cx = (int)fx, cy = (int)fy;
    if (cx < 0 || cx >= g->cols || cy < 0 || cy >= g->rows)
        return GRID_ERR_BOUNDS;
    *out = cy * g->cols + cx;
    return GRID_OK;
}

static int grow_slots(Grid* g, int need) {
    int ncap = g->cap ? g->cap : 64;
    while (ncap < need) ncap *= 2;
    int* np = (int*)realloc(g->prev,  sizeof(int) * (size_t)ncap);
    int* nn = (int*)realloc(g->next,  sizeof(int) * (size_t)ncap);
    int* nc = (int*)realloc(g->slotcell, sizeof(int) * (size_t)ncap);
    GridItem* ni = (GridItem*)realloc(g->items, sizeof(GridItem) * (size_t)ncap);
    if (!np || !nn || !nc || !ni) return GRID_ERR_INVALID;
    g->prev = np; g->next = nn; g->slotcell = nc; g->items = ni;
    for (int i = g->nslots; i < ncap; ++i) {
        g->slotcell[i] = -1;
        g->next[i] = g->free_head;
        g->free_head = i;
    }
    g->cap = ncap;
    return GRID_OK;
}

/* ---- direct id -> slot map (two-level, unsigned id key) -------------- */

static uint32_t id_key(int id) { return (uint32_t)id; }

static int idmap_get(const Grid* g, int id) {
    uint32_t k = id_key(id);
    int* blk = g->idmap[k / IDMAP_BLOCK];
    return blk ? blk[k % IDMAP_BLOCK] : NO_SLOT;
}

static int idmap_set(Grid* g, int id, int slot) {
    uint32_t k = id_key(id);
    uint32_t b = k / IDMAP_BLOCK;
    int* blk = g->idmap[b];
    if (!blk) {
        blk = (int*)malloc(sizeof(int) * IDMAP_BLOCK);
        if (!blk) return GRID_ERR_INVALID;
        for (int i = 0; i < IDMAP_BLOCK; ++i) blk[i] = NO_SLOT;
        g->idmap[b] = blk;
    }
    blk[k % IDMAP_BLOCK] = slot;
    return GRID_OK;
}

static void idmap_reset(Grid* g) {
    /* Clear every allocated block unconditionally. Relying on per-slot
     * state would miss an entry whose slot was already recycled, leaving a
     * stale id->slot mapping that later aliases a different item. */
    for (int b = 0; b < g->idmap_top; ++b) {
        if (g->idmap[b])
            for (int i = 0; i < IDMAP_BLOCK; ++i) g->idmap[b][i] = NO_SLOT;
    }
}

/* doubly-linked cell chains */
static void chain_unlink(Grid* g, int s) {
    int c = g->slotcell[s], p = g->prev[s], n = g->next[s];
    if (p != NO_SLOT) g->next[p] = n; else g->head[c] = n;
    if (n != NO_SLOT) g->prev[n] = p;
    g->prev[s] = g->next[s] = NO_SLOT;
}

static void chain_link_head(Grid* g, int s, int c) {
    int h = g->head[c];
    g->slotcell[s] = c;
    g->prev[s] = NO_SLOT;
    g->next[s] = h;
    if (h != NO_SLOT) g->prev[h] = s;
    g->head[c] = s;
}

static int alloc_slot(Grid* g) {
    if (g->free_head == NO_SLOT)
        if (grow_slots(g, g->nslots + 1) != GRID_OK) return -1;
    int s = g->free_head;
    g->free_head = g->next[s];
    return s;
}

static void reset_empty(Grid* g) {
    idmap_reset(g);
    for (int i = 0; i < g->cols * g->rows; ++i) g->head[i] = NO_SLOT;
    g->free_head = NO_SLOT;
    for (int i = 0; i < g->nslots; ++i) {
        g->slotcell[i] = -1;
        g->next[i] = g->free_head;
        g->free_head = i;
    }
    g->count = 0;
}

/* ====================================================================== */
/* lifecycle                                                              */
/* ====================================================================== */

int grid_init(Grid* g, int cols, int rows, float cell, float minx, float miny) {
    if (!g || cols <= 0 || rows <= 0 || cell <= 0.0f || isnan(cell))
        return GRID_ERR_INVALID;
    memset(g, 0, sizeof(*g));
    g->cols = cols; g->rows = rows; g->cell = cell;
    g->minx = minx; g->miny = miny;
    g->cap = 64; g->free_head = NO_SLOT;

    size_t ncell = (size_t)cols * (size_t)rows;
    g->head = (int*)malloc(sizeof(int) * ncell);
    g->prev = (int*)malloc(sizeof(int) * (size_t)g->cap);
    g->next = (int*)malloc(sizeof(int) * (size_t)g->cap);
    g->slotcell = (int*)malloc(sizeof(int) * (size_t)g->cap);
    g->items = (GridItem*)malloc(sizeof(GridItem) * (size_t)g->cap);
    g->idmap = (int**)calloc((size_t)IDMAP_TOP, sizeof(int*));
    g->scratch = NULL; g->scratch_cap = 0;
    if (!g->head || !g->prev || !g->next || !g->slotcell || !g->items || !g->idmap) {
        grid_free(g);
        return GRID_ERR_INVALID;
    }
    g->idmap_top = IDMAP_TOP;
    for (size_t i = 0; i < ncell; ++i) g->head[i] = NO_SLOT;
    for (int i = 0; i < g->cap; ++i) {
        g->slotcell[i] = -1;
        g->next[i] = g->free_head;
        g->free_head = i;
    }
    return GRID_OK;
}

void grid_free(Grid* g) {
    if (!g) return;
    if (g->idmap) {
        for (int b = 0; b < g->idmap_top; ++b) free(g->idmap[b]);
    }
    free(g->idmap);
    free(g->head); free(g->prev); free(g->next);
    free(g->slotcell); free(g->items); free(g->scratch);
    memset(g, 0, sizeof(*g));
}

/* ====================================================================== */
/* live maintenance                                                       */
/* ====================================================================== */

int grid_insert(Grid* g, int id, float x, float y, float r) {
    if (!g || r < 0.0f || isnan(r)) return GRID_ERR_INVALID;
    int c;
    int rc = cell_strict(g, x, y, &c);
    if (rc != GRID_OK) return rc;
    if (idmap_get(g, id) != NO_SLOT) return GRID_ERR_INVALID; /* duplicate id */

    int s = alloc_slot(g);
    if (s < 0) return GRID_ERR_INVALID;
    if (s >= g->nslots) g->nslots = s + 1;

    g->items[s].id = id; g->items[s].x = x; g->items[s].y = y; g->items[s].r = r;
    chain_link_head(g, s, c);
    if (idmap_set(g, id, s) != GRID_OK) {
        chain_unlink(g, s);
        g->slotcell[s] = -1;
        g->next[s] = g->free_head;
        g->free_head = s;
        return GRID_ERR_INVALID;
    }
    ++g->count;
    return GRID_OK;
}

int grid_move(Grid* g, int id, float x, float y) {
    if (!g) return GRID_ERR_INVALID;
    if (g->count == 0) return GRID_ERR_EMPTY;
    int c;
    int rc = cell_strict(g, x, y, &c);
    if (rc != GRID_OK) return rc;
    int s = idmap_get(g, id);
    if (s == NO_SLOT) return GRID_ERR_NOID;
    if (g->slotcell[s] == c) {
        g->items[s].x = x;
        g->items[s].y = y;
        return GRID_OK;            /* coordinates updated, chain untouched */
    }
    chain_unlink(g, s);
    g->items[s].x = x;
    g->items[s].y = y;
    chain_link_head(g, s, c);
    return GRID_MOVED_CELL;
}

int grid_remove(Grid* g, int id) {
    if (!g) return GRID_ERR_INVALID;
    int s = idmap_get(g, id);
    if (s == NO_SLOT) return GRID_ERR_NOID; /* covers empty grid AND repeated delete */
    chain_unlink(g, s);
    uint32_t k = id_key(id);
    g->idmap[k / IDMAP_BLOCK][k % IDMAP_BLOCK] = NO_SLOT;
    g->slotcell[s] = -1;
    g->next[s] = g->free_head;
    g->free_head = s;
    --g->count;
    return GRID_OK;
}

/* ====================================================================== */
/* full rebuild                                                           */
/* ====================================================================== */

int grid_rebuild(Grid* g, const GridItem* in, int n) {
    if (!g || n < 0 || (n > 0 && !in)) return GRID_ERR_INVALID;

    /* phase 1: validate everything before touching live state */
    for (int i = 0; i < n; ++i) {
        if (in[i].r < 0.0f || isnan(in[i].r)) return GRID_ERR_INVALID;
        int c;
        if (cell_strict(g, in[i].x, in[i].y, &c) != GRID_OK)
            return GRID_ERR_BOUNDS;
    }
    if (g->cap < n && grow_slots(g, n) != GRID_OK) return GRID_ERR_INVALID;

    /* phase 2: build from scratch (idmap blocks are retained, entries reset) */
    reset_empty(g);
    for (int i = 0; i < n; ++i) {
        int s = alloc_slot(g);
        if (s < 0) return GRID_ERR_INVALID;
        if (s >= g->nslots) g->nslots = s + 1;
        g->items[s] = in[i];
        int c;
        (void)cell_strict(g, in[i].x, in[i].y, &c);
        chain_link_head(g, s, c);
        if (idmap_set(g, in[i].id, s) != GRID_OK) return GRID_ERR_INVALID;
    }
    g->count = n;
    return GRID_OK;
}

/* ====================================================================== */
/* query                                                                  */
/* ====================================================================== */

static int slot_less(const Grid* g, int a, int b) {
    if (g->slotcell[a] != g->slotcell[b])
        return g->slotcell[a] < g->slotcell[b];
    return g->items[a].id < g->items[b].id;
}

/* in-place binary min-heap over slot indices, keyed by (cell, id) */
static void sift_down(const Grid* g, int* h, int n, int root) {
    int v = h[root];
    for (;;) {
        int k = root * 2 + 1;
        if (k >= n) break;
        if (k + 1 < n && slot_less(g, h[k + 1], h[k])) ++k;
        if (!slot_less(g, h[k], v)) break;
        h[root] = h[k];
        root = k;
    }
    h[root] = v;
}

int grid_query_circle(Grid* g, float cx, float cy, float rad, int* out, int cap) {
    if (!g || !out || rad < 0.0f || isnan(rad) || cap < 0) return GRID_ERR_INVALID;

    /* The scanned cell range is derived through the SAME forward
     * world->cell mapping used at insertion (truncation, then clamp).
     * Inverting the circle edge algebraically (dividing the edge offset by
     * cell size) can round to the neighbouring cell whenever an item sits
     * exactly on a cell boundary, silently dropping that whole cell. The
     * one-cell outward margin keeps the test conservative: it may scan an
     * extra cell, never miss one. */
    int c0x = (int)((cx - rad - g->minx) / g->cell) - 1;
    int c1x = (int)((cx + rad - g->minx) / g->cell) + 1;
    int c0y = (int)((cy - rad - g->miny) / g->cell) - 1;
    int c1y = (int)((cy + rad - g->miny) / g->cell) + 1;
    if (c0x < 0) c0x = 0;
    if (c0y < 0) c0y = 0;
    if (c1x >= g->cols) c1x = g->cols - 1;
    if (c1y >= g->rows) c1y = g->rows - 1;
    if (c0x > c1x || c0y > c1y) return 0;

    if (g->nslots > g->scratch_cap) {
        int nc = g->scratch_cap ? g->scratch_cap : 256;
        while (nc < g->nslots) nc *= 2;
        int* ns = (int*)realloc(g->scratch, sizeof(int) * (size_t)nc);
        if (!ns) return GRID_ERR_INVALID;
        g->scratch = ns; g->scratch_cap = nc;
    }

    int n = 0;
    for (int y = c0y; y <= c1y; ++y)
        for (int x = c0x; x <= c1x; ++x) {
            for (int s = g->head[y * g->cols + x]; s != NO_SLOT; s = g->next[s]) {
                float dx = g->items[s].x - cx, dy = g->items[s].y - cy;
                float rr = rad + g->items[s].r;
                if (dx * dx + dy * dy <= rr * rr) g->scratch[n++] = s;
            }
        }

    /* canonicalize order: heapsort by (cell, id) */
    for (int i = n / 2 - 1; i >= 0; --i) sift_down(g, g->scratch, n, i);
    int total = n;
    for (int pos = 0; pos < total; ++pos) {
        int s = g->scratch[0];
        if (pos < cap) out[pos] = g->items[s].id;
        g->scratch[0] = g->scratch[--n];
        if (n) sift_down(g, g->scratch, n, 0);
    }
    return total;
}

/* ====================================================================== */
/* introspection                                                          */
/* ====================================================================== */

int grid_count(const Grid* g) { return g ? g->count : 0; }

int grid_cells_used(const Grid* g) {
    if (!g) return 0;
    int used = 0;
    for (int i = 0; i < g->cols * g->rows; ++i) if (g->head[i] != NO_SLOT) ++used;
    return used;
}

void grid_clear(Grid* g) {
    if (!g) return;
    reset_empty(g);
}
