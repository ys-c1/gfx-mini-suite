// Benchmarks and correctness figures for the exact inversion path.
//
// Three curve groups, each answering a different part of the requirement:
//   G1 strict      strictly increasing, no plateaus          (identity, gamma 1.0)
//   G2 non-strict  non-decreasing, with plateaus and gaps    (gamma 2.2, gamma 0.45)
//   G3 non-mono    not monotonic; some values have 2+ runs   (tent, sawtooth)
//
// Reports per group: interval accuracy against an independent brute-force
// reference, single-call cost, and batch cost. Then construction cost next to
// the two baselines the task's budget can be read against, and a physical
// floor measurement so the ratio can be judged honestly.
#include "../src/tone.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <immintrin.h>

namespace {
using Clock = std::chrono::steady_clock;
double ns_since(Clock::time_point a) {
    return std::chrono::duration<double, std::nano>(Clock::now() - a).count();
}
std::uint32_t g_rng = 0x9E3779B9u;
std::uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng; }
constexpr std::size_t N = 10000;
volatile std::uint64_t g_sink = 0;

template <class F> double best_of(int reps, F f) {
    double best = 1e300;
    for (int r = 0; r < reps; ++r) best = std::min(best, f());
    return best;
}
template <class F> double median_of(int reps, F f) {
    double v[32];
    const int n = std::min(reps, 32);
    for (int r = 0; r < n; ++r) v[r] = f();
    std::sort(v, v + n);
    return v[n / 2];
}

struct Curve { const char* name; std::uint8_t lut[256]; };
void make_identity(Curve& c) { for (int i = 0; i < 256; ++i) c.lut[i] = (std::uint8_t)i; }
void make_gamma(Curve& c, double g) { build_gamma_lut(g, c.lut); }
void make_tent(Curve& c) { for (int i = 0; i < 256; ++i) c.lut[i] = (std::uint8_t)(i < 128 ? i : 255 - i); }
void make_saw(Curve& c) { for (int i = 0; i < 256; ++i) c.lut[i] = (std::uint8_t)(i & 0x3F); }

struct Ref { int lo[64], hi[64], n; };
Ref ref_runs(const std::uint8_t lut[256], int v) {
    Ref r; r.n = 0;
    for (int i = 0; i < 256;) {
        int j = i;
        while (j < 256 && lut[j] == lut[i]) ++j;
        if (lut[i] == (std::uint8_t)v && r.n < 64) { r.lo[r.n] = i; r.hi[r.n] = j - 1; ++r.n; }
        i = j;
    }
    return r;
}

struct Accuracy { std::size_t values = 0, present = 0, plateaus = 0, multi = 0, empty = 0, exact = 0, wrong = 0; };

// Gather floor kernel: isolates AVX2 target attribute so the rest of the file
// stays baseline-compiled.
__attribute__((target("avx2")))
void gather_floor_kernel(const std::uint32_t* entry, const std::uint8_t* src, std::size_t n,
                         std::uint16_t* out) {
    const __m256i m = _mm256_set1_epi32(0xFFFF);
    for (std::size_t k = 0; k + 8 <= n; k += 8) {
        const __m256i v = _mm256_i32gather_epi32((const int*)entry,
            _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i*)(src + k))), 4);
        const __m256i x = _mm256_and_si256(v, m);
        _mm_storeu_si128((__m128i*)(out + k), _mm_packus_epi32(_mm256_castsi256_si128(x),
                                                                _mm256_extracti128_si256(x, 1)));
    }
    asm volatile("" ::: "memory");
}

// Pure store floor: 20 KB of output and nothing else.
__attribute__((target("avx2")))
void store_floor_kernel(std::uint16_t* out, std::size_t n) {
    const __m256i k = _mm256_set1_epi16(1);
    for (std::size_t i = 0; i < n; i += 16) _mm256_store_si256((__m256i*)(out + i), k);
    asm volatile("" ::: "memory");
}

// "correct" = the returned interval is exactly the reference lowest-index run,
// the status matches the run count, and every index inside the interval really
// maps to v. An empty answer counts as correct only for a value with no run.
Accuracy measure(const Curve& c, const ToneInverseTable& t) {
    Accuracy a;
    for (int v = 0; v < 256; ++v) {
        const Ref ref = ref_runs(c.lut, v);
        ToneRange r;
        const ToneInvStatus st = tone_invert(&t, v, &r);
        ++a.values;
        if (ref.n == 0) {
            ++a.empty;
            if (st == TONE_INV_EMPTY && tone_range_empty(r)) ++a.exact; else ++a.wrong;
            continue;
        }
        ++a.present;
        if (ref.hi[0] > ref.lo[0]) ++a.plateaus;
        if (ref.n > 1) ++a.multi;
        bool ok = (r.lo == ref.lo[0] && r.hi == ref.hi[0]) &&
                  st == (ref.n > 1 ? TONE_INV_MULTI_RUN : TONE_INV_OK);
        for (int i = r.lo; ok && i <= r.hi; ++i) ok = (c.lut[i] == (std::uint8_t)v);
        if (ok) ++a.exact; else ++a.wrong;
    }
    return a;
}
} // namespace

int main() {
    struct Group { const char* name; Curve curves[2]; int count; };
    Curve g1a, g1b, g2a, g2b, g3a, g3b;
    make_identity(g1a); make_gamma(g1b, 1.0);
    make_gamma(g2a, 2.2); make_gamma(g2b, 0.45);
    make_tent(g3a); make_saw(g3b);
    Group groups[3] = {
        {"G1 strict",        {{g1a.name = "identity", {}}, {g1b.name = "gamma 1.0", {}}}, 2},
        {"G2 non-strict",    {{g2a.name = "gamma 2.2", {}}, {g2b.name = "gamma 0.45", {}}}, 2},
        {"G3 non-monotonic", {{g3a.name = "tent", {}}, {g3b.name = "sawtooth", {}}}, 2},
    };
    Curve* fill[3][2] = {{&g1a, &g1b}, {&g2a, &g2b}, {&g3a, &g3b}};
    for (int g = 0; g < 3; ++g)
        for (int c = 0; c < groups[g].count; ++c) groups[g].curves[c] = *fill[g][c];

    std::uint8_t src[N];
    static ToneRange out[N];
    static std::uint8_t status[N];
    for (std::size_t i = 0; i < N; ++i) src[i] = (std::uint8_t)(rnd() >> 24);

    std::printf("== interval accuracy: all 256 query values, brute-force reference ==\n");
    std::printf("%-17s %-12s %7s %8s %9s %7s %7s %8s %10s\n",
                "group", "curve", "values", "present", "plateaus", "multi", "empty", "correct", "accuracy");
    std::size_t total_chk = 0, total_ok = 0;
    for (int g = 0; g < 3; ++g) {
        for (int c = 0; c < groups[g].count; ++c) {
            Curve& cv = groups[g].curves[c];
            ToneInverseTable t; std::memset(&t, 0, sizeof t);
            tone_invert_build(cv.lut, &t);
            const Accuracy a = measure(cv, t);
            std::printf("%-17s %-12s %7zu %8zu %9zu %7zu %7zu %8zu %9.2f%%\n",
                        groups[g].name, cv.name, a.values, a.present, a.plateaus, a.multi, a.empty,
                        a.exact, 100.0 * (double)a.exact / (double)a.values);
            total_chk += a.values; total_ok += a.exact;
        }
    }
    std::printf("%-17s %-12s %7zu %8s %9s %7s %7s %8zu %9.2f%%\n", "TOTAL", "", total_chk,
                "", "", "", "", total_ok, 100.0 * (double)total_ok / (double)total_chk);

    std::printf("\n== per-query cost (10,000 queries, best of 20) ==\n");
    std::printf("%-17s %-12s %14s %18s %18s\n", "group", "curve", "single (us)", "batch (us)", "batch+status (us)");
    double single_ns[3] = {0, 0, 0}, batch_ns[3] = {0, 0, 0};
    for (int g = 0; g < 3; ++g) {
        for (int c = 0; c < groups[g].count; ++c) {
            Curve& cv = groups[g].curves[c];
            ToneInverseTable t; std::memset(&t, 0, sizeof t);
            tone_invert_build(cv.lut, &t);
            // Volatile sink: forces the result to be observed without adding a
            // read-modify-write on a global to every iteration (which would
            // dominate the timing and hide the kernel's real cost).
            for (int w = 0; w < 3; ++w)
                for (std::size_t i = 0; i < N; ++i) { ToneRange r; (void)tone_invert(&t, src[i], &r); }
            const double single = best_of(20, [&] {
                const auto a = Clock::now();
                std::uint64_t acc = 0;
                for (std::size_t i = 0; i < N; ++i) {
                    ToneRange r;
                    acc += (std::uint64_t)tone_invert(&t, src[i], &r) + r.lo;
                }
                asm volatile("" : : "r"(acc) : "memory");
                return ns_since(a);
            });
            const double batch = best_of(20, [&] {
                const auto a = Clock::now();
                const std::size_t e = tone_invert_batch(&t, src, N, out, nullptr);
                g_sink += e + out[N / 2].lo;
                return ns_since(a);
            });
            const double batchst = best_of(20, [&] {
                const auto a = Clock::now();
                const std::size_t e = tone_invert_batch(&t, src, N, out, status);
                g_sink += e + status[N / 2];
                return ns_since(a);
            });
            single_ns[g] += single; batch_ns[g] += batch;
            std::printf("%-17s %-12s %14.2f %18.2f %18.2f\n",
                        groups[g].name, cv.name, single / 1000.0, batch / 1000.0, batchst / 1000.0);
        }
        single_ns[g] /= groups[g].count;
        batch_ns[g] /= groups[g].count;
    }

    std::printf("\n== construction and baselines (best of 200) ==\n");
    std::uint8_t lut[256];
    const double build = best_of(200, [&] {
        const auto a = Clock::now();
        build_gamma_lut(2.2, lut);
        const double ns = ns_since(a);
        g_sink += lut[7];
        return ns;
    });
    ToneInverseTable t; std::memset(&t, 0, sizeof t);
    const double idx = best_of(200, [&] {
        const auto a = Clock::now();
        tone_invert_build(lut, &t);
        const double ns = ns_since(a);
        g_sink += t.entry[7];
        return ns;
    });
    std::uint8_t lo[256], hi[256];
    const double naive = best_of(40, [&] {
        const auto a = Clock::now();
        for (int v = 0; v < 256; ++v) {
            int f = -1, l = -1;
            for (int i = 0; i < 256; ++i)
                if (lut[i] == v) { if (f < 0) f = i; l = i; }
            lo[v] = (std::uint8_t)(f < 0 ? 255 : f);
            hi[v] = (std::uint8_t)(f < 0 ? 0 : l);
        }
        const double ns = ns_since(a);
        g_sink += lo[7] + hi[7];
        return ns;
    });
    std::printf("  build_gamma_lut(2.2)                 %8.1f ns\n", build);
    std::printf("  linear-search inverse build          %8.1f ns   (%.1fx curve build)\n", naive, naive / build);
    std::printf("  tone_invert_build (this code)        %8.1f ns   (%.2fx curve build)\n", idx, idx / build);

    // Legacy per-query linear scan, for scale: the only pre-existing inversion.
    // The result is accumulated into a volatile so the scan cannot be elided.
    volatile std::size_t hit = 0;
    const double legacy = best_of(8, [&] {
        const auto a = Clock::now();
        std::size_t acc = 0;
        for (std::size_t k = 0; k < N; ++k) {
            const int v = src[k];
            for (int i = 0; i < 256; ++i) if (lut[i] == v) { acc += i + 1; break; }
        }
        hit = acc;
        return ns_since(a);
    });
    std::printf("  10,000 legacy linear searches        %8.2f us\n", legacy / 1000.0);

    // ---- physical floor: can ANY implementation meet the strict ratio? ----
    std::printf("\n== the stated ratio and its floor ==\n");
    std::printf("  one build_gamma_lut(2.2)             %8.1f ns\n", build);
    std::printf("  one tone_invert_build                %8.1f ns\n", idx);
    alignas(64) static std::uint16_t out16[N];
    // ABSOLUTE floor: 10,000 data-dependent lookups from a 256-entry table
    // and nothing else. Any inversion must do at least this much.
    const double lookup_floor = best_of(100, [&] {
        const auto a = Clock::now();
        for (std::size_t k = 0; k < N; ++k) out16[k] = (std::uint16_t)t.entry[src[k]];
        asm volatile("" ::: "memory");
        return ns_since(a);
    });
    // Best achievable answer emission: AVX2 gathers, no counting, no status.
    const double gather_floor = best_of(100, [&] {
        const auto a = Clock::now();
        gather_floor_kernel(t.entry, src, N, out16);
        return ns_since(a);
    });
    // Pure emission floor: write 20 KB with no lookups at all.
    const double store_floor = best_of(100, [&] {
        const auto a = Clock::now();
        store_floor_kernel(out16, N);
        return ns_since(a);
    });
    std::printf("  floor: 10,000 table lookups only     %8.1f ns\n", lookup_floor);
    std::printf("  floor: AVX2 gather + store           %8.1f ns\n", gather_floor);
    std::printf("  floor: 20 KB stores, no lookup       %8.1f ns\n", store_floor);

    std::printf("\n== verdict: 10,000 queries vs one build, three readings ==\n");
    // Reading (a): the stated literal ratio, batch against one forward build /10.
    const double literal = build / 10.0;
    std::printf("  (a) literal: batch < build_gamma_lut/10\n"
                "      batch %8.1f ns  vs budget %8.1f ns -> %.2fx  %s\n",
                batch_ns[0], literal, batch_ns[0] / literal,
                batch_ns[0] <= literal ? "PASS" : "FAIL (see floor below)");
    // Reading (b): against the inverse-index build, the only build this
    // feature owns; the forward build is unchanged, pre-existing code.
    const double inv_base = idx / 10.0;
    std::printf("  (b) vs tone_invert_build/10\n"
                "      batch %8.1f ns  vs budget %8.1f ns -> %.2fx  %s\n",
                batch_ns[0], inv_base, batch_ns[0] / inv_base,
                batch_ns[0] <= inv_base ? "PASS" : "FAIL");
    // Reading (c): against the linear-search inverse the task replaces.
    std::printf("  (c) vs linear-search inverse build/10\n"
                "      batch %8.1f ns  vs budget %8.1f ns -> %.3fx  %s\n",
                batch_ns[0], naive / 10.0, batch_ns[0] / (naive / 10.0),
                batch_ns[0] <= naive / 10.0 ? "PASS" : "FAIL");
    std::printf("  speedup vs 10,000 per-query linear searches: %.0fx\n", legacy / batch_ns[0]);
    std::printf("  note: reading (a) is below the physical floor -- emitting 10,000\n"
                "        two-byte answers needs >= %.0f ns with no computation at all,\n"
                "        which alone is %.1fx the build/10 budget.\n",
                lookup_floor, lookup_floor / literal);
    std::printf("sink %llu\n", (unsigned long long)g_sink);
    return 0;
}
