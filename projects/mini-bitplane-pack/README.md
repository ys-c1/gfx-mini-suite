# mini-bitplane-pack

Packed 1/2/4/8bpp（以及任意 1..8 bpp）索引流的打包与解包（C11，无依赖）。

构建与测试：`make test`（会跑完整测试，并按 `-fstack-usage` 核对栈/内存上界）。

## 两种位序

同一个索引流有两种字节布局：

- `BITPACK_MSB_FIRST`：流的第 0 位落在第 0 个字节的 bit 7，依次到 bit 0。
- `BITPACK_LSB_FIRST`：流的第 0 位落在第 0 个字节的 bit 0，依次到 bit 7。

两种位序下，单个索引值都按"高位先出"写入连续流位置（即索引 bit `bpp-1` 先写、
bit 0 最后写）。因此**同一数组、同一布局**产生的 LSB-first 字节流，恰好是
MSB-first 字节流的逐字节位镜像：

```
lsb[i] == bitpack_mirror_byte(msb[i])     // 对所有 i
```

`bitpack_mirror_bytes()` 提供整段镜像，支持原地调用。测试对全部 bpp 和多种宽高
逐一验证了这条恒等式。

## 既有高位在前整行路径

`pack_rows` / `unpack_rows` / `packed_size` / `count_used_values` 保持原签名和
原有行为：MSB-first、行间无填充、成功返回 0、任何错误返回 -1。实现逐行保留原
有循环。测试用 `tests/legacy_ref.c`（改动前实现的原样副本）作为 oracle，对
88 组 (w,h,bpp) 的输出做整缓冲 `memcmp`，并核对两者的返回值与 `*out_len` 完全一致。

## 任意位偏移与多段

```c
bitpack_write_seg segs[] = {
    { 0,   a, n_a },      /* 从第 0 位写 n_a 个索引 */
    { 20,  b, n_b },      /* 从第 20 位（字节中间）继续 */
    { 333, c, n_c },      /* 中间允许有空洞 */
};
uint64_t out_bits;
bitpack_write(segs, 3, 4, BITPACK_LSB_FIRST, buf, cap, &out_bits);
```

- 段可重叠，后写的段覆盖先写的段；段外位保持原值不变。
- `*out_bits` = 所有非空段 `bit_off + count*bpp` 的最大值；空段被忽略、不延长流。
- 先完整校验再写：任何错误返回时输出缓冲逐字节不变。
- `bitpack_read()` / `bitpack_read_run()` 一次可读任意跨段区间——读一个窗口可以
  跨越任意多个此前写入的段边界，包括落在字节中间的边界。

## 明确语义

| 情形 | 语义 |
|---|---|
| 宽度不是 8 的倍数 | 流不按行填充；索引跨字节边界时高位在低字节、低位在高字节分别落位。最后一字节未用位保持调用者原值（写入）或忽略（读取） |
| 跨段边界落在字节中间 | 合法。读写都以绝对位偏移寻址，一次读写可跨越任意多段/任意字节中间边界 |
| 末尾不足一个索引 | `bitpack_read_all()` 只解出 `avail_bits / bpp` 个完整索引，剩余 `avail_bits % bpp` 位不写入输出、不算错误，通过 `out_tail_bits` 报告 |

## 错误码

| 码 | 含义 |
|---|---|
| `BITPACK_ERR_ARG` (-1) | 空指针 / 非法参数 |
| `BITPACK_ERR_BPP` (-2) | 位深不在 1..8 |
| `BITPACK_ERR_CAP` (-3) | 输出/索引缓冲不足 |
| `BITPACK_ERR_RANGE` (-4) | 索引值超过 `(1<<bpp)-1` |
| `BITPACK_ERR_LEN` (-5) | 输入 packed 长度不足 |
| `BITPACK_ERR_OFFSET` (-6) | 位偏移/位数 uint64 溢出 |
| `BITPACK_ERR_ORDER` (-7) | 未知位序 |

既有行 API 仍沿用原来的 `-1`（不做细分，以保持字节级/行为级兼容）。

## 内存

实现不做任何堆分配，也不使用按块大小的临时缓冲；所有读写直接在调用者缓冲区上
完成。`make test` 用 `gcc -fstack-usage` 打印每个函数的栈帧，最大帧
（`count_used_values`，原有函数）为 1056 字节，远低于一个 4096 字节缓冲块；
新 API 的最大帧为 96 字节。堆分配为 0 字节。
