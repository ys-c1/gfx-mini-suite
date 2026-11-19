# gfx-mini-suite

图形与多媒体领域的小项目集合，每个子目录是一个独立可构建的小库，各自带 `make test`。

| 子项目 | 语言 | 内容 |
|---|---|---|
| projects/mini-grid-cull | C11 | 2D 均匀网格宽相位剔除 |
| projects/mini-bitplane-pack | C11 | 1/2/4/8bpp 图像行打包 |
| projects/mini-tone-lut | C++17 | 色调曲线 LUT 构建与应用 |
| projects/mini-palette-dither | Python 3 | 调色板映射与量化辅助 |
| projects/mini-audio-fifo | C11 | 交织浮点音频 FIFO |
| projects/mini-audio-downmix | C11 | 可配权重的立体声→单声道下混（能量/峰值有界） |

每个子目录进入后执行 `make test` 即可编译并运行自带测试。
