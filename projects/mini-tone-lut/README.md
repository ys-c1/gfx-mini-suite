# mini-tone-lut

Tone curve LUT builder and applier (C++17, no deps)

构建与测试：`make test`。

## 直方图匹配（按目标分布生成映射）

在原有 gamma 曲线接口（未改动）之外新增：

- `histogram_of(src, n, hist)` — 统计像素分布。
- `build_match_lut(src_hist, target_hist, lut)` — 按目标分布的归一化 CDF
  对齐生成映射 LUT，**单调不减**；两张图像素数不同也能对齐（内部归一化）。
  返回 1 成功；返回 0 时 LUT 为恒等映射（退化输入，见下）。
- `hist_mean(hist, total)` — 分布均值。
- `cdf_max_deviation(a, na, b, nb)` — 两个分布归一化 CDF 的最大绝对偏差
  （KS 距离，[0,1]），用于量化输出分布与目标的累计偏差；任一总数为 0
  返回 -1.0。

`apply_lut` 逐像素映射，像素总数前后天然一致。

退化输入的明确行为（不崩溃）：

| 输入 | 结果 |
|---|---|
| 目标分布为空（全 0） | 返回 0，LUT = 恒等 |
| 源图为空（0 像素） | 返回 0，LUT = 恒等 |
| 全零图 / 单色图 | 返回 1，单调 LUT，全部像素映射到目标分布的对应分位 |

## 样例与报告

`make report` 生成自造样例（`data/`，固定种子可复现）并对三张不同尺寸
的暗图做匹配，输出匹配前后均值、与目标的累计偏差及耗时：

```
image           pixels | mean_pre mean_post |   dev_pre  dev_post | us/build+apply
dark_gradient     4096 |    45.33   180.12 |    0.9958    0.0122 |      1.352
dark_noise        3840 |    44.75   180.38 |    0.9969    0.0168 |      1.258
dark_bimodal      1024 |    59.91   183.06 |    0.9901    0.0560 |      0.734
```

测试含旧接口的黄金值回归（改动前后输出逐位一致）：`make test`。
