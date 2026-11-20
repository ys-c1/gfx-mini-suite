// Inverse-path tests: exact integer inversion of a 256-entry curve.
//
// Correctness is established three ways:
//   1. against an independent brute-force reference written here from the
//      problem statement (no shared code with src/tone.cpp),
//   2. on three named curve families with hand-checkable shapes,
//   3. on random curves, including adversarial ones (flat, alternating,
//      sawtooth) produced by a fixed-seed PRNG.
// Forward-path stability is checked against golden bytes captured from the
// original implementation before this feature was added.
#include "../src/tone.h"
#include "golden_forward.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

// ---------------------------------------------------------------------------
// Independent reference: scan the curve and collect runs by brute force.
// ---------------------------------------------------------------------------
struct RefRuns { int lo[256]; int hi[256]; int n; }; // n = number of runs

static RefRuns ref_runs(const std::uint8_t lut[256], int v) {
    RefRuns r; r.n = 0;
    for (int i = 0; i < 256;) {
        int j = i;
        while (j < 256 && lut[j] == lut[i]) ++j;
        if (lut[i] == (std::uint8_t)v) { r.lo[r.n] = i; r.hi[r.n] = j - 1; ++r.n; }
        i = j;
    }
    return r;
}

// Number of disjoint runs producing v, taken from the reference scan.
static int ref_run_count(const std::uint8_t lut[256], int v) { return ref_runs(lut, v).n; }

// Standalone predicate, deliberately not lut_is_monotonic(), so the reference
// and the implementation cannot share a bug.
static bool ref_monotonic(const std::uint8_t lut[256]) {
    for (int i = 1; i < 256; ++i) if (lut[i] < lut[i - 1]) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Golden forward path: byte-for-byte identical to the original implementation.
// ---------------------------------------------------------------------------
static void test_forward_golden() {
    std::uint8_t lut[256], dst[256];
    for (int k = 0; k < 12; ++k) {
        std::memset(lut, 0xAA, sizeof lut);
        build_gamma_lut(k_gammas[k], lut);
        CHECK(std::memcmp(lut, k_golden_lut[k], 256) == 0);
        apply_lut(k_apply_src, 256, lut, dst);
        CHECK(std::memcmp(dst, k_golden_apply[k], 256) == 0);
    }
    // The original clamp/lround behaviour at the boundaries.
    build_gamma_lut(-1.0, lut);
    CHECK(lut[0] == 0 && lut[255] == 255);
    CHECK(lut_is_monotonic(lut));
}

// ---------------------------------------------------------------------------
// Three named curves: strict, plateaus, non-monotonic.
// ---------------------------------------------------------------------------
static void curve_identity(std::uint8_t lut[256]) {
    for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)i;
}
static void curve_gamma(std::uint8_t lut[256]) { build_gamma_lut(2.2, lut); }
static void curve_s_wave(std::uint8_t lut[256]) {
    // Tent: rises 0..127 then descends 127..0. Non-monotonic, and every value
    // 1..127 is produced by exactly two disjoint runs.
    for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)(i < 128 ? i : 255 - i);
}
static void curve_sawtooth(std::uint8_t lut[256]) { // each value appears once
    for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)(255 - i);
}

static void test_inverse_against_reference(const std::uint8_t lut[256], const char* name) {
    ToneInverseTable t;
    std::memset(&t, 0, sizeof t);
    CHECK(tone_invert_build(lut, &t) == 0);
    CHECK(tone_table_verify(&t) == 1);
    CHECK(std::memcmp(t.lut, lut, 256) == 0); // curve copied verbatim

    for (int v = -1; v <= 256; ++v) {
        ToneRange r = TONE_RANGE_NONE;
        const ToneInvStatus st = tone_invert(&t, v, &r);
        if (v < 0 || v > 255) {
            CHECK(st == TONE_INV_OUT_OF_RANGE);
            CHECK(tone_range_empty(r));
            CHECK(tone_pick(&t, v, TONE_PICK_CENTER) == -1);
            continue;
        }
        RefRuns rr = ref_runs(lut, (std::uint8_t)v);
        if (rr.n == 0) {
            CHECK(st == TONE_INV_EMPTY);
            CHECK(tone_range_empty(r));
            CHECK(tone_pick(&t, v, TONE_PICK_HEAD) == -1);
            std::size_t cnt = 123;
            ToneRange runs[8];
            CHECK(tone_invert_all(&t, v, runs, 8, &cnt) == TONE_INV_EMPTY);
            CHECK(cnt == 0);
        } else {
            CHECK(st == (rr.n > 1 ? TONE_INV_MULTI_RUN : TONE_INV_OK));
            CHECK(r.lo == rr.lo[0] && r.hi == rr.hi[0]);
            CHECK(tone_pick(&t, v, TONE_PICK_HEAD) == rr.lo[0]);
            CHECK(tone_pick(&t, v, TONE_PICK_TAIL) == rr.hi[0]);
            CHECK(tone_pick(&t, v, TONE_PICK_CENTER) == (rr.lo[0] + rr.hi[0]) / 2);
            // every run, in lowest-index order
            std::size_t cnt = 0;
            ToneRange runs[16];
            const int st2 = tone_invert_all(&t, (int)v, runs, 16, &cnt);
            CHECK(st2 == (rr.n > 1 ? TONE_INV_MULTI_RUN : TONE_INV_OK));
            CHECK(cnt == (std::size_t)rr.n);
            for (int k = 0; k < rr.n && k < 16; ++k)
                CHECK(runs[k].lo == rr.lo[k] && runs[k].hi == rr.hi[k]);
            // truncation: count is still exact when capacity is not
            std::size_t cnt2 = 0;
            tone_invert_all(&t, (int)v, nullptr, 0, &cnt2);
            CHECK(cnt2 == (std::size_t)rr.n);
            ToneRange one;
            std::size_t cnt3 = 0;
            tone_invert_all(&t, (int)v, &one, 1, &cnt3);
            CHECK(cnt3 == (std::size_t)rr.n);
            CHECK(one.lo == rr.lo[0] && one.hi == rr.hi[0]);
        }
    }

    // Full map agrees with per-value queries.
    ToneRange map[256];
    const std::size_t present = tone_invert_map(&t, map);
    std::size_t expect_present = 0;
    bool seen[256] = {false};
    for (int i = 0; i < 256; ++i) seen[lut[i]] = true;
    for (int v = 0; v < 256; ++v) expect_present += seen[v];
    CHECK(present == expect_present);
    for (int v = 0; v < 256; ++v) {
        ToneRange r;
        tone_invert(&t, v, &r);
        CHECK(r.lo == map[v].lo && r.hi == map[v].hi);
    }
    std::printf("  %-12s verified against reference\n", name);
}

static void test_named_curves() {
    std::uint8_t lut[256];
    curve_identity(lut); test_inverse_against_reference(lut, "identity");
    curve_gamma(lut);    test_inverse_against_reference(lut, "gamma 2.2");
    curve_s_wave(lut);   test_inverse_against_reference(lut, "non-monotonic");
    curve_sawtooth(lut); test_inverse_against_reference(lut, "sawtooth");

    // The identity curve has no plateaus: interval == point.
    curve_identity(lut);
    ToneInverseTable t; std::memset(&t, 0, sizeof t);
    tone_invert_build(lut, &t);
    ToneRange r;
    CHECK(tone_invert(&t, 77, &r) == TONE_INV_OK);
    CHECK(r.lo == 77 && r.hi == 77);
    CHECK(t.flags & TONE_FLAG_MONOTONIC);

    // gamma 2.2 has plateaus and holes: report a real interval and an EMPTY.
    curve_gamma(lut);
    tone_invert_build(lut, &t);
    CHECK(t.flags & TONE_FLAG_MONOTONIC);
    int plateau_v = -1;
    for (int v = 0; v < 256 && plateau_v < 0; ++v)
        if (ref_run_count(lut, v) == 1 && t.last[v] > t.first[v]) plateau_v = v;
    CHECK(plateau_v >= 0);
    CHECK(tone_invert(&t, plateau_v, &r) == TONE_INV_OK);
    CHECK(r.hi > r.lo);
    // every index in the interval really maps to the value
    for (int i = r.lo; i <= r.hi; ++i) CHECK(lut[i] == (std::uint8_t)plateau_v);
    // values not present anywhere in the curve
    int absent = -1;
    for (int v = 0; v < 256 && absent < 0; ++v)
        if (ref_run_count(lut, v) == 0) absent = v;
    CHECK(absent >= 0);
    CHECK(tone_invert(&t, absent, &r) == TONE_INV_EMPTY);
    CHECK(tone_range_empty(r));

    // Non-monotonic curve: a value can have several disjoint runs.
    curve_s_wave(lut); tone_invert_build(lut, &t);
    CHECK(t.flags & TONE_FLAG_NONMONOTONIC);
    CHECK(!(t.flags & TONE_FLAG_MONOTONIC));
    int multi = -1;
    for (int v = 0; v < 256 && multi < 0; ++v) if (ref_run_count(lut, v) > 1) multi = v;
    CHECK(multi >= 0);
    std::size_t cnt = 0;
    ToneRange runs[8];
    CHECK(tone_invert_all(&t, multi, runs, 8, &cnt) == TONE_INV_MULTI_RUN);
    CHECK(cnt == (std::size_t)ref_run_count(lut, multi));
    CHECK(cnt > 1);
    CHECK(tone_invert(&t, multi, &r) == TONE_INV_MULTI_RUN);
    CHECK(r.lo == runs[0].lo && r.hi == runs[0].hi);
    CHECK(runs[0].hi < runs[1].lo); // disjoint, ordered
}

// ---------------------------------------------------------------------------
// Random curves, including adversarial shapes.
// ---------------------------------------------------------------------------
static void test_random_curves() {
    std::uint32_t seed = 0x12345678u;
    unsigned bad = 0;
    for (int trial = 0; trial < 400; ++trial) {
        std::uint8_t lut[256];
        seed = seed * 1664525u + 1013904223u;
        const int kind = (int)(seed >> 24) % 6;
        if (kind == 0) {
            for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)(i ^ (seed >> 16));
        } else if (kind == 1) {
            std::uint8_t cur = 0;
            for (int i = 0; i < 256; ++i) { seed = seed * 1664525u + 1013904223u; if ((seed >> 28) == 0) ++cur; lut[i] = cur; }
        } else if (kind == 2) {
            for (int i = 0; i < 256; ++i) lut[i] = 7; // constant
        } else if (kind == 3) {
            for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)((i & 1) ? 255 : 0); // alternating
        } else if (kind == 4) {
            for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)(i & 0x0F); // sawtooth, 16 runs of 16
        } else {
            build_gamma_lut(0.2 + 5.0 * ((seed >> 8) & 0xFF) / 255.0, lut);
        }
        ToneInverseTable t; std::memset(&t, 0, sizeof t);
        if (tone_invert_build(lut, &t) != 0) { ++bad; continue; }

        // Flags must match an independent monotonicity check.
        const bool mono = ref_monotonic(lut);
        CHECK((int)((t.flags & TONE_FLAG_MONOTONIC) != 0) == mono);
        CHECK((int)((t.flags & TONE_FLAG_NONMONOTONIC) != 0) == !mono);

        for (int v = 0; v < 256; ++v) {
            RefRuns rr = ref_runs(lut, (std::uint8_t)v);
            ToneRange r;
            const ToneInvStatus st = tone_invert(&t, v, &r);
            if (rr.n == 0) { CHECK(st == TONE_INV_EMPTY); CHECK(tone_range_empty(r)); continue; }
            CHECK(st == (rr.n > 1 ? TONE_INV_MULTI_RUN : TONE_INV_OK));
            CHECK(r.lo == rr.lo[0] && r.hi == rr.hi[0]);
            CHECK(r.lo <= r.hi);
            for (int i = r.lo; i <= r.hi; ++i) CHECK(lut[i] == (std::uint8_t)v);
            std::size_t cnt = 0;
            ToneRange many[64];
            tone_invert_all(&t, v, many, 64, &cnt);
            CHECK(cnt == (std::size_t)rr.n);
            if ((int)cnt <= 64)
                for (std::size_t k = 0; k < cnt; ++k)
                    CHECK(many[k].lo == (std::uint8_t)rr.lo[k] && many[k].hi == (std::uint8_t)rr.hi[k]);
        }
    }
    CHECK(bad == 0);
    std::printf("  400 random/adversarial curves verified\n");
}

// ---------------------------------------------------------------------------
// Deterministic behaviour for the three required inputs.
// ---------------------------------------------------------------------------
static void test_nonmonotonic_behaviour() {
    // Value 100 is produced by two disjoint plateaus: 10..12 (hand-made) and
    // 200..203 (hand-made). i & 0x3F only reaches 63, so it adds none.
    std::uint8_t lut[256];
    for (int i = 0; i < 256; ++i) lut[i] = (std::uint8_t)(i & 0x3F);
    lut[10] = lut[11] = lut[12] = 100;
    lut[200] = lut[201] = lut[202] = lut[203] = 100;

    ToneInverseTable t; std::memset(&t, 0, sizeof t);
    CHECK(tone_invert_build(lut, &t) == 0);
    CHECK(t.flags & TONE_FLAG_NONMONOTONIC);
    CHECK(ref_run_count(lut, 100) == 2);

    ToneRange r;
    CHECK(tone_invert(&t, 100, &r) == TONE_INV_MULTI_RUN);
    CHECK(r.lo == 10 && r.hi == 12); // lowest-index run, not a merged 10..203
    CHECK(tone_pick(&t, 100, TONE_PICK_HEAD) == 10);
    CHECK(tone_pick(&t, 100, TONE_PICK_TAIL) == 12);
    CHECK(tone_pick(&t, 100, TONE_PICK_CENTER) == 11);

    std::size_t cnt = 0;
    ToneRange all[8];
    CHECK(tone_invert_all(&t, 100, all, 8, &cnt) == TONE_INV_MULTI_RUN);
    bool saw_tail_run = false;
    for (std::size_t k = 0; k < cnt; ++k)
        if (all[k].lo == 200 && all[k].hi == 203) saw_tail_run = true;
    CHECK(saw_tail_run);
    // Repeated calls are stable.
    ToneRange r2;
    CHECK(tone_invert(&t, 100, &r2) == TONE_INV_MULTI_RUN);
    CHECK(r2.lo == r.lo && r2.hi == r.hi);
}

static void test_out_of_range() {
    std::uint8_t lut[256]; curve_identity(lut);
    ToneInverseTable t; std::memset(&t, 0, sizeof t);
    tone_invert_build(lut, &t);
    const int bad[] = {-1, -128, 256, 1000, -2147483647 - 1, 2147483647};
    for (int v : bad) {
        ToneRange r = {1, 2};
        CHECK(tone_invert(&t, v, &r) == TONE_INV_OUT_OF_RANGE);
        CHECK(tone_range_empty(r));
        std::size_t cnt = 99;
        ToneRange runs[2];
        CHECK(tone_invert_all(&t, v, runs, 2, &cnt) == TONE_INV_OUT_OF_RANGE);
        CHECK(cnt == 99); // untouched
        CHECK(tone_pick(&t, v, TONE_PICK_CENTER) == -1);
    }
    // Boundary values really are in range.
    ToneRange r;
    CHECK(tone_invert(&t, 0, &r) == TONE_INV_OK && r.lo == 0 && r.hi == 0);
    CHECK(tone_invert(&t, 255, &r) == TONE_INV_OK && r.lo == 255 && r.hi == 255);
    // null output pointer is accepted
    CHECK(tone_invert(&t, 7, nullptr) == TONE_INV_OK);
}

static void test_uninitialized() {
    std::uint8_t lut[256]; curve_identity(lut);
    ToneRange r = {3, 4};
    std::size_t cnt = 55;
    ToneRange runs[2];

    // Zero-initialised table: never built.
    ToneInverseTable z; std::memset(&z, 0, sizeof z);
    CHECK(tone_invert(&z, 10, &r) == TONE_INV_UNINITIALIZED);
    CHECK(tone_range_empty(r));
    CHECK(tone_pick(&z, 10, TONE_PICK_HEAD) == -1);
    CHECK(tone_invert_all(&z, 10, runs, 2, &cnt) == TONE_INV_UNINITIALIZED);
    CHECK(cnt == 55);
    CHECK(tone_table_verify(&z) == 0);
    ToneRange map[256];
    CHECK(tone_invert_map(&z, map) == 0);
    CHECK(tone_range_empty(map[0]) && tone_range_empty(map[255]));
    std::uint8_t src[4] = {0, 1, 2, 3}, st[4];
    ToneRange out[4];
    CHECK(tone_invert_batch(&z, src, 4, out, st) == 4);
    for (int i = 0; i < 4; ++i) { CHECK(tone_range_empty(out[i])); CHECK(st[i] == TONE_INV_UNINITIALIZED); }
    CHECK(tone_invert_batch(&z, src, 4, out, nullptr) == 4);

    // Null table pointer.
    CHECK(tone_invert(nullptr, 10, &r) == TONE_INV_UNINITIALIZED);
    CHECK(tone_table_verify(nullptr) == 0);
    CHECK(tone_invert_build(lut, nullptr) == -1);
    CHECK(tone_invert_build(nullptr, &z) == -1);
    CHECK(tone_invert_build(lut, &z) == 0);

    // Built, then corrupted in different ways.
    ToneInverseTable t; std::memset(&t, 0, sizeof t);
    CHECK(tone_invert_build(lut, &t) == 0);
    CHECK(tone_table_verify(&t) == 1);
    const std::uint32_t good_magic = t.magic;
    t.magic = good_magic ^ 1u;
    CHECK(tone_table_verify(&t) == 0);
    CHECK(tone_invert(&t, 10, &r) == TONE_INV_UNINITIALIZED);
    t.magic = good_magic;
    t.first[10] ^= 0x40u; // plausible-looking but wrong index
    CHECK(tone_table_verify(&t) == 0);
    t.first[10] ^= 0x40u;
    CHECK(tone_table_verify(&t) == 1);
    std::memset(&t, 0xFF, sizeof t); // fully overwritten memory
    CHECK(tone_table_verify(&t) == 0);
    CHECK(tone_invert(&t, 0, &r) == TONE_INV_UNINITIALIZED);

    // Rebuilding bumps the generation and never reports 0.
    ToneInverseTable g; std::memset(&g, 0, sizeof g);
    CHECK(g.generation == 0);
    tone_invert_build(lut, &g);
    const std::uint32_t gen1 = g.generation;
    CHECK(gen1 != 0);
    tone_invert_build(lut, &g);
    CHECK(g.generation == gen1 + 1);
}

static void test_batch() {
    std::uint8_t lut[256]; curve_s_wave(lut);
    ToneInverseTable t; std::memset(&t, 0, sizeof t);
    tone_invert_build(lut, &t);

    std::uint8_t src[512], st[512];
    ToneRange out[512];
    for (int i = 0; i < 512; ++i) src[i] = (std::uint8_t)((i * 37 + 11) & 0xFF);
    const std::size_t empty = tone_invert_batch(&t, src, 512, out, st);
    std::size_t expect_empty = 0;
    for (int i = 0; i < 512; ++i) {
        ToneRange r;
        const ToneInvStatus s = tone_invert(&t, src[i], &r);
        CHECK(out[i].lo == r.lo && out[i].hi == r.hi);
        CHECK(st[i] == s);
        expect_empty += (s == TONE_INV_EMPTY);
    }
    CHECK(empty == expect_empty);

    // Every length around the 32-wide SIMD chunk boundary, including a length
    // that leaves a scalar tail, must agree with per-value queries. This also
    // exercises whichever kernel the CPU actually selected.
    for (int len = 0; len <= 130; ++len) {
        for (int i = 0; i < len; ++i) src[i] = (std::uint8_t)((i * 91 + 7) & 0xFF);
        std::size_t e = tone_invert_batch(&t, src, (std::size_t)len, out, st);
        std::size_t want_e = 0;
        for (int i = 0; i < len; ++i) {
            ToneRange r;
            const ToneInvStatus s = tone_invert(&t, src[i], &r);
            CHECK(out[i].lo == r.lo && out[i].hi == r.hi);
            CHECK(st[i] == s);
            want_e += (s == TONE_INV_EMPTY);
        }
        CHECK(e == want_e);
        // Same length without the optional status buffer.
        std::size_t e2 = tone_invert_batch(&t, src, (std::size_t)len, out, nullptr);
        CHECK(e2 == want_e);
        for (int i = 0; i < len; ++i) {
            ToneRange r;
            tone_invert(&t, src[i], &r);
            CHECK(out[i].lo == r.lo && out[i].hi == r.hi);
        }
    }

    // Multi-run values keep their lowest run in batched output too.
    std::uint8_t tent[256];
    for (int i = 0; i < 256; ++i) tent[i] = (std::uint8_t)(i < 128 ? i : 255 - i);
    tone_invert_build(tent, &t);
    std::uint8_t src2[64];
    ToneRange out2[64];
    std::uint8_t st2[64];
    for (int i = 0; i < 64; ++i) src2[i] = (std::uint8_t)(i * 4);
    tone_invert_batch(&t, src2, 64, out2, st2);
    for (int i = 0; i < 64; ++i) {
        ToneRange r;
        const ToneInvStatus s = tone_invert(&t, src2[i], &r);
        CHECK(out2[i].lo == r.lo && out2[i].hi == r.hi);
        CHECK(st2[i] == s);
        if (s == TONE_INV_MULTI_RUN) CHECK(out2[i].lo == src2[i]); // head of the low run
    }

    // Null buffers: defined result, no writes.
    CHECK(tone_invert_batch(&t, nullptr, 8, out, st) == 8);
    CHECK(tone_invert_batch(&t, src, 8, nullptr, st) == 8);
    CHECK(tone_invert_batch(&t, src, 0, out, st) == 0);
}

int main() {
    std::printf("forward golden path :\n");
    test_forward_golden();
    std::printf("inverse vs reference :\n");
    test_named_curves();
    std::printf("random curves :\n");
    test_random_curves();
    std::printf("required inputs :\n");
    test_nonmonotonic_behaviour();
    test_out_of_range();
    test_uninitialized();
    test_batch();
    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
