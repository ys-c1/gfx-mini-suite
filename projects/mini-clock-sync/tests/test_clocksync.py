"""Run the online estimator over the three long recordings and report:
lock time, ratio estimation error (ppm), final drift, and drift bound.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from clocksync import ClockSync
import simulate


def run_recording(name, events, b_of_t, ratio_true_at):
    cs = ClockSync(window=600, check_every=25, lock_checks=5)
    lock_idx = None
    for i, (x, y) in enumerate(events):
        if cs.update(x, y):
            lock_idx = i
            break
    assert cs.locked, f"{name}: never locked"

    x_lock, _ = events[lock_idx]
    # estimation error: frozen ratio vs true ratio at lock time
    err_ppm = abs(cs.r - ratio_true_at(x_lock)) / ratio_true_at(x_lock) * 1e6

    # final drift: convert the last event's B timestamp, compare to truth
    x_end, y_end = events[-1]
    drift = abs(cs.to_ref(y_end) - x_end)
    bound = cs.drift_bound(x_end)

    print(f"{name:14s} lock@{x_lock:7.1f}s  "
          f"ratio err {err_ppm:8.4f} ppm  "
          f"final drift {drift * 1e3:9.4f} ms  "
          f"bound {bound * 1e3:9.4f} ms  "
          f"{'OK' if drift <= bound else 'BOUND VIOLATED'}")

    assert drift <= bound, f"{name}: drift {drift} exceeds bound {bound}"
    return err_ppm, drift, bound


def main():
    # sanity: exact fit on noiseless constant-ratio data
    cs = ClockSync(window=100, check_every=10, lock_checks=3)
    for k in range(400):
        x = k * 0.5
        if cs.update(x, 1.5 * x + 7.0):
            break
    assert cs.locked
    assert abs(cs.r - 1.5) < 1e-12 and abs(cs.b - 7.0) < 1e-9
    assert abs(cs.to_ref(1.5 * 100.0 + 7.0) - 100.0) < 1e-9

    cases = [
        ("constant", *simulate.rec_constant(),
         lambda t: simulate.R0),
        ("linear-drift", *simulate.rec_linear_drift(),
         lambda t: simulate.R0 + 2e-9 * t),
    ]
    # random-walk recording: true ratio at lock taken from its generator
    ev3, b3 = simulate.rec_random_walk()
    # recover true ratio near lock by differentiating the ground truth
    r3_at = lambda t: (b3(t + 0.5) - b3(t - 0.5))
    cases.append(("random-walk", ev3, b3, r3_at))

    print(f"{'recording':14s} {'lock time':>14s}  {'est. error':>16s}  "
          f"{'final drift':>18s}  {'drift bound':>16s}")
    errs = {}
    for name, events, b_of_t, r_at in cases:
        errs[name] = run_recording(name, events, b_of_t, r_at)

    # accuracy expectations per recording
    assert errs["constant"][0] < 0.5, "constant: ratio error too large"
    assert errs["linear-drift"][0] < 2.0, "linear: ratio error too large"
    assert errs["random-walk"][0] < 5.0, "walk: ratio error too large"
    print("all checks passed")


if __name__ == "__main__":
    main()
