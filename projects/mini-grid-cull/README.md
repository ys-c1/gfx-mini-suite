# mini-grid-cull

2D uniform-grid broadphase for sprite culling (C11, no deps)

- `grid`：单层均匀网格，固定格宽。
- `lgrid`：两层宽相——先按密度选格宽建网格（`cell = sqrt(目标每格精灵数 / 密度)`，
  上限 `LGRID_MAX_CELLS` 个格子），再在命中格内做精确圆判定。
  格宽每次切换都会记入 `LGridSwitch` 日志（序号、旧/新格宽、触发密度、在册精灵数），
  重建次数由 `lgrid_rebuilds` 返回。`lgrid_set_layered(lg, 0)` 关闭分层后退回
  固定基准格宽，查询结果与单层 `grid` 完全一致。
- 参数校验逐个报错并说明原因：格宽为零（或负）、密度为负、层数超出
  `[1, LGRID_MAX_LAYERS]`，错误信息写入调用方提供的缓冲区。

构建与测试：`make test`。基准（万级/十万级，比候选数、重建次数、耗时，暴力遍历作基准）：`make bench`。
