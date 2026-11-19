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

// One-sided end tangent (same rule as MATLAB pchip): a 3-point estimate,
// clamped so it can never break monotonicity of the first/last segment.
static double pchip_end_slope(double h0, double h1, double del0, double del1) {
    double d = ((2.0 * h0 + h1) * del0 - h0 * del1) / (h0 + h1);
    if (d * del0 <= 0.0) return 0.0;
    if (del0 * del1 < 0.0 && std::fabs(d) > 3.0 * std::fabs(del0)) return 3.0 * del0;
    return d;
}

int spline_build(const double* xs, const double* ys, std::size_t n, SplineCurve* out) {
    if (!xs || !ys || !out) return -1;
    if (n < 2 || n > SPLINE_MAX_POINTS) return -1;
    for (std::size_t k = 0; k < n; ++k) {
        if (!(xs[k] >= 0.0 && xs[k] <= 1.0)) return -1; // rejects NaN too
        if (!(ys[k] >= 0.0 && ys[k] <= 1.0)) return -1;
        if (k > 0 && !(xs[k] > xs[k - 1])) return -1;   // x must strictly increase
        if (k > 0 && !(ys[k] > ys[k - 1])) return -1;   // y must strictly increase
    }
    if (xs[0] != 0.0 || xs[n - 1] != 1.0) return -1;    // ends pinned in x
    if (ys[0] != 0.0 || ys[n - 1] != 1.0) return -1;    // ends pinned in y

    double h[SPLINE_MAX_POINTS], del[SPLINE_MAX_POINTS], m[SPLINE_MAX_POINTS];
    for (std::size_t k = 0; k + 1 < n; ++k) {
        h[k] = xs[k + 1] - xs[k];
        del[k] = (ys[k + 1] - ys[k]) / h[k]; // > 0 by validation above
    }
    if (n == 2) {
        m[0] = m[1] = del[0];
    } else {
        m[0] = pchip_end_slope(h[0], h[1], del[0], del[1]);
        m[n - 1] = pchip_end_slope(h[n - 2], h[n - 3], del[n - 2], del[n - 3]);
        for (std::size_t k = 1; k + 1 < n; ++k) {
            if (del[k - 1] * del[k] <= 0.0) {
                m[k] = 0.0;
            } else {
                // weighted harmonic mean: keeps each segment monotone (Fritsch-Carlson)
                double w1 = 2.0 * h[k] + h[k - 1];
                double w2 = h[k] + 2.0 * h[k - 1];
                m[k] = (w1 + w2) / (w1 / del[k - 1] + w2 / del[k]);
            }
        }
    }
    out->n = n;
    for (std::size_t k = 0; k < n; ++k) {
        out->x[k] = xs[k];
        out->y[k] = ys[k];
        out->m[k] = m[k];
    }
    return 0;
}

double spline_eval(const SplineCurve* s, double t) {
    std::size_t n = s->n;
    if (t <= s->x[0]) return s->y[0];
    if (t >= s->x[n - 1]) return s->y[n - 1];
    std::size_t k = 0;
    while (t >= s->x[k + 1]) ++k; // t < x[n-1], so k <= n-2
    double hk = s->x[k + 1] - s->x[k];
    double u = (t - s->x[k]) / hk;
    double u2 = u * u, u3 = u2 * u;
    return (2.0 * u3 - 3.0 * u2 + 1.0) * s->y[k]
         + (u3 - 2.0 * u2 + u) * hk * s->m[k]
         + (-2.0 * u3 + 3.0 * u2) * s->y[k + 1]
         + (u3 - u2) * hk * s->m[k + 1];
}

void build_spline_lut(const SplineCurve* s, std::uint8_t lut[256]) {
    for (int i = 0; i < 256; ++i) {
        double v = spline_eval(s, (double)i / 255.0) * 255.0;
        // clamp BEFORE the uint8 cast: a value can never wrap modulo 256
        lut[i] = (std::uint8_t)std::clamp((int)std::lround(v), 0, 255);
    }
    lut[0] = 0; lut[255] = 255;
}
