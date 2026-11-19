#include "../src/tone.h"
#include <cstdio>
#include <cstdlib>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main() {
    std::uint8_t lut[256];
    build_gamma_lut(1.0, lut);
    CHECK(lut[0] == 0);
    CHECK(lut[255] == 255);
    CHECK(lut_is_monotonic(lut));
    for (int i = 0; i < 256; ++i) CHECK(lut[i] == (std::uint8_t)i);

    build_gamma_lut(2.2, lut);
    CHECK(lut_is_monotonic(lut));
    CHECK(lut[128] > 128);
    CHECK(lut_mean(lut) > 100.0);

    std::uint8_t src[8] = {0, 1, 2, 120, 200, 254, 255, 42};
    std::uint8_t dst[8];
    apply_lut(src, 8, lut, dst);
    CHECK(dst[0] == lut[0]);
    CHECK(dst[7] == lut[42]);

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
