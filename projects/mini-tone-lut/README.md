# mini-tone-lut

Tone curve LUT builder and applier (C++17, no deps)

构建与测试：`make test`。
性能基准：`make bench`。

## API

- `build_gamma_lut(gamma, lut)`：一次性构建 256 项（原接口，行为不变）。
- `GammaLutBuilder`：按需分段构建，已构建的条目缓存复用。
  - `build_range(first, last)`：构建半开区间 `[first, last)`，返回新构建的条目数；
    合法空区间返回 0；`first > last` 或越界返回 -1，且不改变任何状态。
  - `build_prefix(n)`：等价于 `build_range(0, n)`，即“构建到索引 n”。
  - `is_built(i)` / `value_at(i)`：查询条目是否已构建及其值；未构建或越界时
    `value_at` 返回 -1。
  - `built_count()` / `built_prefix()`：已构建条目数 / 最长连续前缀长度。
  - `reset()`：清空缓存后可重新填充。
  - 同一区间重复构建幂等（不重复计算 `pow`）；任意顺序（含先建后段的反向顺序）
    拼出的表与 `build_gamma_lut` 逐字节一致；`data()` 可直接传给 `apply_lut`。
  - `gamma <= 0.0` 按 1.0 处理，与 `build_gamma_lut` 一致。

## 性能

分段构建只在首次触达条目时付出一次 `pow` 成本，因此填满整表的计算量与一次性
构建相同。`make bench` 在同一进程内对比整表一次构建与多种分段/乱序顺序的总耗时
（各 4 次重复取最优）：常规分段（≤32 段）开销约 0.99–1.03x；即使每项一次调用的
极端 256 段场景，实测也稳定在 1.2x 预算以内（约 1.13–1.18x）。
