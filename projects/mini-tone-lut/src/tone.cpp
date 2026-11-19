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

void histogram_of(const std::uint8_t* src, std::size_t n, std::uint64_t hist[256]) {
    for (int i = 0; i < 256; ++i) hist[i] = 0;
    for (std::size_t i = 0; i < n; ++i) ++hist[src[i]];
}

static std::uint64_t hist_total(const std::uint64_t hist[256]) {
    std::uint64_t t = 0;
    for (int i = 0; i < 256; ++i) t += hist[i];
    return t;
}

int build_match_lut(const std::uint64_t src_hist[256],
                    const std::uint64_t target_hist[256],
                    std::uint8_t lut[256]) {
    std::uint64_t ns = hist_total(src_hist);
    std::uint64_t nt = hist_total(target_hist);
    if (ns == 0 || nt == 0) { /* degenerate: identity mapping, report failure */
        for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)i;
        return 0;
    }
    /* Normalized target CDF. */
    double ct[256];
    std::uint64_t acc = 0;
    for (int j = 0; j < 256; ++j) { acc += target_hist[j]; ct[j] = (double)acc / (double)nt; }
    /* For each source bin, map to the smallest target bin whose CDF covers
     * the source CDF. j never decreases, so the LUT is non-decreasing. */
    std::uint64_t sacc = 0;
    int j = 0;
    for (int i = 0; i < 256; ++i) {
        sacc += src_hist[i];
        double cs = (double)sacc / (double)ns;
        while (j < 255 && ct[j] < cs) ++j;
        lut[i] = (std::uint8_t)j;
    }
    return 1;
}

double hist_mean(const std::uint64_t hist[256], std::uint64_t total) {
    if (total == 0) return 0.0;
    double s = 0.0;
    for (int i = 0; i < 256; ++i) s += (double)i * (double)hist[i];
    return s / (double)total;
}

double cdf_max_deviation(const std::uint64_t a[256], std::uint64_t na,
                         const std::uint64_t b[256], std::uint64_t nb) {
    if (na == 0 || nb == 0) return -1.0;
    double ca = 0.0, cb = 0.0, dmax = 0.0;
    for (int i = 0; i < 256; ++i) {
        ca += (double)a[i] / (double)na;
        cb += (double)b[i] / (double)nb;
        double d = std::fabs(ca - cb);
        if (d > dmax) dmax = d;
    }
    return dmax;
}
