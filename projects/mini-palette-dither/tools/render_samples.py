"""Renders the poster images for the sample report.

Run from ``projects/mini-palette-dither`` (``make bench`` regenerates these
before the report)::

    python3 tools/render_samples.py

Produces, per sample:

  ``samples/<name>.png``          source, truecolour
  ``samples/<name>-plain.png``    undithered 4-colour mapping (palette PNG)
  ``samples/<name>-dither.png``   block-adaptive result (palette PNG)
  ``samples/<name>-strength.png`` block strengths as a grey map
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "src"))
sys.path.insert(0, HERE)

import dither  # noqa: E402
import sample_gen  # noqa: E402
from pngio import write_png  # noqa: E402

BLOCK_SIZE = 6
STRENGTH_MIN = 0.0
STRENGTH_MAX = 0.75
TEXTURE_SCALE = 64.0


def _strength_image(pixels, width, height, block_size):
    values = dither.block_strengths(pixels, width, height, block_size,
                                    STRENGTH_MIN, STRENGTH_MAX, TEXTURE_SCALE)
    blocks_w = (width + block_size - 1) // block_size
    rgb = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            v = int(round(values[(y // block_size) * blocks_w +
                                 x // block_size] * 255))
            i = (y * width + x) * 3
            rgb[i] = rgb[i + 1] = rgb[i + 2] = v
    return rgb


def main(argv):
    out_dir = argv[1] if len(argv) > 1 else os.path.join(ROOT, "samples")
    os.makedirs(out_dir, exist_ok=True)
    for sample in sample_gen.SAMPLES:
        width, height, rgb = sample_gen.render_pixels(sample)
        pixels = sample_gen.to_rgb_list(rgb)
        pal = dither.validate_palette(sample.palette)
        plain = dither.dither_indices(pixels, width, height, pal, BLOCK_SIZE,
                                      STRENGTH_MIN, STRENGTH_MAX,
                                      enabled=False, texture_scale=TEXTURE_SCALE)
        dithered = dither.dither_indices(pixels, width, height, pal, BLOCK_SIZE,
                                         STRENGTH_MIN, STRENGTH_MAX,
                                         texture_scale=TEXTURE_SCALE)
        write_png(os.path.join(out_dir, sample.name + ".png"),
                  width, height, rgb)
        write_png(os.path.join(out_dir, sample.name + "-plain.png"),
                  width, height, None, palette=pal, indices=plain)
        write_png(os.path.join(out_dir, sample.name + "-dither.png"),
                  width, height, None, palette=pal, indices=dithered)
        write_png(os.path.join(out_dir, sample.name + "-strength.png"),
                  width, height,
                  _strength_image(pixels, width, height, BLOCK_SIZE))
        print("rendered", sample.name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
