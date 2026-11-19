#include "grid.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

int grid_init(Grid* g, int cols, int rows, float cell, float minx, float miny) {
    if (!g || cols <= 0 || rows <= 0 || cell <= 0.0f) return -1;
    g->cols = cols; g->rows = rows; g->cell = cell;
    g->minx = minx; g->miny = miny;
    g->count = 0; g->cap = 64;
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
}
