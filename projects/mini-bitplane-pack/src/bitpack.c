#include "bitpack.h"

/* ------------------------------------------------------------------ *
 * Legacy row API.
 *
 * The loops below are the original implementation unchanged: same
 * argument checks, same -1 returns, same MSB-first bit order (bit 7 of
 * byte 0 first), so their output is byte-for-byte identical.
 * ------------------------------------------------------------------ */

size_t packed_size(int w, int h, int bpp) {
    if (w <= 0 || h <= 0 || bpp <= 0 || bpp > 8) return 0;
    size_t bits = (size_t)w * (size_t)h * (size_t)bpp;
    return (bits + 7u) / 8u;
}

int pack_rows(const uint8_t* idx, int w, int h, int bpp, uint8_t* out, size_t out_cap, size_t* out_len) {
    if (!idx || !out || !out_len) return -1;
    if (w <= 0 || h <= 0 || bpp <= 0 || bpp > 8) return -1;
    size_t need = packed_size(w, h, bpp);
    if (need == 0 || need > out_cap) return -1;
    uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
    for (size_t i = 0; i < need; ++i) out[i] = 0;
    size_t bit = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t v = idx[(size_t)y * w + x];
            if (v > vmax) return -1;
            for (int b = bpp - 1; b >= 0; --b) {
                if (v & (1u << b)) out[bit >> 3] |= (uint8_t)(0x80u >> (bit & 7u));
                ++bit;
            }
        }
    *out_len = need;
    return 0;
}

int unpack_rows(const uint8_t* packed, size_t packed_len, int w, int h, int bpp, uint8_t* idx) {
    if (!packed || !idx) return -1;
    if (w <= 0 || h <= 0 || bpp <= 0 || bpp > 8) return -1;
    size_t need = packed_size(w, h, bpp);
    if (packed_len < need) return -1;
    size_t bit = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t v = 0;
            for (int b = 0; b < bpp; ++b) {
                v = (uint8_t)(v << 1);
                if (packed[bit >> 3] & (uint8_t)(0x80u >> (bit & 7u))) v |= 1u;
                ++bit;
            }
            idx[(size_t)y * w + x] = v;
        }
    return 0;
}

int count_used_values(const uint8_t* idx, size_t n, int bpp) {
    if (!idx || bpp <= 0 || bpp > 8) return -1;
    uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
    int seen[256];
    for (int i = 0; i < 256; ++i) seen[i] = 0;
    for (size_t i = 0; i < n; ++i) { if (idx[i] > vmax) return -1; seen[idx[i]] = 1; }
    int n_used = 0;
    for (int i = 0; i <= vmax; ++i) n_used += seen[i];
    return n_used;
}

/* ------------------------------------------------------------------ *
 * Generalized stream API.
 *
 * All access goes through two bit primitives.  They work directly on
 * the caller's buffer, so no scratch block is allocated anywhere.
 * ------------------------------------------------------------------ */

static int valid_bpp(int bpp) { return bpp >= 1 && bpp <= 8; }

/* Stream bit at absolute position pos.  MSB-first uses 0x80 >> (pos&7);
 * LSB-first uses 0x01 << (pos&7).  Caller guarantees pos/8 < cap. */
static int bit_get(const uint8_t* buf, uint64_t pos, bitpack_order order) {
    unsigned mask = (order == BITPACK_LSB_FIRST)
                  ? (1u << (unsigned)(pos & 7u))
                  : (0x80u >> (unsigned)(pos & 7u));
    return (buf[pos >> 3] & mask) != 0;
}

static void bit_put(uint8_t* buf, uint64_t pos, bitpack_order order, int value) {
    unsigned mask = (order == BITPACK_LSB_FIRST)
                  ? (1u << (unsigned)(pos & 7u))
                  : (0x80u >> (unsigned)(pos & 7u));
    if (value) buf[pos >> 3] |= (uint8_t)mask;
    else       buf[pos >> 3] &= (uint8_t)~mask;
}

static int order_ok(bitpack_order order) {
    return order == BITPACK_MSB_FIRST || order == BITPACK_LSB_FIRST;
}

/* 0 when [off, off+bits) is representable; -1 on uint64 overflow. */
static int range_fits(uint64_t off, uint64_t bits) {
    return bits <= UINT64_MAX - off;
}

static int validate_write(const bitpack_write_seg* segs, size_t nsegs,
                          int bpp, bitpack_order order,
                          size_t out_cap, uint64_t* out_bits) {
    if (!segs || nsegs == 0) return BITPACK_ERR_ARG;
    if (!valid_bpp(bpp)) return BITPACK_ERR_BPP;
    if (!order_ok(order)) return BITPACK_ERR_ORDER;
    uint64_t cap_bits = (uint64_t)out_cap * 8u;
    uint8_t vmax = (uint8_t)((1u << bpp) - 1u);
    uint64_t end = 0;
    for (size_t s = 0; s < nsegs; ++s) {
        const bitpack_write_seg* g = &segs[s];
        if (g->count == 0) continue;            /* ignored, no offset validation */
        if (!g->idx) return BITPACK_ERR_ARG;
        if (g->count > UINT64_MAX / (uint64_t)bpp) return BITPACK_ERR_OFFSET;
        uint64_t bits = (uint64_t)g->count * (uint64_t)bpp;
        if (!range_fits(g->bit_off, bits)) return BITPACK_ERR_OFFSET;
        if (g->bit_off + bits > cap_bits) return BITPACK_ERR_CAP;
        for (size_t i = 0; i < g->count; ++i)
            if (g->idx[i] > vmax) return BITPACK_ERR_RANGE;
        if (g->bit_off + bits > end) end = g->bit_off + bits;
    }
    *out_bits = end;
    return BITPACK_OK;
}

int bitpack_write(const bitpack_write_seg* segs, size_t nsegs,
                  int bpp, bitpack_order order,
                  uint8_t* out, size_t out_cap, uint64_t* out_bits) {
    if (!out || !out_bits) return BITPACK_ERR_ARG;
    uint64_t end = 0;
    int rc = validate_write(segs, nsegs, bpp, order, out_cap, &end);
    if (rc != BITPACK_OK) return rc;

    for (size_t s = 0; s < nsegs; ++s) {
        const bitpack_write_seg* g = &segs[s];
        uint64_t pos = g->bit_off;
        for (size_t i = 0; i < g->count; ++i) {
            uint8_t v = g->idx[i];
            for (int b = bpp - 1; b >= 0; --b, ++pos)
                bit_put(out, pos, order, (v >> b) & 1u);
        }
    }
    *out_bits = end;
    return BITPACK_OK;
}

int bitpack_write_run(const uint8_t* idx, size_t count,
                      int bpp, bitpack_order order, uint64_t bit_off,
                      uint8_t* out, size_t out_cap, uint64_t* out_bits) {
    bitpack_write_seg seg = { bit_off, idx, count };
    return bitpack_write(&seg, 1, bpp, order, out, out_cap, out_bits);
}

int bitpack_read(const uint8_t* packed, size_t packed_len,
                 const bitpack_read_seg* segs, size_t nsegs,
                 int bpp, bitpack_order order) {
    if (!packed || !segs || nsegs == 0) return BITPACK_ERR_ARG;
    if (!valid_bpp(bpp)) return BITPACK_ERR_BPP;
    if (!order_ok(order)) return BITPACK_ERR_ORDER;
    uint64_t avail = (uint64_t)packed_len * 8u;
    for (size_t s = 0; s < nsegs; ++s) {
        const bitpack_read_seg* g = &segs[s];
        if (g->count && !g->idx) return BITPACK_ERR_ARG;
        if (g->count > UINT64_MAX / (uint64_t)bpp) return BITPACK_ERR_OFFSET;
        uint64_t bits = (uint64_t)g->count * (uint64_t)bpp;
        if (!range_fits(g->bit_off, bits)) return BITPACK_ERR_OFFSET;
        if (g->bit_off + bits > avail) return BITPACK_ERR_LEN;
    }
    for (size_t s = 0; s < nsegs; ++s) {
        const bitpack_read_seg* g = &segs[s];
        uint64_t pos = g->bit_off;
        for (size_t i = 0; i < g->count; ++i) {
            uint8_t v = 0;
            for (int b = 0; b < bpp; ++b, ++pos)
                v = (uint8_t)((v << 1) | (unsigned)bit_get(packed, pos, order));
            g->idx[i] = v;
        }
    }
    return BITPACK_OK;
}

int bitpack_read_run(const uint8_t* packed, size_t packed_len,
                     uint64_t bit_off, size_t count,
                     int bpp, bitpack_order order, uint8_t* idx) {
    bitpack_read_seg seg = { bit_off, idx, count };
    return bitpack_read(packed, packed_len, &seg, 1, bpp, order);
}

int bitpack_read_all(const uint8_t* packed, size_t packed_len,
                     uint64_t avail_bits, int bpp, bitpack_order order,
                     uint8_t* idx, size_t idx_cap,
                     size_t* out_count, unsigned* out_tail_bits) {
    if (!packed) return BITPACK_ERR_ARG;
    if (!idx && idx_cap) return BITPACK_ERR_ARG;
    if (!valid_bpp(bpp)) return BITPACK_ERR_BPP;
    if (!order_ok(order)) return BITPACK_ERR_ORDER;
    uint64_t count64 = avail_bits / (uint64_t)bpp;
    unsigned tail = (unsigned)(avail_bits % (uint64_t)bpp);
    if (count64 > (uint64_t)idx_cap) return BITPACK_ERR_CAP;
    size_t count = (size_t)count64;
    uint64_t need_bytes = (avail_bits / 8u) + ((avail_bits & 7u) ? 1u : 0u);
    if (need_bytes > (uint64_t)packed_len) return BITPACK_ERR_LEN;
    if (count) {
        bitpack_read_seg seg = { 0, idx, count };
        int rc = bitpack_read(packed, packed_len, &seg, 1, bpp, order);
        if (rc != BITPACK_OK) return rc;
    }
    if (out_count) *out_count = count;
    if (out_tail_bits) *out_tail_bits = tail;
    return BITPACK_OK;
}

uint8_t bitpack_mirror_byte(uint8_t b) {
    b = (uint8_t)((b & 0xF0u) >> 4 | (b & 0x0Fu) << 4);
    b = (uint8_t)((b & 0xCCu) >> 2 | (b & 0x33u) << 2);
    b = (uint8_t)((b & 0xAAu) >> 1 | (b & 0x55u) << 1);
    return b;
}

int bitpack_mirror_bytes(const uint8_t* in, size_t n, uint8_t* out) {
    if (!in || !out) return BITPACK_ERR_ARG;
    for (size_t i = 0; i < n; ++i) out[i] = bitpack_mirror_byte(in[i]);
    return BITPACK_OK;
}
