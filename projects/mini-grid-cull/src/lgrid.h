#ifndef LGRID_H
#define LGRID_H
#include "grid.h"
#include <stddef.h>

/* Two-layer broadphase:
 *   layer 1 - uniform grid whose cell width is chosen from sprite density
 *   layer 2 - exact circle test on items of the hit cells
 * Layering can be switched off; then it behaves exactly like plain Grid
 * with the fixed base cell width.
 */

#define LGRID_MAX_LAYERS 2
#define LGRID_TARGET_PER_CELL 4.0f   /* desired items per cell when layered */
#define LGRID_MAX_CELLS (1 << 20)    /* hard cap on cols*rows */

typedef struct {
    unsigned long seq;      /* switch sequence number, starts at 1 */
    float old_cell;
    float new_cell;
    float density;          /* density that triggered the switch */
    int items;              /* live items at switch time */
} LGridSwitch;

typedef struct {
    Grid grid;              /* layer-1 broadphase storage */
    int layers;             /* requested layer count, 1..LGRID_MAX_LAYERS */
    int layered;            /* layering currently enabled */
    float base_cell;        /* fixed cell width used when layering is off */
    float density;          /* last known density (items per unit area) */
    float minx, miny, maxx, maxy;
    LGridSwitch* switches;  /* cell-width switch log */
    int nswitch, cap_switch;
    unsigned long rebuilds; /* grid rebuilds caused by cell-width changes */
    long last_candidates;   /* broadphase candidates of the last query */
} LGrid;

/* All parameter problems are reported into err (one line each, with the
 * reason); returns -1 if any was found. */
int  lgrid_init(LGrid* lg, int layers, float cell, float density,
                float minx, float miny, float maxx, float maxy,
                char* err, size_t errcap);
void lgrid_free(LGrid* lg);
int  lgrid_insert(LGrid* lg, int id, float x, float y, float r);
int  lgrid_query_circle(LGrid* lg, float cx, float cy, float rad, int* out, int cap);
/* Update density; when layered, rebuilds the grid (and logs the switch)
 * if the derived cell width changes by more than 5%. */
int  lgrid_set_density(LGrid* lg, float density, char* err, size_t errcap);
/* off: fall back to the fixed base cell, results identical to plain Grid */
void lgrid_set_layered(LGrid* lg, int on);

int  lgrid_count(const LGrid* lg);
float lgrid_cell(const LGrid* lg);
unsigned long lgrid_rebuilds(const LGrid* lg);
int  lgrid_switch_count(const LGrid* lg);
const LGridSwitch* lgrid_switches(const LGrid* lg);
long lgrid_last_candidates(const LGrid* lg);
#endif
