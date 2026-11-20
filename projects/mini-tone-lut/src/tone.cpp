#include "tone.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace {

constexpr double kEps = 1e-12;

/* Solve the sum-to-one equality constrained least squares on the active set.
 * Writes non-negative weights into w (length n, zero outside active) and
 * returns the residual sum of squares, or -1 if the system is singular. */
double solve_active_set(const double* const* cols, int n, const std::uint8_t* target,
                        int mask, double* w, double* gram) {
    int idx[TONE_FIT_MAX_CURVES], m = 0;
    for (int i = 0; i < n; ++i)
        if (mask & (1 << i)) idx[m++] = i;
    if (m == 0) {
        double e = 0.0;
        for (int k = 0; k < 256; ++k) e += (double)target[k] * target[k];
        for (int i = 0; i < n; ++i) w[i] = 0.0;
        return e;
    }

    /* KKT system:  [G 1; 1^T 0] [x; lambda] = [c; 1]. */
    int k = m + 1;
    for (int a = 0; a < m; ++a)
        for (int b = 0; b < m; ++b) gram[a * k + b] = 0.0;
    double rhs[TONE_FIT_MAX_CURVES + 1];
    rhs[m] = 1.0;
    for (int a = 0; a < m; ++a) {
        double c = 0.0;
        for (int t = 0; t < 256; ++t) c += cols[idx[a]][t] * target[t];
        rhs[a] = c;
        for (int b = 0; b < m; ++b) {
            double s = 0.0;
            for (int t = 0; t < 256; ++t) s += cols[idx[a]][t] * cols[idx[b]][t];
            gram[a * k + b] = s;
        }
        gram[a * k + m] = 1.0;
        gram[m * k + a] = 1.0;
    }
    gram[m * k + m] = 0.0;

    double sol[TONE_FIT_MAX_CURVES + 1];
    /* The KKT matrix is indefinite; do a plain LU via Gaussian elimination. */
    for (int col = 0; col < k; ++col) {
        int piv = col;
        for (int r = col + 1; r < k; ++r)
            if (std::fabs(gram[r * k + col]) > std::fabs(gram[piv * k + col])) piv = r;
        if (std::fabs(gram[piv * k + col]) <= kEps) return -1.0;
        if (piv != col) {
            for (int c = 0; c < k; ++c) std::swap(gram[col * k + c], gram[piv * k + c]);
            std::swap(rhs[col], rhs[piv]);
        }
        for (int r = col + 1; r < k; ++r) {
            double f = gram[r * k + col] / gram[col * k + col];
            if (f == 0.0) continue;
            for (int c = col; c < k; ++c) gram[r * k + c] -= f * gram[col * k + c];
            rhs[r] -= f * rhs[col];
        }
    }
    for (int r = k - 1; r >= 0; --r) {
        double s = rhs[r];
        for (int c = r + 1; c < k; ++c) s -= gram[r * k + c] * sol[c];
        sol[r] = s / gram[r * k + r];
    }

    for (int i = 0; i < n; ++i) w[i] = 0.0;
    for (int a = 0; a < m; ++a) {
        if (sol[a] < -1e-9) return -1.0; /* not a feasible active set */
        w[idx[a]] = std::max(0.0, sol[a]);
    }
    /* Renormalize to absorb clamping / round-off. */
    double sum = 0.0;
    for (int a = 0; a < m; ++a) sum += w[idx[a]];
    if (sum <= kEps) return -1.0;
    for (int a = 0; a < m; ++a) w[idx[a]] /= sum;

    double e = 0.0;
    for (int t = 0; t < 256; ++t) {
        double v = 0.0;
        for (int i = 0; i < n; ++i) v += w[i] * cols[i][t];
        double d = v - target[t];
        e += d * d;
    }
    return e;
}

} // namespace

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

int tone_normalize_weights(int n, const double* weights, double* out) {
    if (n < 0 || n > TONE_FIT_MAX_CURVES || (!weights && n > 0) || (!out && n > 0)) return -1;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(weights[i]) || weights[i] < 0.0) return -1;
        sum += weights[i];
    }
    if (sum <= kEps) return -1;
    /* Copy before writing so out may alias weights. */
    double tmp[TONE_FIT_MAX_CURVES];
    for (int i = 0; i < n; ++i) tmp[i] = weights[i] / sum;
    for (int i = 0; i < n; ++i) out[i] = tmp[i];
    return 0;
}

int blend_luts(const std::uint8_t* const* luts, int n, const double* weights,
               std::uint8_t out[256]) {
    if (n <= 0 || n > TONE_FIT_MAX_CURVES || !luts || !out) return -1;
    for (int i = 0; i < n; ++i) if (!luts[i]) return -1;
    double w[TONE_FIT_MAX_CURVES];
    if (tone_normalize_weights(n, weights, w) != 0) return -1;

    for (int t = 0; t < 256; ++t) {
        double v = 0.0;
        for (int i = 0; i < n; ++i) v += w[i] * luts[i][t];
        /* The +1e-9 keeps exact .5 values from rounding down after FP noise. */
        out[t] = (std::uint8_t)std::clamp((long)std::lround(v + 1e-9), 0L, 255L);
    }
    /* Endpoints are fixed for every blend: a convex combination already maps
     * 0->0 and 255->255 when the inputs do, but pin them in case a caller
     * passes a curve whose endpoints were not pinned. */
    out[0] = 0;
    out[255] = 255;
    return 0;
}

int fit_blend_weights(const std::uint8_t* const* luts, int n,
                      const std::uint8_t target[256],
                      double* out_weights, double* out_rmse) {
    if (n <= 0 || n > TONE_FIT_MAX_CURVES || !luts || !target) return -1;
    for (int i = 0; i < n; ++i) if (!luts[i]) return -1;

    const double* cols[TONE_FIT_MAX_CURVES];
    double colbuf[TONE_FIT_MAX_CURVES][256];
    for (int i = 0; i < n; ++i) {
        for (int t = 0; t < 256; ++t) colbuf[i][t] = (double)luts[i][t];
        cols[i] = colbuf[i];
    }

    double gram[(TONE_FIT_MAX_CURVES + 1) * (TONE_FIT_MAX_CURVES + 1)];

    double best_w[TONE_FIT_MAX_CURVES] = {0.0};
    double best_e = std::numeric_limits<double>::infinity();
    double w[TONE_FIT_MAX_CURVES];

    /* Non-empty active sets. The unconstrained-on-active-set solution can land
     * on the boundary; feasible sets are evaluated by their true residual, so
     * duplicates are harmless and we keep the global (hence exact) optimum. */
    for (int mask = 1; mask < (1 << n); ++mask) {
        double e = solve_active_set(cols, n, target, mask, w, gram);
        if (e < 0.0) continue;
        if (e < best_e - 1e-12) {
            best_e = e;
            for (int i = 0; i < n; ++i) best_w[i] = w[i];
        }
    }
    if (!std::isfinite(best_e)) return -1;

    /* Recompute RMSE on the actual 8-bit LUT the caller would use. */
    std::uint8_t blended[256];
    if (blend_luts(luts, n, best_w, blended) != 0) return -1;
    double sse = 0.0;
    for (int t = 0; t < 256; ++t) {
        double d = (double)blended[t] - target[t];
        sse += d * d;
    }
    if (out_weights) for (int i = 0; i < n; ++i) out_weights[i] = best_w[i];
    if (out_rmse) *out_rmse = std::sqrt(sse / 256.0);
    return 0;
}
