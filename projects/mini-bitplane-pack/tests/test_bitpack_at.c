#define _POSIX_C_SOURCE 199309L
#include "../src/bitpack.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* 确定性伪随机数，保证测试可复现 */
static uint64_t rng_state = 0x9e3779b97f4a7c15ULL;
static uint64_t rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return rng_state >> 11;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* 独立参考：按格式定义逐位取第 bit 位（MSB 优先） */
static int ref_bit(const uint8_t* packed, size_t bit) {
    return (packed[bit >> 3] >> (7u - (bit & 7u))) & 1;
}

/* 1) 任意位偏移局部读：跨字节、跨行，与逐位参考及顺序解包对照 */
static void test_slices(void) {
    int sizes[][2] = {{8, 8}, {3, 3}, {5, 2}, {17, 9}, {64, 40}};
    int bpps[] = {1, 2, 4, 8};
    static uint8_t idx[4096], back[4096], packed[8192], seg[4096];

    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        int w = sizes[s][0], h = sizes[s][1];
        for (unsigned b = 0; b < sizeof(bpps) / sizeof(bpps[0]); ++b) {
            int bpp = bpps[b];
            uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
            size_t n = (size_t)w * h;
            size_t total_bits = n * (size_t)bpp;
            for (size_t i = 0; i < n; ++i) idx[i] = (uint8_t)((i * 7u + 3u) % (vmax + 1u));
            size_t len = 0;
            CHECK(pack_rows(idx, w, h, bpp, packed, sizeof(packed), &len) == 0);
            CHECK(unpack_rows(packed, len, w, h, bpp, back) == 0);

            for (int t = 0; t < 200; ++t) {
                size_t start = (size_t)(rnd() % total_bits);
                size_t maxbits = total_bits - start;
                size_t want = (size_t)(rnd() % (maxbits + 1u));
                if (want / (size_t)bpp > sizeof(seg)) want = sizeof(seg) * (size_t)bpp;
                size_t got = 0;
                CHECK(unpack_rows_at(packed, len, w, h, bpp, start, (long)want, seg, &got) == UNPACK_AT_OK);
                CHECK(got == want / (size_t)bpp);
                /* 与独立逐位参考对照（覆盖跨字节、非索引对齐起始） */
                int ok = 1;
                for (size_t i = 0; i < got && ok; ++i) {
                    uint8_t v = 0;
                    for (int bb = 0; bb < bpp; ++bb)
                        v = (uint8_t)((v << 1) | ref_bit(packed, start + i * (size_t)bpp + (size_t)bb));
                    if (v != seg[i]) ok = 0;
                }
                CHECK(ok);
                /* 索引对齐时与既有顺序解包结果对照（覆盖跨行） */
                if (start % (size_t)bpp == 0)
                    CHECK(memcmp(seg, back + start / (size_t)bpp, got) == 0);
            }
        }
    }
}

/* 2) 末尾不足一个索引：截断并报告实际数量 */
static void test_truncation(void) {
    uint8_t idx[5] = {1, 2, 3, 4, 5}, packed[4], seg[8];
    size_t len = 0, got = 999;
    CHECK(pack_rows(idx, 5, 1, 4, packed, sizeof(packed), &len) == 0); /* 总 20 位 = 5 个 4bpp 索引 */

    CHECK(unpack_rows_at(packed, len, 5, 1, 4, 0, 7, seg, &got) == UNPACK_AT_OK);
    CHECK(got == 1 && seg[0] == 1);            /* 7 位 = 1 个索引 + 3 位零头截断 */

    got = 999;
    CHECK(unpack_rows_at(packed, len, 5, 1, 4, 4, 3, seg, &got) == UNPACK_AT_OK);
    CHECK(got == 0);                            /* 不足一个索引 → 0 个，仍成功 */

    got = 0;
    CHECK(unpack_rows_at(packed, len, 5, 1, 4, 6, 9, seg, &got) == UNPACK_AT_OK);
    CHECK(got == 2);                            /* 跨字节起始（位 6..14）取 2 个索引 */

    got = 999;
    CHECK(unpack_rows_at(packed, len, 5, 1, 4, 18, 2, seg, &got) == UNPACK_AT_OK);
    CHECK(got == 0);                            /* 紧贴末尾、只剩零头 */
}

/* 3) 同一位置重复读取结果稳定 */
static void test_stable(void) {
    enum { W = 17, H = 9, BPP = 4 };
    uint8_t idx[W * H], packed[W * H], a[16], b[16];
    size_t len = 0;
    for (size_t i = 0; i < W * H; ++i) idx[i] = (uint8_t)(i * 11u % 16u);
    CHECK(pack_rows(idx, W, H, BPP, packed, sizeof(packed), &len) == 0);
    size_t start = 37; /* 非索引对齐、跨字节 */
    size_t got_a = 0, got_b = 0;
    CHECK(unpack_rows_at(packed, len, W, H, BPP, start, 16 * BPP, a, &got_a) == UNPACK_AT_OK);
    for (int r = 0; r < 8; ++r) {
        memset(b, 0xAA, sizeof(b));
        CHECK(unpack_rows_at(packed, len, W, H, BPP, start, 16 * BPP, b, &got_b) == UNPACK_AT_OK);
        CHECK(got_b == got_a);
        CHECK(memcmp(a, b, got_a) == 0);
    }
}

/* 4) 三类拒绝可区分 */
static void test_rejections(void) {
    uint8_t packed[1] = {0}, seg[4];
    size_t got = 0;
    /* w=2,h=2,bpp=2 → 总 8 位 */
    CHECK(unpack_rows_at(packed, 1, 2, 2, 2, 8, 1, seg, &got) == UNPACK_AT_ERR_START);
    CHECK(unpack_rows_at(packed, 1, 2, 2, 2, 100, 1, seg, &got) == UNPACK_AT_ERR_START);
    CHECK(unpack_rows_at(packed, 1, 2, 2, 2, 0, -1, seg, &got) == UNPACK_AT_ERR_NEG);
    CHECK(unpack_rows_at(packed, 1, 2, 2, 2, 4, -100, seg, &got) == UNPACK_AT_ERR_NEG);
    CHECK(unpack_rows_at(packed, 1, 2, 2, 2, 0, 9, seg, &got) == UNPACK_AT_ERR_SPAN);
    CHECK(unpack_rows_at(packed, 1, 2, 2, 2, 7, 2, seg, &got) == UNPACK_AT_ERR_SPAN);
    /* 三者互不相同 */
    CHECK(UNPACK_AT_ERR_START != UNPACK_AT_ERR_NEG);
    CHECK(UNPACK_AT_ERR_NEG != UNPACK_AT_ERR_SPAN);
    CHECK(UNPACK_AT_ERR_START != UNPACK_AT_ERR_SPAN);
    /* 紧贴边界的合法读不受影响 */
    CHECK(unpack_rows_at(packed, 1, 2, 2, 2, 6, 2, seg, &got) == UNPACK_AT_OK && got == 1);
    /* 参数类错误仍为 -1 */
    CHECK(unpack_rows_at(packed, 1, 2, 2, 9, 0, 1, seg, &got) == UNPACK_AT_ERR_ARG);
    CHECK(unpack_rows_at(packed, 0, 2, 2, 2, 0, 1, seg, &got) == UNPACK_AT_ERR_ARG);
}

/* 5) 对照：随机定位分段重建全图，与既有顺序解包逐字节一致 */
static void test_full_reconstruction(void) {
    enum { W = 17, H = 9, BPP = 4, N = W * H };
    uint8_t idx[N], back[N], recon[N], packed[N], seg[8];
    size_t len = 0;
    for (size_t i = 0; i < N; ++i) idx[i] = (uint8_t)((i * 5u + 1u) % 16u);
    CHECK(pack_rows(idx, W, H, BPP, packed, sizeof(packed), &len) == 0);
    CHECK(unpack_rows(packed, len, W, H, BPP, back) == 0);

    size_t total_bits = (size_t)N * BPP, pos = 0, off = 0;
    while (pos < total_bits) {
        size_t remain = total_bits - pos;
        size_t want = (size_t)BPP * (1u + rnd() % 7u); /* 每次 1..7 个索引 */
        if (want > remain) want = remain;              /* remain 必为 bpp 整数倍 */
        size_t got = 0;
        CHECK(unpack_rows_at(packed, len, W, H, BPP, pos, (long)want, seg, &got) == UNPACK_AT_OK);
        memcpy(recon + off, seg, got);
        off += got;
        pos += got * (size_t)BPP;
    }
    CHECK(off == N);
    CHECK(memcmp(recon, back, N) == 0);
}

/* 6) 性能：千级像素样例，1000 次随机定位总耗时 < 一次全量解包的 1/10 */
static void test_perf(void) {
    enum { W = 320, H = 240, BPP = 4, N = W * H, REPS = 10 };
    static uint8_t idx[N], back[N], packed[N * BPP / 8];
    for (size_t i = 0; i < N; ++i) idx[i] = (uint8_t)((i * 2654435761u >> 13) % 16u);
    size_t len = 0;
    CHECK(pack_rows(idx, W, H, BPP, packed, sizeof(packed), &len) == 0);

    uint64_t t0 = now_ns();
    for (int r = 0; r < REPS; ++r)
        CHECK(unpack_rows(packed, len, W, H, BPP, back) == 0);
    double full_avg = (double)(now_ns() - t0) / REPS;

    uint8_t seg[4];
    size_t got = 0;
    t0 = now_ns();
    for (int r = 0; r < REPS; ++r)
        for (int i = 0; i < 1000; ++i) {
            size_t start = (size_t)(rnd() % (N - 4)) * BPP;
            CHECK(unpack_rows_at(packed, len, W, H, BPP, start, 4 * BPP, seg, &got) == UNPACK_AT_OK);
        }
    double rand_avg = (double)(now_ns() - t0) / REPS;

    printf("perf: %d px, full unpack %.0f ns, 1000 random reads %.0f ns (%.2f%%)\n",
           N, full_avg, rand_avg, rand_avg / full_avg * 100.0);
    CHECK(rand_avg * 10.0 < full_avg);
}

int main(void) {
    test_slices();
    test_truncation();
    test_stable();
    test_rejections();
    test_full_reconstruction();
    test_perf();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
