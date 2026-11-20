#ifndef GRID_H
#define GRID_H
#include <stddef.h>
typedef struct { float x, y, r; int id; } GridItem;
typedef struct {
    float minx, miny, cell;
    int cols, rows;
    int* head;      /* per-cell head index, -1 empty */
    int* next;      /* per-item next index */
    GridItem* items;
    int count, cap;
    float maxr;     /* max item radius seen; internal, used by batch queries */
} Grid;
int grid_init(Grid* g, int cols, int rows, float cell, float minx, float miny);
void grid_free(Grid* g);
int grid_insert(Grid* g, int id, float x, float y, float r);
int grid_query_circle(const Grid* g, float cx, float cy, float rad, int* out, int cap);
int grid_count(const Grid* g);
void grid_clear(Grid* g);
int grid_cells_used(const Grid* g);

/* Axis-aligned viewport window, inclusive bounds (x0 <= x1, y0 <= y1). */
typedef struct { float x0, y0, x1, y1; } GridWindow;

/*
 * Batch rectangle-window query answered with a single grid traversal.
 *
 * - Every grid cell covered by at least one window (after radius inflation)
 *   is visited at most once, so every item is examined at most once.
 * - Each item id appears at most once in the merged out[] array.
 * - out[] is ordered by window index: all items whose first hitting window
 *   is w come before those first hit by w+1; within a window ids are in
 *   ascending item index (insertion order), independent of grid layout.
 *   Overlapping/nested windows therefore contribute only items not already
 *   claimed by an earlier window.
 * - win_count[w], when non-NULL, receives the true per-window hit count
 *   (items intersecting that window, overlaps counted in every window).
 * - If the number of distinct hits exceeds cap, exactly cap ids are written
 *   (a deterministic prefix of the full ordering) and the full distinct
 *   count is returned, mirroring grid_query_circle.
 *
 * Returns distinct hit count (>= 0), or -1 on invalid arguments:
 * NULL grid/wins, nwin < 0 or > 32767, cap < 0, reversed/NaN window rect.
 * nwin == 0 is valid and returns 0 without writing anything.
 */
int grid_query_windows(const Grid* g, const GridWindow* wins, int nwin,
                       int* out, int cap, int* win_count);

/* Exact scratch bytes (beyond out[]) that grid_query_windows will allocate. */
size_t grid_query_windows_scratch(const Grid* g, int nwin);
#endif
