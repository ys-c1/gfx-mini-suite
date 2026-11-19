#ifndef GRID_H
#define GRID_H

/* ---- return codes ---------------------------------------------------- */
#define GRID_OK           0
#define GRID_MOVED_CELL   1  /* grid_move: entry relocated to another cell   */
#define GRID_ERR_INVALID (-1)  /* NULL pointer / bad argument / duplicate id */
#define GRID_ERR_BOUNDS  (-2)  /* target position lies outside the grid      */
#define GRID_ERR_NOID    (-3)  /* id not present (e.g. repeated delete)      */
#define GRID_ERR_EMPTY   (-4)  /* grid currently holds no live entries       */

typedef struct { float x, y, r; int id; } GridItem;

typedef struct {
    float minx, miny, cell;
    int cols, rows;

    int* head;       /* head[c]: first live slot in cell c, -1 if empty    */
    int* prev;       /* prev slot within the cell chain, -1 at head        */
    int* next;       /* next slot within the cell chain, -1 at tail;
                        doubles as the free-slot chain when slotcell[s] == -1 */
    int* slotcell;   /* cell index of slot s, -1 when the slot is free     */
    GridItem* items; /* per-slot item data                                 */

    /* direct id -> slot map, two-level over the unsigned id space so that
     * arbitrary int ids (including negative ones) cost one extra pointer
     * chase at most and never need collision probing. -1 = absent.        */
    int** idmap;
    int idmap_top;   /* number of top-level buckets (power of two)         */

    int* scratch;    /* candidate buffer reused by grid_query_circle       */
    int  scratch_cap;

    int free_head;   /* first free slot, -1 when none                      */
    int nslots;      /* slots ever allocated (indices 0 .. nslots-1)       */
    int count, cap;  /* live entry count / allocated slot capacity         */
} Grid;

int  grid_init(Grid* g, int cols, int rows, float cell, float minx, float miny);
void grid_free(Grid* g);

/* live-entry maintenance ------------------------------------------------ */
int  grid_insert(Grid* g, int id, float x, float y, float r);
int  grid_move(Grid* g, int id, float x, float y);
int  grid_remove(Grid* g, int id);

/* full-rebuild path: replace every live entry with items[0..n) ---------- */
int  grid_rebuild(Grid* g, const GridItem* in, int n);

/* circle query; ids are returned sorted by (cell, id), independent of
 * insert/move/remove history. Return value is the total hit count; only
 * the first `cap` hits are written to `out` (count semantics unchanged). */
int  grid_query_circle(Grid* g, float cx, float cy, float rad, int* out, int cap);

int  grid_count(const Grid* g);
int  grid_cells_used(const Grid* g);
void grid_clear(Grid* g);
#endif
