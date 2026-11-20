#include "lgrid.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

static void errf(char* err, size_t cap, const char* fmt, ...) {
    if (!err || cap == 0) return;
    size_t len = strlen(err);
    if (len >= cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(err + len, cap - len, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    len += (size_t)n < cap - len ? (size_t)n : cap - len - 1;
    if (len < cap - 1 && (len == 0 || err[len - 1] != '\n')) {
        err[len] = '\n';
        err[len + 1] = '\0';
    }
}

/* cell width derived from density: aim for LGRID_TARGET_PER_CELL items/cell,
 * clamped so the grid never exceeds LGRID_MAX_CELLS cells and never
 * degenerates below one cell or a tiny positive width. */
static float cell_for_density(const LGrid* lg, float density) {
    float w = lg->maxx - lg->minx, h = lg->maxy - lg->miny;
    float cell = lg->base_cell;
    if (density > 0.0f) {
        cell = sqrtf(LGRID_TARGET_PER_CELL / density);
        float min_cell = sqrtf((w * h) / (float)LGRID_MAX_CELLS);
        float max_cell = w > h ? w : h;
        if (cell < min_cell) cell = min_cell;
        if (cell > max_cell) cell = max_cell;
    }
    if (cell < 1e-6f) cell = 1e-6f;
    return cell;
}

static int log_switch(LGrid* lg, float old_cell, float new_cell) {
    if (lg->nswitch == lg->cap_switch) {
        int ncap = lg->cap_switch ? lg->cap_switch * 2 : 8;
        LGridSwitch* ns = (LGridSwitch*)realloc(lg->switches, sizeof(LGridSwitch) * (size_t)ncap);
        if (!ns) return -1;
        lg->switches = ns;
        lg->cap_switch = ncap;
    }
    LGridSwitch* s = &lg->switches[lg->nswitch++];
    s->seq = (unsigned long)lg->nswitch;
    s->old_cell = old_cell;
    s->new_cell = new_cell;
    s->density = lg->density;
    s->items = lg->grid.count;
    return 0;
}

/* rebuild the layer-1 grid with a new cell width, reinserting all items;
 * every rebuild is counted and every actual width change is logged. */
static int rebuild(LGrid* lg, float new_cell) {
    int cols = (int)ceilf((lg->maxx - lg->minx) / new_cell);
    int rows = (int)ceilf((lg->maxy - lg->miny) / new_cell);
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    Grid ng;
    if (grid_init(&ng, cols, rows, new_cell, lg->minx, lg->miny) != 0) return -1;
    for (int i = 0; i < lg->grid.count; ++i) {
        GridItem* it = &lg->grid.items[i];
        if (grid_insert(&ng, it->id, it->x, it->y, it->r) != 0) {
            grid_free(&ng);
            return -1;
        }
    }
    float old_cell = lg->grid.cell;
    grid_free(&lg->grid);
    lg->grid = ng;
    ++lg->rebuilds;
    if (old_cell != new_cell) log_switch(lg, old_cell, new_cell);
    return 0;
}

int lgrid_init(LGrid* lg, int layers, float cell, float density,
               float minx, float miny, float maxx, float maxy,
               char* err, size_t errcap) {
    if (err && errcap) err[0] = '\0';
    if (!lg) {
        errf(err, errcap, "lgrid_init: LGrid pointer is NULL");
        return -1;
    }
    memset(lg, 0, sizeof *lg);
    int bad = 0;
    if (cell <= 0.0f) {
        errf(err, errcap, "lgrid_init: cell width must be > 0 (got %g)%s",
             (double)cell, cell == 0.0f ? " - zero cell width would make cell lookup divide by zero" : "");
        bad = 1;
    }
    if (density < 0.0f) {
        errf(err, errcap, "lgrid_init: density must be >= 0 (got %g) - negative density has no physical meaning",
             (double)density);
        bad = 1;
    }
    if (layers < 1 || layers > LGRID_MAX_LAYERS) {
        errf(err, errcap, "lgrid_init: layers %d out of range [1, %d] - this broadphase supports at most %d layers",
             layers, LGRID_MAX_LAYERS, LGRID_MAX_LAYERS);
        bad = 1;
    }
    if (!(maxx > minx) || !(maxy > miny)) {
        errf(err, errcap, "lgrid_init: world bounds empty or inverted ([%g,%g] x [%g,%g])",
             (double)minx, (double)maxx, (double)miny, (double)maxy);
        bad = 1;
    }
    if (bad) return -1;

    lg->layers = layers;
    lg->layered = (layers == LGRID_MAX_LAYERS);
    lg->base_cell = cell;
    lg->density = density;
    lg->minx = minx; lg->miny = miny;
    lg->maxx = maxx; lg->maxy = maxy;

    float c0 = lg->layered ? cell_for_density(lg, density) : cell;
    int cols = (int)ceilf((maxx - minx) / c0);
    int rows = (int)ceilf((maxy - miny) / c0);
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (grid_init(&lg->grid, cols, rows, c0, minx, miny) != 0) {
        errf(err, errcap, "lgrid_init: grid allocation failed (%d x %d cells)", cols, rows);
        return -1;
    }
    return 0;
}

void lgrid_free(LGrid* lg) {
    if (!lg) return;
    grid_free(&lg->grid);
    free(lg->switches);
    lg->switches = NULL;
    lg->nswitch = lg->cap_switch = 0;
}

int lgrid_insert(LGrid* lg, int id, float x, float y, float r) {
    if (!lg) return -1;
    return grid_insert(&lg->grid, id, x, y, r);
}

int lgrid_query_circle(LGrid* lg, float cx, float cy, float rad, int* out, int cap) {
    if (!lg) return -1;
    long cand = 0;
    /* layer 1: cells overlapped by the query circle (grid_query_circle_ex);
     * layer 2: exact distance test inside those cells (same call) */
    int n = grid_query_circle_ex(&lg->grid, cx, cy, rad, out, cap, &cand);
    lg->last_candidates = cand;
    return n;
}

int lgrid_set_density(LGrid* lg, float density, char* err, size_t errcap) {
    if (err && errcap) err[0] = '\0';
    if (!lg) {
        errf(err, errcap, "lgrid_set_density: LGrid pointer is NULL");
        return -1;
    }
    if (density < 0.0f) {
        errf(err, errcap, "lgrid_set_density: density must be >= 0 (got %g) - negative density has no physical meaning",
             (double)density);
        return -1;
    }
    lg->density = density;
    if (!lg->layered) return 0;
    float want = cell_for_density(lg, density);
    float cur = lg->grid.cell;
    if (fabsf(want - cur) / cur > 0.05f)
        return rebuild(lg, want);
    return 0;
}

void lgrid_set_layered(LGrid* lg, int on) {
    if (!lg) return;
    on = on && lg->layers == LGRID_MAX_LAYERS;
    if (!!on == !!lg->layered) return;
    lg->layered = on;
    float want = on ? cell_for_density(lg, lg->density) : lg->base_cell;
    float cur = lg->grid.cell;
    if (fabsf(want - cur) / cur > 0.05f)
        rebuild(lg, want); /* best effort; grid stays valid either way */
}

int lgrid_count(const LGrid* lg) { return lg ? lg->grid.count : 0; }
float lgrid_cell(const LGrid* lg) { return lg ? lg->grid.cell : 0.0f; }
unsigned long lgrid_rebuilds(const LGrid* lg) { return lg ? lg->rebuilds : 0; }
int lgrid_switch_count(const LGrid* lg) { return lg ? lg->nswitch : 0; }
const LGridSwitch* lgrid_switches(const LGrid* lg) { return lg ? lg->switches : NULL; }
long lgrid_last_candidates(const LGrid* lg) { return lg ? lg->last_candidates : 0; }
