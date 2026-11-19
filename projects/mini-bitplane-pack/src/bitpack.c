#include "bitpack.h"

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
