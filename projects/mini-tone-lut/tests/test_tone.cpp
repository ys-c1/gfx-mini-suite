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

    /* ---- regression: pre-change curve outputs must be bit-identical ---- */
    build_gamma_lut(2.2, lut);
    CHECK(lut[1] == 21 && lut[64] == 136 && lut[128] == 186 && lut[192] == 224 && lut[254] == 255);
    apply_lut(src, 8, lut, dst);
    const std::uint8_t golden[8] = {0, 21, 28, 181, 228, 255, 255, 112};
    for (int i = 0; i < 8; ++i) CHECK(dst[i] == golden[i]);

    /* ---- histogram matching ---- */
    /* source: 100 dark pixels in [0,10); target: uniform. Different counts
     * for the deviation check are exercised via a second histogram. */
    std::uint64_t sh[256] = {0}, th[256] = {0};
    std::uint8_t simg[100];
    for (int i = 0; i < 100; ++i) simg[i] = (std::uint8_t)i; /* 100 distinct dark values */
    histogram_of(simg, 100, sh);
    CHECK(sh[0] == 1 && sh[99] == 1 && sh[100] == 0);
    for (int i = 0; i < 256; ++i) th[i] = 4; /* uniform target, 1024 px */
    std::uint8_t mlut[256];
    CHECK(build_match_lut(sh, th, mlut) == 1);
    CHECK(lut_is_monotonic(mlut));
    std::uint8_t out[100];
    apply_lut(simg, 100, mlut, out);
    std::uint64_t oh[256] = {0};
    histogram_of(out, 100, oh);
    std::uint64_t total = 0;
    for (int i = 0; i < 256; ++i) total += oh[i];
    CHECK(total == 100); /* pixel count preserved */
    double dev = cdf_max_deviation(oh, 100, th, 1024); /* counts differ: aligned via normalized CDF */
    CHECK(dev >= 0.0 && dev < 0.05);
    CHECK(hist_mean(oh, 100) > hist_mean(sh, 100)); /* dark image got brighter */

    /* deviation metric: identical shape with different counts -> 0; empty -> -1 */
    std::uint64_t z[256] = {0};
    CHECK(cdf_max_deviation(sh, 100, sh, 100) == 0.0);
    {
        std::uint64_t a[256] = {0}, b[256] = {0};
        a[7] = 50; b[7] = 5000; /* same shape, different totals */
        CHECK(cdf_max_deviation(a, 50, b, 5000) == 0.0);
    }
    CHECK(cdf_max_deviation(sh, 100, z, 0) == -1.0);

    /* degenerate 1: empty target distribution -> 0 + identity LUT */
    CHECK(build_match_lut(sh, z, mlut) == 0);
    for (int i = 0; i < 256; ++i) CHECK(mlut[i] == (std::uint8_t)i);

    /* degenerate 2: empty source image -> 0 + identity LUT */
    CHECK(build_match_lut(z, th, mlut) == 0);
    for (int i = 0; i < 256; ++i) CHECK(mlut[i] == (std::uint8_t)i);

    /* degenerate 3a: all-zero image -> valid monotone LUT, lands on one bin */
    std::uint64_t ah[256] = {0};
    std::uint8_t aimg[16] = {0};
    histogram_of(aimg, 16, ah);
    CHECK(build_match_lut(ah, th, mlut) == 1);
    CHECK(lut_is_monotonic(mlut));
    apply_lut(aimg, 16, mlut, out);
    for (int i = 0; i < 16; ++i) CHECK(out[i] == mlut[0]);

    /* degenerate 3b: single-color image -> valid monotone LUT, uniform output */
    std::uint64_t ch[256] = {0};
    std::uint8_t cimg[64];
    for (int i = 0; i < 64; ++i) cimg[i] = 128;
    histogram_of(cimg, 64, ch);
    CHECK(build_match_lut(ch, th, mlut) == 1);
    CHECK(lut_is_monotonic(mlut));
    apply_lut(cimg, 64, mlut, out);
    for (int i = 0; i < 64; ++i) CHECK(out[i] == mlut[128]);

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
