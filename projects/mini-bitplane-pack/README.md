# mini-bitplane-pack

Packed 1/2/4/8bpp image row packer (C11, no deps)

两种入口：

- 整幅：`pack_rows(idx, w, h, bpp, out, cap, &len)`，一次打包整幅索引图。
- 逐行流式（大图省内存，峰值 = 目标缓冲 + 一行）：

```c
bitpack_stream s;
pack_begin(&s, w, h, bpp, out, out_cap);      // 给宽度/高度/位深和目标缓冲
for (y = 0; y < h; ++y)
    pack_submit_row(&s, y, row);              // 每行提交即写入目标缓冲
pack_finish(&s, &out_len);                    // 收尾，校验行数齐全
```

输出位布局与 `pack_rows` 完全一致。行号必须按 0..h-1 顺序提交，
乱序（`BITPACK_ERR_ROW_ORDER`）、重复（`BITPACK_ERR_ROW_DUP`）、
越界（`BITPACK_ERR_ROW_RANGE`）、像素值超位深（`BITPACK_ERR_VALUE`）、
finish 时缺行（`BITPACK_ERR_INCOMPLETE`）都会以错误码报出；
出错后状态不变，可修正后重提交同一行。

构建与测试：`make test`。
