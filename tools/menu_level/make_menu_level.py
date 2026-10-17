# NOT ORIGINAL - the menu_level feature (thug2-decomp.ini [features] menu_backdrop=3; see
# src/game/front/menu_level.h): generates the files of a front end built in Los Angeles from the game's own
# files, into a data overlay folder the DLL reads before the game's archives and data folder.
#
#   python -I tools/menu_level/make_menu_level.py [--game C:\Games\THUG2] [--out <folder>] [--view 1] [--lift 0]
#
# The overlay (default: <game>\thug2-decomp-data) gets:
#   scripts\game\Levels.qb     the game's, with level_Mainmenu pointing at LA's scene, sky and collision, the
#                              skate shop's scene loaded too (extra_scene: the wheel's meshes are in it), and the
#                              move (menu_level_yaw / menu_level_offset) the DLL reads for the cameras; load_level
#                              loads a level's extra_scene after its own scene.
#   levels\mainmenu\mainmenu.qb  the shop's node array with every node moved into LA by that move, after a
#                              position-only stand-in for each of LA's nodes (LA's collision triggers nodes by index).
# Nothing of the game is stored in the repo: the files are made from the game folder each time.
#
# The move is one turn about the up axis plus a shift: it takes the main menu camera's place in the shop
# (mainmenu_camera02, placed from the skater: see SHOP_CAMERA) to
# the backdrop view's place (src/game/front/menu_backdrop.cpp kViews), the camera's heading to the view's.
# Needs NeverScript (tools/build_neverscript.ps1 -> out/neverscript/ns.exe).

import argparse
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The main menu camera in the skate shop (menu_backdrop debug log, the camera steady: "placed for camera FCC018D3 at
# (59.833, -12.740, 181.704), field of view 45"): the skater at TRG_Playerrestart (84.833, -63.740, 61.704) +
# targetOffset mainmneu_cam_offset (-25, 51, -40) + positionOffset (0, 0, 160) - it looks at the skater + the
# target offset, straight along the shop's -z.
SHOP_CAMERA = (59.833, -12.740, 181.704)
SHOP_TARGET = (59.833, -12.740, 21.704)

# Node fields that are points (moved and turned); Angles are turned; everything else is kept.
POINT_FIELDS = ("Pos", "MidPosition", "EndPosition")


def checksum(name):
    return (zlib.crc32(name.lower().encode()) ^ 0xFFFFFFFF) & 0xFFFFFFFF


def lzss_decode(data, size):
    # src/game/util/lzss.cpp Decode: a 4096-byte window filled with spaces, writing from 4096 - 18.
    window = bytearray(b" " * 4096)
    r = 4096 - 18
    out = bytearray()
    i = 0
    flags = 0
    while len(out) < size and i < len(data):
        flags >>= 1
        if not flags & 0x100:
            flags = data[i] | 0xFF00
            i += 1
        if flags & 1:
            if i >= len(data):
                break
            c = data[i]
            i += 1
            out.append(c)
            window[r] = c
            r = (r + 1) & 4095
        else:
            if i + 1 >= len(data):
                break
            position = data[i] | ((data[i + 1] & 0xF0) << 4)
            last = (data[i + 1] & 0x0F) + 2
            i += 2
            for k in range(last + 1):
                c = window[(position + k) & 4095]
                out.append(c)
                window[r] = c
                r = (r + 1) & 4095
    return bytes(out[:size])


def prx_file(path, name):
    # An archive as stored: {total size, version, file count}, then per file {size, compressed size, name size
    # (int16), use count, checksum}, its name, and its data padded to 4 bytes (src/game/file/pip.cpp NextRawEntry).
    data = open(path, "rb").read()
    _, _, count = struct.unpack_from("<iii", data, 0)
    offset = 12
    wanted = checksum(name)
    for _ in range(count):
        size, compressed, name_size, _, file_checksum = struct.unpack_from("<iihHI", data, offset)
        stored = compressed or size
        body = offset + 16 + name_size
        entry_name = data[offset + 16:body].split(b"\0")[0].decode("ascii", "replace")
        if file_checksum == wanted or entry_name.lower().replace("/", "\\") == name.lower():
            raw = data[body:body + stored]
            return lzss_decode(raw, size) if compressed else raw
        offset = body + ((stored + 3) & ~3)
    sys.exit("%s: %s not found" % (path, name))


def views():
    # The backdrop views, from the C++ (one source): {{pos}, {back}, {up}} per view.
    text = open(os.path.join(REPO, "src", "game", "front", "menu_backdrop.cpp"), encoding="utf-8").read()
    body = re.search(r"kViews\[\] = \{(.*?)\n\};", text, re.S).group(1)
    found = []
    for pos, back in re.findall(r"\{\{([^}]*)\}, \{([^}]*)\}, \{[^}]*\}\}", body):
        found.append(([float(v.rstrip("f")) for v in pos.split(",")], [float(v.rstrip("f")) for v in back.split(",")]))
    return found


def heading(d):
    # The game's turn about Y adds to atan2(x, z) (rows of CreateRotateYMatrix: (c, 0, -s), (0, 1, 0), (s, 0, c)).
    return math.atan2(d[0], d[2])


def make_move(view, lift):
    view_pos, view_back = view
    forward = [-v for v in view_back]
    shop_forward = [t - c for t, c in zip(SHOP_TARGET, SHOP_CAMERA)]
    yaw = heading(forward) - heading(shop_forward)
    turned = turn(SHOP_CAMERA, yaw)
    offset = [view_pos[i] - turned[i] for i in range(3)]
    offset[1] += lift
    return yaw, offset


def turn(p, yaw):
    # p * RotateY(yaw), row vector times the game's matrix.
    c, s = math.cos(yaw), math.sin(yaw)
    return (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)


def matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return [[1, 0, 0], [0, c, s], [0, -s, c]]


def rot_y(a):
    c, s = math.cos(a), math.sin(a)
    return [[c, 0, -s], [0, 1, 0], [s, 0, c]]


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return [[c, s, 0], [-s, c, 0], [0, 0, 1]]


def turn_angles(angles, yaw):
    # Node angles make Rz * Ry * Rx (Mth::Matrix::RotateLocal: X, then Y, then Z, each applied on the left); the
    # turned orientation is that times RotateY(yaw), split back into angles.
    x, y, z = angles
    if x == 0.0 and z == 0.0:
        return (x, y + yaw, z)
    m = matmul(matmul(matmul(rot_z(z), rot_y(y)), rot_x(x)), rot_y(yaw))
    ny = math.asin(max(-1.0, min(1.0, m[2][0])))
    nx = math.atan2(-m[2][1], m[2][2])
    nz = math.atan2(-m[1][0], m[0][0])
    return (nx, ny, nz)


def fmt(v):
    return "(%s)" % ", ".join("%.6f" % c for c in v)


def move_nodes(text, yaw, offset):
    vector = r"\(\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\)"
    moved = 0

    def point(m):
        nonlocal moved
        moved += 1
        p = turn([float(m.group(i)) for i in (2, 3, 4)], yaw)
        return "%s=%s" % (m.group(1), fmt([p[i] + offset[i] for i in range(3)]))

    def angles(m):
        return "Angles=%s" % fmt(turn_angles([float(m.group(i)) for i in (1, 2, 3)], yaw))

    text = re.sub(r"\b(%s)=%s" % ("|".join(POINT_FIELDS), vector), point, text)
    text = re.sub(r"\bAngles=%s" % vector, angles, text)
    return text, moved


def edit_levels(text, yaw, offset):
    # level_Mainmenu: LA's scene, sky and collision; the shop's scene as an extra one; the move.
    start = text.index("level_Mainmenu = {")
    end = text.index("\n}", start)
    block = text[start:end]
    replacements = [
        ('scnpre="mainmenuscn.pre"', 'scnpre="LAscn.pre"'),
        ('level="mainmenu"', 'level="LA"'),
        ('colpre="mainmenucol.pre"', 'colpre="LAcol.pre"'),
    ]
    for old, new in replacements:
        if block.count(old) != 1:
            sys.exit("Levels.qb: level_Mainmenu has %d of %s" % (block.count(old), old))
        block = block.replace(old, new)
    added = ('    sky="LA_Sky" \n    extra_scene="mainmenu" \n    menu_level_yaw=%.6f \n    menu_level_offset=%s \n'
             % (yaw, fmt(offset)))
    block = block.replace('    level="LA" \n', '    level="LA" \n' + added)
    if "extra_scene" not in block:
        sys.exit("Levels.qb: level_Mainmenu's level= line not as expected")
    text = text[:start] + block + text[end:]

    # load_level: a level's extra_scene loaded after its own scene (not in net games or the park editor).
    line = "                LoadScene scene=<level> \n"
    if text.count(line) != 1:
        sys.exit("Levels.qb: load_level's LoadScene line found %d times" % text.count(line))
    text = text.replace(line, line + "                if gotparam extra_scene {\n"
                        "                    LoadScene scene=<extra_scene> \n                } \n")
    return text


def array_elements(text, name):
    # The top-level { ... } elements of the array `name = [ ... ]` and where the array's [ ... ] is in `text`.
    start = text.index(name + " =")
    open_at = text.index("[", start)
    elements, depth, element_start, i = [], 0, None, open_at + 1
    while True:
        c = text[i]
        if c == '"':
            i = text.index('"', i + 1)
        elif c in "{[(":
            if depth == 0 and c == "{":
                element_start = i
            depth += 1
        elif c in "}])":
            if depth == 0:
                return elements, open_at, i
            depth -= 1
            if depth == 0 and c == "}":
                elements.append(text[element_start:i + 1])
        i += 1


def la_placeholders(text, shop_text):
    # LA's collision triggers nodes by name (0x00434A60 finds the node by its Name - 0x0045CFC0 - and reads its
    # TriggerScript without checking that it was found): the front end's node array starts with one stand-in per LA
    # node - its position, angles and name only (no class or script: nothing is made or run) - so every name LA's
    # collision uses is there. A name the shop's nodes also have (Box01, ParticleBox_10) is left off the stand-in:
    # the menu's scripts find the shop's node. (The shop's nodes follow; they link to no node by index.)
    shop_names = {n.lower() for n in re.findall(r"\bName=(\w+)", shop_text)}
    elements, _, _ = array_elements(text, "LA_NodeArray")
    placeholders = []
    for element in elements:
        fields = [m.group(0) for m in re.finditer(r"\b(?:Pos|Angles)=\([^)]*\)", element)]
        name = re.search(r"\bName=(\w+)", element)
        if name and name.group(1).lower() not in shop_names:
            fields.append(name.group(0))
        placeholders.append("    { %s } " % " ".join(fields) if fields else "    { } ")
    return placeholders


def ns(tool, *args):
    result = subprocess.run([tool, *args], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("ns %s failed:\n%s%s" % (" ".join(args), result.stdout, result.stderr))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default=r"C:\Games\THUG2")
    parser.add_argument("--out", help="the overlay folder (default: <game>\\thug2-decomp-data)")
    parser.add_argument("--view", type=int, default=1, help="the backdrop view (menu_backdrop.cpp kViews), 1 or 2")
    parser.add_argument("--lift", type=float, default=0.0, help="raise everything by this much (units)")
    parser.add_argument("--ns", default=os.path.join(REPO, "out", "neverscript", "ns.exe"))
    args = parser.parse_args()
    out = args.out or os.path.join(args.game, "thug2-decomp-data")
    if not os.path.exists(args.ns):
        sys.exit("%s not found: build it with tools\\build_neverscript.ps1" % args.ns)

    yaw, offset = make_move(views()[args.view - 1], args.lift)
    print("move: turn %.3f degrees about up, then shift by %s" % (math.degrees(yaw), fmt(offset)))

    pre = os.path.join(args.game, "Data", "pre")
    sources = {
        r"scripts\game\Levels.qb": prx_file(os.path.join(pre, "qb_scripts.prx"), r"scripts\game\levels.qb"),
        r"levels\mainmenu\mainmenu.qb": prx_file(os.path.join(pre, "mainmenu_scripts.prx"),
                                                 r"levels\mainmenu\mainmenu.qb"),
    }
    with tempfile.TemporaryDirectory() as work:
        la = os.path.join(work, "LA.qb")
        open(la, "wb").write(prx_file(os.path.join(pre, "LA_scripts.prx"), r"levels\la\la.qb"))
        ns(args.ns, "-d", la, "-o", la + ".ns")
        shop = os.path.join(work, "shop.qb")
        open(shop, "wb").write(sources[r"levels\mainmenu\mainmenu.qb"])
        ns(args.ns, "-d", shop, "-o", shop + ".ns")
        placeholders = la_placeholders(open(la + ".ns", encoding="utf-8").read(),
                                       open(shop + ".ns", encoding="utf-8").read())
        for name, data in sources.items():
            base = os.path.join(work, os.path.basename(name))
            open(base, "wb").write(data)
            ns(args.ns, "-d", base, "-o", base + ".ns")
            text = open(base + ".ns", encoding="utf-8").read()
            if name.endswith("Levels.qb"):
                text = edit_levels(text, yaw, offset)
            else:
                text, moved = move_nodes(text, yaw, offset)
                _, open_at, _ = array_elements(text, "mainmenu_NodeArray")
                text = text[:open_at + 1] + "\n" + "\n".join(placeholders) + text[open_at + 1:]
                print("node array: %d points moved, after %d LA placeholders" % (moved, len(placeholders)))
            open(base + ".new.ns", "w", encoding="utf-8").write(text)
            target = os.path.join(out, name)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            ns(args.ns, "-c", base + ".new.ns", "-o", target, "-targetGame", "thug2")
            print("wrote", target)


if __name__ == "__main__":
    main()
