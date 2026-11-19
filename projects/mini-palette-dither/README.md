# mini-palette-dither

调色板映射与量化辅助。

- `map_nearest` / `nearest_index` / `quantize_uniform` / `palette_usage`：
  原始映射路径，行为保持不变。
- `quantize_diffused(rows, palette, scan="serpentine")`：Floyd–Steinberg
  误差扩散（以行数组为输入）。
- `map_diffused(pixels, palette, width, scan=...)`：平铺缓冲包装。
- `quantization_error` / `usage_entropy`：总量化误差与使用均匀度。

`scan` 仅接受 `"forward"`（逐行同向）与 `"serpentine"`（蛇形），其它取值
一律 `ValueError`。误差按通道独立累加，每次写入邻域时钳制到
`[0, MAX_WORK_VALUE]`，因此不会无界增长，也不会串到相邻通道。

`make test` 运行测试，`make bench` 用四色调色板比较三张合成图的
总量化误差、8×8 块均值误差与使用熵，并给出两种扫描顺序的耗时。
