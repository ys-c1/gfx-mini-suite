# mini-tone-lut

Tone curve LUT builder and applier (C++17, no deps)

构建与测试：`make test`。

## API

- `build_gamma_lut` / `apply_lut` / `lut_mean` / `lut_is_monotonic`：单曲线构建与应用。
- `tone_normalize_weights(n, w, out)`：校验并把权重归一化（`out` 可与 `w` 别名）。
- `blend_luts(luts, n, w, out)`：按权重混合 n 条曲线。权重内部归一化，可传原始值（如 `2,2,0`）；
  结果取整到 8 位，端点固定为 `0`/`255`；单调曲线的凸组合仍单调。
- `fit_blend_weights(luts, n, target, out_w, out_rmse)`：反问题，求 `w >= 0, sum(w)=1`
  使混合曲线在最小二乘意义下最接近 `target`。通过枚举有效集（active set）求得精确的
  约束最优解，`out_rmse` 按最终取整、钉端点后的 LUT 计算。曲线数上限 `TONE_FIT_MAX_CURVES`（8）。

## 基准（本机，-O2）

三条曲线（gamma 1.0 / 2.2 / 0.8），两个目标，各 200 次取平均：

| 目标 | 拟合权重 | RMSE | 耗时 |
|---|---|---|---|
| 由 `{0.5, 0.3, 0.2}` 混合出的曲线（真值恢复） | `{0.4984, 0.3009, 0.2007}` | 0.0000 | 0.007 ms |
| 三次方暗调曲线（凸包外，无法精确匹配） | `{0, 0, 1}` | 55.52 | 0.006 ms |

第二组目标落在三条曲线张成的集合之外，最优解退到边界，权重为单纯形上的最近点。
