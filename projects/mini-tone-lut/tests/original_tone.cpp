#include "tone.h"
#include <cmath>
#include <algorithm>

void build_gamma_lut(double gamma, std::uint8_t lut[256]) {
    if (gamma <= 0.0) gamma = 1.0;
    for (int i = 0; i < 256; ++i) {
        double v = std::pow((double)i / 255.0, 1.0 / gamma) * 255.0;
        lut[i] = (std::uint8_t)std::clamp((int)std::lround(v), 0, 255);
    }
    lut[0] = 0; lut[255] = 255;
}

void apply_lut(const std::uint8_t* src, std::size_t n, const std::uint8_t lut[256], std::uint8_t* dst) {
    for (std::size_t i = 0; i < n; ++i) dst[i] = lut[src[i]];
}

int lut_is_monotonic(const std::uint8_t lut[256]) {
    for (int i = 1; i < 256; ++i) if (lut[i] < lut[i - 1]) return 0;
    return 1;
}

double lut_mean(const std::uint8_t lut[256]) {
    double s = 0.0;
    for (int i = 0; i < 256; ++i) s += lut[i];
    return s / 256.0;
}
