#ifndef GRID_H
#define GRID_H

/* Adaptive cell-size bounds. A rebuild re-bins every item, so its work is
 * O(count + cells); GRID_MAX_CELLS caps the cell term, and the hysteresis in
 * grid.c caps how often a rebuild can happen (see README). */
#define GRID_MIN_CELL  0.001f
#define GRID_MAX_CELL  1024.0f
#define GRID_MAX_CELLS (1 << 18)
#define GRID_DEFAULT_REBUILD_BUDGET 64

typedef struct { float x, y, r; int id; } GridItem;

typedef struct {
    float minx, miny;   /* arena origin */
    float maxx, maxy;   /* arena upper bound (items live in [min, max]) */
    float cell;         /* current cell edge length */
    int cols, rows;
    int* head;          /* per-cell head index, -1 empty */
    int* next;          /* per-item next index */
    GridItem* items;
    int count, cap;
    float maxr;         /* largest item radius, used to widen queries */
    int adaptive;       /* 0 = fixed cells (grid_init), 1 = density-adaptive */
    int rebuilds;       /* relayouts since init/clear */
    int epoch_rebuilds; /* relayouts since last grid_new_epoch */
    int budget;         /* max density rebuilds per epoch, < 0 = unbounded */
    int relinked;       /* total items re-binned by relayouts */
} Grid;

/* Fixed-size grid: cols x rows cells of `cell` units, origin (minx, miny).
 * Never rebuilds. */
int grid_init(Grid* g, int cols, int rows, float cell, float minx, float miny);

/* Density-adaptive grid covering [minx,maxx] x [miny,maxy]. Cell size follows
 * the current mean spacing (target ~4 items per cell) with 2x hysteresis.
 * `cell_hint` <= 0 starts with a single cell over the whole arena. The arena
 * grows geometrically if an item lands outside it. */
int grid_init_adaptive(Grid* g, float minx, float miny, float maxx, float maxy,
                       float cell_hint);

void grid_free(Grid* g);

/* Insert one item. At most one rebuild can be triggered per call. */
int grid_insert(Grid* g, int id, float x, float y, float r);

/* Circle query. Returns the exact number of matching items (may exceed `cap`;
 * at most `cap` ids are written). Result is independent of grid layout. */
int grid_query_circle(const Grid* g, float cx, float cy, float rad, int* out, int cap);

int grid_count(const Grid* g);
void grid_clear(Grid* g);
int grid_cells_used(const Grid* g);

/* Introspection. */
int grid_cells(const Grid* g);        /* cols * rows currently allocated */
float grid_cell_size(const Grid* g);
int grid_rebuilds(const Grid* g);     /* total relayouts */
int grid_relinked(const Grid* g);     /* total items re-binned */
void grid_set_rebuild_budget(Grid* g, int per_epoch);
void grid_new_epoch(Grid* g);         /* resets the per-epoch rebuild budget */

#endif
