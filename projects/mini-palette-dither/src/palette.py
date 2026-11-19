"""Small palette helpers used by the demo script."""

def nearest_index(pixel, palette):
    best_i, best_d = 0, None
    for i, c in enumerate(palette):
        d = (pixel[0] - c[0]) ** 2 + (pixel[1] - c[1]) ** 2 + (pixel[2] - c[2]) ** 2
        if best_d is None or d < best_d:
            best_i, best_d = i, d
    return best_i


def map_nearest(pixels, palette):
    return [nearest_index(p, palette) for p in pixels]


def quantize_uniform(values, levels):
    if levels < 2:
        raise ValueError("levels must be >= 2")
    step = 255.0 / (levels - 1)
    return [min(255, max(0, int(round(v / step)) * int(round(step)))) for v in values]


def palette_usage(indices, n_colors):
    counts = [0] * n_colors
    for i in indices:
        counts[i] += 1
    return counts
