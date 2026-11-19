// Bench + sample-point comparison: control-point spline vs gamma power curve.
#include "../src/tone.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static double gamma_curve(double t, double g) {
    return std::pow(t, 1.0 / g);
}

int main() {
    const double GAMMA = 2.2;
    double xs[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
    double ys[5] = {0.0, 0.22, 0.52, 0.80, 1.0};
    SplineCurve s;
    if (spline_build(xs, ys, 5, &s) != 0) { std::printf("spline_build failed\n"); return 1; }

    // ---- 5 sample points: value and local slope, spline vs gamma ----
    std::printf("sample-point comparison (slope = central difference, h=1e-3)\n");
    std::printf("%6s | %8s %8s | %8s %8s\n", "x", "spline", "gamma", "spl_slope", "gam_slope");
    const double pts[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
    for (int i = 0; i < 5; ++i) {
        double t = pts[i], h = 1e-3;
        double t0 = t - h < 0.0 ? t : t - h, t1 = t + h > 1.0 ? t : t + h;
        double ss = (spline_eval(&s, t1) - spline_eval(&s, t0)) / (t1 - t0);
        double gs = (gamma_curve(t1, GAMMA) - gamma_curve(t0, GAMMA)) / (t1 - t0);
        std::printf("%6.2f | %8.4f %8.4f | %8.4f %8.4f\n",
                    t, spline_eval(&s, t), gamma_curve(t, GAMMA), ss, gs);
    }

    // ---- timing: apply each LUT to 100,000 pixels ----
    const std::size_t N = 100000;
    std::uint8_t* src = (std::uint8_t*)std::malloc(N);
    std::uint8_t* dst = (std::uint8_t*)std::malloc(N);
    unsigned rng = 12345;
    for (std::size_t i = 0; i < N; ++i) { rng = rng * 1664525u + 1013904223u; src[i] = (std::uint8_t)(rng >> 24); }

    std::uint8_t lut_s[256], lut_g[256];
    build_spline_lut(&s, lut_s);
    build_gamma_lut(GAMMA, lut_g);

    const int REPS = 200;
    double best_s = 1e30, best_g = 1e30;
    for (int r = 0; r < REPS; ++r) {
        auto a = std::chrono::steady_clock::now();
        apply_lut(src, N, lut_s, dst);
        auto b = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(b - a).count();
        if (ms < best_s) best_s = ms;

        a = std::chrono::steady_clock::now();
        apply_lut(src, N, lut_g, dst);
        b = std::chrono::steady_clock::now();
        ms = std::chrono::duration<double, std::milli>(b - a).count();
        if (ms < best_g) best_g = ms;
    }
    std::printf("apply to %zu pixels (best of %d runs):\n", N, REPS);
    std::printf("  spline LUT : %.4f ms (%.2f ns/pixel)\n", best_s, best_s * 1e6 / (double)N);
    std::printf("  gamma  LUT : %.4f ms (%.2f ns/pixel)\n", best_g, best_g * 1e6 / (double)N);

    // sink so the loops are not optimized away
    unsigned long acc = 0;
    for (std::size_t i = 0; i < N; ++i) acc += dst[i];
    std::printf("  checksum(dst)=%lu\n", acc);
    std::free(src); std::free(dst);
    return 0;
}
