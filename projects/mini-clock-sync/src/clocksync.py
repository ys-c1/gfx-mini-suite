"""Online clock-rate ratio estimation between two free-running device clocks.

Model:  y = r * x + b + noise
where (x, y) are timestamps of the *same* event observed by clock A
(reference) and clock B.  A fixed ratio r is never exact — both clocks
drift — so the ratio is estimated online over a sliding window.  Once
the estimate is stable it is frozen ("locked"), subsequent B timestamps
are converted with the frozen estimate, and an explicit upper bound on
the residual drift is carried along:

    |to_ref(y) - x_true|  <=  drift_bound(x)

        = [ E0 + (m * sigma_r + kappa * T_w) * dt + 0.5 * kappa * dt^2 ] / r

    E0      max |residual| in the window at lock time (B seconds)
    sigma_r standard error of the slope at lock time
    m       lock threshold multiplier (frozen from the lock criterion)
    kappa   safety * max |dr/dt| observed before lock  (s^-1)
    T_w     time span of the estimation window at lock
    dt      x - x_lock
"""

from collections import deque
import math


class ClockSync:
    def __init__(self, window=600, check_every=25, lock_checks=5,
                 lock_sigma_mult=4.0, rate_safety=3.0):
        self.window = window              # pairs kept in the sliding window
        self.check_every = check_every    # events between stability checks
        self.lock_checks = lock_checks    # consecutive stable checks to lock
        self.lock_sigma_mult = lock_sigma_mult
        self.rate_safety = rate_safety

        self._xs = deque()
        self._ys = deque()
        self._n = 0
        self._prev_r = None
        self._prev_x = None
        self._stable = 0
        self._max_rate = 0.0              # max |dr/dt| seen before lock

        self.locked = False
        self.r = None                     # frozen ratio
        self.b = None                     # frozen offset
        self.lock_x = None                # reference time at lock
        self.e0 = None                    # max |residual| at lock (B seconds)
        self.sigma_r = None               # slope std error at lock
        self.kappa = None                 # bound on |dr/dt| after lock
        self.lock_span = None             # window time span at lock

    # -- estimation --------------------------------------------------

    def _fit(self):
        n = len(self._xs)
        sx = sum(self._xs)
        sy = sum(self._ys)
        sxx = sum(x * x for x in self._xs)
        sxy = sum(x * y for x, y in zip(self._xs, self._ys))
        den = n * sxx - sx * sx
        r = (n * sxy - sx * sy) / den
        b = (sy - r * sx) / n
        return r, b

    def _residual_stats(self, r, b):
        res = [y - (r * x + b) for x, y in zip(self._xs, self._ys)]
        n = len(res)
        var = sum(e * e for e in res) / max(n - 2, 1)
        return max(abs(e) for e in res), math.sqrt(var)

    def update(self, x, y):
        """Feed one aligned (A, B) timestamp pair.  Returns locked state."""
        if self.locked:
            return True
        self._xs.append(x)
        self._ys.append(y)
        if len(self._xs) > self.window:
            self._xs.popleft()
            self._ys.popleft()
        self._n += 1
        if len(self._xs) < self.window or self._n % self.check_every:
            return False

        r, b = self._fit()
        span = self._xs[-1] - self._xs[0]
        _, s = self._residual_stats(r, b)
        n = len(self._xs)
        # slope std error for (roughly) uniform x over the window span
        sigma_r = math.sqrt(12.0 * s * s / (n * span * span))

        if self._prev_r is not None:
            dt = x - self._prev_x
            rate = abs(r - self._prev_r) / dt
            self._max_rate = max(self._max_rate, rate)
            if abs(r - self._prev_r) <= self.lock_sigma_mult * sigma_r:
                self._stable += 1
            else:
                self._stable = 0
        self._prev_r = r
        self._prev_x = x

        if self._stable >= self.lock_checks:
            e0, _ = self._residual_stats(r, b)
            self.locked = True
            self.r, self.b = r, b
            self.lock_x = x
            self.e0 = e0
            self.sigma_r = sigma_r
            self.kappa = max(self.rate_safety * self._max_rate, 1e-12)
            self.lock_span = span
        return self.locked

    # -- conversion after lock ---------------------------------------

    def to_ref(self, y):
        """Convert a B-clock timestamp to the A (reference) clock."""
        assert self.locked, "not locked yet"
        return (y - self.b) / self.r

    def drift_bound(self, x):
        """Upper bound (seconds, A-clock) on |to_ref(y) - x_true|.

        Valid for reference time x >= lock_x, under the assumption that
        |dr/dt| stays below kappa after lock.
        """
        assert self.locked, "not locked yet"
        dt = max(0.0, x - self.lock_x)
        rate0 = self.lock_sigma_mult * self.sigma_r + self.kappa * self.lock_span
        bound_b = self.e0 + rate0 * dt + 0.5 * self.kappa * dt * dt
        return bound_b / self.r
