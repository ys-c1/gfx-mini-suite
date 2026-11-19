#ifndef TONE_H
#define TONE_H
#include <cstddef>
#include <cstdint>
void build_gamma_lut(double gamma, std::uint8_t lut[256]);
void apply_lut(const std::uint8_t* src, std::size_t n, const std::uint8_t lut[256], std::uint8_t* dst);
int lut_is_monotonic(const std::uint8_t lut[256]);
double lut_mean(const std::uint8_t lut[256]);

/* ---- histogram matching (distribution-specified tone mapping) ---- */

/* Count pixel values of src[0..n) into hist. hist is zeroed first. */
void histogram_of(const std::uint8_t* src, std::size_t n, std::uint64_t hist[256]);

/* Build a monotone (non-decreasing) LUT that maps the source distribution
 * onto the target distribution by CDF alignment. Histograms are normalized
 * internally, so the two images may have different pixel counts.
 * Returns 1 on success.
 * Degenerate inputs (explicit results, never a crash):
 *   - target histogram sums to 0 ("empty target"): returns 0, lut = identity.
 *   - source histogram sums to 0 (empty image):    returns 0, lut = identity.
 *   - all-zero / single-color source: valid; returns 1 and a LUT that sends
 *     the single occupied bin to the matching target quantile. */
int build_match_lut(const std::uint64_t src_hist[256],
                    const std::uint64_t target_hist[256],
                    std::uint8_t lut[256]);

/* Mean pixel value of a distribution. total must be the sum of hist;
 * returns 0.0 when total == 0. */
double hist_mean(const std::uint64_t hist[256], std::uint64_t total);

/* Max absolute difference between the two normalized CDFs (KS distance,
 * in [0,1]). Counts may differ between a and b. Returns -1.0 when either
 * total is 0 (deviation undefined). */
double cdf_max_deviation(const std::uint64_t a[256], std::uint64_t na,
                         const std::uint64_t b[256], std::uint64_t nb);
#endif
