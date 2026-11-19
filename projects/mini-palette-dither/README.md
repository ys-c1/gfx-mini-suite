# mini-palette-dither

调色板映射与**按局部纹理自适应强度**的抖动。纯 Python 3，无第三方依赖。

四色输出下的大面积渐变会出阶梯色带；全图统一强度抖动虽然能压掉色带，却会把文字边缘
弄脏。这里的抖动强度按区块的**局部方差**逐块计算，渐变区近满强度、文字/边缘区被压到
零（或配置下界），并且只输出调色板里已有的颜色。

## 快速开始

```bash
make test     # 全部单元测试 + 打包器集成测试
make samples  # 生成样例 PNG（含强度热力图）
make bench    # 跑三组样例的对照报告，写 samples/report.json
```

`make test` 会先用仓库里的 `mini-bitplane-pack` 编译一个薄封装
`build/bitpack_cli`，让体积与一致性检查跑在真实的 C 打包器上，而不是 Python 复刻。

## API

```python
from dither import dither_indices

indices = dither_indices(
    pixels,            # [(r, g, b), ...] 长度必须等于 width * height
    width, height,
    palette,           # [(r, g, b), ...] 最多 256 色
    block_size=8,      # 区块边长，像素
    strength_min=0.0,  # 高方差区块（文字）的强度下界
    strength_max=0.75, # 平坦区块（渐变）的强度上界
    enabled=True,      # False 时逐像素等于 palette.map_nearest
    texture_scale=64.0,
)
```

- **关闭抖动时逐像素等于现有映射。** `enabled=False` 直接返回
  `palette.map_nearest(pixels, palette)`；`strength_min == strength_max == 0`
  走同一条路径（全文不再有任何误差扩散）。
- **只输出调色板已有颜色。** 返回值是 `palette` 的下标，测试逐像素校验
  `0 <= idx < len(palette)`。
- **确定性。** 不用随机数、不用 set/dict 迭代序，整数扫描方向固定，同一输入多次运行
  （含不同 `PYTHONHASHSEED` 的独立进程）索引图逐字节一致。

辅助函数：

| 函数 | 作用 |
|---|---|
| `block_strengths(...)` | 返回每个区块的强度，行优先，便于做热力图或调参 |
| `block_grid_size(w, h, block)` | 区块网格尺寸（含边缘不完整块） |
| `text_mask(pixels, w, h, block)` | 标出强度被压到下界的区块，用于校验文字未被改动 |
| `validate_palette(...)` / `validate_params(...)` | 单独做参数校验（`dither_indices` 内部也会调用） |

## 强度模型

每个 `block_size × block_size` 区块算一次强度：

```
variance = 三个通道相对区块均值的均方差（再对通道求平均）
t        = clamp(sqrt(variance / texture_scale), 0, 1)
strength = strength_max - (strength_max - strength_min) * t
```

- 平坦/渐变区块局部方差小 → 接近 `strength_max`，色带被打散；
- 文字笔画与边缘所在区块方差高出一到两个数量级 → 压到 `strength_min`；
- 开方是为了让曲线在真实边缘处迅速触底、在平滑内容上保留细调，避免“稍有噪声的渐变”
  被一刀切当成纹理。

误差扩散用蛇形扫描的 Floyd–Steinberg，但每条误差按两端区块强度的较小值 `gate` 缩放：
`strength_min == 0` 的区块既不接收也不送出误差，因此文字区逐像素等于无抖动映射。
默认 `strength_min = 0`，这条保证是严格的；若把下界调高，文字区会获得同等的部分抖动，
这是显式配置的结果而不是意外泄漏。

`block_strengths`/`text_mask` 对边缘不完整块只用真实存在的像素；`block_size` 大于图像尺寸
时整图视为一个区块，也是合法输入。

## 参数校验

以下输入抛 `ValueError`（即使 `enabled=False` 也校验）：

- 区块尺寸非正整数，或不是 `int`（`0`、负数、`2.0`、`"4"`、`True` 都拒绝）；
- 调色板为 `None`、空、超过 256 色，或元素不是 0..255 的整数 RGB 三元组；
- 强度上下界倒置（`strength_min > strength_max`）、超出 `[0, 1]`、`NaN`/`inf`；
- `texture_scale <= 0` 或非有限值；
- 像素数量与 `width * height` 不符、像素越界或非有限值。

## 报告

`make bench` 输出三组自造样例（每组自带四色调色板），两种区块尺寸各一行。完整数字在
`samples/report.json`；下表的表头与报告一致：

```
sample   block  mean err plain  mean err dith  tile dL plain  tile dL dith  tile worst plain  tile worst dith  bias plain  bias dith  luma bound  pack B  pack ratio  deflate ratio  bpp p/d  ms      text chg
-------  -----  --------------  -------------  -------------  ------------  ---------------  --------------  ----------  ---------  ----------  ------  ----------  -------------  -------  ------  ---------
vgrad    6      22.39           25.04          16.64          6.96          36.66            27.75           -1.26       -2.16      64.46       6144    1.000       5.18           2/2      49      0/2232
vgrad    12     22.39           23.43          16.64          11.84         36.66            28.25           -1.26       -1.47      56.67       6144    1.000       2.73           2/2      52      0/3744
hgrad    6      36.22           39.45          21.00          12.46         47.15            35.15           -4.48       -4.75      111.95      6144    1.000       13.84          2/2      50      0/1296
hgrad    12     36.22           37.76          21.00          14.05         47.15            47.15           -4.48       -4.54      77.03       6144    1.000       5.92           2/2      52      0/2304
radial   6      19.10           21.91          19.37          4.94          34.55            22.01           -2.28       +0.15      62.45       6144    1.000       4.54           2/2      57      0/2304
radial   12     19.10           21.30          19.37          6.81          34.55            34.23           -2.28       +0.14      58.37       6144    1.000       4.09           2/2      51      0/3888
```

列含义：

- **mean err**：逐像素、三通道平均绝对色差，对照原图。抖动会在逐像素误差上略微变差，
  这是误差扩散的正常代价。
- **tile dL / tile worst**：每个 8×8 瓦片**平均色**的亮度误差（均值 / 最差值）。色带的
  本质是瓦片平均色漂移，这一列才是抖动是否真的消掉台阶的指标；三组样例均显著下降。
- **bias**：全图带符号平均亮度偏差；**luma bound**：逐像素 `|dLuma|` 的最大值（亮度
  偏差上界）。
- **pack B / pack ratio**：仓库现有 `mini-bitplane-pack` 的输出字节数与倍率。
- **deflate ratio**：对打包流做 zlib 后的倍率，见下节。
- **bpp p/d**：两份索引图实际需要的每像素位数。
- **ms**：抖动耗时（纯 Python，192×128 约 50 ms）。
- **text chg**：文字区块内索引相对无抖动映射发生变化的像素数 / 该区域像素总数，必须为 0。

### 关于 1.15× 体积预算

仓库现有的 `mini-bitplane-pack` 是**定长行打包**：输出恰好 `w*h*bpp` 位，与内容无关。
四色即 2bpp，所以抖动与不抖动**写出的打包体积完全相同**（表中 `pack ratio` 恒为 1.000，
`bpp p/d` 均为 2/2）。在这种打包器下 1.15× 的上界是结构性满足的，不依赖抖动幅度。

需要说明的是：抖动本质上增加索引图熵。若把同一份打包流再交给熵编码（表中
`deflate ratio`，3～14×），体积会明显超过 1.15×。这组数据如实列出，是为了在流水线换成
熵编码打包器时不被这个“达标”误导。测试 `tests/test_integration.py` 同时校验：
定长打包体积不超预算、2bpp 帧宽未被抖动推高、打包/解包可逆、跨进程索引图逐字节一致。

## 样例

`samples/` 下每组样例四个文件：

- `<name>.png`：原图（真彩）；
- `<name>-plain.png`：无抖动的四色映射，能看到明显色带；
- `<name>-dither.png`：自适应抖动结果，色带被打散、文字保持干净；
- `<name>-strength.png`：强度热力图（越黑越接近 `strength_min`），可直接看出文字区块被
  压到零。

样例由 `tools/sample_gen.py` 以固定种子的整数 PRNG 程序化生成，跨平台可复现；PNG 读写
在 `tools/pngio.py`，同样零依赖。

## 文件

| 路径 | 内容 |
|---|---|
| `src/dither.py` | 自适应抖动实现、强度模型、校验、文字掩码 |
| `src/palette.py` | 原有最近色映射与工具函数（未改动） |
| `tests/test_dither.py` | 单元测试：等价性、确定性、强度排序、非法参数 |
| `tests/test_integration.py` | 真实打包器上的体积预算、跨进程一致性、逆打包 |
| `tests/test_pngio.py` | PNG 读写回环 |
| `tools/bench_dither.py` | 报告生成 |
| `tools/render_samples.py` | 样例 PNG 渲染 |
| `tools/bitpack_cli.c` | `mini-bitplane-pack` 的薄 CLI 封装 |
