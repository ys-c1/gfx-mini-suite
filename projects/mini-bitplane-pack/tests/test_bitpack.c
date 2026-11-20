#include "../src/bitpack.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

#define BIG_W 8192
#define BIG_H 96
#define BIG_BPP 4
static uint8_t big_row[BIG_W];
static uint8_t big_out[BIG_W * BIG_H / 2];
static uint8_t big_back[BIG_W * BIG_H];

int main(void) {
    int sizes[][2] = {{8, 8}, {3, 3}, {5, 2}, {1, 1}, {17, 9}};
    int bpps[] = {1, 2, 4, 8};
    uint8_t idx[4096], back[4096], packed[8192], packed2[8192];

    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        int w = sizes[s][0], h = sizes[s][1];
        for (unsigned b = 0; b < sizeof(bpps) / sizeof(bpps[0]); ++b) {
            int bpp = bpps[b];
            uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
            size_t n = (size_t)w * h;
            for (size_t i = 0; i < n; ++i) idx[i] = (uint8_t)((i * 7u + 3u) % (vmax + 1u));
            size_t len = 0;
            CHECK(pack_rows(idx, w, h, bpp, packed, sizeof(packed), &len) == 0);
            CHECK(len == packed_size(w, h, bpp));
            CHECK(unpack_rows(packed, len, w, h, bpp, back) == 0);
            CHECK(memcmp(idx, back, n) == 0);
            CHECK(count_used_values(idx, n, bpp) >= 1);

            /* 流式入口：与整幅入口逐字节一致 */
            bitpack_stream st;
            size_t len2 = 0;
            CHECK(pack_begin(&st, w, h, bpp, packed2, sizeof(packed2)) == BITPACK_OK);
            for (int y = 0; y < h; ++y)
                CHECK(pack_submit_row(&st, y, idx + (size_t)y * w) == BITPACK_OK);
            CHECK(pack_finish(&st, &len2) == BITPACK_OK);
            CHECK(len2 == len);
            CHECK(memcmp(packed, packed2, len) == 0);
        }
    }

    CHECK(pack_rows(idx, 8, 8, 4, packed, 1, (size_t[]){0}) == -1);
    uint8_t bad[4] = {16, 0, 0, 0};
    CHECK(pack_rows(bad, 2, 2, 4, packed, sizeof(packed), (size_t[]){0}) == -1);

    /* 流式错误路径：乱序 / 重复 / 越界 / 非法值 / 缺行 */
    {
        bitpack_stream st;
        uint8_t row[4] = {0, 1, 2, 3};
        uint8_t badrow[4] = {0, 4, 0, 0}; /* 4 超出 2bpp */
        CHECK(pack_begin(&st, 4, 3, 2, packed2, sizeof(packed2)) == BITPACK_OK);
        CHECK(pack_submit_row(&st, 1, row) == BITPACK_ERR_ROW_ORDER); /* 跳行 */
        CHECK(pack_submit_row(&st, -1, row) == BITPACK_ERR_ROW_RANGE);
        CHECK(pack_submit_row(&st, 3, row) == BITPACK_ERR_ROW_RANGE);
        CHECK(pack_submit_row(&st, 0, badrow) == BITPACK_ERR_VALUE);
        CHECK(pack_submit_row(&st, 0, row) == BITPACK_OK);            /* 出错后可重提交同一行 */
        CHECK(pack_submit_row(&st, 0, row) == BITPACK_ERR_ROW_DUP);   /* 重复 */
        CHECK(pack_finish(&st, (size_t[]){0}) == BITPACK_ERR_INCOMPLETE);
        CHECK(pack_submit_row(&st, 1, row) == BITPACK_OK);
        CHECK(pack_submit_row(&st, 2, row) == BITPACK_OK);
        size_t len = 0;
        CHECK(pack_finish(&st, &len) == BITPACK_OK);
        CHECK(len == packed_size(4, 3, 2));
        CHECK(pack_rows(row, 4, 1, 2, packed, sizeof(packed), (size_t[]){0}) == 0);
        CHECK(memcmp(packed, packed2, packed_size(4, 1, 2)) == 0); /* 首行内容一致 */
        CHECK(pack_begin(&st, 4, 3, 2, packed2, 1) == BITPACK_ERR_ARG);
        CHECK(pack_begin(&st, 4, 3, 9, packed2, sizeof(packed2)) == BITPACK_ERR_ARG);
    }

    /* 8192 宽长图：逐行喂，峰值 = 目标缓冲 + 一行 */
    {
        bitpack_stream st;
        size_t len = 0;
        CHECK(pack_begin(&st, BIG_W, BIG_H, BIG_BPP, big_out, sizeof(big_out)) == BITPACK_OK);
        for (int y = 0; y < BIG_H; ++y) {
            for (int x = 0; x < BIG_W; ++x)
                big_row[x] = (uint8_t)((x * 5 + y * 11) & 0xF);
            CHECK(pack_submit_row(&st, y, big_row) == BITPACK_OK);
        }
        CHECK(pack_finish(&st, &len) == BITPACK_OK);
        CHECK(len == packed_size(BIG_W, BIG_H, BIG_BPP));
        CHECK(unpack_rows(big_out, len, BIG_W, BIG_H, BIG_BPP, big_back) == 0);
        for (int y = 0; y < BIG_H; ++y)
            for (int x = 0; x < BIG_W; ++x)
                CHECK(big_back[(size_t)y * BIG_W + x] == (uint8_t)((x * 5 + y * 11) & 0xF));
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
