"""Small palette helpers used by the demo script.

``nearest_index`` / ``map_nearest`` keep their original brute-force
signature and semantics.  ``PaletteIndex`` is an optional kd-tree based
acceleration structure that returns the *same* index as ``nearest_index``
(squared Euclidean distance, strict comparison, smallest palette index wins
on ties); rebuild it after the palette is edited.
"""


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


class _Node:
    __slots__ = ("axis", "index", "plane", "left", "right")

    def __init__(self, axis, index, plane, left, right):
        self.axis = axis    # split axis: 0=R, 1=G, 2=B
        self.index = index  # palette index stored at this node
        self.plane = plane  # split coordinate (palette[index][axis])
        self.left = left
        self.right = right


def _dist2(a, b):
    return (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2


def _nn_search(root, palette, pixel):
    """Nearest-neighbour search on a kd-tree.

    Returns ``(index, squared_distance, nodes_visited)``.  Ties are resolved
    to the smallest palette index, matching ``nearest_index`` exactly.
    """
    best_i = -1
    best_d = None
    visited = 0

    def visit(node):
        nonlocal best_i, best_d, visited
        visited += 1

        i = node.index
        d = _dist2(pixel, palette[i])
        if best_d is None or d < best_d or (d == best_d and i < best_i):
            best_i, best_d = i, d

        diff = pixel[node.axis] - node.plane
        if diff <= 0:
            near, far = node.left, node.right
        else:
            near, far = node.right, node.left

        if near is not None:
            visit(near)

        # Prune the far half only when its plane is strictly farther than
        # the incumbent.  On diff**2 == best_d a far-side point can only
        # tie the current best, and a tied point with a smaller index must
        # still win, so the branch has to be visited.
        if far is not None and (best_d is None or diff * diff <= best_d):
            visit(far)

    visit(root)
    return best_i, best_d, visited


class PaletteIndex:
    """Balanced 3-D kd-tree over an RGB palette.

    Construction (``__init__`` / ``rebuild`` / ``add``):
        O(n log^2 n) time with k = 3 fixed, O(n) space.  The log^2 factor
        comes from sorting at every node; a presorted-median build would be
        O(n log n).  ``add`` appends one colour and does a full rebuild, so
        inserting m colours one by one costs O(m^2 log^2 m) in total.

    Query (``query``):
        O(log n) nodes visited on average, O(n) worst case, O(log n) stack.

    Results are identical to ``nearest_index``: the same integer squared
    distance is used, and equidistant colours resolve to the smallest
    palette index.  An empty palette returns 0, again matching
    ``nearest_index``; a one-colour palette always returns 0.
    """

    def __init__(self, palette=()):
        # Snapshot the input: later edits of the caller's list must not
        # desynchronise the tree.
        self._palette = tuple(tuple(c) for c in palette)
        self._root = self._build(list(range(len(self._palette))), 0)

    def _build(self, ids, depth):
        if not ids:
            return None
        axis = depth % 3
        # Index as tiebreaker keeps duplicate colours in a deterministic order.
        ids.sort(key=lambda i: (self._palette[i][axis], i))
        m = len(ids) // 2
        median = ids[m]
        return _Node(
            axis,
            median,
            self._palette[median][axis],
            self._build(ids[:m], depth + 1),
            self._build(ids[m + 1:], depth + 1),
        )

    def rebuild(self, palette):
        """Replace the palette and rebuild the tree from scratch."""
        self._palette = tuple(tuple(c) for c in palette)
        self._root = self._build(list(range(len(self._palette))), 0)
        return self

    def add(self, color):
        """Append one colour and rebuild; returns the new palette index."""
        self._palette = self._palette + (tuple(color),)
        self._root = self._build(list(range(len(self._palette))), 0)
        return len(self._palette) - 1

    def query(self, pixel):
        """Return the palette index of the nearest colour to ``pixel``."""
        if self._root is None:
            return 0  # parity with nearest_index on an empty palette
        best_i, _best_d, _visited = _nn_search(self._root, self._palette, pixel)
        return best_i

    @property
    def palette(self):
        return self._palette

    def __len__(self):
        return len(self._palette)
