"""Synthetic long recordings of two free-running clocks, with ground truth.

Each recording is a stream of events observed by both clocks:
clock A (reference, treated as truth) and clock B whose rate ratio
r(t) = dB/dt drifts.  Events get independent timestamp jitter on both
sides, mimicking detection/quantization noise.
"""

import random


def simulate(duration, dt_event, jitter_a, jitter_b, ratio_at, seed=0):
    """Generate one recording.

    ratio_at: callable r(t) -> instantaneous B/A rate ratio.
    Returns (events, b_of_t):
      events  list of (x, y) timestamp pairs (A, B), jittered
      b_of_t  exact cumulative B-clock time function (ground truth)
    """
    rng = random.Random(seed)
    # integrate r(t) on a fine grid for the exact B(t) curve
    step = min(dt_event / 10.0, 0.05)
    ts = [0.0]
    bs = [0.0]
    t = 0.0
    while t < duration:
        r = ratio_at(t + 0.5 * step)
        t += step
        ts.append(t)
        bs.append(bs[-1] + r * step)

    def b_of_t(x):
        i = int(x / step)
        if i >= len(ts) - 1:
            return bs[-1]
        f = (x - ts[i]) / step
        return bs[i] * (1.0 - f) + bs[i + 1] * f

    events = []
    x = 0.0
    while x <= duration:
        y = b_of_t(x)
        events.append((x + rng.gauss(0.0, jitter_a),
                       y + rng.gauss(0.0, jitter_b)))
        x += dt_event
    return events, b_of_t


# --- the three long recordings (1 h each, one event every 0.5 s) ----

DURATION = 3600.0
DT_EVENT = 0.5
R0 = 1.0 + 50e-6          # nominal B/A ratio: B runs 50 ppm fast


def rec_constant():
    """Rec 1: fixed ratio offset, small jitter.  The easy case."""
    return simulate(DURATION, DT_EVENT, 20e-6, 20e-6,
                    ratio_at=lambda t: R0, seed=1)


def rec_linear_drift():
    """Rec 2: ratio drifts linearly, 2e-9/s (7.2 ppm over the hour)."""
    alpha = 2e-9
    return simulate(DURATION, DT_EVENT, 20e-6, 20e-6,
                    ratio_at=lambda t: R0 + alpha * t, seed=2)


def rec_random_walk():
    """Rec 3: random-walk ratio, larger jitter, one 1 ppm step at 40 min."""
    rw = random.Random(3)
    sigma = 5e-9            # per sqrt(second)
    steps = []
    t = 0.0
    r = R0
    while t < DURATION:
        r += rw.gauss(0.0, sigma) + (1e-6 if abs(t - 2400.0) < 0.025 else 0.0)
        steps.append(r)
        t += 0.05

    def ratio_at(t):
        i = min(int(t / 0.05), len(steps) - 1)
        return steps[i]

    return simulate(DURATION, DT_EVENT, 100e-6, 100e-6,
                    ratio_at=ratio_at, seed=3)
