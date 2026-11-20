// Benchmark: total cost of filling a 256-entry gamma LUT through segmented,
// out-of-order builder calls vs. the one-shot build_gamma_lut() baseline.
//
// Both paths perform exactly 256 pow() calls when the table is complete; the
// builder additionally does one bool check per entry plus per-call range
// validation. This program measures whether that bookkeeping stays within
// the review budget of 1.2x.
#include "../src/tone.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using clock_ = std::chrono::steady_clock;

static double now_seconds() {
    static const auto start = clock_::now();
    return std::chrono::duration<double>(clock_::now() - start).count();
}

// Volatile sink so the optimizer cannot elide the LUT work.
static volatile std::uint8_t g_sink;

static double time_full(int iters, double gamma) {
    double t0 = now_seconds();
    std::uint64_t checksum = 0;
    for (int it = 0; it < iters; ++it) {
        std::uint8_t lut[256];
        build_gamma_lut(gamma, lut);
        checksum += lut[(unsigned)it & 0xFF];
    }
    double dt = now_seconds() - t0;
    g_sink = (std::uint8_t)checksum;
    return dt;
}

static double time_segmented(int iters, double gamma, int nseg, bool reverse,
                             unsigned seed) {
    std::vector<int> order(nseg);
    for (int k = 0; k < nseg; ++k) order[k] = k;
    if (reverse) {
        for (int k = 0; k < nseg; ++k) order[k] = nseg - 1 - k;
    } else {
        std::mt19937 rng(seed);
        std::shuffle(order.begin(), order.end(), rng);
    }
    // Segment boundaries; nseg always divides 256 for the cases we run.
    std::vector<int> bounds(nseg + 1);
    for (int k = 0; k <= nseg; ++k) bounds[k] = k * (256 / nseg);

    double t0 = now_seconds();
    std::uint64_t checksum = 0;
    for (int it = 0; it < iters; ++it) {
        GammaLutBuilder b(gamma);
        for (int s = 0; s < nseg; ++s) {
            int k = order[s];
            checksum += (std::uint8_t)b.build_range(bounds[k], bounds[k + 1]);
        }
        checksum += b.data()[(unsigned)it & 0xFF];
    }
    double dt = now_seconds() - t0;
    g_sink = (std::uint8_t)checksum;
    return dt;
}

int main() {
    constexpr int kIters = 20000;
    constexpr int kRepeats = 4;
    const double gamma = 2.2;

    struct Case { int nseg; bool reverse; const char* label; };
    const Case cases[] = {
        {1,   false, "1 segment (single range)"},
        {4,   false, "4 segments, shuffled order"},
        {8,   true,  "8 segments, reverse order"},
        {16,  false, "16 segments, shuffled order"},
        {16,  true,  "16 segments, reverse order"},
        {32,  false, "32 segments, shuffled order"},
        {64,  false, "64 segments, shuffled order"},
        {128, false, "128 segments, shuffled order"},
        {256, false, "256 segments (per-entry), shuffled"},
        {256, true,  "256 segments (per-entry), reverse"},
    };

    // Correctness sanity: every segmented ordering must match the one-shot LUT.
    std::uint8_t ref[256];
    build_gamma_lut(gamma, ref);
    for (const Case& c : cases) {
        GammaLutBuilder b(gamma);
        std::mt19937 rng(1234);
        std::vector<int> order(c.nseg);
        for (int k = 0; k < c.nseg; ++k) order[k] = k;
        if (c.reverse) std::reverse(order.begin(), order.end());
        else std::shuffle(order.begin(), order.end(), rng);
        for (int k : order)
            b.build_range(k * (256 / c.nseg), (k + 1) * (256 / c.nseg));
        for (int i = 0; i < 256; ++i) {
            if (b.data()[i] != ref[i]) {
                std::printf("MISMATCH case %s at %d\n", c.label, i);
                return 2;
            }
        }
    }

    std::printf("iters/case=%d, repeats=%d (best of repeats reported)\n",
                kIters, kRepeats);
    std::printf("%-38s %12s %12s %8s %s\n",
                "case", "full (s)", "segmented(s)", "ratio", "verdict");

    bool all_ok = true;
    double worst = 0.0;
    for (const Case& c : cases) {
        double best_full = 1e9, best_seg = 1e9;
        for (int r = 0; r < kRepeats; ++r) {
            best_full = std::min(best_full, time_full(kIters, gamma));
            best_seg = std::min(best_seg,
                time_segmented(kIters, gamma, c.nseg, c.reverse, 99u + r));
        }
        double ratio = best_seg / best_full;
        worst = std::max(worst, ratio);
        bool ok = ratio <= 1.2;
        all_ok &= ok;
        std::printf("%-38s %12.6f %12.6f %7.3fx %s\n",
                    c.label, best_full, best_seg, ratio,
                    ok ? "ok" : "FAIL(>1.2x)");
    }
    std::printf("worst ratio: %.3fx -> %s\n", worst,
                all_ok ? "PASS" : "FAIL");
    return all_ok ? 0 : 1;
}
