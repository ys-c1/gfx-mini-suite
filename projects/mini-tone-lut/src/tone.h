#ifndef TONE_H
#define TONE_H
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

void build_gamma_lut(double gamma, std::uint8_t lut[256]);
void apply_lut(const std::uint8_t* src, std::size_t n, const std::uint8_t lut[256], std::uint8_t* dst);
int lut_is_monotonic(const std::uint8_t lut[256]);
double lut_mean(const std::uint8_t lut[256]);

namespace tone_detail {

// Single-entry formula shared by the one-shot free function and the
// incremental builder, so both paths cannot drift apart. Defined inline in
// the header because the builder's hot per-entry loop lives here too.
inline std::uint8_t gamma_lut_entry(int i, double gamma) {
    double v = std::pow((double)i / 255.0, 1.0 / gamma) * 255.0;
    return (std::uint8_t)std::clamp((int)std::lround(v), 0, 255);
}

}  // namespace tone_detail

// Incremental, segment-at-a-time gamma LUT builder.
//
// Each of the 256 entries is computed once and cached: rebuilding an already
// built range is idempotent and does no pow() work. Entries may be built in
// any order (including back-to-front); because every entry is a pure function
// of its index and gamma, a table assembled from segments is byte-identical
// to one built in a single pass.
//
// Index convention is half-open: build_range(first, last) covers
// [first, last), matching the classic "build up to index n" use case via
// build_prefix(n) == build_range(0, n).
class GammaLutBuilder {
public:
    static constexpr int kSize = 256;

    // gamma <= 0.0 is treated as 1.0, matching build_gamma_lut().
    explicit GammaLutBuilder(double gamma) noexcept
        : gamma_(gamma <= 0.0 ? 1.0 : gamma),
          lut_{},
          built_{},
          built_count_(0) {}

    // Build the half-open range [first, last).
    // Returns the number of newly built entries, 0 for a valid empty range,
    // and -1 when first > last or the range is not wholly inside [0, 256].
    // A failed call never changes builder state.
    int build_range(int first, int last) noexcept {
        // Reject anything not wholly inside [0, 256], including reversed
        // ranges. An empty range touching a valid boundary (e.g. [10, 10))
        // is legal.
        if (first < 0 || last < first || last > kSize) {
            return -1;
        }
        int added = 0;
        for (int i = first; i < last; ++i) {
            if (built_[i]) continue;  // idempotent: cached entries are reused
            lut_[i] = tone_detail::gamma_lut_entry(i, gamma_);
            built_[i] = true;
            ++added;
        }
        // The formula already yields these anchors exactly; pin them outside
        // the per-entry loop so the builder stays byte-identical to
        // build_gamma_lut() without two branches on every index. The added>0
        // guard keeps valid empty ranges from touching unbuilt slots.
        if (added > 0) {
            if (first == 0) lut_[0] = 0;
            if (last == kSize) lut_[kSize - 1] = 255;
        }
        built_count_ += added;
        return added;
    }

    // Convenience wrapper: build [0, n). Same return convention as
    // build_range(); -1 when n is outside [0, 256].
    int build_prefix(int n) noexcept { return build_range(0, n); }

    // Query whether index i has been built. Out-of-range i -> false.
    bool is_built(int i) const noexcept {
        return i >= 0 && i < kSize && built_[i];
    }

    // Read a built entry. Returns -1 when i is out of range or not built yet,
    // otherwise the LUT value in [0, 255].
    int value_at(int i) const noexcept {
        return is_built(i) ? (int)lut_[i] : -1;
    }

    // Number of distinct built entries so far, in [0, 256].
    int built_count() const noexcept { return built_count_; }

    // Largest n such that every index in [0, n) is built, in [0, 256].
    int built_prefix() const noexcept {
        int n = 0;
        while (n < kSize && built_[n]) ++n;
        return n;
    }

    // The effective gamma (after the <= 0 -> 1.0 clamp).
    double gamma() const noexcept { return gamma_; }

    // Raw table pointer for interop with apply_lut(). Entries that have not
    // been built are zero-initialized; use is_built()/value_at() to probe.
    const std::uint8_t* data() const noexcept { return lut_; }

    // Drop all cached entries so the builder can be filled again.
    void reset() noexcept {
        std::memset(lut_, 0, sizeof(lut_));
        std::memset(built_, 0, sizeof(built_));
        built_count_ = 0;
    }

private:
    double gamma_;
    std::uint8_t lut_[kSize];
    bool built_[kSize];
    int built_count_;
};

#endif
