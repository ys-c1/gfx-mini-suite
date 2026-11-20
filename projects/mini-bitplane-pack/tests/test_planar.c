#include "../src/planar.h"
#include "../src/bitpack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void fill_chunky(uint8_t* idx, size_t n, int bpp) {
    unsigned vmax = (1u << bpp) - 1u;
    for (size_t i = 0; i < n; ++i) idx[i] = (uint8_t)((i * 7u + 3u) % (vmax + 1u));
    if (n) { idx[0] = 0; idx[n - 1] = (uint8_t)vmax; }
}

/* 构造补位为 0 的规范位平面数据 */
static void fill_planar(uint8_t* buf, int w, int h, int bpp) {
    int rb = (w + 7) / 8;
    size_t ps = planar_plane_bytes(w, h);
    for (size_t j = 0; j < ps * (size_t)bpp; ++j) buf[j] = (uint8_t)(j * 31u + 7u);
    if (w % 8) {
        uint8_t keep = (uint8_t)(0xFFu << (8 - (w % 8)));
        for (int p = 0; p < bpp; ++p)
            for (int y = 0; y < h; ++y)
                buf[(size_t)p * ps + (size_t)y * (size_t)rb + (size_t)rb - 1] &= keep;
    }
}

/* 往返：chunky->planar->chunky 与 planar->chunky->planar 均逐字节一致 */
static void roundtrip(int w, int h, int bpp, int null_scratch) {
    size_t n = chunky_bytes(w, h), ps = planar_plane_bytes(w, h);
    size_t P = planar_bytes(w, h, bpp), need = planar_buf_bytes(w, h, bpp);
    uint8_t* orig = (uint8_t*)malloc(n);
    uint8_t* buf = (uint8_t*)malloc(need);
    uint8_t* ref = (uint8_t*)malloc(P > 0 ? P : 1);
    uint8_t* sc = null_scratch ? NULL : (uint8_t*)malloc(ps);

    fill_chunky(orig, n, bpp);
    memcpy(buf, orig, n);
    CHECK(chunky_to_planar(buf, need, w, h, bpp, sc) == BP_OK);
    memcpy(ref, buf, P);                                   /* 规范平面形态 */
    CHECK(planar_to_chunky(buf, need, w, h, bpp, sc) == BP_OK);
    CHECK(memcmp(buf, orig, n) == 0);                      /* 交错侧往返一致 */
    CHECK(chunky_to_planar(buf, need, w, h, bpp, sc) == BP_OK);
    CHECK(memcmp(buf, ref, P) == 0);                       /* 平面侧往返一致 */

    fill_planar(buf, w, h, bpp);
    memcpy(ref, buf, P);
    CHECK(planar_to_chunky(buf, need, w, h, bpp, sc) == BP_OK);
    CHECK(chunky_to_planar(buf, need, w, h, bpp, sc) == BP_OK);
    CHECK(memcmp(buf, ref, P) == 0);                       /* 规范平面数据往返一致 */

    free(orig); free(buf); free(ref); free(sc);
}

/* 非零补位被规范化为 0，像素数据不受影响 */
static void padding_normalized(int w, int h, int bpp) {
    int rb = (w + 7) / 8;
    size_t ps = planar_plane_bytes(w, h), P = planar_bytes(w, h, bpp);
    size_t need = planar_buf_bytes(w, h, bpp);
    uint8_t* buf = (uint8_t*)malloc(need);
    uint8_t* ref = (uint8_t*)malloc(P);
    uint8_t* sc = (uint8_t*)malloc(ps);
    uint8_t tail = (uint8_t)(0xFFu >> (w % 8));

    fill_planar(buf, w, h, bpp);
    for (int p = 0; p < bpp; ++p)
        for (int y = 0; y < h; ++y)
            buf[(size_t)p * ps + (size_t)y * (size_t)rb + (size_t)rb - 1] |= tail; /* 弄脏补位 */
    CHECK(planar_to_chunky(buf, need, w, h, bpp, sc) == BP_OK);
    CHECK(chunky_to_planar(buf, need, w, h, bpp, sc) == BP_OK);
    fill_planar(ref, w, h, bpp);
    CHECK(memcmp(buf, ref, P) == 0);                       /* 与规范形态一致 */

    free(buf); free(ref); free(sc);
}

/* 金丝雀：转换不得越出一个平面暂存与缓冲区边界 */
static void canary(int w, int h, int bpp) {
    const size_t G = 16;
    size_t n = chunky_bytes(w, h), ps = planar_plane_bytes(w, h);
    size_t need = planar_buf_bytes(w, h, bpp);
    uint8_t* raw = (uint8_t*)malloc(need + 2 * G);
    uint8_t* sraw = (uint8_t*)malloc(ps + 2 * G);
    memset(raw, 0xA5, need + 2 * G);
    memset(sraw, 0x5A, ps + 2 * G);
    uint8_t* buf = raw + G;
    uint8_t* sc = sraw + G;
    fill_chunky(buf, n, bpp);
    CHECK(chunky_to_planar(buf, need, w, h, bpp, sc) == BP_OK);
    CHECK(planar_to_chunky(buf, need, w, h, bpp, sc) == BP_OK);
    for (size_t i = 0; i < G; ++i) {
        CHECK(raw[i] == 0xA5 && raw[G + need + i] == 0xA5);
        CHECK(sraw[i] == 0x5A && sraw[G + ps + i] == 0x5A);
    }
    free(raw); free(sraw);
}

int main(void) {
    int sizes[][2] = {{1,1},{8,8},{5,2},{3,3},{7,1},{9,9},{16,16},{17,9},{31,5},{64,64}};
    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        for (int bpp = 1; bpp <= 8; ++bpp) {
            roundtrip(sizes[s][0], sizes[s][1], bpp, 0);
            roundtrip(sizes[s][0], sizes[s][1], bpp, 1); /* NULL scratch -> 内部分配一个平面 */
        }

    /* 尾部补位：w 非 8 倍数 */
    padding_normalized(5, 3, 2);
    padding_normalized(17, 9, 8);
    padding_normalized(13, 1, 1);

    /* 手工金样：布局与补位规则锁定 */
    {
        uint8_t buf[8] = {0,1,2,3,0,1,2,3}, sc[1];
        CHECK(chunky_to_planar(buf, sizeof(buf), 8, 1, 2, sc) == BP_OK);
        CHECK(buf[0] == 0x55 && buf[1] == 0x33);           /* plane0=bit0, plane1=bit1 */
        CHECK(planar_to_chunky(buf, sizeof(buf), 8, 1, 2, sc) == BP_OK);
        CHECK(buf[0]==0 && buf[1]==1 && buf[2]==2 && buf[3]==3);
    }
    {
        uint8_t buf[8] = {1,0,1,1,0,0,0,0}, sc[1];
        CHECK(chunky_to_planar(buf, sizeof(buf), 5, 1, 1, sc) == BP_OK);
        CHECK(buf[0] == 0xB0);                             /* 10110 000：补位为 0 */
    }

    /* 三类错误码互不相同且各归其类 */
    CHECK(BP_ERR_PLANES != BP_ERR_LENGTH && BP_ERR_LENGTH != BP_ERR_RANGE && BP_ERR_PLANES != BP_ERR_RANGE);
    {
        uint8_t buf[64], sc[8];
        CHECK(chunky_to_planar(buf, sizeof(buf), 8, 8, 0, sc) == BP_ERR_PLANES);
        CHECK(chunky_to_planar(buf, sizeof(buf), 8, 8, 9, sc) == BP_ERR_PLANES);
        CHECK(planar_to_chunky(buf, sizeof(buf), 0, 8, 4, sc) == BP_ERR_PLANES);
        CHECK(planar_to_chunky(buf, sizeof(buf), 8, -1, 4, sc) == BP_ERR_PLANES);
        CHECK(chunky_to_planar(buf, 4, 8, 8, 4, sc) == BP_ERR_LENGTH);
        CHECK(planar_to_chunky(buf, 4, 8, 8, 4, sc) == BP_ERR_LENGTH);
        buf[0] = 16;
        CHECK(chunky_to_planar(buf, sizeof(buf), 2, 2, 4, sc) == BP_ERR_RANGE);
        buf[0] = 255;
        CHECK(chunky_to_planar(buf, sizeof(buf), 2, 2, 4, sc) == BP_ERR_RANGE);
    }

    /* 原交错路径（pack_rows/unpack_rows）行为锁定，不许变 */
    {
        uint8_t idx[4] = {0,1,2,3}, packed[4], back[4];
        size_t len = 0;
        CHECK(pack_rows(idx, 4, 1, 2, packed, sizeof(packed), &len) == 0);
        CHECK(len == 1 && packed[0] == 0x1B);
        CHECK(unpack_rows(packed, len, 4, 1, 2, back) == 0);
        CHECK(memcmp(idx, back, 4) == 0);
        CHECK(packed_size(4, 1, 2) == 1);
    }

    canary(17, 9, 8);
    canary(8, 8, 1);
    canary(31, 5, 4);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
