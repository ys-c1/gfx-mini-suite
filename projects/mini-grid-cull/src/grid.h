#ifndef GRID_H
#define GRID_H
typedef struct { float x, y, r; int id; } GridItem;
typedef struct {
    float minx, miny, cell;
    int cols, rows;
    int* head;      /* per-cell head index, -1 empty */
    int* next;      /* per-item next index */
    GridItem* items;
    int count, cap;
} Grid;
int grid_init(Grid* g, int cols, int rows, float cell, float minx, float miny);
void grid_free(Grid* g);
int grid_insert(Grid* g, int id, float x, float y, float r);
int grid_query_circle(const Grid* g, float cx, float cy, float rad, int* out, int cap);
int grid_count(const Grid* g);
void grid_clear(Grid* g);
int grid_cells_used(const Grid* g);
#endif
