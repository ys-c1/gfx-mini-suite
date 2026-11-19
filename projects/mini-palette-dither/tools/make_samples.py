"""Write PNGs for the generated samples.

Run from ``projects/mini-palette-dither``::

    python3 tools/make_samples.py

Pure Python on purpose: the library itself has no third-party dependencies.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sample_gen  # noqa: E402
from pngio import write_png  # noqa: E402


def main(argv):
    out_dir = argv[1] if len(argv) > 1 else "samples"
    os.makedirs(out_dir, exist_ok=True)
    for sample in sample_gen.SAMPLES:
        width, height, rgb = sample_gen.render_pixels(sample)
        path = os.path.join(out_dir, sample.name + ".png")
        write_png(path, width, height, rgb)
        print("wrote %s (%dx%d)" % (path, width, height))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
