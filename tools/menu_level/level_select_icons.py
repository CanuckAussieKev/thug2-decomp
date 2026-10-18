# NOT ORIGINAL - the level select's signs for the levels we add (tools/menu_level/level_select.py): THUG2's sign
# frame (the road-sign diamond of its sign_training) with a pictogram of our own drawn in it, as the game's
# images\LevelSelectSprites\<name>.img.xbx.
#
# The game's level select sprites (Levelselect.prx) are 8-bit paletted images:
#   u32 version (2), u32 bits per texel (8), u32 width, u32 height, u32 format (0x13 = paletted), u32 0,
#   u16 width, u16 height, u32 palette size in bytes (64: 16 BGRA colours), the palette, then width * height
#   indices stored swizzled (tools/thps4_levels/tex.py unswizzle_order) - read by 0x004E0790.
# Nothing of the game is stored here: the frame is read from the user's Levelselect.prx each time.

import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "thps4_levels"))
import tex  # noqa: E402 (unswizzle_order)

SIZE = 64
FRAME_SOURCE = r"images\LevelSelectSprites\sign_training.img.xbx"
# The diamond's inside: |x - 32| + |y - 32| <= INSIDE (pixel centres) is cleared to the fill and drawn on; the
# frame (white edge, grey bevel) is outside it.
INSIDE = 26.0
FILL = 0xD7


def read_img(data):
    version, depth, w, h, fmt, _, w2, h2, palette_size = struct.unpack_from("<IIIIIIHHI", data, 0)
    if version != 2 or depth != 8 or fmt != 0x13 or (w2, h2) != (w, h):
        raise ValueError("not an 8-bit paletted img.xbx (version %d, %d bits, format 0x%X)" % (version, depth, fmt))
    palette = [tuple(data[32 + i:36 + i]) for i in range(0, palette_size, 4)]  # (b, g, r, a)
    stored = data[32 + palette_size:32 + palette_size + w * h]
    order = tex.unswizzle_order(w, h)
    return w, h, palette, [stored[order[i]] for i in range(w * h)]


def write_img(w, h, palette, indices):
    stored = bytearray(w * h)
    for i, at in enumerate(tex.unswizzle_order(w, h)):
        stored[at] = indices[i]
    head = struct.pack("<IIIIIIHHI", 2, 8, w, h, 0x13, 0, w, h, len(palette) * 4)
    return head + b"".join(bytes(c) for c in palette) + bytes(stored)


# --- shapes: each a function (x, y) -> inside, on the 64 x 64 sign (y down) ----------------------------------------

def poly(*points):
    def inside(x, y):
        hit = False
        n = len(points)
        for i in range(n):
            (x1, y1), (x2, y2) = points[i], points[(i + 1) % n]
            if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
                hit = not hit
        return hit
    return inside


def rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1


def circle(cx, cy, r):
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def ellipse(cx, cy, rx, ry, angle=0.0):
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))

    def inside(x, y):
        dx, dy = x - cx, y - cy
        u, v = dx * c + dy * s, -dx * s + dy * c
        return (u / rx) ** 2 + (v / ry) ** 2 <= 1
    return inside


def ring(cx, cy, r, width):
    return lambda x, y: r - width / 2 <= math.hypot(x - cx, y - cy) <= r + width / 2


def line(x1, y1, x2, y2, width):
    # A segment with round ends.
    dx, dy = x2 - x1, y2 - y1
    length2 = dx * dx + dy * dy or 1e-9

    def inside(x, y):
        t = max(0.0, min(1.0, ((x - x1) * dx + (y - y1) * dy) / length2))
        return (x - x1 - t * dx) ** 2 + (y - y1 - t * dy) ** 2 <= width * width / 4
    return inside


def polyline(points, width):
    segments = [line(*points[i], *points[i + 1], width) for i in range(len(points) - 1)]
    return lambda x, y: any(s(x, y) for s in segments)


def arc(cx, cy, r, width, a0, a1):
    # Degrees, 0 = +x, 90 = down (+y).
    points = [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))
              for a in [a0 + (a1 - a0) * i / 24 for i in range(25)]]
    return polyline(points, width)


def leaf(x0, y0, x1, y1, bend, width):
    # A frond: from (x0, y0) to its tip (x1, y1), bowed sideways by `bend`, widest in the middle.
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    nx, ny = -dy / length, dx / length
    upper, lower = [], []
    for i in range(13):
        t = i / 12
        bow = bend * 4 * t * (1 - t)
        cx, cy = x0 + dx * t + nx * bow, y0 + dy * t + ny * bow
        half = width / 2 * math.sin(math.pi * min(1.0, t * 1.15)) if t < 0.87 else width / 2 * (1 - t) / 0.13 * 0.4
        upper.append((cx + nx * half, cy + ny * half))
        lower.append((cx - nx * half, cy - ny * half))
    return poly(*(upper + lower[::-1]))


INK, CUT = 1, 0


def skateboard():
    return [(INK, polyline([(15.5, 26.5), (19, 30), (45, 30), (48.5, 26.5)], 3.6)),
            (INK, rect(22.5, 31, 27.5, 33.5)), (INK, rect(36.5, 31, 41.5, 33.5)),
            (INK, circle(23, 37, 3.4)), (INK, circle(41, 37, 3.4)),
            (CUT, circle(23, 37, 1.1)), (CUT, circle(41, 37, 1.1))]


def cone():
    return [(INK, poly((29.5, 14), (34.5, 14), (41.5, 41), (22.5, 41))), (CUT, rect(0, 21.5, 64, 25)),
            (CUT, rect(0, 30, 64, 33.5)), (INK, rect(19, 41, 45, 45))]


def bars():
    # A barred cell window (Alcatraz).
    shapes = [(INK, rect(21, 17, 43, 47)), (CUT, rect(24.5, 20.5, 39.5, 43.5))]
    shapes += [(INK, rect(x - 1.1, 20, x + 1.1, 44)) for x in (28.5, 32, 35.5)]
    shapes += [(INK, rect(24, 30.8, 40, 33))]
    return shapes


def bridge():
    # The Golden Gate: two towers, the main cable, the deck and the hangers.
    def cable(x):
        if x < 24:
            return 30 - (x - 13) / 11 * 11
        if x > 40:
            return 19 + (x - 40) / 11 * 11
        return 19 + 11 * (1 - ((x - 32) / 8) ** 2)
    shapes = [(INK, polyline([(x, cable(x)) for x in [13 + i * 1.5 for i in range(26)]], 1.8))]
    shapes += [(INK, rect(x, cable(x), x + 0.9, 36)) for x in (27, 29.5, 34, 36.5)]
    for tx in (24, 40):
        shapes += [(INK, rect(tx - 2, 15, tx + 2, 44)), (CUT, rect(tx - 0.8, 21, tx + 0.8, 23.5)),
                   (CUT, rect(tx - 0.8, 27, tx + 0.8, 29.5))]
    shapes += [(INK, rect(12, 35, 52, 38.5))]
    return shapes


def mortarboard():
    board = [(32, 16), (49, 24.5), (32, 33), (15, 24.5)]
    grown = [(32, 14.5), (51.5, 24.5), (32, 35), (12.5, 24.5)]
    return [(INK, poly((22, 28), (42, 28), (41.5, 39), (32, 42.5), (22.5, 39))), (CUT, poly(*grown)),
            (INK, poly(*board)), (INK, polyline([(32, 24.5), (45, 28), (45, 38)], 1.4)),
            (INK, poly((43.5, 37.5), (46.5, 37.5), (47, 43), (43, 43)))]


def palm():
    trunk = [(35, 47), (34, 41), (32.5, 35), (31.5, 29), (31.5, 24)]
    shapes = [(INK, line(*trunk[i], *trunk[i + 1], 4.0 - i * 0.4)) for i in range(len(trunk) - 1)]
    top = (31.5, 23)
    for tip, bend in (((17, 29), 4), ((20, 16.5), 3), ((33, 13), -2), ((44, 17), -3), ((47, 28), -4)):
        shapes.append((INK, leaf(top[0], top[1], tip[0], tip[1], bend, 5.2)))
    shapes += [(INK, circle(30, 25.5, 1.8)), (INK, circle(33.5, 26, 1.8)), (INK, ellipse(33, 47.5, 9, 2.2))]
    return shapes


def anchor():
    shapes = [(INK, ring(32, 16.5, 3.2, 2.0)), (INK, rect(30.4, 19.5, 33.6, 45)),
              (INK, line(24.5, 24, 39.5, 24, 2.8)), (INK, arc(32, 33.5, 11.5, 3.0, 15, 165))]
    for side in (1, -1):
        ex, ey = 32 + side * 11.1, 36.5
        shapes.append((INK, poly((ex - side * 2.6, ey + 1.5), (ex + side * 3.5, ey - 5.5), (ex + side * 2.2, ey + 2.4))))
    return shapes


def bigben():
    return [(INK, poly((27, 20), (37, 20), (32, 9))), (INK, rect(26, 19.5, 38, 31.5)),
            (CUT, circle(32, 25.5, 4.0)), (INK, line(32, 25.5, 32, 22.8, 1.0)), (INK, line(32, 25.5, 34.2, 26.5, 1.0)),
            (INK, rect(27.5, 31, 36.5, 46)), (CUT, rect(29.8, 33.5, 30.9, 43)), (CUT, rect(33.1, 33.5, 34.2, 43)),
            (INK, rect(25, 45, 39, 48))]


def paw():
    return [(INK, poly((25, 40), (28.5, 33), (35.5, 33), (39, 40), (36, 44.5), (28, 44.5))),
            (INK, circle(32, 38.5, 5.6)),
            (INK, ellipse(21.5, 30, 3.1, 4.1, -25)), (INK, ellipse(27.5, 22.5, 3.2, 4.3, -8)),
            (INK, ellipse(36.5, 22.5, 3.2, 4.3, 8)), (INK, ellipse(42.5, 30, 3.1, 4.1, 25))]


def ferris():
    cx, cy, r = 32, 27, 12
    shapes = [(INK, line(cx, cy, 24, 46.5, 2.6)), (INK, line(cx, cy, 40, 46.5, 2.6)),
              (INK, line(22.5, 46.5, 41.5, 46.5, 2.2)), (INK, ring(cx, cy, r, 2.0))]
    for i in range(8):
        a = math.radians(i * 45 + 22.5)
        shapes.append((INK, line(cx, cy, cx + r * math.cos(a), cy + r * math.sin(a), 1.1)))
        shapes.append((INK, circle(cx + (r + 0.5) * math.cos(a), cy + (r + 0.5) * math.sin(a) + 1.6, 2.3)))
    shapes.append((INK, circle(cx, cy, 2.4)))
    return shapes


def skyline():
    # Chicago: a stepped tower with two masts among smaller blocks.
    shapes = [(INK, rect(29, 13, 35, 43)), (INK, rect(26.5, 21, 37.5, 43)), (INK, rect(24, 29, 40, 43)),
              (INK, rect(30, 7.5, 31, 13.5)), (INK, rect(33, 7.5, 34, 13.5)),
              (INK, rect(17.5, 33, 22.5, 43)), (INK, rect(41.5, 30, 46.5, 43)), (INK, rect(18, 43, 46, 45.5))]
    shapes += [(CUT, rect(x, 35, x + 1.1, 41)) for x in (19.4, 43.4)]
    return shapes


def motorbike():
    return [(INK, ring(20.5, 38, 5.6, 2.4)), (INK, ring(43.5, 38, 5.6, 2.4)),
            (INK, circle(20.5, 38, 1.4)), (INK, circle(43.5, 38, 1.4)),
            (INK, poly((23, 33), (28, 28.5), (38, 28.5), (41, 32), (35, 36), (26, 36))),
            (INK, poly((25, 29), (31, 26.5), (33, 28.5), (26, 31))), (INK, line(43.5, 38, 38.5, 25.5, 2.2)),
            (INK, line(36, 24.5, 41, 23.5, 1.6)), (INK, line(20.5, 38, 27, 33, 2.0))]


def torch():
    # The Statue of Liberty's torch.
    flame = poly((32, 9.5), (37, 16), (36.5, 21), (32, 23.5), (27.5, 21), (27, 16), (29.5, 13.5), (30.5, 16.5))
    return [(INK, flame), (CUT, poly((32, 15.5), (34, 19), (32, 21.5), (30, 19))),
            (INK, poly((25, 24), (39, 24), (37, 28), (27, 28))), (INK, rect(28.5, 28, 35.5, 31)),
            (INK, poly((29.5, 31), (34.5, 31), (33.5, 46), (30.5, 46))), (INK, rect(27.5, 45, 36.5, 48))]


def wave():
    shapes = [(INK, poly((18, 40), (21, 33), (26, 26.5), (31.5, 22.5), (37.5, 21.5), (42.5, 23.5), (45.5, 28), (44, 30.5),
                          (41, 28), (37.5, 28), (35.5, 30.5), (36, 34), (39, 36), (43, 37), (46.5, 40)))]
    shapes += [(INK, rect(18.5, 42, 45.5, 44.5))]
    return shapes


def sun():
    shapes = [(INK, circle(32, 31, 7.5))]
    for i in range(12):
        a = math.radians(i * 30)
        shapes.append((INK, line(32 + 10.5 * math.cos(a), 31 + 10.5 * math.sin(a),
                                 32 + 15.5 * math.cos(a), 31 + 15.5 * math.sin(a), 2.2)))
    return shapes


def house():
    return [(INK, poly((32, 14), (47, 28), (43.5, 28), (43.5, 45), (20.5, 45), (20.5, 28), (17, 28))),
            (CUT, rect(28.5, 34, 35.5, 45.5)), (CUT, rect(23.5, 30.5, 27.5, 34.5)), (CUT, rect(36.5, 30.5, 40.5, 34.5)),
            (INK, rect(38.5, 16, 41.5, 23))]


def ramp():
    # A half-pipe.
    shapes = [(INK, poly((18, 26), (21.5, 26), (21.5, 28), (23.5, 33.5), (27, 37), (32, 38), (37, 37), (40.5, 33.5),
                          (42.5, 28), (42.5, 26), (46, 26), (46, 43), (18, 43)))]
    shapes += [(INK, rect(17.5, 24.5, 22.5, 26.5)), (INK, rect(41.5, 24.5, 46.5, 26.5))]
    return shapes


def dome():
    # An onion dome (Moscow).
    return [(INK, poly((32, 10), (33.5, 15), (39, 19.5), (41, 25), (38.5, 29.5), (25.5, 29.5), (23, 25), (25, 19.5),
                       (30.5, 15))),
            (INK, rect(25, 29.5, 39, 31.5)), (INK, rect(26, 31.5, 38, 47)), (CUT, poly((30, 47.5), (30, 38), (32, 35.5),
                                                                                      (34, 38), (34, 47.5))),
            (CUT, rect(28, 34, 29.2, 39)), (CUT, rect(34.8, 34, 36, 39))]


def flame():
    return [(INK, poly((32, 10), (37, 18), (41.5, 23), (43, 31), (41, 39), (35.5, 45), (28.5, 45), (23, 39),
                       (21, 31), (23.5, 24), (27, 29), (28.5, 21))),
            (CUT, poly((32, 27), (36, 33), (36.5, 38), (34, 42), (30, 42), (27.5, 38), (28.5, 32), (30.5, 35)))]


def gear():
    # A cog (THPS3's Foundry).
    shapes = []
    for i in range(8):
        a = math.radians(i * 45)
        shapes.append((INK, line(32 + 8 * math.cos(a), 31 + 8 * math.sin(a),
                                 32 + 14 * math.cos(a), 31 + 14 * math.sin(a), 4.2)))
    shapes += [(INK, circle(32, 31, 11)), (CUT, circle(32, 31, 4))]
    return shapes


def pine():
    # A fir tree (Canada).
    return [(INK, poly((32, 11), (39, 20), (36, 20), (42, 28), (38.5, 28), (45, 37), (19, 37), (25.5, 28), (22, 28),
                       (28, 20), (25, 20))),
            (INK, rect(29.5, 37, 34.5, 45))]


def redeemer():
    # Christ the Redeemer (Rio): a figure with its arms out, on its pedestal.
    return [(INK, circle(32, 15, 2.6)), (INK, rect(17, 18.5, 47, 21.5)), (INK, poly((28.5, 18.5), (35.5, 18.5),
                                                                                    (35, 40), (29, 40))),
            (INK, rect(27.5, 40, 36.5, 46))]


def torii():
    # A shrine gate (Tokyo).
    return [(INK, poly((15.5, 16), (48.5, 16), (46, 21), (18, 21))), (INK, rect(19.5, 25, 44.5, 27.5)),
            (INK, rect(22, 20, 25.5, 47)), (INK, rect(38.5, 20, 42, 47)), (INK, rect(30.5, 20, 33.5, 25.5))]


def ship():
    # A cruise liner.
    shapes = [(INK, poly((15, 34), (49, 34), (44, 43), (20, 43))), (INK, rect(20, 28, 44, 34)),
              (INK, rect(24, 23, 40, 28)), (INK, poly((33, 16), (38, 16), (38.5, 23), (32.5, 23)))]
    shapes += [(CUT, circle(x, 31, 1.1)) for x in (23.5, 28, 32.5, 37, 41.5)]
    return shapes


def crate():
    # A shipping crate (the Warehouse).
    return [(INK, rect(19, 19, 45, 45)), (CUT, rect(22, 22, 42, 42)), (INK, line(22, 22, 42, 42, 3.4)),
            (INK, line(42, 22, 22, 42, 3.4)), (INK, rect(19, 30.5, 45, 33.5))]


def ufo():
    # Roswell.
    return [(INK, ellipse(32, 30, 16, 5)), (INK, ellipse(32, 26, 7.5, 6)), (CUT, ellipse(32, 30, 13.5, 1.2)),
            (INK, line(25, 34, 21, 44, 2.0)), (INK, line(39, 34, 43, 44, 2.0)), (INK, line(32, 35, 32, 45, 2.0))]


PICTOGRAMS = {f.__name__: f for f in (skateboard, cone, bars, bridge, mortarboard, palm, anchor, bigben, paw, ferris,
                                       skyline, motorbike, torch, wave, sun, house, ramp, dome, flame, gear, pine,
                                       redeemer, torii, ship, crate, ufo)}

SAMPLES = 4


def coverage(shapes):
    # Each pixel's ink, 0..1, from SAMPLES x SAMPLES points (the last shape covering a point decides it).
    out = [0.0] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if abs(x + 0.5 - 32) + abs(y + 0.5 - 32) > INSIDE + 1:
                continue
            hits = 0
            for j in range(SAMPLES):
                for i in range(SAMPLES):
                    px, py = x + (i + 0.5) / SAMPLES, y + (j + 0.5) / SAMPLES
                    ink = 0
                    for mode, shape in shapes:
                        if shape(px, py):
                            ink = mode
                    hits += ink
            out[y * SIZE + x] = hits / (SAMPLES * SAMPLES)
    return out


def make_sign(frame_data, pictogram):
    # The frame's image with its inside cleared and the pictogram drawn in black over the fill (the nearest of the
    # frame palette's opaque greys: the game tints the sign with the menu colour).
    w, h, palette, indices = read_img(frame_data)
    if (w, h) != (SIZE, SIZE):
        raise ValueError("the sign frame is %dx%d" % (w, h))
    greys = [(i, c[0]) for i, c in enumerate(palette) if c[3] == 0xFF and c[0] == c[1] == c[2]]
    ink = coverage(PICTOGRAMS[pictogram]())
    out = list(indices)
    for y in range(SIZE):
        for x in range(SIZE):
            if abs(x + 0.5 - 32) + abs(y + 0.5 - 32) > INSIDE:
                continue
            value = FILL * (1 - ink[y * SIZE + x])
            out[y * SIZE + x] = min(greys, key=lambda g: abs(g[1] - value))[0]
    return write_img(w, h, palette, out)


# --- a preview -----------------------------------------------------------------------------------------------------
#   python -I tools/menu_level/level_select_icons.py <out.png> [--game C:\Games\THUG2] [pictogram...]
# draws the signs (3x, on grey) into a PNG to look at.

def write_png(path, w, h, rgba):
    import zlib
    raw = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def main():
    import argparse
    sys.path.insert(0, HERE)
    from make_menu_level import prx_file
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("names", nargs="*")
    parser.add_argument("--game", default=r"C:\Games\THUG2")
    args = parser.parse_args()
    frame = prx_file(os.path.join(args.game, "Data", "pre", "Levelselect.prx"), FRAME_SOURCE)
    names = args.names or list(PICTOGRAMS)
    scale, cols, grey = 3, 7, 90
    cell = SIZE * scale + 6
    rows = (len(names) + cols - 1) // cols
    width, height = cols * cell, rows * cell
    out = bytearray([grey, grey, grey, 255] * (width * height))
    for n, name in enumerate(names):
        _, _, palette, indices = read_img(make_sign(frame, name))
        ox, oy = (n % cols) * cell, (n // cols) * cell
        for y in range(SIZE * scale):
            for x in range(SIZE * scale):
                b, g, r, a = palette[indices[(y // scale) * SIZE + x // scale]]
                o = ((oy + y) * width + ox + x) * 4
                out[o:o + 4] = bytes([int(c * a / 255 + grey * (1 - a / 255)) for c in (r, g, b)] + [255])
    write_png(args.out, width, height, out)
    print("wrote %s: %s" % (args.out, ", ".join(names)))


if __name__ == "__main__":
    main()
