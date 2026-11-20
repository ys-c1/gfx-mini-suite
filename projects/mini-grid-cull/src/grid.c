#include "grid.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

int grid_init(Grid* g, int cols, int rows, float cell, float minx, float miny) {
    if (!g || cols <= 0 || rows <= 0 || cell <= 0.0f) return -1;
    g->cols = cols; g->rows = rows; g->cell = cell;
    g->minx = minx; g->miny = miny;
    g->count = 0; g->cap = 64;
    g->maxr = 0.0f;
    g->head = (int*)malloc(sizeof(int) * (size_t)(cols * rows));
    g->next = (int*)malloc(sizeof(int) * (size_t)g->cap);
    g->items = (GridItem*)malloc(sizeof(GridItem) * (size_t)g->cap);
    if (!g->head || !g->next || !g->items) { grid_free(g); return -1; }
    for (int i = 0; i < cols * rows; ++i) g->head[i] = -1;
    return 0;
}

void grid_free(Grid* g) {
    if (!g) return;
    free(g->head); free(g->next); free(g->items);
    g->head = NULL; g->next = NULL; g->items = NULL;
    g->count = 0; g->cap = 0;
    g->maxr = 0.0f;
}

static int cell_of(const Grid* g, float x, float y) {
    int cx = (int)((x - g->minx) / g->cell);
    int cy = (int)((y - g->miny) / g->cell);
    if (cx < 0) cx = 0; if (cx >= g->cols) cx = g->cols - 1;
    if (cy < 0) cy = 0; if (cy >= g->rows) cy = g->rows - 1;
    return cy * g->cols + cx;
}

int grid_insert(Grid* g, int id, float x, float y, float r) {
    if (!g || r < 0.0f) return -1;
    if (g->count == g->cap) {
        int ncap = g->cap * 2;
        int* nn = (int*)realloc(g->next, sizeof(int) * (size_t)ncap);
        GridItem* ni = (GridItem*)realloc(g->items, sizeof(GridItem) * (size_t)ncap);
        if (!nn || !ni) return -1;
        g->next = nn; g->items = ni; g->cap = ncap;
    }
    int idx = g->count++;
    g->items[idx].id = id; g->items[idx].x = x; g->items[idx].y = y; g->items[idx].r = r;
    if (r > g->maxr) g->maxr = r;
    int c = cell_of(g, x, y);
    g->next[idx] = g->head[c];
    g->head[c] = idx;
    return 0;
}

int grid_query_circle(const Grid* g, float cx, float cy, float rad, int* out, int cap) {
    if (!g || !out || rad < 0.0f) return -1;
    int c0x = (int)((cx - rad - g->minx) / g->cell);
    int c1x = (int)((cx + rad - g->minx) / g->cell);
    int c0y = (int)((cy - rad - g->miny) / g->cell);
    int c1y = (int)((cy + rad - g->miny) / g->cell);
    if (c0x < 0) c0x = 0; if (c0y < 0) c0y = 0;
    if (c1x >= g->cols) c1x = g->cols - 1;
    if (c1y >= g->rows) c1y = g->rows - 1;
    int n = 0;
    for (int y = c0y; y <= c1y; ++y)
        for (int x = c0x; x <= c1x; ++x) {
            for (int i = g->head[y * g->cols + x]; i != -1; i = g->next[i]) {
                float dx = g->items[i].x - cx, dy = g->items[i].y - cy;
                float d2 = dx * dx + dy * dy;
                float rr = rad + g->items[i].r;
                if (d2 <= rr * rr) {
                    if (n < cap) out[n] = g->items[i].id;
                    ++n;
                }
            }
        }
    return n;
}

int grid_count(const Grid* g) { return g ? g->count : 0; }

int grid_cells_used(const Grid* g) {
    if (!g) return 0;
    int used = 0;
    for (int i = 0; i < g->cols * g->rows; ++i) if (g->head[i] != -1) ++used;
    return used;
}

void grid_clear(Grid* g) {
    if (!g) return;
    for (int i = 0; i < g->cols * g->rows; ++i) g->head[i] = -1;
    g->count = 0;
    g->maxr = 0.0f;
}

/* ===================== batch window query ===================== */

#define GRID_MAX_WINDOWS 32767

/* circle (x,y,r) vs inclusive rect [x0,x1]x[y0,y1]; boundary counts,
   matching the <= convention of grid_query_circle. */
static int circle_hits_rect(float x, float y, float r,
                            float x0, float y0, float x1, float y1) {
    float nx = x < x0 ? x0 : (x > x1 ? x1 : x);
    float ny = y < y0 ? y0 : (y > y1 ? y1 : y);
    float dx = x - nx, dy = y - ny;
    return dx * dx + dy * dy <= r * r;
}

/* bits needed to hold 0..nwin inclusive (0 = unvisited, window w stored as
   w+1), hence the comparison against nwin+1, not nwin */
static int stamp_bits(int nwin) {
    int b = 1;
    while (b < 16 && (1 << b) < nwin + 1) ++b;
    return b;
}

/* packed LSB-first stamps, one per item; value 0 means "unvisited",
   window index w is stored as w+1. Field width b is in 1..16. */
static unsigned stamp_get(const uint32_t* p, int b, int i) {
    size_t bit = (size_t)i * (size_t)b;
    size_t word = bit >> 5;
    int sh = (int)(bit & 31);
    uint32_t mask = (1u << b) - 1u;
    /* assemble via 64-bit window: reads p[word] and p[word+1] safely */
    uint64_t pair = (uint64_t)p[word]
                  | ((sh + b > 32) ? (uint64_t)p[word + 1] << 32 : 0);
    return (unsigned)((pair >> sh) & mask);
}
static void stamp_set(uint32_t* p, int b, int i, unsigned v) {
    size_t bit = (size_t)i * (size_t)b;
    size_t word = bit >> 5;
    int sh = (int)(bit & 31);
    uint64_t mask = ((uint64_t)1 << b) - 1;
    uint64_t pair = (uint64_t)p[word]
                  | ((sh + b > 32) ? (uint64_t)p[word + 1] << 32 : 0);
    pair = (pair & ~(mask << sh)) | ((uint64_t)(v & mask) << sh);
    p[word] = (uint32_t)pair;
    if (sh + b > 32) p[word + 1] = (uint32_t)(pair >> 32);
}

static int valid_rect(const GridWindow* w) {
    return w->x0 <= w->x1 && w->y0 <= w->y1
        && isfinite(w->x0) && isfinite(w->y0)
        && isfinite(w->x1) && isfinite(w->y1);
}

size_t grid_query_windows_scratch(const Grid* g, int nwin) {
    if (!g || nwin <= 0) return 0;
    int b = stamp_bits(nwin);
    size_t stamp = ((size_t)g->count * (size_t)b + 31) / 32 * 4 + 4;
    size_t box = sizeof(int) * (size_t)nwin * 4;
    size_t hist = sizeof(int) * (size_t)nwin;
    size_t extra = (nwin > 64) ? sizeof(int) * (size_t)nwin * 2 : 0;
    return stamp + box + hist + extra;
}

int grid_query_windows(const Grid* g, const GridWindow* wins, int nwin,
                       int* out, int cap, int* win_count) {
    if (!g || cap < 0) return -1;
    if (nwin == 0) return 0;
    if (!wins || nwin < 0 || nwin > GRID_MAX_WINDOWS) return -1;
    for (int w = 0; w < nwin; ++w)
        if (!valid_rect(&wins[w])) return -1;
    if (win_count) for (int w = 0; w < nwin; ++w) win_count[w] = 0;

    const int N = g->count;
    const int b = stamp_bits(nwin);

    uint32_t* base = NULL;
    uint32_t* stamp = NULL;
    int* bx0 = NULL; int* by0 = NULL; int* bx1 = NULL; int* by1 = NULL;
    int* hist = NULL; int* active = NULL; int* joined = NULL;
    {
        size_t wcnt = (size_t)nwin;
        size_t n_stamp = N > 0
                       ? ((size_t)N * (size_t)b + 31) / 32 + 1 : 0;
        size_t n_box = wcnt * 4;
        size_t n_hist = wcnt;
        /* generic (>64 windows) sweep needs two per-window index buffers */
        size_t n_extra = nwin > 64 ? wcnt * 2 : 0;
        base = (uint32_t*)calloc(n_stamp + n_box + n_hist + n_extra,
                                 sizeof(uint32_t));
        if (!base) return -1;
        stamp = n_stamp ? base : NULL;
        int* im = (int*)(base + n_stamp);
        bx0 = im; by0 = im + nwin; bx1 = im + 2 * nwin; by1 = im + 3 * nwin;
        hist = im + 4 * nwin;
        if (nwin > 64) {
            active = im + 5 * nwin;
            joined = im + 6 * nwin;
        }
    }

    /* conservative cell boxes, inflated by the largest sprite radius so
       any item that can intersect a window is reachable in one traversal */
    float ux0 = wins[0].x0, uy0 = wins[0].y0;
    float ux1 = wins[0].x1, uy1 = wins[0].y1;
    for (int w = 0; w < nwin; ++w) {
        ux0 = fminf(ux0, wins[w].x0); uy0 = fminf(uy0, wins[w].y0);
        ux1 = fmaxf(ux1, wins[w].x1); uy1 = fmaxf(uy1, wins[w].y1);
        int cx0 = (int)floorf((wins[w].x0 - g->maxr - g->minx) / g->cell);
        int cy0 = (int)floorf((wins[w].y0 - g->maxr - g->miny) / g->cell);
        int cx1 = (int)floorf((wins[w].x1 + g->maxr - g->minx) / g->cell);
        int cy1 = (int)floorf((wins[w].y1 + g->maxr - g->miny) / g->cell);
        if (cx0 < 0) cx0 = 0;
        if (cy0 < 0) cy0 = 0;
        if (cx1 >= g->cols) cx1 = g->cols - 1;
        if (cy1 >= g->rows) cy1 = g->rows - 1;
        bx0[w] = cx0; by0[w] = cy0; bx1[w] = cx1; by1[w] = cy1;
    }
    int row0 = (int)floorf((uy0 - g->maxr - g->miny) / g->cell);
    int row1 = (int)floorf((uy1 + g->maxr - g->miny) / g->cell);
    if (row0 < 0) row0 = 0;
    if (row1 >= g->rows) row1 = g->rows - 1;

    const int fast = nwin <= 64;

    /* Event-driven sweep line. Each window contributes enter/leave events
       on its inflated cell box boundaries. Row and cell coverage masks are
       maintained incrementally, so a cell costs O(1) mask updates plus work
       proportional only to the windows actually covering it -- never to the
       total window count. Each cell/item is therefore examined at most once. */
    if (fast) {
        /* per-column event masks, indexed by cell column / row */
        uint64_t* xenter = (uint64_t*)calloc((size_t)g->cols + 1, sizeof(uint64_t));
        uint64_t* xleave = (uint64_t*)calloc((size_t)g->cols + 1, sizeof(uint64_t));
        uint64_t* yenter = (uint64_t*)calloc((size_t)g->rows + 1, sizeof(uint64_t));
        uint64_t* yleave = (uint64_t*)calloc((size_t)g->rows + 1, sizeof(uint64_t));
        if ((!xenter || !xleave || !yenter || !yleave)) {
            free(xenter); free(xleave); free(yenter); free(yleave);
            free(base);
            return -1;
        }
        for (int w = 0; w < nwin; ++w) {
            uint64_t bit = 1ULL << w;
            xenter[bx0[w]] |= bit;
            xleave[bx1[w] + 1] |= bit;   /* expire just past last cell */
            yenter[by0[w]] |= bit;
            yleave[by1[w] + 1] |= bit;
        }
        uint64_t rowmask = 0;
        for (int y = row0; y <= row1; ++y) {
            rowmask &= ~yleave[y];
            rowmask |= yenter[y];
            if (rowmask == 0) continue;
            /* restrict the x sweep to the union x-span of windows live on
               this row so empty columns are never visited */
            int xlo = g->cols, xhi = -1;
            uint64_t rr = rowmask;
            while (rr) {
                int w = (int)__builtin_ctzll(rr);
                rr &= rr - 1;
                if (bx0[w] < xlo) xlo = bx0[w];
                if (bx1[w] > xhi) xhi = bx1[w];
            }
            uint64_t cellmask = 0;   /* rebuilt per row; no cross-row carry */
            for (int x = xlo; x <= xhi; ++x) {
                cellmask &= ~xleave[x];
                cellmask |= xenter[x];
                cellmask &= rowmask;      /* drop windows not on this row */
                if (cellmask == 0) continue;
                int ci = y * g->cols + x;
                if (g->head[ci] == -1) continue;
                /* windows whose (uninflated) rect fully contains this cell:
                   every item centered here is inside the rect with distance
                   0, hence an exact hit for any radius -- no float test */
                float cwest = g->minx + x * g->cell;
                float ceast = cwest + g->cell;
                float cnorth = g->miny + y * g->cell;
                float csouth = cnorth + g->cell;
                uint64_t inside = 0, e = cellmask;
                while (e) {
                    int w = (int)__builtin_ctzll(e);
                    e &= e - 1;
                    if (cwest >= wins[w].x0 && ceast <= wins[w].x1
                     && cnorth >= wins[w].y0 && csouth <= wins[w].y1)
                        inside |= 1ULL << w;
                }
                uint64_t border = cellmask & ~inside;
                for (int i = g->head[ci]; i != -1; i = g->next[i]) {
                    float px = g->items[i].x, py = g->items[i].y;
                    float pr = g->items[i].r;
                    uint64_t hit = inside;       /* all distance-0 hits */
                    /* exact circle-rect test only for border windows */
                    e = border;
                    while (e) {
                        int w = (int)__builtin_ctzll(e);
                        e &= e - 1;
                        if (circle_hits_rect(px, py, pr,
                                wins[w].x0, wins[w].y0,
                                wins[w].x1, wins[w].y1))
                            hit |= 1ULL << w;
                    }
                    if (!hit) continue;
                    int fw = (int)__builtin_ctzll(hit); /* smallest index */
                    if (stamp) stamp_set(stamp, b, i, (unsigned)fw + 1);
                    ++hist[fw];
                    if (win_count) {
                        uint64_t h = hit;
                        while (h) {
                            int w = (int)__builtin_ctzll(h);
                            h &= h - 1;
                            ++win_count[w];
                        }
                    }
                }
            }
        }
        free(xenter); free(xleave); free(yenter); free(yleave);
    } else {
        for (int y = row0; y <= row1; ++y) {
            int nactive = 0;
            for (int w = 0; w < nwin; ++w)
                if (by0[w] <= y && y <= by1[w])
                    active[nactive++] = w;
            if (nactive == 0) continue;
            for (int x = 0; x < g->cols; ++x) {
                int nc = 0;
                for (int k = 0; k < nactive; ++k) {
                    int w = active[k];
                    if (bx0[w] <= x && x <= bx1[w]) joined[nc++] = w;
                }
                if (nc == 0) continue;
                int ci = y * g->cols + x;
                if (g->head[ci] == -1) continue;
                for (int i = g->head[ci]; i != -1; i = g->next[i]) {
                    float px = g->items[i].x, py = g->items[i].y;
                    float pr = g->items[i].r;
                    int fw = -1;
                    for (int q = 0; q < nc; ++q) {
                        int w = joined[q];
                        if (!circle_hits_rect(px, py, pr,
                                wins[w].x0, wins[w].y0,
                                wins[w].x1, wins[w].y1))
                            continue;
                        if (win_count) ++win_count[w];
                        if (fw < 0 || w < fw) fw = w;
                    }
                    if (fw >= 0 && stamp) {
                        stamp_set(stamp, b, i, (unsigned)fw + 1);
                        ++hist[fw];
                    }
                }
            }
        }
    }

    /* histogram -> per-window start offsets (hist reused), then place ids
       in window-major order. Within a window items are placed by ascending
       item index = insertion order, independent of grid layout, so the
       result is fully deterministic and only window order drives it. */
    int total = 0;
    for (int w = 0; w < nwin; ++w) total += hist[w];
    int acc = 0;
    for (int w = 0; w < nwin; ++w) {
        int h = hist[w];
        hist[w] = acc;
        acc += h;
    }
    if (N > 0 && cap > 0) {
        for (int i = 0; i < N; ++i) {
            unsigned s = stamp_get(stamp, b, i);
            if (s == 0) continue;
            int w = (int)s - 1;
            int pos = hist[w]++;
            if (pos < cap) out[pos] = g->items[i].id;
        }
    }

    free(base);
    return total;
}
