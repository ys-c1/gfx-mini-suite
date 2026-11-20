#ifndef TONE_H
#define TONE_H
#include <cstddef>
#include <cstdint>
void build_gamma_lut(double gamma, std::uint8_t lut[256]);
void apply_lut(const std::uint8_t* src, std::size_t n, const std::uint8_t lut[256], std::uint8_t* dst);
int lut_is_monotonic(const std::uint8_t lut[256]);
double lut_mean(const std::uint8_t lut[256]);

// ---- exact integer-domain inversion ---------------------------------------
// The curve is a total function lut: [0,255] -> [0,255]. Inverting a value v
// means finding every index i with lut[i] == v. The answer is always stated as
// an index interval (a maximal run of equal bytes), never as a single point.
//
// Degenerate inputs have a fixed answer, documented once here:
//   * never built / overwritten table   -> TONE_INV_UNINITIALIZED, empty range
//   * v outside [0,255]                 -> TONE_INV_OUT_OF_RANGE, empty range
//   * v not present in the curve        -> TONE_INV_EMPTY, empty range
//   * v present in more than one run    -> TONE_INV_MULTI_RUN, lowest run
//                                          (use tone_invert_all for the rest)
// An empty range is lo=0xFF, hi=0x00 (lo > hi), so a genuine interval always
// satisfies lo <= hi, including the full [0,255].
//
// Plateau rule: the interval [first,last] is the answer and needs no tie-break.
// If one index is required, tone_pick() chooses deterministically:
//   HEAD   = first index  -> exactly what a 0..255 linear search returns
//   TAIL   = last index   -> exactly what a 255..0 linear search returns
//   CENTER = (first+last)/2, rounded down (default; minimises worst-case error)

struct ToneRange {
    std::uint8_t lo;
    std::uint8_t hi;
};

inline bool tone_range_empty(ToneRange r) { return r.lo > r.hi; }
constexpr ToneRange TONE_RANGE_NONE = {0xFFu, 0x00u};

enum ToneInvStatus : std::uint8_t {
    TONE_INV_OK            = 0u, // exactly one run; out_range is that run
    TONE_INV_EMPTY         = 1u, // no index maps to v
    TONE_INV_OUT_OF_RANGE  = 2u, // v < 0 or v > 255
    TONE_INV_UNINITIALIZED = 3u, // table was never built (or lost its magic)
    TONE_INV_MULTI_RUN     = 4u  // several disjoint runs; out_range is the lowest
};

enum TonePickPolicy : std::uint32_t {
    TONE_PICK_CENTER = 0u,
    TONE_PICK_HEAD   = 1u,
    TONE_PICK_TAIL   = 2u
};

enum : std::uint32_t {
    TONE_INV_MAGIC         = 0x54564E49u, // 'INVT'
    TONE_FLAG_READY        = 1u << 0,
    TONE_FLAG_MONOTONIC    = 1u << 1,     // lut[i] >= lut[i-1] for all i
    TONE_FLAG_NONMONOTONIC = 1u << 2      // at least one decreasing step
};

// Inverse index of a 256-byte curve. The curve is copied verbatim; building
// never modifies it.  Zero-initialise the structure before the first build.
//
// entry[v] packs the whole answer for query v into one 32-bit word:
//     bit  31      set iff v is absent (EMPTY); zero for present values
//     bits 16..23  ToneInvStatus (OK / MULTI_RUN for present values)
//     bits  8..15  last index of the lowest-index run
//     bits  0..7   first index of that same run
// so one indexed load answers a query; the status never needs a second table.
// The absence bit doubles as the sign bit, so a SIMD kernel can count empties
// with one movemask/popcount per eight queries.
struct ToneInverseTable {
    std::uint32_t magic;       // TONE_INV_MAGIC once built
    std::uint32_t generation;  // increments on every successful build; 0 = never built
    std::uint32_t flags;
    std::uint32_t reserved;
    std::uint8_t  lut[256];    // forward curve, byte-identical to the source
    std::uint8_t  first[256];  // first index of the lowest-index run (0 if absent)
    std::uint8_t  last[256];   // last index of that same run (0 if absent)
    std::uint32_t entry[256];  // packed interval + status, see above
};

// Build or rebuild the index. lut and out must not overlap. Returns 0 on
// success; -1 (and leaves *out untouched) when lut or out is null.
int tone_invert_build(const std::uint8_t lut[256], ToneInverseTable* out);

// The cost model, so a caller can budget: building the index is three linear
// passes over 256 bytes with no pow and no per-value search. Every query after
// that is one indexed 32-bit load, independent of curve shape.
//
// Invert v. Writes *out_range (TONE_RANGE_NONE on every non-OK status) unless
// out_range is null. Defined inline: it is a single table lookup on the hot path.
inline ToneInvStatus tone_invert(const ToneInverseTable* t, int v, ToneRange* out_range) {
    if (t == nullptr || t->magic != TONE_INV_MAGIC || (t->flags & TONE_FLAG_READY) == 0u) {
        if (out_range) *out_range = TONE_RANGE_NONE;
        return TONE_INV_UNINITIALIZED;
    }
    if (v < 0 || v > 255) {
        if (out_range) *out_range = TONE_RANGE_NONE;
        return TONE_INV_OUT_OF_RANGE;
    }
    // Branchless: an absent value already stores lo=0xFF, hi=0x00 in its
    // entry, which *is* the empty encoding, and its status byte is EMPTY.
    // So the same three extractions serve both cases and no data-dependent
    // branch can mispredict on plateaus or gaps.
    const std::uint32_t e = t->entry[v];
    if (out_range) {
        out_range->lo = (std::uint8_t)(e & 0xFFu);
        out_range->hi = (std::uint8_t)((e >> 8) & 0xFFu);
    }
    return (ToneInvStatus)((e >> 16) & 0xFFu);
}

// Every preimage run of v, lowest index first, written to runs[0..min(*count,cap)-1].
// *out_count receives the true number of runs even when cap is smaller; it is
// set to 0 when v is empty. Returns OK (one run) or MULTI_RUN (two or more).
// EMPTY / OUT_OF_RANGE / UNINITIALIZED leave runs untouched.
int tone_invert_all(const ToneInverseTable* t, int v, ToneRange* runs, std::size_t cap,
                    std::size_t* out_count);

// One representative index for v under the chosen policy, or -1 when v has no
// preimage (also -1 for an unbuilt table or an out-of-range value).
int tone_pick(const ToneInverseTable* t, int v, TonePickPolicy policy);

// Check the stored index against the stored curve. Returns 1 when the table is
// built, self-consistent and its flags match lut, 0 otherwise. O(256); not on
// the hot path. A zeroed or overwritten table returns 0.
int tone_table_verify(const ToneInverseTable* t);

// Invert n values. out_ranges[i] is the answer for src[i] (same status
// semantics as tone_invert). status may be null; when non-null it receives the
// per-element ToneInvStatus. Returns the number of elements with no preimage
// (EMPTY), or n when src or out_ranges is null. Returns n with every output
// written (as EMPTY / UNINITIALIZED) when the table is unbuilt, so output
// buffers are always fully defined after the call. src and out_ranges must not
// overlap.
std::size_t tone_invert_batch(const ToneInverseTable* t, const std::uint8_t* src, std::size_t n,
                              ToneRange* out_ranges, std::uint8_t* status);

// Write the complete inverse of the curve: out_map[v] is the answer for every
// possible query value v in one O(256) pass. Because the query domain is
// exhaustive, this map answers any number of later lookups by plain indexing.
// Returns the number of values present in the curve.
std::size_t tone_invert_map(const ToneInverseTable* t, ToneRange out_map[256]);

#endif
