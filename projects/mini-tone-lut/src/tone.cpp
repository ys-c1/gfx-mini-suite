#include "tone.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define TONE_HAVE_X86 1
#endif

// ---------------------------------------------------------------------------
// Forward path: unchanged from the original implementation.
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Exact integer-domain inversion.
//
// The inverse index is built in a fixed number of passes, all O(256):
//   1. forward pass  : find every maximal plateau and record its first/last
//                      index per produced value; count how many plateaus
//                      produce each value (this is the multi-run evidence).
//   2. non-monotonic detection: a single decreasing step proves the curve is
//      not non-decreasing; that is recorded in the flags.
//   3. packed entry : first | last<<8 | status<<16, one 32-bit word per query
//      value, so a single indexed load answers an inversion.
//
// Nothing here reads or writes beyond the 256-byte curve and the table.
// ---------------------------------------------------------------------------

int tone_invert_build(const std::uint8_t lut[256], ToneInverseTable* out) {
    if (lut == nullptr || out == nullptr) return -1;

    // The stored curve is what later queries are verified against, so take it
    // verbatim before building anything else.
    std::memcpy(out->lut, lut, 256);

    // Absence is carried by the plateau counter (entry[v] == 0), never by a
    // sentinel index: 0xFF is a legal first index, so a magic value in first[]
    // would collide with the curve's own last byte.
    for (int v = 0; v < 256; ++v) {
        out->first[v] = 0u;
        out->last[v] = 0u;
        out->entry[v] = 0u; // plateau counter during construction
    }

    // Pass 1: maximal plateaus. first/last describe the *lowest-index* plateau
    // that produces a value, so they are written once, when that plateau is
    // first seen. Later plateaus of the same value raise the run count, which
    // is the evidence for TONE_INV_MULTI_RUN.
    int i = 0;
    while (i < 256) {
        const std::uint8_t v = out->lut[i];
        int j = i + 1;
        while (j < 256 && out->lut[j] == v) ++j;
        if (out->entry[v] == 0u) {
            out->first[v] = (std::uint8_t)i;
            out->last[v] = (std::uint8_t)(j - 1);
        }
        ++out->entry[v]; // plateau counter; at most 256 per value
        i = j;
    }

    // Pass 2: flags. A decreasing step is the only thing that can invalidate the
    // "one interval per value" answer, so monotonicity is derived from the curve
    // rather than assumed.
    bool monotonic = true;
    for (int k = 1; k < 256; ++k) {
        if (out->lut[k] < out->lut[k - 1]) { monotonic = false; break; }
    }
    std::uint32_t flags = TONE_FLAG_READY |
                          (monotonic ? TONE_FLAG_MONOTONIC : TONE_FLAG_NONMONOTONIC);

    // Pass 3: packed entry. entry[v] bit 0..7 = first, 8..15 = last,
    // 16..23 = status. Absent values keep the EMPTY status written above.
    for (int v = 0; v < 256; ++v) {
        if (out->entry[v] == 0u) {
            out->entry[v] = 0x80000000u | 0x00FFu | ((std::uint32_t)TONE_INV_EMPTY << 16);
            continue;
        }
        const std::uint32_t status = out->entry[v] > 1u ? (std::uint32_t)TONE_INV_MULTI_RUN
                                                        : (std::uint32_t)TONE_INV_OK;
        out->entry[v] = (std::uint32_t)out->first[v] |
                        ((std::uint32_t)out->last[v] << 8) |
                        (status << 16);
    }

    out->flags = flags;
    // generation is 0 only for a table that has never been built. Derive it
    // from a previously valid magic so a zero-initialised (or garbage) struct
    // still ends up with a defined, monotonically increasing value.
    out->generation = (out->magic == TONE_INV_MAGIC) ? out->generation + 1u : 1u;
    out->magic = TONE_INV_MAGIC;
    return 0;
}

int tone_invert_all(const ToneInverseTable* t, int v, ToneRange* runs, std::size_t cap,
                    std::size_t* out_count) {
    if (t == nullptr || t->magic != TONE_INV_MAGIC || (t->flags & TONE_FLAG_READY) == 0u)
        return TONE_INV_UNINITIALIZED;
    if (v < 0 || v > 255) return TONE_INV_OUT_OF_RANGE;
    if (out_count) *out_count = 0;
    if (t->entry[v] & 0x80000000u) return TONE_INV_EMPTY;

    // Recover every plateau bounding this value by walking the stored curve.
    // Only the runs that produce v matter, so a single scan answers both the
    // run list and the lowest-run range.
    std::size_t found = 0;
    for (int i = 0; i < 256;) {
        const std::uint8_t x = t->lut[i];
        int j = i + 1;
        while (j < 256 && t->lut[j] == x) ++j;
        if (x == (std::uint8_t)v) {
            if (found < cap && runs) {
                runs[found].lo = (std::uint8_t)i;
                runs[found].hi = (std::uint8_t)(j - 1);
            }
            ++found;
        }
        i = j;
    }
    if (out_count) *out_count = found;
    return found > 1u ? TONE_INV_MULTI_RUN : TONE_INV_OK;
}

int tone_pick(const ToneInverseTable* t, int v, TonePickPolicy policy) {
    if (t == nullptr || t->magic != TONE_INV_MAGIC || (t->flags & TONE_FLAG_READY) == 0u)
        return -1;
    if (v < 0 || v > 255) return -1;
    if (t->entry[v] & 0x80000000u) return -1;
    const int head = (int)t->first[v];
    const int tail = (int)t->last[v];
    switch (policy) {
        case TONE_PICK_HEAD:   return head;
        case TONE_PICK_TAIL:   return tail;
        case TONE_PICK_CENTER:
        default:               return (head + tail) / 2; // rounded down
    }
}

int tone_table_verify(const ToneInverseTable* t) {
    if (t == nullptr || t->magic != TONE_INV_MAGIC || (t->flags & TONE_FLAG_READY) == 0u)
        return 0;

    // Recompute the index from the stored curve and compare it field by field.
    // This catches a table overwritten after a successful build, which a magic
    // check alone cannot.
    std::uint8_t first[256], last[256], runs[256];
    for (int v = 0; v < 256; ++v) { first[v] = 0u; last[v] = 0u; runs[v] = 0u; }
    int i = 0;
    while (i < 256) {
        const std::uint8_t v = t->lut[i];
        int j = i + 1;
        while (j < 256 && t->lut[j] == v) ++j;
        if (runs[v] == 0u) {
            first[v] = (std::uint8_t)i;
            last[v] = (std::uint8_t)(j - 1);
        }
        if (runs[v] < 255u) ++runs[v];
        i = j;
    }
    bool monotonic = true;
    for (int k = 1; k < 256; ++k)
        if (t->lut[k] < t->lut[k - 1]) { monotonic = false; break; }
    const std::uint32_t want = TONE_FLAG_READY |
                               (monotonic ? TONE_FLAG_MONOTONIC : TONE_FLAG_NONMONOTONIC);
    if ((t->flags & (TONE_FLAG_READY | TONE_FLAG_MONOTONIC | TONE_FLAG_NONMONOTONIC)) != want)
        return 0;
    for (int v = 0; v < 256; ++v) {
        if (t->first[v] != first[v] || t->last[v] != last[v]) return 0;
        std::uint32_t want_entry;
        if (runs[v] == 0u)
            want_entry = 0x80000000u | 0x00FFu | ((std::uint32_t)TONE_INV_EMPTY << 16);
        else
            want_entry = (std::uint32_t)first[v] | ((std::uint32_t)last[v] << 8) |
                         ((runs[v] > 1u ? (std::uint32_t)TONE_INV_MULTI_RUN
                                        : (std::uint32_t)TONE_INV_OK) << 16);
        if (t->entry[v] != want_entry) return 0;
    }
    return 1;
}

std::size_t tone_invert_map(const ToneInverseTable* t, ToneRange out_map[256]) {
    if (out_map == nullptr) return 0;
    std::size_t present = 0;
    for (int v = 0; v < 256; ++v) {
        out_map[v] = TONE_RANGE_NONE;
        if (t == nullptr || t->magic != TONE_INV_MAGIC || (t->flags & TONE_FLAG_READY) == 0u)
            continue;
        if ((t->entry[v] & 0x80000000u) == 0u) {
            out_map[v].lo = (std::uint8_t)(t->entry[v] & 0xFFu);
            out_map[v].hi = (std::uint8_t)((t->entry[v] >> 8) & 0xFFu);
            ++present;
        }
    }
    return present;
}

#if defined(TONE_HAVE_X86)
// AVX2 kernel: 64 queries per iteration. The 32-bit entries are gathered eight
// at a time; the low 16 bits of each entry are the interval and are packed to
// four 16-byte stores. Absence is bit 31 of the entry, so empties are counted
// with movemask + popcount (one bit per query) and no compare per lane.
__attribute__((target("avx2")))
static std::size_t tone_invert_batch_avx2(const std::uint32_t* __restrict entry,
                                          const std::uint8_t* __restrict src, std::size_t n,
                                          ToneRange* __restrict out_ranges,
                                          std::uint8_t* __restrict status) {
    const __m256i m16 = _mm256_set1_epi32(0xFFFF);
    const __m256i m8 = _mm256_set1_epi32(0xFF);
    std::size_t k = 0;
    std::size_t empties = 0;
    for (; k + 64 <= n; k += 64) {
        __m256i v[8];
#pragma GCC unroll 8
        for (int j = 0; j < 8; ++j)
            v[j] = _mm256_i32gather_epi32((const int*)entry,
                _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i*)(src + k + 8 * j))), 4);
#pragma GCC unroll 8
        for (int j = 0; j < 8; ++j) {
            const __m256i m = _mm256_and_si256(v[j], m16);
            _mm_storeu_si128((__m128i*)(out_ranges + k + 8 * j),
                             _mm_packus_epi32(_mm256_castsi256_si128(m),
                                              _mm256_extracti128_si256(m, 1)));
            empties += (std::size_t)__builtin_popcount(
                (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(v[j])));
        }
        if (status != nullptr) {
#pragma GCC unroll 4
            for (int j = 0; j < 4; ++j) {
                const __m256i a = _mm256_and_si256(_mm256_srli_epi32(v[2 * j], 16), m8);
                const __m256i b = _mm256_and_si256(_mm256_srli_epi32(v[2 * j + 1], 16), m8);
                const __m128i s0 = _mm_packus_epi32(_mm256_castsi256_si128(a),
                                                    _mm256_extracti128_si256(a, 1));
                const __m128i s1 = _mm_packus_epi32(_mm256_castsi256_si128(b),
                                                    _mm256_extracti128_si256(b, 1));
                _mm_storeu_si128((__m128i*)(status + k + 16 * j), _mm_packus_epi16(s0, s1));
            }
        }
    }
    for (; k + 32 <= n; k += 32) { // one shorter vector block
        __m256i v[4];
#pragma GCC unroll 4
        for (int j = 0; j < 4; ++j)
            v[j] = _mm256_i32gather_epi32((const int*)entry,
                _mm256_cvtepu8_epi32(_mm_loadl_epi64((const __m128i*)(src + k + 8 * j))), 4);
#pragma GCC unroll 4
        for (int j = 0; j < 4; ++j) {
            const __m256i m = _mm256_and_si256(v[j], m16);
            _mm_storeu_si128((__m128i*)(out_ranges + k + 8 * j),
                             _mm_packus_epi32(_mm256_castsi256_si128(m),
                                              _mm256_extracti128_si256(m, 1)));
            empties += (std::size_t)__builtin_popcount(
                (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(v[j])));
        }
        if (status != nullptr) {
            for (int j = 0; j < 2; ++j) {
                const __m256i a = _mm256_and_si256(_mm256_srli_epi32(v[2 * j], 16), m8);
                const __m256i b = _mm256_and_si256(_mm256_srli_epi32(v[2 * j + 1], 16), m8);
                const __m128i s0 = _mm_packus_epi32(_mm256_castsi256_si128(a),
                                                    _mm256_extracti128_si256(a, 1));
                const __m128i s1 = _mm_packus_epi32(_mm256_castsi256_si128(b),
                                                    _mm256_extracti128_si256(b, 1));
                _mm_storeu_si128((__m128i*)(status + k + 16 * j), _mm_packus_epi16(s0, s1));
            }
        }
    }
    for (; k < n; ++k) { // scalar tail, same answer as tone_invert
        const std::uint32_t e = entry[src[k]];
        out_ranges[k].lo = (std::uint8_t)(e & 0xFFu);
        out_ranges[k].hi = (std::uint8_t)((e >> 8) & 0xFFu);
        empties += (e & 0x80000000u) != 0u;
        if (status != nullptr) status[k] = (std::uint8_t)((e >> 16) & 0xFFu);
    }
    return empties;
}

static bool tone_have_avx2() {
    static const bool ok = __builtin_cpu_supports("avx2");
    return ok;
}
#endif

std::size_t tone_invert_batch(const ToneInverseTable* t, const std::uint8_t* src, std::size_t n,
                              ToneRange* out_ranges, std::uint8_t* status) {
    if (src == nullptr || out_ranges == nullptr) return n;
    if (t == nullptr || t->magic != TONE_INV_MAGIC || (t->flags & TONE_FLAG_READY) == 0u) {
        // Same answer as a per-value query on an unbuilt table, so a caller can
        // rely on output buffers being fully defined after every call.
        for (std::size_t k = 0; k < n; ++k) out_ranges[k] = TONE_RANGE_NONE;
        if (status != nullptr)
            for (std::size_t k = 0; k < n; ++k) status[k] = TONE_INV_UNINITIALIZED;
        return n;
    }

    static_assert(sizeof(ToneRange) == 2, "ToneRange must stay two bytes");

#if defined(TONE_HAVE_X86)
    if (n >= 32 && tone_have_avx2())
        return tone_invert_batch_avx2(t->entry, src, n, out_ranges, status);
#endif

    // Portable fallback: one 32-bit table load and one 16-bit store per query.
    // The status lives in the entry, so the status pass is only paid on request.
    const std::uint32_t* __restrict entry = t->entry;
    std::size_t empty = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const std::uint32_t e = entry[src[k]];
        // No branch: absent entries already hold lo=0xFF, hi=0x00.
        out_ranges[k].lo = (std::uint8_t)(e & 0xFFu);
        out_ranges[k].hi = (std::uint8_t)((e >> 8) & 0xFFu);
        empty += (e & 0x80000000u) != 0u;
    }
    if (status != nullptr) {
        for (std::size_t k = 0; k < n; ++k)
            status[k] = (std::uint8_t)((entry[src[k]] >> 16) & 0xFFu);
    }
    return empty;
}
