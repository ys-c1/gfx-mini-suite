#ifndef TONE_H
#define TONE_H
#include <cstddef>
#include <cstdint>
void build_gamma_lut(double gamma, std::uint8_t lut[256]);
void apply_lut(const std::uint8_t* src, std::size_t n, const std::uint8_t lut[256], std::uint8_t* dst);
int lut_is_monotonic(const std::uint8_t lut[256]);
double lut_mean(const std::uint8_t lut[256]);

// ---- control-point smooth curve (monotone cubic Hermite, Fritsch-Carlson) ----
// Control points are normalized: x and y in [0,1]. Both ends are pinned:
// the first point must be (0,0) and the last (1,1).
constexpr std::size_t SPLINE_MAX_POINTS = 64;
struct SplineCurve {
    double x[SPLINE_MAX_POINTS];
    double y[SPLINE_MAX_POINTS];
    double m[SPLINE_MAX_POINTS]; // per-knot tangent (dy/dx)
    std::size_t n;
};
// Returns 0 on success; returns -1 and leaves *out untouched when:
//   n < 2 or n > SPLINE_MAX_POINTS, xs not strictly increasing,
//   ys not strictly increasing, any coordinate outside [0,1],
//   or the ends are not pinned at (0,0) / (1,1).
int spline_build(const double* xs, const double* ys, std::size_t n, SplineCurve* out);
// Evaluate the curve at t (clamped to the knot range). C1 across knots.
double spline_eval(const SplineCurve* s, double t);
// Build a 256-entry LUT from the curve. lut[0]=0 and lut[255]=255 always.
void build_spline_lut(const SplineCurve* s, std::uint8_t lut[256]);
#endif
