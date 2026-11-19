#define _POSIX_C_SOURCE 200809L
#include "../src/bitpack.h"
#include "legacy_ref.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails = 0;
static long checks = 0;

#define CHECK(c) do { \
    ++checks; \
    if (!(c)) { ++fails; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } \
} while (0)

#define CHECK_MSG(c, ...) do { \
    ++checks; \
    if (!(c)) { ++fails; printf("FAIL %s:%d  %s  (", __FILE__, __LINE__, #c); \
                printf(__VA_ARGS__); printf(")\n"); } \
} while (0)

static uint8_t rng_state[4] = {0x9e, 0x37, 0x79, 0xb9};
static uint32_t rng_u32(void) { /* xorshift128, deterministic */
    uint32_t t = rng_state[3];
    uint32_t s = rng_state[0];
    rng_state[3] = rng_state[2];
    rng_state[2] = rng_state[1];
    rng_state[1] = s;
    t ^= t << 11; t ^= t >> 8;
    rng_state[0] = t ^ s ^ (s >> 19);
    return rng_state[0];
}

static void hexline(const char* label, const uint8_t* p, size_t n) {
    printf("%s", label);
    for (size_t i = 0; i < n; ++i) printf("%02x", p[i]);
    printf("\n");
}

/* ---------------- A. legacy path, bytes unchanged ---------------- */

static void test_legacy_identity(void) {
    static uint8_t idx[65536], got[16384], ref[16384], back[65536], refback[65536];
    static const int dims[][2] = {
        {8,8},{3,3},{5,2},{1,1},{17,9},{1,64},{64,1},{2,3},{7,5},{31,17},{129,7}
    };
    long rows = 0, bytes = 0;
    for (unsigned s = 0; s < sizeof(dims)/sizeof(dims[0]); ++s) {
        int w = dims[s][0], h = dims[s][1];
        size_t n = (size_t)w * h;
        for (int bpp = 1; bpp <= 8; ++bpp) {
            uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
            for (size_t i = 0; i < n; ++i) idx[i] = (uint8_t)(rng_u32() & vmax);
            size_t len_got = 0, len_ref = 0;
            memset(got, 0xAA, sizeof(got));
            memset(ref, 0x55, sizeof(ref));
            int rg = pack_rows(idx, w, h, bpp, got, sizeof(got), &len_got);
            int rr = ref_pack_rows(idx, w, h, bpp, ref, sizeof(ref), &len_ref);
            CHECK(rg == 0 && rr == 0);
            CHECK(len_got == len_ref);
            CHECK(len_got == packed_size(w, h, bpp));
            CHECK(len_got == ref_packed_size(w, h, bpp));
            CHECK(memcmp(got, ref, len_got) == 0);          /* whole buffer */
            CHECK(packed_size(w, h, bpp) == ref_packed_size(w, h, bpp));
            CHECK(pack_rows(idx, w, h, bpp, got, sizeof(got), &len_got) == 0);
            CHECK(memcmp(got, ref, len_got) == 0);
            CHECK(unpack_rows(ref, len_ref, w, h, bpp, back) == 0);
            CHECK(ref_unpack_rows(ref, len_ref, w, h, bpp, refback) == 0);
            CHECK(memcmp(back, idx, n) == 0);
            CHECK(memcmp(back, refback, n) == 0);
            CHECK(count_used_values(idx, n, bpp) == ref_count_used_values(idx, n, bpp));
            ++rows; bytes += (long)len_got;
        }
    }
    printf("A. legacy identity: %ld (w,h,bpp) cases, %ld packed bytes compared byte-for-byte\n",
           rows, bytes);
}

static void test_legacy_errors_unchanged(void) {
    static uint8_t buf[256], out[256];
    size_t len = 0;
    /* every legacy pair must agree on both rc and the len output */
    struct { int w, h, bpp; size_t cap; } cases[] = {
        {8,8,4,1},{2,2,4,2},{0,4,4,256},{4,0,4,256},{4,4,0,256},
        {4,4,9,256},{4,4,-1,256},{-1,4,4,256},{4,-1,4,256}
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        size_t a = 123, b = 123;
        int rg = pack_rows(buf, cases[i].w, cases[i].h, cases[i].bpp, out, cases[i].cap, &a);
        int rr = ref_pack_rows(buf, cases[i].w, cases[i].h, cases[i].bpp, out, cases[i].cap, &b);
        CHECK(rg == rr);
        CHECK(a == b);
        int ug = unpack_rows(out, 0, cases[i].w, cases[i].h, cases[i].bpp, buf);
        int ur = ref_unpack_rows(out, 0, cases[i].w, cases[i].h, cases[i].bpp, buf);
        CHECK(ug == ur);
        CHECK(count_used_values(buf, 4, cases[i].bpp) == ref_count_used_values(buf, 4, cases[i].bpp));
    }
    uint8_t bad[4] = {16,0,0,0};
    CHECK(pack_rows(bad, 2, 2, 4, out, sizeof(out), &len) == -1);
    CHECK(ref_pack_rows(bad, 2, 2, 4, out, sizeof(out), &len) == -1);
    printf("A. legacy error paths: rc values identical to reference on %u cases\n",
           (unsigned)(sizeof(cases)/sizeof(cases[0])));
}

/* ---------------- B. bit orderings and mirror ---------------- */

static void test_orders_and_mirror(void) {
    static uint8_t idx[4096], msb[8192], lsb[8192], mirror[8192];
    static const int dims[][2] = {{1,1},{1,2},{2,1},{3,3},{3,5},{5,1},{7,3},{8,8},{13,7},{17,9}};
    long cases = 0;
    for (unsigned s = 0; s < sizeof(dims)/sizeof(dims[0]); ++s) {
        int w = dims[s][0], h = dims[s][1];
        size_t n = (size_t)w * h;
        for (int bpp = 1; bpp <= 8; ++bpp) {
            uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
            for (size_t i = 0; i < n; ++i) idx[i] = (uint8_t)(rng_u32() & vmax);
            uint64_t bm = 0, bl = 0;
            memset(msb, 0, sizeof(msb)); memset(lsb, 0, sizeof(lsb));
            int r1 = bitpack_write_run(idx, n, bpp, BITPACK_MSB_FIRST, 0, msb, sizeof(msb), &bm);
            int r2 = bitpack_write_run(idx, n, bpp, BITPACK_LSB_FIRST, 0, lsb, sizeof(lsb), &bl);
            CHECK(r1 == BITPACK_OK && r2 == BITPACK_OK);
            CHECK(bm == bl);
            CHECK(bm == (uint64_t)n * (uint64_t)bpp);
            CHECK(packed_size(w, h, bpp) == (size_t)((bm + 7u) / 8u));
            size_t nbytes = (size_t)((bm + 7u) / 8u);
            /* 1) the MSB-first stream API reproduces legacy output exactly */
            {
                static uint8_t legacy[8192];
                size_t llen = 0;
                CHECK(pack_rows(idx, w, h, bpp, legacy, sizeof(legacy), &llen) == 0);
                CHECK(llen == nbytes);
                CHECK(memcmp(legacy, msb, llen) == 0);
            }
            /* 2) the two streams are per-byte mirror images */
            CHECK(bitpack_mirror_bytes(msb, nbytes, mirror) == BITPACK_OK);
            CHECK(memcmp(mirror, lsb, nbytes) == 0);
            CHECK(bitpack_mirror_bytes(lsb, nbytes, mirror) == BITPACK_OK);
            CHECK(memcmp(mirror, msb, nbytes) == 0);
            /* 3) mirror in place is the same operation */
            memcpy(mirror, msb, nbytes);
            CHECK(bitpack_mirror_bytes(mirror, nbytes, mirror) == BITPACK_OK);
            CHECK(memcmp(mirror, lsb, nbytes) == 0);
            /* 4) both orders round-trip */
            static uint8_t back[4096];
            CHECK(bitpack_read_run(msb, nbytes, 0, n, bpp, BITPACK_MSB_FIRST, back) == BITPACK_OK);
            CHECK(memcmp(back, idx, n) == 0);
            CHECK(bitpack_read_run(lsb, nbytes, 0, n, bpp, BITPACK_LSB_FIRST, back) == BITPACK_OK);
            CHECK(memcmp(back, idx, n) == 0);
            /* 5) decoding is order-specific: reading the LSB-first bytes as
                   MSB-first takes the top bpp bits of byte 0 instead */
            if (bpp > 1) {
                uint8_t wrong = 0;
                CHECK(bitpack_read_run(lsb, nbytes, 0, 1, bpp, BITPACK_MSB_FIRST, &wrong) == BITPACK_OK);
                CHECK(wrong == (uint8_t)(lsb[0] >> (8 - bpp)));
            }
            ++cases;
        }
    }
    /* width not a multiple of 8: last index straddles a byte boundary */
    {
        memset(idx, 0, sizeof(idx));
        idx[0] = 1; idx[1] = 0; idx[2] = 1;      /* w=3, 1bpp -> bits 101xxxxx */
        uint8_t m[1] = {0}, l[1] = {0};
        uint64_t bits;
        CHECK(bitpack_write_run(idx, 3, 1, BITPACK_MSB_FIRST, 0, m, 1, &bits) == BITPACK_OK);
        CHECK(bitpack_write_run(idx, 3, 1, BITPACK_LSB_FIRST, 0, l, 1, &bits) == BITPACK_OK);
        CHECK(bits == 3);
        CHECK(m[0] == 0xA0 && l[0] == 0x05);
        CHECK(bitpack_mirror_byte(m[0]) == l[0]);
        printf("B. order/mirror: width-not-multiple-of-8 sample: w=3 bpp=1 idx=101 "
               "msb=%02x lsb=%02x mirror-ok\n", m[0], l[0]);
    }
    printf("B. order/mirror: %ld (w,h,bpp) cases, both orders round-trip, streams are exact mirrors\n",
           cases);
}

/* ---------------- C. arbitrary bit offset, multi-seg, cross-seg ---------------- */

static uint8_t g_expected[16384];   /* reference bit image, built bit by bit */

static void ref_put(uint64_t pos, int bit, bitpack_order order) {
    unsigned mask = (order == BITPACK_LSB_FIRST) ? (1u << (unsigned)(pos & 7u))
                                                 : (0x80u >> (unsigned)(pos & 7u));
    if (bit) g_expected[pos >> 3] |= (uint8_t)mask;
    else     g_expected[pos >> 3] &= (uint8_t)~mask;
}

static void test_offsets_and_segments(void) {
    static uint8_t buf[8192], idx[4096], back[4096], seg_idx[512];
    long cases = 0;
    for (int bpp = 1; bpp <= 8; ++bpp)
        for (int trial = 0; trial < 40; ++trial) {
            bitpack_order order = (trial & 1) ? BITPACK_LSB_FIRST : BITPACK_MSB_FIRST;
            uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
            uint64_t off1 = (uint64_t)(rng_u32() % 61u);            /* not byte aligned */
            size_t n1 = 1 + rng_u32() % 20u;
            uint64_t off2 = off1 + (uint64_t)n1 * (uint64_t)bpp - (rng_u32() % 7u); /* overlaps seg 1 */
            if (off2 > 500) off2 = 500;
            size_t n2 = 1 + rng_u32() % 20u;
            uint64_t off3 = off2 + (uint64_t)n2 * (uint64_t)bpp + (uint64_t)(rng_u32() % 9u); /* gap */
            if (off3 > 600) off3 = 600;
            size_t n3 = 1 + rng_u32() % 12u;

            for (size_t i = 0; i < n1; ++i) idx[i] = (uint8_t)(rng_u32() & vmax);
            for (size_t i = 0; i < n2; ++i) idx[n1 + i] = (uint8_t)(rng_u32() & vmax);
            for (size_t i = 0; i < n3; ++i) idx[n1 + n2 + i] = (uint8_t)(rng_u32() & vmax);

            memset(buf, 0x5A, sizeof(buf));                          /* sentinel */
            memset(g_expected, 0x5A, sizeof(g_expected));
            bitpack_write_seg segs[3] = {
                { off1, idx, n1 },
                { off2, idx + n1, n2 },
                { off3, idx + n1 + n2, n3 },
            };
            uint64_t bits = 0;
            int rc = bitpack_write(segs, 3, bpp, order, buf, sizeof(buf), &bits);
            CHECK(rc == BITPACK_OK);
            uint64_t want_bits = 0, a = off1 + (uint64_t)n1*bpp, b = off2 + (uint64_t)n2*bpp,
                     c = off3 + (uint64_t)n3*bpp;
            want_bits = a > b ? a : b; want_bits = want_bits > c ? want_bits : c;
            CHECK(bits == want_bits);

            /* independent reference: write bits one at a time, later seg wins */
            for (size_t s = 0; s < 3; ++s) {
                uint64_t pos = segs[s].bit_off;
                for (size_t i = 0; i < segs[s].count; ++i)
                    for (int k = bpp - 1; k >= 0; --k, ++pos)
                        ref_put(pos, (segs[s].idx[i] >> k) & 1, order);
            }
            size_t nbytes = (size_t)((bits + 7u) / 8u);
            CHECK_MSG(memcmp(buf, g_expected, nbytes) == 0,
                      "bpp=%d trial=%d off=%llu,%llu,%llu", bpp, trial,
                      (unsigned long long)off1, (unsigned long long)off2,
                      (unsigned long long)off3);
            /* bits beyond the written stream stay untouched */
            for (size_t i = nbytes + 1; i < 1024; ++i) CHECK(buf[i] == 0x5A);

            /* one read spanning all three segments and the gaps between them */
            uint64_t start = 0; size_t total = (size_t)((bits - start) / (uint64_t)bpp);
            if (total > 0 && total <= sizeof(back)) {
                memset(back, 0xEE, sizeof(back));
                CHECK(bitpack_read_run(buf, nbytes, start, total, bpp, order, back) == BITPACK_OK);
                /* every decoded index must equal what the bit image says */
                for (size_t i = 0; i < total; ++i) {
                    uint8_t expect = 0;
                    for (int k = 0; k < bpp; ++k)
                        expect = (uint8_t)((expect << 1) |
                            ((g_expected[(start + (uint64_t)i*bpp + (uint64_t)k) >> 3] >>
                              ((order == BITPACK_LSB_FIRST)
                                 ? (unsigned)((start + (uint64_t)i*bpp + (uint64_t)k) & 7u)
                                 : (7u - (unsigned)((start + (uint64_t)i*bpp + (uint64_t)k) & 7u)))) & 1u));
                    CHECK(back[i] == expect);
                }
                /* and the three sub-ranges independently */
                CHECK(bitpack_read_run(buf, nbytes, off1, n1, bpp, order, back) == BITPACK_OK);
                /* seg 2 overlapped seg 1 with an offset that is not a whole
                   number of indices, so only indices whose whole bit pattern
                   was rewritten by seg 2 can be compared */
                uint64_t tail_off = off1 + (uint64_t)n1*bpp;
                if (off2 + (uint64_t)n2*bpp > tail_off && (tail_off - off2) % (uint64_t)bpp == 0) {
                    size_t skip = (size_t)((tail_off - off2) / (uint64_t)bpp);
                    if (skip < n2) {
                        CHECK(bitpack_read_run(buf, nbytes, tail_off, n2 - skip, bpp, order, seg_idx) == BITPACK_OK);
                        CHECK(memcmp(seg_idx, idx + n1 + skip, n2 - skip) == 0);
                    }
                }
            }
            ++cases;
        }
    printf("C. offsets/segments: %ld randomized multi-segment writes (unaligned starts, "
           "overlap, gaps) match bit-by-bit reference\n", cases);
}

static void test_cross_boundary_explicit(void) {
    /* 4bpp indices written as two segments; read the seam in one call.
       Segment A: 5 indices at bit 0. Segment B: 5 indices at bit 20.
       Total 40 bits = 5 bytes. The seam at bit 20 is mid-byte (byte 2 bit 4). */
    uint8_t a[5] = {0x1, 0x2, 0x3, 0x4, 0x5};
    uint8_t b[5] = {0x6, 0x7, 0x8, 0x9, 0xA};
    uint8_t buf[8], out[16];
    uint64_t bits = 0;
    memset(buf, 0, sizeof(buf));
    bitpack_write_seg w[2] = { {0, a, 5}, {20, b, 5} };
    CHECK(bitpack_write(w, 2, 4, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits) == BITPACK_OK);
    CHECK(bits == 40);
    hexline("C. seam image (5 bytes, seam at bit 20) : ", buf, 5);
    CHECK(bitpack_read_run(buf, 5, 0, 10, 4, BITPACK_MSB_FIRST, out) == BITPACK_OK);
    CHECK(memcmp(out, a, 5) == 0 && memcmp(out + 5, b, 5) == 0);
    /* read a window that starts 2 indices before the seam and crosses it */
    CHECK(bitpack_read_run(buf, 5, 12, 6, 4, BITPACK_MSB_FIRST, out) == BITPACK_OK);
    CHECK(out[0] == 0x4 && out[1] == 0x5 && out[2] == 0x6 && out[3] == 0x7 && out[4] == 0x8 && out[5] == 0x9);
    printf("C. cross-boundary: read window starting at bit 12, 6 indices: "
           "%x %x %x %x %x %x (crosses seam)\n", out[0], out[1], out[2], out[3], out[4], out[5]);
}

/* ---------------- D. trailing partial index semantics ---------------- */

static void test_tail_semantics(void) {
    uint8_t buf[16], out[16];
    size_t count = 999;
    unsigned tail = 999;
    /* 3bpp stream of 5 indices = 15 bits; add 2 more bits = 17 bits.
       Complete indices = 5 (uses bits 0..15); tail = 2 bits. */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x92; buf[1] = 0x4E; buf[2] = 0x80;      /* 100 100 101 001 001 11 */
    int rc = bitpack_read_all(buf, 3, 17, 3, BITPACK_MSB_FIRST, out, 16, &count, &tail);
    CHECK(rc == BITPACK_OK);
    CHECK(count == 5);
    CHECK(tail == 2);
    /* the partial tail is never decoded into out (out[5] untouched) */
    CHECK(bitpack_read_all(buf, 2, 17, 3, BITPACK_MSB_FIRST, out, 16, &count, &tail)
          == BITPACK_ERR_LEN);                        /* 2 bytes = 16 bits < 17 */
    CHECK(bitpack_read_all(buf, 3, 17, 3, BITPACK_MSB_FIRST, out, 4, &count, &tail)
          == BITPACK_ERR_CAP);                        /* 5 indices do not fit in 4 */
    CHECK(bitpack_read_all(buf, 3, 15, 3, BITPACK_MSB_FIRST, out, 16, &count, &tail) == BITPACK_OK);
    CHECK(count == 5 && tail == 0);
    CHECK(bitpack_read_all(buf, 3, 16, 3, BITPACK_MSB_FIRST, out, 16, &count, &tail) == BITPACK_OK);
    CHECK(count == 5 && tail == 1);
    CHECK(bitpack_read_all(buf, 3, 0, 3, BITPACK_MSB_FIRST, out, 16, &count, &tail) == BITPACK_OK);
    CHECK(count == 0 && tail == 0);
    CHECK(bitpack_read_all(buf, 3, 22, 7, BITPACK_MSB_FIRST, out, 16, &count, &tail) == BITPACK_OK);
    CHECK(count == 3 && tail == 1);                 /* 22 bits / 7bpp = 3 idx + 1 bit */
    printf("D. tail semantics: avail=17 bits @3bpp -> count=5, tail=2 (partial index not decoded); "
           "tail=0/tail=1/tail=0 all explicit\n");
}

/* ---------------- E. distinct error codes ---------------- */

static void test_error_codes(void) {
    uint8_t buf[16] = {0}, idx[8] = {1,2,3,4,5,6,7,0}, out[8];
    uint64_t bits;
    int codes[8];
    int n = 0;
    bitpack_write_seg g = {0, idx, 4};
    bitpack_read_seg r = {0, out, 4};

    codes[n++] = bitpack_write(&g, 1, 0,  BITPACK_MSB_FIRST, buf, sizeof(buf), &bits);  /* bpp */
    codes[n++] = bitpack_write(&g, 1, 9,  BITPACK_MSB_FIRST, buf, sizeof(buf), &bits);
    codes[n++] = bitpack_write(&g, 1, -3, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits);
    codes[n++] = bitpack_write(&g, 1, 4,  (bitpack_order)7,  buf, sizeof(buf), &bits);  /* order */
    codes[n++] = bitpack_write(&g, 1, 4,  BITPACK_MSB_FIRST, buf, 1, &bits);           /* cap */
    codes[n++] = bitpack_write(&g, 1, 4,  BITPACK_MSB_FIRST, NULL, sizeof(buf), &bits);/* arg */
    codes[n++] = bitpack_write(NULL, 0, 4, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits);
    {
        uint8_t bad[8] = {0,0,16,0,0,0,0,0};
        bitpack_write_seg bg = {0, bad, 4};
        codes[n++] = bitpack_write(&bg, 1, 4, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits); /* range */
    }
    for (int i = 0; i < n; ++i) CHECK(codes[i] < 0);
    CHECK(codes[0] == BITPACK_ERR_BPP);
    CHECK(codes[1] == BITPACK_ERR_BPP);
    CHECK(codes[2] == BITPACK_ERR_BPP);
    CHECK(codes[3] == BITPACK_ERR_ORDER);
    CHECK(codes[4] == BITPACK_ERR_CAP);
    CHECK(codes[5] == BITPACK_ERR_ARG);
    CHECK(codes[6] == BITPACK_ERR_ARG);
    CHECK(codes[7] == BITPACK_ERR_RANGE);
    /* the six error classes that can occur on the write path are distinct */
    CHECK(BITPACK_ERR_ARG != BITPACK_ERR_BPP && BITPACK_ERR_ARG != BITPACK_ERR_CAP &&
          BITPACK_ERR_ARG != BITPACK_ERR_RANGE && BITPACK_ERR_ARG != BITPACK_ERR_LEN &&
          BITPACK_ERR_ARG != BITPACK_ERR_OFFSET && BITPACK_ERR_ARG != BITPACK_ERR_ORDER &&
          BITPACK_ERR_BPP != BITPACK_ERR_CAP && BITPACK_ERR_BPP != BITPACK_ERR_RANGE &&
          BITPACK_ERR_BPP != BITPACK_ERR_LEN && BITPACK_ERR_BPP != BITPACK_ERR_OFFSET &&
          BITPACK_ERR_BPP != BITPACK_ERR_ORDER && BITPACK_ERR_CAP != BITPACK_ERR_RANGE &&
          BITPACK_ERR_CAP != BITPACK_ERR_LEN && BITPACK_ERR_CAP != BITPACK_ERR_OFFSET &&
          BITPACK_ERR_CAP != BITPACK_ERR_ORDER && BITPACK_ERR_RANGE != BITPACK_ERR_LEN &&
          BITPACK_ERR_RANGE != BITPACK_ERR_OFFSET && BITPACK_ERR_RANGE != BITPACK_ERR_ORDER &&
          BITPACK_ERR_LEN != BITPACK_ERR_OFFSET && BITPACK_ERR_LEN != BITPACK_ERR_ORDER &&
          BITPACK_ERR_OFFSET != BITPACK_ERR_ORDER);
    /* read-side codes */
    memset(buf, 0, sizeof(buf));
    CHECK(bitpack_read(buf, sizeof(buf), &r, 1, 0, BITPACK_MSB_FIRST) == BITPACK_ERR_BPP);
    CHECK(bitpack_read(buf, sizeof(buf), &r, 1, 4, (bitpack_order)2) == BITPACK_ERR_ORDER);
    CHECK(bitpack_read(buf, 1, &r, 1, 4, BITPACK_MSB_FIRST) == BITPACK_ERR_LEN);
    CHECK(bitpack_read(buf, sizeof(buf), NULL, 0, 4, BITPACK_MSB_FIRST) == BITPACK_ERR_ARG);
    CHECK(bitpack_read(NULL, sizeof(buf), &r, 1, 4, BITPACK_MSB_FIRST) == BITPACK_ERR_ARG);
    CHECK(bitpack_read_run(buf, sizeof(buf), 0, 1, 4, BITPACK_MSB_FIRST, NULL) == BITPACK_ERR_ARG);
    /* offset overflow, distinct from capacity */
    {
        bitpack_write_seg og = {UINT64_MAX - 3, idx, 4};
        CHECK(bitpack_write(&og, 1, 4, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits)
              == BITPACK_ERR_OFFSET);
        bitpack_read_seg org = {UINT64_MAX - 3, out, 4};
        CHECK(bitpack_read(buf, sizeof(buf), &org, 1, 4, BITPACK_MSB_FIRST)
              == BITPACK_ERR_OFFSET);
    }
    /* index range: every bpp boundary */
    for (int bpp = 1; bpp <= 8; ++bpp) {
        uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
        uint8_t one_ok[2] = { vmax, 0 };
        uint8_t one_bad[2] = { (uint8_t)(vmax + 1), 0 };
        bitpack_write_seg ok = {0, one_ok, 1}, bad = {0, one_bad, 1};
        if (bpp == 8) {
            CHECK(bitpack_write(&ok, 1, 8, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits) == BITPACK_OK);
        } else {
            CHECK(bitpack_write(&ok, 1, bpp, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits) == BITPACK_OK);
            CHECK(bitpack_write(&bad, 1, bpp, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits)
                  == BITPACK_ERR_RANGE);
        }
    }
    printf("E. error codes: BPP=%d ARG=%d CAP=%d RANGE=%d LEN=%d OFFSET=%d ORDER=%d "
           "(6 distinct classes on write, all exercised)\n",
           BITPACK_ERR_BPP, BITPACK_ERR_ARG, BITPACK_ERR_CAP, BITPACK_ERR_RANGE,
           BITPACK_ERR_LEN, BITPACK_ERR_OFFSET, BITPACK_ERR_ORDER);
}

static void test_failed_write_leaves_buffer_untouched(void) {
    uint8_t buf[16], copy[16];
    uint8_t idx[4] = {1,2,3,16};   /* 16 illegal at 4bpp (max 15) */
    uint64_t bits;
    memset(buf, 0xC3, sizeof(buf));
    memcpy(copy, buf, sizeof(buf));
    bitpack_write_seg g = {3, idx, 4};
    CHECK(bitpack_write(&g, 1, 4, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits) == BITPACK_ERR_RANGE);
    CHECK(memcmp(buf, copy, sizeof(buf)) == 0);
    /* and a successful empty-count segment does not extend the stream or write */
    bitpack_write_seg e = {12345, idx, 0};
    CHECK(bitpack_write(&e, 1, 4, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits) == BITPACK_OK);
    CHECK(bits == 0);
    CHECK(memcmp(buf, copy, sizeof(buf)) == 0);
    /* a bad later segment must also leave the buffer untouched: the earlier
       segment's valid indices must not have been committed */
    {
        uint8_t bp[16], bp2[16];
        uint8_t good[3] = {1,2,3};
        uint8_t bad[2] = {4,0x99};
        bitpack_write_seg two[2] = { {0, good, 3}, {20, bad, 2} };
        memset(bp, 0x5E, sizeof(bp));
        memcpy(bp2, bp, sizeof(bp));
        CHECK(bitpack_write(two, 2, 4, BITPACK_LSB_FIRST, bp, sizeof(bp), &bits)
              == BITPACK_ERR_RANGE);
        CHECK(memcmp(bp, bp2, sizeof(bp)) == 0);
    }
    printf("E. atomic errors: rejected write leaves output buffer byte-identical "
           "(including partial multi-segment writes)\n");
}

/* ---------------- F. read_all over segments + boundary cases ---------------- */

static void test_crossing_all(void) {
    /* Build one long stream via 4 unaligned segments, then decode it all
       with a single read_all and verify index-for-index. */
    uint8_t buf[4096], want[2048], got[2048];
    uint64_t offs[4]; size_t lens[4];
    uint64_t bits = 0;
    for (int bpp = 1; bpp <= 8; ++bpp) {
        uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
        uint64_t pos = 0;
        size_t k = 0, total = 0;
        for (int s = 0; s < 4; ++s) {
            offs[s] = pos;
            lens[s] = 11 + s * 3;                 /* 11,14,17,20 indices */
            for (size_t i = 0; i < lens[s]; ++i) want[k + i] = (uint8_t)(rng_u32() & vmax);
            pos += (uint64_t)lens[s] * (uint64_t)bpp;
            k += lens[s];
        }
        total = k;
        bitpack_write_seg segs[4];
        size_t at = 0;
        for (int s = 0; s < 4; ++s) { segs[s].bit_off = offs[s]; segs[s].idx = want + at; segs[s].count = lens[s]; at += lens[s]; }
        memset(buf, 0, sizeof(buf));
        CHECK(bitpack_write(segs, 4, bpp, BITPACK_MSB_FIRST, buf, sizeof(buf), &bits) == BITPACK_OK);
        size_t count = 0; unsigned tail = 77;
        CHECK(bitpack_read_all(buf, (bits + 7) / 8, bits, bpp, BITPACK_MSB_FIRST,
                               got, sizeof(got), &count, &tail) == BITPACK_OK);
        CHECK(count == total);
        CHECK(tail == 0);
        CHECK(memcmp(got, want, total) == 0);
        /* decode a window that starts mid-stream and crosses all four seams */
        if (bits > 16) {
            uint64_t woff = 5;                       /* deliberately not byte aligned */
            size_t wn = (size_t)((bits - woff) / (uint64_t)bpp);
            CHECK(bitpack_read_run(buf, (bits + 7) / 8, woff, wn, bpp, BITPACK_MSB_FIRST, got) == BITPACK_OK);
            /* every decoded value must equal the corresponding windowed index */
            for (size_t i = 0; i < wn; ++i) {
                uint8_t v = 0;
                uint64_t p = woff + (uint64_t)i * (uint64_t)bpp;
                for (int k = 0; k < bpp; ++k)
                    v = (uint8_t)((v << 1) | ((buf[(p + (uint64_t)k) >> 3] >>
                        (7u - (unsigned)((p + (uint64_t)k) & 7u))) & 1u));
                CHECK(got[i] == v);
            }
        }
    }
    /* one read call, several disjoint windows, both orders */
    {
        uint8_t buf[32], a[8], b[8], c[8];
        uint64_t bits = 0;
        uint8_t src[8] = {1,2,3,4,5,6,7,8};
        memset(buf, 0, sizeof(buf));
        CHECK(bitpack_write_run(src, 8, 4, BITPACK_LSB_FIRST, 0, buf, sizeof(buf), &bits) == BITPACK_OK);
        memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b)); memset(c, 0, sizeof(c));
        bitpack_read_seg rs[3] = { {0, a, 2}, {12, b, 3}, {24, c, 2} };
        CHECK(bitpack_read(buf, (bits + 7) / 8, rs, 3, 4, BITPACK_LSB_FIRST) == BITPACK_OK);
        CHECK(a[0] == 1 && a[1] == 2 && b[0] == 4 && b[1] == 5 && b[2] == 6 && c[0] == 7 && c[1] == 8);
        printf("F. multi-segment read: 3 disjoint windows decoded in one call\n");
    }
    printf("F. whole-stream decode across 4 unaligned segments: exact for bpp 1..8\n");
}

/* ---------------- G. throughput + memory, 1,000,000 indices ---------------- */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void test_perf(void) {
    const size_t N = 1000000;
    static uint8_t raw[1000000];
    printf("G. throughput @ %zu indices (write, best of 5 timed reps after warmup)\n", N);
    printf("   %-4s %-12s %10s %12s %12s %10s\n",
           "bpp", "order", "bytes", "Mindex/s", "MiB/s", "ns/index");
    for (int bpp = 1; bpp <= 8; ++bpp) {
        uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
        for (size_t i = 0; i < N; ++i) raw[i] = (uint8_t)(rng_u32() & vmax);
        size_t need = (N * (size_t)bpp + 7u) / 8u;
        uint8_t* out = (uint8_t*)malloc(need);
        if (!out) { printf("   malloc failed\n"); ++fails; continue; }
        for (int order = 0; order < 2; ++order) {
            bitpack_order od = order ? BITPACK_LSB_FIRST : BITPACK_MSB_FIRST;
            double best = 1e9;
            uint64_t bits = 0;
            /* untimed warmup, then 5 timed reps, report the best */
            memset(out, 0, need);
            if (bitpack_write_run(raw, N, bpp, od, 0, out, need, &bits) != BITPACK_OK) {
                printf("   warmup failed\n"); ++fails; break;
            }
            for (int rep = 0; rep < 5; ++rep) {
                memset(out, 0, need);
                double t0 = now_sec();
                int rc = bitpack_write_run(raw, N, bpp, od, 0, out, need, &bits);
                double dt = now_sec() - t0;
                if (rc != BITPACK_OK) { printf("   rc=%d\n", rc); ++fails; break; }
                if (dt < best) best = dt;
            }
            CHECK(bits == (uint64_t)N * (uint64_t)bpp);
            printf("   %-4d %-12s %10zu %12.1f %12.1f %10.1f\n", bpp,
                   order ? "LSB-first" : "MSB-first", need,
                   (double)N / best / 1e6,
                   (double)need / best / (1024.0 * 1024.0),
                   best * 1e9 / (double)N);
        }
        /* legacy row path on the same data, as a regression baseline */
        {
            int w = 1000, h = 1000;              /* 1,000,000 indices */
            size_t llen = 0;
            double best = 1e9;
            for (int rep = 0; rep < 6; ++rep) {
                double t0 = now_sec();
                int rc = ref_pack_rows(raw, w, h, bpp, out, need, &llen);
                double dt = now_sec() - t0;
                if (rc != 0) { printf("   legacy rc=%d\n", rc); ++fails; break; }
                if (dt < best) best = dt;
            }
            CHECK(llen == need);
            printf("   %-4d %-12s %10zu %12.1f %12.1f %10.1f\n", bpp, "legacy-row", need,
                   (double)N / best / 1e6,
                   (double)need / best / (1024.0 * 1024.0),
                   best * 1e9 / (double)N);
        }
        free(out);
    }
}

int main(void) {
    printf("== mini-bitplane-pack full test run ==\n");
    test_legacy_identity();
    test_legacy_errors_unchanged();
    test_orders_and_mirror();
    test_offsets_and_segments();
    test_cross_boundary_explicit();
    test_tail_semantics();
    test_error_codes();
    test_failed_write_leaves_buffer_untouched();
    test_crossing_all();
    test_perf();
    printf("\n%ld checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
