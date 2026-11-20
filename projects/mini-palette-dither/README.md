# mini-palette-dither

调色板映射与抖动辅助。纯 Python 3，无第三方依赖。

原来的调色板里有大量重复槽位（同一个颜色存了几份）和从未被任何像素用到的
槽位，常常占去几十个名额。这里把它们清掉，并把所有索引重映射到新表；抖动
仍然按新表工作，且每一像素的输出误差有可计算的上界。关闭去重时输出与改动
前逐字节一致。

## 两段流水线

**第一段：去重 + 重映射**（`src/palette.py`）

```python
import palette

mapping = palette.dedupe_palette(palette, usage)   # usage[i] = 老槽位 i 的像素数
mapping.palette   # 压缩后的颜色表（保持原相对顺序）
mapping.remap     # 老下标 -> 新下标，被删掉的槽位是 -1
mapping.usage     # 合并后的使用计数，对应新表
mapping.saved_slots

new_indices = palette.remap_indices(old_indices, mapping)  # 老索引流重映射
```

规则：

- 重复颜色归并到**第一个被保留的**槽位；被删重复槽位的计数合并进保留者。
- 使用计数为 0 的槽位被删除（只有给了 `usage` 才知道谁没用过）；
  `remove_unused=False` 只去重、不删未用。
- 保留槽位保持原相对顺序，所以 `remap` 在保留项上是单调的。
- 不给 `usage` 时无从判断「未用」，只做重复归并。
- `enabled=False`：布局完全不变（`remap == range(n)`），供对比旧输出。

**第二段：抖动**（`src/dither.py`）

```python
import dither

indices, stats = dither.dither(pixels, width, mapping.palette, factor=0.5)
# 或一次做完，校验 mapping 后用新表抖动：
indices, stats = dither.dither_mapping(pixels, width, mapping)
```

抖动只读压缩后的表，返回的索引已经是新编号，不需要、也不会再套 `remap`。
`factor=0` 时误差项全为 0，输出逐像素等于 `palette.map_nearest`。

## 误差上界

每个被选中的表项 `i` 有一个每通道钳制值

```
S_i = cell_i / sqrt(3)      cell_i = 表项 i 到最近的其他表项的距离
```

Floyd–Steinberg 四个权重之和为 1（7/16 + 1/16 + 5/16 + 3/16），所以任一像素
收到的修正量 `|c| <= S_max`（逐通道），即 RGB 向量长度 `<= sqrt(3)*S_max`。
抖动输出的颜色 `p` 是最近的表项，于是

```
|x - p| <= |x + c - nearest(x)| + |c| <= D + 2*sqrt(3)*S_max
```

`D` 是源像素到最近表项的距离。`stats["error_bound"]` 是本图所有像素该上界的
最大值，`stats["max_error"]` 是实测最大平方误差，`stats["within_bound"]`
检查实测值不超过上界。`factor` 只缩放扩散项，不改变钳制，所以上界对任何
`factor` 都成立。

## 错误处理：不夹取，报位置

所有校验失败都抛 `PaletteError`（同时是 `IndexError` 和 `ValueError`），
`exc.position` 与消息里都带位置：

| 情况 | 抛错位置 |
|---|---|
| 空调色板（0 色） | `palette[0]` |
| 颜色通道越界 / 不是 int / 不是三元组 | `palette[3]`、`pixels[17]` |
| 索引越界（负数或 >= 色数） | `indices[7]` |
| 索引指到被删槽位 | `indices[7]`（消息里给出 `remap[old] = -1`） |
| usage 长度与调色板不符 | `usage[64]` |
| remap 长度与调色板不符 | `remap[64]` |
| remap 目标越界 / 目标颜色与原色不符 | `remap[64]` |
| 全部槽位都未使用 | `palette[0]` |

`palette_usage` 旧实现对越界索引直接 `IndexError`，现在同样是报错，
但位置与文本统一，且绝不夹取到 0 或 255。

## 测试与基准

```bash
make test    # 单元测试 + 「关闭去重输出不变」检查
make bench   # 64 / 256 槽两组的槽位、误差、耗时对照
```

`tools/bench_dither.py` 的调色板按已知缺陷布局构造（`base + 重复槽 + 未用槽`），
期望 remap 表是**手写公式**算出来的（`[0..base-1] + [j % base ...] + [-1 ...]`），
脚本断言实测 remap 与该手算表逐项一致。基准输出（本机）：

```
case before   off    on saved hand      bound max_on max_off mean_on mean_off   blk_on  blk_off   ms_on  ms_off
  64     64    64    52    12   12     2523.0   1083    1083   36.05    34.05    17153    20738    82.5    95.0
 256    256   256   200    56   56      972.0    768     768   18.04    17.91    10057    11060   271.0   339.3
```

- `before/off/on`：原始槽位 / 关闭去重 / 开启去重后的槽位数；`saved` 与
  `hand`（手算值）一致：64 组省 12 槽，256 组省 56 槽。
- `bound`：推导出的平方误差上界；`max_on/max_off`：实测最大平方误差。
- `mean_on/mean_off`：逐像素平均平方误差（去重后色表更小，这一项可能略高，
  这是少用槽位的直接代价）。
- `blk_on/blk_off`：8×8 块均值平方误差，抖动主要改善的是这一项。
- `ms_on/ms_off`：去重 + 重映射 + 抖动的总墙钟耗时（会随机器波动，其余列
  可复现）。
