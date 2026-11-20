// A/B forward-path comparison: original implementation vs current.
//
// The original source is vendored under tests/original_tone.cpp (copied from
// git HEAD before this feature). Both are compiled into this one binary with
// renamed symbols, and every forward-path function is compared byte for byte
// over many gammas and buffer lengths. This is the "before and after"对照
// the task asks for: any change to the existing curve build or apply shows up
// as a mismatch here regardless of the golden arrays.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>

// Original (tests/original_tone.cpp, macro-renamed at compile time).
void orig_build(double, std::uint8_t[256]);
void orig_apply(const std::uint8_t*, std::size_t, const std::uint8_t[256], std::uint8_t*);
int orig_mono(const std::uint8_t[256]);
double orig_mean(const std::uint8_t[256]);
// Current (src/tone.cpp, macro-renamed at compile time).
void cur_build(double, std::uint8_t[256]);
void cur_apply(const std::uint8_t*, std::size_t, const std::uint8_t[256], std::uint8_t*);
int cur_mono(const std::uint8_t[256]);
double cur_mean(const std::uint8_t[256]);

int main() {
    const double gammas[] = {-5.0, -1.0, 0.0, 1e-6, 0.05, 0.1, 0.25, 0.45, 0.5, 0.7,
                             0.9, 1.0, 1.1, 1.5, 1.8, 2.2, 2.4, 2.6, 3.0, 4.5,
                             7.5, 10.0, 50.0, 1e6};
    int cases = 0, mismatches = 0;
    std::uint8_t a[256], b[256], src[4096], da[4096], db[4096];
    unsigned rng = 0xC0FFEEu;
    for (int rep = 0; rep < 3; ++rep) {
        for (int i = 0; i < 4096; ++i) { rng = rng * 1664525u + 1013904223u; src[i] = (std::uint8_t)(rng >> 24); }
        for (double g : gammas) {
            std::memset(a, 0xAA, sizeof a);
            std::memset(b, 0x55, sizeof b);
            orig_build(g, a);
            cur_build(g, b);
            ++cases;
            if (std::memcmp(a, b, 256) != 0) { ++mismatches; std::printf("build mismatch gamma=%.17g\n", g); }
            const std::size_t lens[] = {0, 1, 2, 255, 256, 1000, 4096};
            for (std::size_t n : lens) {
                orig_apply(src, n, a, da);
                cur_apply(src, n, b, db);
                ++cases;
                if (std::memcmp(da, db, n) != 0) { ++mismatches; std::printf("apply mismatch gamma=%.17g n=%zu\n", g, n); }
            }
            ++cases;
            if (orig_mono(a) != cur_mono(b)) { ++mismatches; std::printf("mono mismatch %.17g\n", g); }
            ++cases;
            if (orig_mean(a) != cur_mean(b)) { ++mismatches; std::printf("mean mismatch %.17g\n", g); }
        }
    }
    for (int t = 0; t < 2000; ++t) {
        for (int i = 0; i < 256; ++i) { rng = rng * 1664525u + 1013904223u; a[i] = (std::uint8_t)(rng >> 24); }
        ++cases; if (orig_mono(a) != cur_mono(a)) ++mismatches;
        ++cases; if (orig_mean(a) != cur_mean(a)) ++mismatches;
    }
    std::printf("forward A/B: %d cases, %d mismatches\n", cases, mismatches);
    return mismatches ? 1 : 0;
}
