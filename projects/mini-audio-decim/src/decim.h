/*
 * decim -- multistage decimator with runtime-designed, runtime-verified
 * Kaiser-windowed FIR anti-alias filters.
 *
 * Why multistage
 * --------------
 * Decimating by D in one step needs a filter whose transition band is
 * measured in units of 1/D of the input rate; its length grows roughly
 * linearly with D. Splitting D = m1*m2*...*mK lets every stage work on a
 * signal whose rate has already been lowered, so the total number of
 * multiply-accumulates per input sample is much smaller while the
 * alias rejection is at least as good.  With the design rule below the
 * composite rejection for every frequency that aliases is provably the
 * rejection of the *first* stage that pushes that frequency past its
 * own output Nyquist (see "Alias proof" below).
 *
 * Filter design basis (no tables, no fixed coefficients)
 * ------------------------------------------------------
 * Every stage is a Type-I (odd-length, symmetric) linear-phase FIR
 * designed at decim_init() time from closed-form formulas:
 *
 *   ideal lowpass   h_d[n] = 2*fc * sinc(2*fc*(n-M)),   M = (N-1)/2
 *   Kaiser window   w[n]   = I0(beta*sqrt(1-((n-M)/M)^2)) / I0(beta)
 *   taps            h[n]   = h_d[n] * w[n] / sum(h_d*w)
 *
 *   cutoff          fc = (fp + fs)/2        (cycles / stage input sample)
 *   window shape    beta = 0.1102*(A-8.7)                  A > 50
 *                         0.5842*(A-21)^0.4 + 0.07886*(A-21)  21 <= A <= 50
 *                         0                                      A < 21
 *   length          N = ceil((A-8)/(2.285*dw)) + 1, dw = 2*pi*(fs-fp)
 *
 * The length/window formulas are Kaiser's empirical design equations
 * (J. F. Kaiser, "Nonrecursive digital filter design using the
 * I0-sinh window function", Proc. IEEE ISCAS 1974; Oppenheim &
 * Schafer, Discrete-Time Signal Processing, 3rd ed., section 7.3).
 * A is the design attenuation in dB.  It is derived from the requested
 * stopband attenuation plus 6 dB of margin (Kaiser's own rule of thumb),
 * or from the passband ripple bound when that is stricter:
 *
 *   A = max(stop_db + 6, -20*log10(delta_p))
 *
 * where delta_p is the per-stage linear passband deviation implied by
 * the requested total ripple bound (split equally across stages).
 *
 * The approximate formulas only place the first candidate.  The
 * coefficients are *not* trusted because a formula produced them.  For
 * every stage the library evaluates the realized response on a dense
 * grid and requires
 *
 *   passband   [0, fp]   max |H(f)-1| <= delta_p
 *   stopband   [fs, .5]  max |H(f)|   <= 10^(-(stop_db+3)/20)
 *
 * i.e. 3 dB tighter than the advertised stop_db, so the measured
 * cascade can never sit exactly on the requirement.  If the candidate
 * misses either bound the length is increased by two and the filter is
 * rebuilt; if the tap budget runs out, decim_init() fails with
 * DECIM_E_DESIGN rather than returning a filter that does not meet the
 * advertised specification.  The whole cascade is then measured again
 * on [0, passband*0.5/D] and on the alias band [0.5/D, 0.5]; the
 * measured worst-case values are stored in the Decim object and
 * reported by the harness.
 *
 * Stage band edges
 * ----------------
 * Let `fp_out = passband * 0.5 / D` be the overall passband edge in
 * cycles per *input* sample (the default passband = 0.8 puts it at
 * 0.4/D).  Stage k has
 *
 *   fp_k = fp_out * P_k          P_k = m1*...*m(k-1)
 *   fs_k = 0.5 / m_k             (its own output Nyquist)
 *
 * so fs_k > fp_k whenever passband < 1.
 *
 * Alias proof
 * -----------
 * Fix an input tone at f >= 0.5/D (exactly the frequencies that alias
 * in the final D-fold decimation).  Claim: at some stage this tone is
 * in that stage's stopband, [0.5/m_k, 0.5], and is therefore
 * attenuated by at least stop_db.
 *
 * Suppose not: at every stage f_k < 0.5/m_k.  Then no stage aliases the
 * tone, so f_k = f*P_k for every k.  In particular the last stage sees
 * f_last = f*D/m_last >= 0.5/m_last, contradicting the supposition.
 * Let k be the first stage with f_k >= 0.5/m_k.  Earlier stages did not
 * alias (f_j < 0.5/m_j), hence f_k = f*P_k and the tone is inside stage
 * k's stopband.  Later stages have passband gain at most 1+delta_p each,
 * so
 *
 *   composite gain <= (1+delta_p)^(K-1) * 10^(-(stop_db+3)/20)
 *
 * which for the defaults (stop_db = 80 dB, ripple 0.01 dB) is below
 * -80 dB, far under the required -40 dB.  This is checked numerically
 * at init and by the unit tests.
 *
 * Deterministic edge cases
 * ------------------------
 *  * total factor == 1 (empty stage list, or every factor == 1):
 *    the output is a byte-for-byte copy of the input, including NaN
 *    payloads and signed zero.  Status DECIM_OK_BYPASS.  In-place
 *    processing is allowed on this path (memmove).
 *
 *  * input length not divisible by the total factor, and at least the
 *    filter support long: each stage emits floor(n_k / m_k) frames and
 *    drops the n_k mod m_k trailing input frames of that stage; nested
 *    floors give exactly floor(n/D) output frames.  The dropped tail
 *    never affects an emitted output because the filters are causal.
 *    Status DECIM_OK_TAIL and stats.tail_frames = n mod D.
 *
 *  * input shorter than the filter support (n < decim_span_input()):
 *    zero initial state and zero extension past the end, exactly as if
 *    the signal were padded with zeros.  Output is the causal
 *    convolution, floor(n/D) frames (possibly zero).  This holds
 *    whether or not n is a multiple of D.  Status DECIM_OK_SHORT.
 *
 *  * n == 0: zero outputs, no reads or writes, status DECIM_OK (or
 *    DECIM_OK_BYPASS when D == 1).
 *
 * The status is one value, so documentation needs a precedence:
 * argument errors, then bypass, then empty input, then short input,
 * then a non-divisible tail.  A short input that is also non-divisible
 * (5 samples, D = 6) therefore reports OK_SHORT; the caller still gets
 * the exact floor(n/D) count from stats/decim_out_frames, and
 * stats.partial is set as well, so neither fact is hidden.
 */
#ifndef DECIM_H
#define DECIM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DECIM_MAX_STAGES 8

typedef enum {
    DECIM_OK        =  0, /* all input represented, all bounds met       */
    DECIM_OK_BYPASS =  1, /* total factor 1: bit-identical copy          */
    DECIM_OK_TAIL   =  2, /* n mod D != 0: floor(n/D) frames emitted     */
    DECIM_OK_SHORT  =  3, /* n shorter than the filter support          */
    DECIM_E_ARG     = -1, /* bad pointer, factor, config or capacity     */
    DECIM_E_DESIGN  = -2, /* specification not achievable in tap budget  */
    DECIM_E_SPACE   = -3, /* out_cap too small, nothing written          */
    DECIM_E_NOMEM   = -4  /* allocation failure                          */
} decim_status;

typedef struct {
    double passband;  /* overall passband edge as a fraction of the output
                         Nyquist frequency, (0,1); default 0.8         */
    double stop_db;   /* required alias rejection, >= 40 dB              */
    double ripple_db; /* max total passband ripple, dB, > 0              */
    int max_taps;     /* per-stage tap budget, odd, default 8191         */
} DecimConfig;

typedef struct {
    int m;          /* decimation factor of this stage                   */
    int taps;       /* odd length, Type I                                */
    int delay;      /* (taps-1)/2 input samples                          */
    double beta;    /* Kaiser window shape                               */
    double fc;      /* cutoff, cycles / stage input sample               */
    double fp;      /* designed passband edge                            */
    double fs;      /* designed stopband edge                            */
    double *h;      /* taps, sum(h) == 1                                 */
    double measured_ripple_db; /* realized passband peak-to-peak, dB    */
    double measured_stop_db;   /* realized stopband attenuation, dB     */
} DecimStage;

typedef struct {
    DecimConfig cfg;
    int n_stages;
    int total;      /* product of the stage factors (>= 1)               */
    int span_taps;  /* composite impulse-response length, input samples  */
    DecimStage stages[DECIM_MAX_STAGES];
    double measured_ripple_db; /* whole-cascade passband p-p, dB         */
    double measured_alias_db;  /* whole-cascade worst alias attenuation  */
} Decim;

typedef struct {
    size_t in_frames;
    size_t out_frames;
    size_t tail_frames; /* input frames not represented, n mod D        */
    int bypass;         /* bit-identical copy path ran                  */
    int short_input;    /* in_frames < span_taps                        */
    int partial;        /* n mod D != 0                                 */
    double peak_out;    /* max |out|                                    */
} DecimStats;

/* Fill cfg with passband=0.8, stop_db=80, ripple_db=0.01, max_taps=8191. */
void decim_config_default(DecimConfig *cfg);

/*
 * Design and verify a cascade.
 *
 * factors[0..n_stages-1] are the per-stage decimation factors, in
 * processing order.  Factors equal to 1 are legal and are skipped;
 * an empty list (n_stages == 0) is a pure passthrough.  The total
 * factor is the product of the listed factors and is available from
 * decim_total_factor().
 *
 * cfg may be NULL for the defaults.  Returns DECIM_OK or a negative
 * status.  On failure the object is zeroed and decim_free() is safe.
 */
int decim_init(Decim *d, const int *factors, int n_stages,
               const DecimConfig *cfg);

void decim_free(Decim *d);

/* Product of the stage factors; 1 for an empty/skipped list. */
int decim_total_factor(const Decim *d);

/* Number of output frames for in_frames input frames: floor(n/D). */
size_t decim_out_frames(const Decim *d, size_t in_frames);

/* Length of the composite impulse response in input samples:
 * sum_k (taps_k - 1) * P_k + 1.  An input shorter than this lies
 * entirely inside the filter's transient and is convolved against zero
 * history; this is the DECIM_OK_SHORT threshold. */
size_t decim_span_input(const Decim *d);

/*
 * Realized linear gain of one stage at f cycles per stage-input sample.
 * Returns |H(f)|.
 */
double decim_stage_gain(const DecimStage *s, double f);

/*
 * Realized steady-state gain of the whole cascade for an input tone at
 * f cycles per input sample, including the alias folding performed by
 * the decimators.  For f < 0.5/D this is the ordinary response; for
 * f >= 0.5/D it is the gain of the image that lands in the output
 * band, which is exactly the quantity bounded by the alias spec.
 */
double decim_response(const Decim *d, double f);

/*
 * Process in_frames mono float samples.
 *
 * Emits exactly floor(n/D) samples when the plan is not a bypass, or n
 * samples on the bypass path.  out_cap must be at least that large or
 * DECIM_E_SPACE is returned and out receives no writes.  out may alias
 * in only on the bypass path.  stats may be NULL; when non-NULL it is
 * overwritten (not accumulated).
 *
 * Arithmetic: the taps are designed and stored in double, and every
 * stage accumulates in double.  Each stage's output is stored in
 * double as well, so the only float rounding is the final store to
 * `out` (and the state a caller sees).  This keeps the alias floor
 * below -130 dB for the default design instead of the ~-110 dB that
 * per-stage float storage would impose.
 */
int decim_run(const Decim *d, const float *in, size_t in_frames,
              float *out, size_t out_cap, DecimStats *stats);

const char *decim_status_name(int status);

#ifdef __cplusplus
}
#endif

#endif /* DECIM_H */
