"""Tiny test runner: executes every test_* function in the given modules."""

import importlib
import sys


def main(names):
    failed = total = 0
    for name in names:
        mod = importlib.import_module(name)
        for attr in sorted(dir(mod)):
            if not attr.startswith("test_") or not callable(getattr(mod, attr)):
                continue
            total += 1
            try:
                getattr(mod, attr)()
                print("PASS", "%s.%s" % (name, attr))
            except AssertionError as exc:
                failed += 1
                print("FAIL", "%s.%s" % (name, attr), exc)
    print("%d checks, %d failures" % (total, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
