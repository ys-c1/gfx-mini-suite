# mini-grid-cull

2D uniform-grid broadphase for sprite culling (C11, no deps)

构建与测试：`make test`；基准数据：`make bench`。

## 两种格子模式

- `grid_init(&g, cols, rows, cell, minx, miny)`：固定格子，行为与原来一致，永不重建。
- `grid_init_adaptive(&g, minx, miny, maxx, maxy, cell_hint)`：格子边长跟随当前密度。
  目标边长 `sqrt(arena_area * 4 / n)`（约 4 个精灵/格），当前边长超出目标的
  `[0.5x, 2.0x]` 带（2 倍滞回）才重排。`cell_hint <= 0` 表示从单格起步。
  物品落在声明的范围外时，包围盒按 2 倍扩张并重排，不会丢失物品。

两种模式下 `grid_query_circle` 的结果完全一致：候选按圆心所在格收集，再用
圆-圆重叠精确过滤，与格子边界落在何处无关。返回值为真实命中数（可大于 `cap`，
此时最多写出 `cap` 个 id；这一点和原实现一致）。

## 重建代价的上界

1. 单次重排不超过 `GRID_MAX_CELLS`（2^18）个格子，工作量 `O(count + GRID_MAX_CELLS)`。
2. 2 倍滞回使插入序列的重排次数为 `O(log4 n)`；一次完整增长中所有重排的重排
   物品总数是几何级数，`O(n)`。100 万精灵实测 8 次重排、共 37.9 万次重排物品。
3. `grid_set_rebuild_budget(g, k)` 限制每个 epoch 的重排次数（默认 64，负数=不限）。
   预算用尽后继续用旧布局服务，查询仍然精确（只是每格候选数变多/格子数偏多）。
   用 `grid_new_epoch(g)` 重置配额。

`grid_cells_used` / `grid_cells` / `grid_cell_size` / `grid_rebuilds` /
`grid_relinked` 用于观察布局。

## 基准（1000x1000 世界，同一密度下的查询半径使命中数可比，`make bench`）

| 精灵数 | 密度 | 自适应格子 | 重建次数 | 重排物品 | 自适应查询 | 固定 cell 20 | 固定 cell 4 |
|---|---|---|---|---|---|---|---|
| 1 万 | 0.01 | 30.0 | 5 | 5.9k | 0.12 us | 0.11 us | 0.11 us |
| 10 万 | 0.10 | 7.5 | 7 | 94.7k | 0.17–0.22 us | 0.44–0.55 us | 0.16–0.18 us |
| 100 万 | 1.00 | 3.75 | 8 | 379k | 1.7–2.0 us | 10.7–11.4 us | 1.6–2.0 us |

固定格在稀疏场景调优就对稠密场景失效（100 万时慢 6 倍），自适应在三组里都
接近“恰好调对”的固定格，而重建只有个位数次。测试与基准都对比了暴力计算，
查询结果逐条一致（`tests/test_adaptive.c`、`tests/bench_adaptive.c`）。

注：100 万那组自适应查询偶发抖动（1.7–4.1 us，个别运行明显偏高），固定 cell 4
基本稳定在 1.6–2.0 us；两者在这组本来就只在同一量级，自适应不占优。抖动来自
按行扫描格子时 cache 行为对格子数的敏感性，未做进一步调优。
