#include "../src/tone.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

// Verify that building [first,last) segments in a given permutation yields a
// table byte-identical to the one-shot build_gamma_lut() reference.
static void check_segmented(double gamma, const int* bounds,
                            const int* order, int nseg) {
    std::uint8_t ref[256];
    build_gamma_lut(gamma, ref);

    GammaLutBuilder b(gamma);
    for (int s = 0; s < nseg; ++s) {
        int k = order[s];
        int added = b.build_range(bounds[k], bounds[k + 1]);
        CHECK(added == bounds[k + 1] - bounds[k]);  // fresh build: all new
    }
    CHECK(b.built_count() == 256);
    CHECK(b.built_prefix() == 256);
    for (int i = 0; i < 256; ++i) {
        CHECK(b.is_built(i));
        CHECK(b.value_at(i) == (int)ref[i]);
    }
    CHECK(std::memcmp(b.data(), ref, 256) == 0);
    CHECK(lut_is_monotonic(b.data()));

    // Rebuilding every segment again must be a no-op (idempotent).
    for (int s = 0; s < nseg; ++s) {
        int k = order[s];
        CHECK(b.build_range(bounds[k], bounds[k + 1]) == 0);
    }
    CHECK(b.built_count() == 256);
    CHECK(std::memcmp(b.data(), ref, 256) == 0);
}

int main() {
    std::uint8_t lut[256];
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

    // ---- GammaLutBuilder: segmented builds across gammas and orderings ----
    const double gammas[] = {1.0, 2.2, 0.45, 0.0, -3.0};
    for (double g : gammas) {
        // 16 segments of 16 entries each.
        int bounds[17], fwd[16], rev[16], shuf[16];
        for (int k = 0; k < 16; ++k) {
            bounds[k] = k * 16;
            fwd[k] = k;
            rev[k] = 15 - k;
            shuf[k] = k;
        }
        bounds[16] = 256;
        // Fixed-seed permutation: 0,15,1,14,2,13,... (out-of-order, both ends)
        for (int k = 0; k < 16; ++k) shuf[k] = (k % 2 == 0) ? k / 2 : 15 - k / 2;

        check_segmented(g, bounds, fwd, 16);
        check_segmented(g, bounds, rev, 16);
        check_segmented(g, bounds, shuf, 16);
    }

    // One-call whole-table build via the builder must also match exactly.
    {
        std::uint8_t ref[256];
        build_gamma_lut(2.2, ref);
        GammaLutBuilder b(2.2);
        CHECK(b.build_range(0, 256) == 256);
        CHECK(std::memcmp(b.data(), ref, 256) == 0);
        CHECK(b.build_range(0, 256) == 0);  // fully cached
    }

    // ---- Overlapping, duplicate and partial incremental builds ----
    {
        GammaLutBuilder b(2.2);
        CHECK(b.build_range(100, 110) == 10);
        CHECK(b.built_count() == 10);
        CHECK(b.build_range(105, 120) == 10);  // 5 overlap reused, 10 new
        CHECK(b.built_count() == 20);
        CHECK(b.build_range(100, 120) == 0);   // fully contained: idempotent
        CHECK(b.built_count() == 20);
        for (int i = 100; i < 120; ++i) CHECK(b.is_built(i));
        CHECK(!b.is_built(99));
        CHECK(!b.is_built(120));

        // Build a back segment first, then the front: values must still match.
        GammaLutBuilder b2(2.2);
        std::uint8_t ref[256];
        build_gamma_lut(2.2, ref);
        CHECK(b2.build_range(200, 256) == 56);   // back segment first
        CHECK(b2.built_prefix() == 0);            // nothing built at the front
        CHECK(b2.build_range(0, 200) == 200);
        CHECK(b2.built_prefix() == 256);
        CHECK(std::memcmp(b2.data(), ref, 256) == 0);
    }

    // ---- Partial-state queries before completion ----
    {
        GammaLutBuilder b(1.0);
        CHECK(b.built_count() == 0);
        CHECK(b.built_prefix() == 0);
        CHECK(b.build_prefix(8) == 8);
        CHECK(b.built_prefix() == 8);
        CHECK(b.build_prefix(8) == 0);            // same prefix: idempotent
        for (int i = 0; i < 8; ++i) {
            CHECK(b.is_built(i));
            CHECK(b.value_at(i) == i);            // gamma 1.0 -> identity
        }
        CHECK(!b.is_built(8));
        CHECK(b.value_at(8) == -1);               // not built yet
        CHECK(b.build_prefix(256) == 248);
        CHECK(b.built_prefix() == 256);
    }

    // ---- Zero-length and out-of-range inputs: explicit return values ----
    {
        GammaLutBuilder b(2.2);
        CHECK(b.build_range(10, 10) == 0);        // valid empty range
        CHECK(b.build_range(0, 0) == 0);
        CHECK(b.build_range(256, 256) == 0);
        CHECK(b.built_count() == 0);              // empty builds add nothing

        CHECK(b.build_range(-1, 10) == -1);       // negative start
        CHECK(b.build_range(0, 257) == -1);       // past the end
        CHECK(b.build_range(256, 257) == -1);
        CHECK(b.build_range(10, 5) == -1);        // reversed
        CHECK(b.build_prefix(-1) == -1);
        CHECK(b.build_prefix(257) == -1);
        CHECK(b.built_count() == 0);              // failed calls change nothing

        // Query probes out of range report "not available", never UB.
        CHECK(!b.is_built(-1));
        CHECK(!b.is_built(256));
        CHECK(b.value_at(-1) == -1);
        CHECK(b.value_at(256) == -1);
    }

    // ---- gamma clamping matches the free function ----
    {
        GammaLutBuilder b(0.0);
        CHECK(b.gamma() == 1.0);
        std::uint8_t ref[256];
        build_gamma_lut(-1.0, ref);
        CHECK(b.build_range(0, 256) == 256);
        CHECK(std::memcmp(b.data(), ref, 256) == 0);
    }

    // ---- reset() empties the builder so it can be refilled ----
    {
        GammaLutBuilder b(2.2);
        b.build_range(0, 100);
        b.reset();
        CHECK(b.built_count() == 0);
        CHECK(b.built_prefix() == 0);
        CHECK(!b.is_built(0));
        CHECK(b.value_at(50) == -1);
        CHECK(b.build_range(0, 100) == 100);      // all entries computed again
    }

    // ---- builder output interoperates with the existing applier ----
    {
        GammaLutBuilder b(2.2);
        b.build_range(0, 256);
        std::uint8_t src[4] = {0, 42, 200, 255};
        std::uint8_t dst[4];
        apply_lut(src, 4, b.data(), dst);
        CHECK(dst[1] == b.data()[42]);
        CHECK(dst[3] == 255);
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
