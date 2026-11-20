#include "../src/tone.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void print_report(const char* label, const char* curve_names,
                         const double* w, int n, double rmse, double ms) {
    std::printf("  %-22s weights=[", label);
    for (int i = 0; i < n; ++i)
        std::printf("%s%.4f", i ? ", " : "", w[i]);
    std::printf("]  RMSE=%.4f  time=%.3f ms  (%s)\n", rmse, ms, curve_names);
}

int main() {
    std::uint8_t lut[256];

    /* --- existing behaviour ------------------------------------------- */
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

    /* --- weight normalization ----------------------------------------- */
    double w[3] = {2.0, 2.0, 0.0}, wn[3];
    CHECK(tone_normalize_weights(3, w, wn) == 0);
    CHECK(std::fabs(wn[0] - 0.5) < 1e-12 && std::fabs(wn[1] - 0.5) < 1e-12);
    double bad[3] = {-1.0, 2.0, 0.0};
    CHECK(tone_normalize_weights(3, bad, wn) == -1);
    double zero[3] = {0.0, 0.0, 0.0};
    CHECK(tone_normalize_weights(3, zero, wn) == -1);
    double nanw[3] = {NAN, 1.0, 0.0};
    CHECK(tone_normalize_weights(3, nanw, wn) == -1);
    /* Aliasing: out == weights must be safe. */
    double alias[3] = {1.0, 3.0, 0.0};
    CHECK(tone_normalize_weights(3, alias, alias) == 0);
    CHECK(std::fabs(alias[0] - 0.25) < 1e-12 && std::fabs(alias[1] - 0.75) < 1e-12);

    /* --- forward blend ------------------------------------------------- */
    std::uint8_t a[256], b[256], mid[256], mix[256];
    build_gamma_lut(1.0, a);  /* identity     */
    build_gamma_lut(2.2, b);
    const std::uint8_t* two[2] = {a, b};
    double half[2] = {0.5, 0.5};
    CHECK(blend_luts(two, 2, half, mix) == 0);
    CHECK(mix[0] == 0 && mix[255] == 255);
    CHECK(lut_is_monotonic(mix));
    for (int t = 0; t < 256; ++t) {
        int want = (int)std::lround(0.5 * a[t] + 0.5 * b[t]);
        CHECK(mix[t] == (std::uint8_t)want);
    }
    /* Unnormalized weights give the same result as normalized ones. */
    double raw[2] = {7.0, 7.0};
    CHECK(blend_luts(two, 2, raw, mid) == 0);
    for (int t = 0; t < 256; ++t) CHECK(mid[t] == mix[t]);
    /* A single curve at weight 1 reproduces itself. */
    double one[2] = {0.0, 1.0};
    CHECK(blend_luts(two, 2, one, mid) == 0);
    for (int t = 0; t < 256; ++t) CHECK(mid[t] == b[t]);
    /* Same input given as raw curve pointers must agree. */
    double one_a[2] = {1.0, 0.0};
    CHECK(blend_luts(two, 2, one_a, mid) == 0);
    for (int t = 0; t < 256; ++t) CHECK(mid[t] == a[t]);
    /* Invalid inputs. */
    CHECK(blend_luts(two, 2, bad, mid) == -1);
    CHECK(blend_luts(two, 0, half, mix) == -1);
    CHECK(blend_luts(nullptr, 2, half, mix) == -1);

    /* --- inverse fit: recover known weights ---------------------------- */
    build_gamma_lut(0.8, lut);
    std::uint8_t c[256];
    for (int t = 0; t < 256; ++t) c[t] = lut[t];
    const std::uint8_t* three[3] = {a, b, c};
    const char* names = "identity/g2.2/g0.8";

    double truth[3] = {0.5, 0.3, 0.2};
    std::uint8_t target[256];
    CHECK(blend_luts(three, 3, truth, target) == 0);

    double got[3], rmse = 0.0;
    CHECK(fit_blend_weights(three, 3, target, got, &rmse) == 0);
    double time_ref = 0.0;
    {
        auto t0 = std::chrono::steady_clock::now();
        const int reps = 200;
        for (int r = 0; r < reps; ++r) fit_blend_weights(three, 3, target, got, &rmse);
        auto t1 = std::chrono::steady_clock::now();
        time_ref = std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
    }
    std::printf("3 curves, 2 targets\n");
    print_report("recover {0.5,.3,.2}", names, got, 3, rmse, time_ref);
    CHECK(rmse < 1.0);                              /* near-exact recovery */
    CHECK(fit_blend_weights(three, 3, target, got, &rmse) == 0);
    CHECK(std::fabs(got[0] - 0.5) < 0.03);
    CHECK(std::fabs(got[1] - 0.3) < 0.03);
    CHECK(std::fabs(got[2] - 0.2) < 0.03);

    /* Weights must be a valid convex combination, and the blend of the fit
     * must itself be monotonic with fixed endpoints. */
    CHECK(got[0] >= 0 && got[1] >= 0 && got[2] >= 0);
    CHECK(std::fabs(got[0] + got[1] + got[2] - 1.0) < 1e-9);

    /* --- inverse fit: target outside the hull -------------------------- */
    /* A very dark curve blended from bright inputs: optimum sits on a face,
     * one weight must be driven to ~0. */
    for (int t = 0; t < 256; ++t) {
        double v = 255.0 * std::pow(t / 255.0, 3.0);
        target[t] = (std::uint8_t)std::lround(v);
    }
    target[0] = 0; target[255] = 255;
    double got2[3], rmse2 = 0.0;
    CHECK(fit_blend_weights(three, 3, target, got2, &rmse2) == 0);
    double time_out = 0.0;
    {
        auto t0 = std::chrono::steady_clock::now();
        const int reps = 200;
        for (int r = 0; r < reps; ++r) fit_blend_weights(three, 3, target, got2, &rmse2);
        auto t1 = std::chrono::steady_clock::now();
        time_out = std::chrono::duration<double, std::milli>(t1 - t0).count() / reps;
    }
    print_report("fit dark target", names, got2, 3, rmse2, time_out);
    CHECK(rmse2 > 0.0);                            /* cannot be matched exactly */
    double r2 = 0.0;
    for (int t = 0; t < 256; ++t) r2 += (target[t] - (double)t) * (target[t] - (double)t);
    CHECK(rmse2 < std::sqrt(r2 / 256.0));          /* but better than identity */
    CHECK(got2[0] >= 0 && got2[1] >= 0 && got2[2] >= 0);
    CHECK(std::fabs(got2[0] + got2[1] + got2[2] - 1.0) < 1e-9);

    std::uint8_t checked[256];
    CHECK(blend_luts(three, 3, got2, checked) == 0);
    CHECK(lut_is_monotonic(checked));
    CHECK(checked[0] == 0 && checked[255] == 255);

    /* --- invalid inputs ------------------------------------------------ */
    CHECK(fit_blend_weights(three, 0, target, got, &rmse) == -1);
    CHECK(fit_blend_weights(nullptr, 3, target, got, &rmse) == -1);
    CHECK(fit_blend_weights(three, 3, nullptr, got, &rmse) == -1);
    /* Null outputs are allowed (query the error only). */
    CHECK(fit_blend_weights(three, 3, target, nullptr, &rmse) == 0);

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
