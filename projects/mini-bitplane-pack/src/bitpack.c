#include "bitpack.h"

size_t packed_size(int w, int h, int bpp) {
    if (w <= 0 || h <= 0 || bpp <= 0 || bpp > 8) return 0;
    size_t bits = (size_t)w * (size_t)h * (size_t)bpp;
    return (bits + 7u) / 8u;
}

int pack_begin(bitpack_stream* s, int w, int h, int bpp, uint8_t* out, size_t out_cap) {
    if (!s || !out) return BITPACK_ERR_ARG;
    if (w <= 0 || h <= 0 || bpp <= 0 || bpp > 8) return BITPACK_ERR_ARG;
    size_t need = packed_size(w, h, bpp);
    if (need == 0 || need > out_cap) return BITPACK_ERR_ARG;
    for (size_t i = 0; i < need; ++i) out[i] = 0;
    s->w = w;
    s->h = h;
    s->bpp = bpp;
    s->next_row = 0;
    s->out = out;
    s->out_len = need;
    s->row_bits = (size_t)w * (size_t)bpp;
    s->vmax = (uint8_t)((1u << bpp) - 1u);
    return BITPACK_OK;
}

int pack_submit_row(bitpack_stream* s, int y, const uint8_t* row) {
    if (!s || !row || !s->out) return BITPACK_ERR_ARG;
    if (y < 0 || y >= s->h) return BITPACK_ERR_ROW_RANGE;
    if (y < s->next_row) return BITPACK_ERR_ROW_DUP;
    if (y > s->next_row) return BITPACK_ERR_ROW_ORDER;
    /* 先整行校验再写：出错时目标缓冲不留残位，调用方修正后可重提交同一行 */
    for (int x = 0; x < s->w; ++x)
        if (row[x] > s->vmax) return BITPACK_ERR_VALUE;
    size_t bit = (size_t)y * s->row_bits;
    for (int x = 0; x < s->w; ++x) {
        uint8_t v = row[x];
        for (int b = s->bpp - 1; b >= 0; --b) {
            if (v & (1u << b)) s->out[bit >> 3] |= (uint8_t)(0x80u >> (bit & 7u));
            ++bit;
        }
    }
    ++s->next_row;
    return BITPACK_OK;
}

int pack_finish(bitpack_stream* s, size_t* out_len) {
    if (!s || !s->out) return BITPACK_ERR_ARG;
    if (s->next_row != s->h) return BITPACK_ERR_INCOMPLETE;
    if (out_len) *out_len = s->out_len;
    return BITPACK_OK;
}

int pack_rows(const uint8_t* idx, int w, int h, int bpp, uint8_t* out, size_t out_cap, size_t* out_len) {
    if (!idx || !out || !out_len) return -1;
    bitpack_stream s;
    if (pack_begin(&s, w, h, bpp, out, out_cap) != BITPACK_OK) return -1;
    for (int y = 0; y < h; ++y)
        if (pack_submit_row(&s, y, idx + (size_t)y * (size_t)w) != BITPACK_OK) return -1;
    if (pack_finish(&s, out_len) != BITPACK_OK) return -1;
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
