#ifndef TONE_H
#define TONE_H
#include <cstddef>
#include <cstdint>

/* Maximum number of curves accepted by fit_blend_weights. The fitter
 * enumerates active sets, so the cost grows as 2^n (255 solves at n=8). */
#define TONE_FIT_MAX_CURVES 8

void build_gamma_lut(double gamma, std::uint8_t lut[256]);
void apply_lut(const std::uint8_t* src, std::size_t n, const std::uint8_t lut[256], std::uint8_t* dst);
int lut_is_monotonic(const std::uint8_t lut[256]);
double lut_mean(const std::uint8_t lut[256]);

/* Normalize n weights into out (out may alias weights): every weight must be
 * finite and >= 0, and the sum must be > 0. Returns 0 on success, -1 on
 * invalid input (out is left untouched). */
int tone_normalize_weights(int n, const double* weights, double* out);

/* Blend n curves by weights. Weights are normalized internally, so raw
 * weights (e.g. 2,2,0) are accepted. The result is rounded to 8 bits,
 * endpoints are pinned to 0 and 255, and monotonic input curves always give a
 * monotonic result (a convex combination of monotone functions is monotone).
 * Returns 0 on success, -1 on invalid input. */
int blend_luts(const std::uint8_t* const* luts, int n, const double* weights,
               std::uint8_t out[256]);

/* Inverse problem: find weights w >= 0, sum(w) = 1 such that the blend of
 * luts[] is as close as possible to target in the least-squares sense. The
 * constrained optimum is found exactly by enumerating active sets; RMSE is
 * measured on the final rounded/pinned LUT. On success writes n weights and
 * sets *out_rmse (either output pointer may be null). Returns 0 on success,
 * -1 on invalid input. */
int fit_blend_weights(const std::uint8_t* const* luts, int n,
                      const std::uint8_t target[256],
                      double* out_weights, double* out_rmse);

#endif
