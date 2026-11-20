#include "planar.h"
#include <stdlib.h>
#include <string.h>

static int row_bytes(int w) { return (w + 7) / 8; }

size_t planar_plane_bytes(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    return (size_t)row_bytes(w) * (size_t)h;
}

size_t planar_bytes(int w, int h, int bpp) {
    if (bpp <= 0 || bpp > 8) return 0;
    return planar_plane_bytes(w, h) * (size_t)bpp;
}

size_t chunky_bytes(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    return (size_t)w * (size_t)h;
}

size_t planar_buf_bytes(int w, int h, int bpp) {
    size_t c = chunky_bytes(w, h), p = planar_bytes(w, h, bpp);
    return c > p ? c : p;
}

/* 连续位流（MSB 优先）读写 n 位，0 <= n <= 8 */
static unsigned get_bits(const uint8_t* b, size_t off, int n) {
    unsigned v = 0;
    for (int j = 0; j < n; ++j) {
        size_t t = off + (size_t)j;
        v = (v << 1) | ((b[t >> 3] >> (7 - (t & 7))) & 1u);
    }
    return v;
}

static void set_bits(uint8_t* b, size_t off, int n, unsigned v) {
    for (int j = 0; j < n; ++j) {
        size_t t = off + (size_t)j;
        uint8_t m = (uint8_t)(0x80u >> (t & 7u));
        if ((v >> (n - 1 - j)) & 1u) b[t >> 3] |= m;
        else b[t >> 3] &= (uint8_t)~m;
    }
}

/* 每像素 1 字节 -> 每像素 bpp 位（原地，收缩，前向安全） */
static int compact_pixels(uint8_t* buf, size_t n, int bpp) {
    uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
    for (size_t i = 0; i < n; ++i) {
        uint8_t v = buf[i];
        if (v > vmax) return BP_ERR_RANGE;
        set_bits(buf, i * (size_t)bpp, bpp, v);
    }
    return BP_OK;
}

/* 每像素 bpp 位 -> 每像素 1 字节（原地，扩张，反向安全） */
static void expand_pixels(uint8_t* buf, size_t n, int bpp) {
    for (size_t i = n; i-- > 0; )
        buf[i] = (uint8_t)get_bits(buf, i * (size_t)bpp, bpp);
}

/* 每像素 b+1 位 -> b 位，丢弃最高位（原地，收缩，前向安全） */
static void shrink_pixels(uint8_t* buf, size_t n, int b) {
    unsigned keep = (1u << b) - 1u;
    for (size_t i = 0; i < n; ++i) {
        unsigned v = get_bits(buf, i * (size_t)(b + 1), b + 1) & keep;
        set_bits(buf, i * (size_t)b, b, v);
    }
}

/* 每像素 b 位 -> b+1 位，新最高位置 0（原地，扩张，反向安全） */
static void grow_pixels(uint8_t* buf, size_t n, int b) {
    for (size_t i = n; i-- > 0; ) {
        unsigned v = b ? get_bits(buf, i * (size_t)b, b) : 0;
        set_bits(buf, i * (size_t)(b + 1), b + 1, v);
    }
}

/* 从每像素 b+1 位的源中提取最高位平面（行尾补 0）到 plane */
static void peel_plane(const uint8_t* buf, int w, int h, int b, uint8_t* plane, size_t ps) {
    int rb = row_bytes(w);
    size_t i = 0;
    memset(plane, 0, ps);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x, ++i)
            if (get_bits(buf, i * (size_t)(b + 1), 1))
                plane[(size_t)y * (size_t)rb + ((size_t)x >> 3)] |= (uint8_t)(0x80u >> (x & 7));
}

/* 把 plane 合并为每像素 b+1 位源的最高位（忽略行尾补位） */
static void merge_plane(uint8_t* buf, int w, int h, int b, const uint8_t* plane) {
    int rb = row_bytes(w);
    size_t i = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x, ++i)
            if (plane[(size_t)y * (size_t)rb + ((size_t)x >> 3)] & (uint8_t)(0x80u >> (x & 7)))
                set_bits(buf, i * (size_t)(b + 1), 1, 1);
}

static int check_args(const uint8_t* buf, size_t buf_len, int w, int h, int bpp) {
    if (w <= 0 || h <= 0 || bpp <= 0 || bpp > 8) return BP_ERR_PLANES;
    if (!buf || buf_len < planar_buf_bytes(w, h, bpp)) return BP_ERR_LENGTH;
    return BP_OK;
}

int chunky_to_planar(uint8_t* buf, size_t buf_len, int w, int h, int bpp, uint8_t* scratch) {
    int rc = check_args(buf, buf_len, w, h, bpp);
    if (rc != BP_OK) return rc;
    size_t n = chunky_bytes(w, h), ps = planar_plane_bytes(w, h);
    uint8_t* sc = scratch;
    if (!sc) { sc = (uint8_t*)malloc(ps); if (!sc) return BP_ERR_LENGTH; }
    rc = compact_pixels(buf, n, bpp);
    if (rc == BP_OK) {
        for (int p = bpp - 1; p >= 0; --p) {
            peel_plane(buf, w, h, p, sc, ps);   /* 源 [0, ceil(n*(p+1)/8)) 与槽位不相交 */
            if (p > 0) shrink_pixels(buf, n, p); /* 收缩到 [0, ceil(n*p/8)) <= p*ps */
            memcpy(buf + (size_t)p * ps, sc, ps);
        }
    }
    if (!scratch) free(sc);
    return rc;
}

int planar_to_chunky(uint8_t* buf, size_t buf_len, int w, int h, int bpp, uint8_t* scratch) {
    int rc = check_args(buf, buf_len, w, h, bpp);
    if (rc != BP_OK) return rc;
    size_t n = chunky_bytes(w, h), ps = planar_plane_bytes(w, h);
    uint8_t* sc = scratch;
    if (!sc) { sc = (uint8_t*)malloc(ps); if (!sc) return BP_ERR_LENGTH; }
    for (int p = 0; p < bpp; ++p) {
        memcpy(sc, buf + (size_t)p * ps, ps); /* 暂存平面 p：扩张会覆盖其槽位 */
        grow_pixels(buf, n, p);               /* 扩张到 [0, ceil(n*(p+1)/8)) <= (p+1)*ps */
        merge_plane(buf, w, h, p, sc);
    }
    expand_pixels(buf, n, bpp);
    if (!scratch) free(sc);
    return BP_OK;
}
