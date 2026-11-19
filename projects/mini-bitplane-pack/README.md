# mini-bitplane-pack

Packed 1/2/4/8bpp image row packer (C11, no deps)

- `pack_rows` / `unpack_rows`：整图顺序打包/解包。
- `unpack_rows_at(packed, len, w, h, bpp, start_bit, nbits, idx, out_count)`：按位偏移定位的局部解码，起始位可落在任意字节/行边界之间；`nbits` 末尾不足一个索引的零头按截断处理，实际读到的索引数由 `out_count` 报告。错误码可区分：`-2` 起始位越界、`-3` 长度为负、`-4` 起始位+长度超出总位数、`-1` 其他参数错误。

构建与测试：`make test`（`test_bitpack` 为既有顺序解包回归，`test_bitpack_at` 为随机定位对照测试，含跨字节/跨行正确性、截断、稳定性、可区分拒绝、顺序 vs 随机逐字节对照及性能门槛）。
