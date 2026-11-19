#include "../src/tone.h"
#include <cstdio>
#include <cmath>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

// Golden bytes of the PRE-EXISTING gamma path (build_gamma_lut(2.2)),
// captured before the spline feature was added. Any change to the
// original curve path breaks this test byte-for-byte.
static const unsigned char GAMMA_22_GOLDEN[256] = {
  0, 21, 28, 34, 39, 43, 46, 50, 53, 56, 59, 61, 64, 66, 68, 70,
 72, 74, 76, 78, 80, 82, 84, 85, 87, 89, 90, 92, 93, 95, 96, 98,
 99,101,102,103,105,106,107,109,110,111,112,114,115,116,117,118,
119,120,122,123,124,125,126,127,128,129,130,131,132,133,134,135,
136,137,138,139,140,141,142,143,144,144,145,146,147,148,149,150,
151,151,152,153,154,155,156,156,157,158,159,160,160,161,162,163,
164,164,165,166,167,167,168,169,170,170,171,172,173,173,174,175,
175,176,177,178,178,179,180,180,181,182,182,183,184,184,185,186,
186,187,188,188,189,190,190,191,192,192,193,194,194,195,195,196,
197,197,198,199,199,200,200,201,202,202,203,203,204,205,205,206,
206,207,207,208,209,209,210,210,211,212,212,213,213,214,214,215,
215,216,217,217,218,218,219,219,220,220,221,221,222,223,223,224,
224,225,225,226,226,227,227,228,228,229,229,230,230,231,231,232,
232,233,233,234,234,235,235,236,236,237,237,238,238,239,239,240,
240,241,241,242,242,243,243,244,244,245,245,246,246,247,247,248,
248,249,249,249,250,250,251,251,252,252,253,253,254,254,255,255,
};

static void test_rejection() {
    SplineCurve s;
    double x2[2] = {0.0, 1.0}, y2[2] = {0.0, 1.0};
    CHECK(spline_build(x2, y2, 2, &s) == 0);          // minimum accepted

    CHECK(spline_build(x2, y2, 0, &s) == -1);         // too few points
    CHECK(spline_build(x2, y2, 1, &s) == -1);         // too few points
    CHECK(spline_build(nullptr, y2, 2, &s) == -1);
    CHECK(spline_build(x2, nullptr, 2, &s) == -1);
    CHECK(spline_build(x2, y2, 2, nullptr) == -1);

    double xb1[3] = {0.0, 0.5, 0.5}, yg[3] = {0.0, 0.5, 1.0}; // x not increasing (equal)
    CHECK(spline_build(xb1, yg, 3, &s) == -1);
    double xb2[3] = {0.0, 0.7, 0.4};                       // x not increasing (decreasing)
    CHECK(spline_build(xb2, yg, 3, &s) == -1);
    double yb1[3] = {0.0, 0.5, 0.5};                       // y not increasing
    double xg[3]  = {0.0, 0.5, 1.0};
    CHECK(spline_build(xg, yb1, 3, &s) == -1);
    double yb2[3] = {0.0, 0.8, 0.3};                       // y decreasing
    CHECK(spline_build(xg, yb2, 3, &s) == -1);
    double xo[2]  = {0.0, 1.2};                            // x out of [0,1]
    CHECK(spline_build(xo, y2, 2, &s) == -1);
    double yo[2]  = {-0.1, 1.0};                           // y out of [0,1]
    CHECK(spline_build(x2, yo, 2, &s) == -1);
    double xnp[2] = {0.1, 1.0};                            // x end not pinned at 0
    CHECK(spline_build(xnp, y2, 2, &s) == -1);
    double ynp[2] = {0.0, 0.9};                            // y end not pinned at 1
    CHECK(spline_build(x2, ynp, 2, &s) == -1);
}

static void test_curve_properties() {
    // S-curve: gentle shadows, midtone contrast, compressed highlights
    double xs[5] = {0.0, 0.25, 0.5, 0.75, 1.0};
    double ys[5] = {0.0, 0.22, 0.52, 0.80, 1.0};
    SplineCurve s;
    CHECK(spline_build(xs, ys, 5, &s) == 0);

    // ends fixed
    CHECK(spline_eval(&s, 0.0) == 0.0);
    CHECK(spline_eval(&s, 1.0) == 1.0);

    // passes through every control point
    for (int k = 0; k < 5; ++k)
        CHECK(std::fabs(spline_eval(&s, xs[k]) - ys[k]) < 1e-12);

    // strictly monotonic on a dense grid (4097 samples, double precision)
    double prev = spline_eval(&s, 0.0);
    for (int i = 1; i <= 4096; ++i) {
        double v = spline_eval(&s, (double)i / 4096.0);
        CHECK(v > prev);
        prev = v;
    }

    // C1 continuity at every interior knot: left and right difference
    // quotients must agree (tolerance scaled to the 1e-6 probe step)
    for (int k = 1; k < 4; ++k) {
        double h = 1e-6;
        double dl = (spline_eval(&s, xs[k]) - spline_eval(&s, xs[k] - h)) / h;
        double dr = (spline_eval(&s, xs[k] + h) - spline_eval(&s, xs[k])) / h;
        CHECK(std::fabs(dl - dr) < 1e-3);
    }

    // LUT: monotonic, ends pinned, no wrap near saturation
    std::uint8_t lut[256];
    build_spline_lut(&s, lut);
    CHECK(lut[0] == 0);
    CHECK(lut[255] == 255);
    CHECK(lut_is_monotonic(lut));
    CHECK(lut[254] <= 255 && lut[254] >= lut[253]); // no 256->0 wrap at the top

    // 2-point degenerate case = pure linear ramp
    double x2[2] = {0.0, 1.0}, y2[2] = {0.0, 1.0};
    SplineCurve lin;
    CHECK(spline_build(x2, y2, 2, &lin) == 0);
    CHECK(std::fabs(spline_eval(&lin, 0.37) - 0.37) < 1e-12);
}

static void test_gamma_path_unchanged() {
    std::uint8_t lut[256];
    build_gamma_lut(2.2, lut);
    for (int i = 0; i < 256; ++i)
        CHECK(lut[i] == GAMMA_22_GOLDEN[i]); // byte-identical, all 256 entries

    build_gamma_lut(1.0, lut);
    for (int i = 0; i < 256; ++i)
        CHECK(lut[i] == (std::uint8_t)i);
}

int main() {
    test_rejection();
    test_curve_properties();
    test_gamma_path_unchanged();
    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
