#include "../src/bitpack.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void) {
    int sizes[][2] = {{8, 8}, {3, 3}, {5, 2}, {1, 1}, {17, 9}};
    int bpps[] = {1, 2, 4, 8};
    uint8_t idx[4096], back[4096], packed[8192];

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
        }
    }

    CHECK(pack_rows(idx, 8, 8, 4, packed, 1, (size_t[]){0}) == -1);
    uint8_t bad[4] = {16, 0, 0, 0};
    CHECK(pack_rows(bad, 2, 2, 4, packed, sizeof(packed), (size_t[]){0}) == -1);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
