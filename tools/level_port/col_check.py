# (Has a TypeScript twin, col_check.ts, which the TypeScript tools use: until this file is removed - once the Python
# tools importing it are ported, docs/typescript.md - change both, and run tools/parity's cases.)
# NOT ORIGINAL - offline checks of a THUG2 collision file (levels\<x>\<x>.col.xbx, tools/level_port/thug2_col.py's
# form): whether every face can be found through its object's BSP tree, as THUG2's queries walk it.
#
#   python -I tools/level_port/col_check.py <file.col.xbx | file.prx> ...
#
# A split {axis, point in sixteenths} sends a query to the left child when its low end is below the split, to the
# right one when its high end is at or above it (the trees are built so: a face is in the left child when its lowest
# vertex is below the split, in the right one when its highest is at or above). The query here is a short vertical
# segment through each face's centroid (+-1 unit on y, the axis a skater's feelers mostly run along). A face that
# no such query reaches can't be hit: a floor the skater falls through, a wall he passes (the recentred levels'
# trees once kept their old split points - tools/thaw_levels/thaw_levels.ts moveTree).

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import prx  # noqa: E402
import thug2_col  # noqa: E402


def _leaves(node, lo, hi, out):
    if node[0] == "leaf":
        out.update(node[1])
        return
    _, axis, point, left, right = node
    split = point / 16.0
    if lo[axis] < split:
        _leaves(left, lo, hi, out)
    if hi[axis] >= split:
        _leaves(right, lo, hi, out)


def unreachable(o):
    # The indices of the faces of collision object `o` that a segment through their centroid doesn't reach.
    missing = []
    verts = o["verts"]
    for i, (_, _, a, b, c) in enumerate(o["faces"]):
        centre = [(verts[a][k] + verts[b][k] + verts[c][k]) / 3.0 for k in range(3)]
        lo = (centre[0], centre[1] - 1.0, centre[2])
        hi = (centre[0], centre[1] + 1.0, centre[2])
        found = set()
        _leaves(o["bsp"], lo, hi, found)
        if i not in found:
            missing.append(i)
    return missing


def check(col):
    # (faces, unreachable faces, objects with some) of a read collision file.
    faces = missing = objects = 0
    for o in col["objects"]:
        faces += len(o["faces"])
        m = len(unreachable(o))
        missing += m
        objects += 1 if m else 0
    return faces, missing, objects


def col_files(path):
    # (name, data) of the collision files in a .col.xbx or a .prx.
    if path.lower().endswith(".prx"):
        return [(n, d) for n, d in prx.entries(path, lambda n: n.lower().endswith(".col.xbx"))]
    return [(os.path.basename(path), open(path, "rb").read())]


def main():
    bad = 0
    for path in sys.argv[1:]:
        for name, data in col_files(path):
            faces, missing, objects = check(thug2_col.read_col(data, name))
            print("%s: %d faces, %d unreachable (in %d objects)" % (name, faces, missing, objects))
            bad += missing
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
