# NOT ORIGINAL - converts Tony Hawk's American Wasteland (PC) levels to THUG2's formats so that THUG2 can load and
# play them (see README.md). Nothing of either game is stored in the repo: the files are made from the user's copies.
#
#   python -I tools/thaw_levels/thaw_levels.py selfcheck [--thug2 C:\Games\THUG2] [--thaw C:\Games\THAW]
#   python -I tools/thaw_levels/thaw_levels.py convert [levels...] [--thug2 ...] [--thaw ...] [--out <folder>]
#   then: python -I tools/menu_level/make_menu_level.py   (installs them into the data overlay, with level select signs)
#
# convert reads each level's "net" archive (<thaw>\Game\data\worlds\worldzones\<zone>\<zone>_net.pak.wpc: the area
# alone, as THAW loads it for classic and online games), the sky (data\skies\TestLevel_Sky), data\pak\qb.pak.wpc's
# levels.qb and data\pak\dbg.pak.wpc's checksum names, and writes <thug2>\thug2-decomp-thaw\ (not read by the game):
# levels\thaw_<zone>\thaw_<zone>.scn.xbx / .tex.xbx / .col.xbx / .qb (the node array), the sky's scene and textures,
# and levels.json (what make_menu_level.py adds to Levels.qb and the level select: tools/level_port/ported_levels.py).

import argparse
import math
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "level_port"))

import common  # noqa: E402
import geometry  # noqa: E402
import nodes  # noqa: E402
import peds_cars  # noqa: E402
import ported_levels  # noqa: E402
import qb  # noqa: E402
import script_port  # noqa: E402
import thaw  # noqa: E402
import thaw_scripts  # noqa: E402
import thug2_col  # noqa: E402
import thug2_scene  # noqa: E402
from binio import FormatError  # noqa: E402

STAGING = "thug2-decomp-thaw"
SKY = "TestLevel_Sky"   # every THAW level's sky (levels.qb), coloured by the time of day

# THAW's levels: zone (the folder, archives and levels.qb's level=), the level select's sign text, where it is, a
# THUG2 level select sign that suits it, the sign's level number (unique: THPS4 19-31, THUG1 40-58, THPS3 60-76) and
# whether `convert` makes it by default. Classic mode's levels first, then the story's areas of Los Angeles.
LEVELS = [
    # zone      sign text            location                                  icon              num  default
    ("Z_CH",   "CHICAGO",           "Chicago, Illinois, USA",                 "sign_boston",     80, True),
    ("Z_MA",   "THE MALL",          "a shopping mall (no place named)",       "sign_training",   81, True),
    ("Z_DN",   "MINNEAPOLIS",       "Minneapolis, Minnesota, USA",            "sign_boston",     82, True),
    ("Z_JA",   "KYOTO",             "Kyoto, Japan",                           "sign_airport",    83, True),
    ("Z_SZ",   "SANTA CRUZ",        "Santa Cruz, California, USA",            "sign_la",         84, True),
    ("Z_SV2",  "THE RUINS",         "ruins (no place named)",                 "sign_skatopia",   85, True),
    ("Z_SV",   "VANS PARK",         "Los Angeles, California, USA",           "sign_la",         86, True),
    ("Z_HO",   "HOLLYWOOD",         "Hollywood, Los Angeles, USA",            "sign_la",         87, True),
    ("Z_BH",   "BEVERLY HILLS",     "Beverly Hills, Los Angeles, USA",        "sign_la",         88, True),
    ("Z_DT",   "DOWNTOWN",          "Downtown Los Angeles, USA",              "sign_la",         89, True),
    ("Z_EL",   "EAST LOS ANGELES",  "East Los Angeles, USA",                  "sign_la",         90, True),
    ("Z_SM",   "SANTA MONICA",      "Santa Monica, Los Angeles, USA",         "sign_la",         91, True),
    ("Z_SR",   "SKATE RANCH",       "Los Angeles, California, USA",           "sign_la",         92, True),
    ("Z_LV",   "CASINO",            "Los Angeles, California, USA",           "sign_la",         93, True),
    ("Z_OI",   "OIL RIG",           "off Los Angeles, California, USA",       "sign_la",         94, True),
]
BY_ZONE = {zone.lower(): (zone, sign, location, icon, num, default)
           for zone, sign, location, icon, num, default in LEVELS}

# Collision face flags: THUG2's meanings up to 0x1000 (invisible); 0x2000, 0x4000 and 0x8000 aren't THUG2's - dropped.
FACE_KEEP = 0x1FFF
# Material pass flags THUG2 has the data for here (uv wibble only with its values, below): textured, environment,
# decal, smooth, transparent, ignore vertex alpha. (0x80, texture animation 0x800, 0x80000... dropped.)
PASS_KEEP = (thug2_scene.PASS_TEXTURED | thug2_scene.PASS_ENVIRONMENT | thug2_scene.PASS_DECAL |
             thug2_scene.PASS_SMOOTH | thug2_scene.PASS_TRANSPARENT | thug2_scene.PASS_IGNORE_VERTEX_ALPHA)
BLEND_MODES = 14    # THUG2's blend modes 0..13; THAW's others (rare) become 5 (blend)
# Blend modes that mix by the previous pass's alpha: THUG2's pixel shader (0x004DD2C0) reads it from r1 after a pass
# that wrote r1.a - after another of these (which writes only r1.rgb) the ps.1.1 doesn't assemble and the game
# crashes loading (Santa Monica: one material 0, 13, 13). Such a pass is left out.
BLEND_BY_PREVIOUS = (12, 13)
DEFAULT_MIP = (1, 4, -8.0, -8.0)  # as nearly every material of THUG2's own levels


# --- files ------------------------------------------------------------------------------------------------------

def find_path(folder, *parts):
    # A path under `folder`, each part matched without regard to case.
    path = folder
    for part in parts:
        if not os.path.isdir(path):
            return None
        match = next((e for e in os.listdir(path) if e.lower() == part.lower()), None)
        if match is None:
            return None
        path = os.path.join(path, match)
    return path


def zone_pak(args, zone):
    return find_path(args.thaw, "Game", "data", "worlds", "worldzones", zone, zone + "_net.pak.wpc")


def zone_files(path):
    # {".geom" / ".tex" / ".col": data, "<file name>.qb": data} of a zone's archive, without its _sfx sub-zone.
    files = {}
    for entry, data in thaw.pak_files(path):
        if entry.flags & thaw.PAK_SFX:
            continue
        if entry.path:
            files[os.path.basename(entry.path).lower().replace(".wpc", "")] = data
        elif entry.extension in (".geom", ".tex", ".col"):
            files[entry.extension] = data
    return files


def level_structs(args, names):
    data = next(d for e, d in thaw.pak_files(find_path(args.thaw, "Game", "data", "pak", "qb.pak.wpc"))
                if e.path and e.path.lower().endswith("\\levels.qb.wpc"))
    q = thaw.QbItems(data, "levels.qb", names)
    found = {}
    for _, value in q.globals:
        if isinstance(value, qb.Struct) and isinstance(value.get("level"), str):
            found.setdefault(value.get("level").lower(), value)
    return found


# --- scene ------------------------------------------------------------------------------------------------------

def to_thug2_material(m):
    passes = []
    for p in m["passes"]:
        flags = p["flags"] & PASS_KEEP
        if p["flags"] & thug2_scene.PASS_UV_WIBBLE and "uv_wibble" in p:
            flags |= thug2_scene.PASS_UV_WIBBLE
        mode = p["blend"] & 0xFF
        r, g, b, _ = p["color"]
        out = {"texture": p["texture"], "flags": flags,
               "has_color": int(any(abs(c - 0.5) > 1e-6 for c in (r, g, b))), "color": (r, g, b),
               "blend_mode": mode if mode < BLEND_MODES else 5, "fixed_alpha": p["blend"] >> 24,
               "u_address": p["address"][0], "v_address": p["address"][1],
               "envmap_tiling": p["env_tiling"], "filtering": p["filtering"], "mip": DEFAULT_MIP}
        if flags & thug2_scene.PASS_UV_WIBBLE:
            out["uv_wibble"] = p["uv_wibble"][:8]
        if len(passes) >= 2 and out["blend_mode"] in BLEND_BY_PREVIOUS and \
                passes[-1]["blend_mode"] in BLEND_BY_PREVIOUS:
            continue
        passes.append(out)
    return {"checksum": m["checksum"], "name_checksum": m["name_checksum"], "passes": passes,
            "alpha_cutoff": m["alpha_cutoff"], "sorted": 0, "draw_order": m["draw_order"], "single_sided": 0,
            "no_backface_culling": int(bool(m["double_sided"])), "z_bias": m["z_bias"], "grassify": 0,
            "specular_power": 0.0}


FVF_TEXCOUNT = 0xF00


def mesh_vertices(m, shift):
    # THAW's FVF vertices as THUG2's (position, normal when there are some, colour, at least one uv set), moved by
    # `shift`. Returns (data, stride, fvf, has_normals).
    fvf = m["fvf"]
    has_normals = bool(fvf & thug2_scene.FVF_NORMAL)
    sets = (fvf & FVF_TEXCOUNT) >> thug2_scene.FVF_TEXCOUNT_SHIFT
    expected = 12 + (12 if has_normals else 0) + (4 if fvf & thug2_scene.FVF_DIFFUSE else 0) + 8 * sets
    if fvf & ~(thug2_scene.FVF_XYZ | thug2_scene.FVF_NORMAL | thug2_scene.FVF_DIFFUSE | FVF_TEXCOUNT) or \
            expected != m["stride"]:
        raise FormatError("mesh FVF %X with stride %d" % (fvf, m["stride"]))
    out = bytearray()
    src, stride = m["vertices"], m["stride"]
    for i in range(m["vertex_count"]):
        v = src[i * stride:(i + 1) * stride]
        x, y, z = struct.unpack_from("<3f", v, 0)
        out += struct.pack("<3f", x + shift[0], y + shift[1], z + shift[2])
        at = 12
        if has_normals:
            out += v[at:at + 12]
            at += 12
        if fvf & thug2_scene.FVF_DIFFUSE:
            out += v[at:at + 4]
            at += 4
        else:
            out += b"\x80\x80\x80\x80"
        out += v[at:] if sets else bytes(8)
    sets = max(sets, 1)
    new_fvf = thug2_scene.FVF_XYZ | thug2_scene.FVF_DIFFUSE | (sets << thug2_scene.FVF_TEXCOUNT_SHIFT)
    if has_normals:
        new_fvf |= thug2_scene.FVF_NORMAL
    return bytes(out), 12 + (12 if has_normals else 0) + 4 + 8 * sets, new_fvf, has_normals


def convert_scene(geom, warnings, shift=(0.0, 0.0, 0.0), drop=frozenset(), local=frozenset()):
    # THAW's scene as THUG2's: the materials mapped (to_thug2_material), a THUG2 sector per THAW sector with its
    # meshes' vertices and strips as they are. `drop`: sector checksums left out (the far-away LOD copies). `local`:
    # sectors in their own space (the LevelObjects'), not moved by `shift`.
    materials = [to_thug2_material(m) for m in geom["materials"]]
    by_checksum = {m["checksum"]: m for m in materials}
    sectors = []
    for s in geom["sectors"]:
        if s["checksum"] in drop:
            continue
        if s["flags"] & thug2_scene.SEC_BILLBOARD:
            # (every vertex at the pivot: THAW's shader spreads them out to face the camera)
            warnings.append("sector %08X: billboard - left out" % s["checksum"])
            continue
        out = {"checksum": s["checksum"], "bone": -1, "flags": thug2_scene.SEC_TEXCOORDS | thug2_scene.SEC_COLORS,
               "meshes": []}
        points = []
        for m in s["meshes"]:
            if len(m["strip"]) < 3 or not m["vertex_count"]:
                continue
            material = by_checksum.get(m["material"])
            if material is None:
                warnings.append("sector %08X: material %08X missing - mesh left out" % (s["checksum"], m["material"]))
                continue
            data, stride, fvf, has_normals = mesh_vertices(m, (0.0, 0.0, 0.0) if s["checksum"] in local else shift)
            if has_normals:
                out["flags"] |= thug2_scene.SEC_NORMALS
            positions = [struct.unpack_from("<3f", data, i * stride) for i in range(m["vertex_count"])]
            points += positions
            lo, hi, center, radius = geometry.bounds(positions)
            mesh = {"sphere": (center[0], center[1], center[2], radius), "bbox": tuple(lo) + tuple(hi),
                    "flags": geometry.MESH_THUG2, "material": m["material"], "lods": [m["strip"]],
                    "strips": [m["strip"]], "stride": stride, "vertex_count": m["vertex_count"],
                    "buffers": [(0, data)], "vertex_shader": (fvf, 0),
                    "offsets": (12, 24, 28) if has_normals else (0, 12, 16), "has_wibble": 0, "index_sets": 1,
                    "pixel_shader": geometry.pixel_shader(material)}
            if mesh["pixel_shader"] == geometry.PS_FROM_MATERIAL:
                mesh["ps_value"] = 0
                mesh["ps_data"] = b""
            out["meshes"].append(mesh)
        if not out["meshes"]:
            continue
        if any(not mesh["offsets"][0] for mesh in out["meshes"]):
            out["flags"] &= ~thug2_scene.SEC_NORMALS
        lo, hi, center, radius = geometry.bounds(points)
        out["bbox"] = tuple(lo) + tuple(hi)
        out["sphere"] = (center[0], center[1], center[2], radius)
        sectors.append(out)
    return {"versions": (1, 1, 1), "materials": materials, "sectors": sectors, "hierarchy_count": 0, "tail": b""}


NIGHT_NAMES = ("nighton", "_tod_light")


def night_sectors(geom, names):
    # The sectors THAW shows only at night (its time of day scripts: NightOn_* lamps and glows, *_tod_light_*
    # glows): left out, the levels being lit as by day.
    return {s["checksum"] for s in geom["sectors"]
            if any(word in names.get(s["checksum"], "").lower() for word in NIGHT_NAMES)}


# --- collision --------------------------------------------------------------------------------------------------

# Per zone: pieces left out altogether (scene, collision, node) - story blockers THAW's goal scripts take away, which
# free skate here doesn't run. The Mall: an invisible wall across the glass doors by the start (the way in shut).
LEFT_OUT = {"Z_MA": ("Z_MA_Mall_First_Rail_Blocker",)}


def left_out(zone):
    return {qb.checksum(n) for n in LEFT_OUT.get(zone.upper(), ())}


# A breakable's faces, as THUG2's own (its Airport's AP_Shatter_Roof pieces): non-collidable, trigger, no skater
# shadow, invisible - THAW's had decal and camera collidable too (0xF0).
BREAKABLE_FACE = 0x1150


def move_tree(node, shift):
    # A BSP tree moved by `shift`: its split points are world coordinates (in sixteenths), not relative to the box.
    # Left where they were, a moved object's queries all went down one side of each split and never found the faces
    # on the other: The Mall's floors at both ends of its escalator were missing (the walker stopped there, as at a
    # drop with no bottom) - 198 of its 1074 objects have splits.
    if node[0] != "split":
        return node
    _, axis, point, left, right = node
    return ("split", axis, point + int(round(shift[axis] * 16.0)), move_tree(left, shift), move_tree(right, shift))


def convert_col(col, keep_triggers, shift, drop=frozenset(), breakables=frozenset(), local=frozenset()):
    # THAW's collision is THUG2's (with the reaches and BSP trees): the face flags THUG2 doesn't have dropped, trigger
    # faces only for kill zones (THUG2 runs a trigger face's node's TriggerScript without checking it exists), moved
    # by `shift` (whole sixteenths: the fixed-point vertices are relative to the box, which moves with them; the BSP
    # trees' split points are world coordinates and move too).
    objects = []
    for o in col["objects"]:
        if o["checksum"] in drop:
            continue
        o = dict(o)
        keep = keep_triggers(o["checksum"])
        o["faces"] = [((flags & FACE_KEEP) if keep else (flags & FACE_KEEP & ~thug2_col.FACE_TRIGGER), terrain, a, b, c)
                      for flags, terrain, a, b, c in o["faces"]]
        if o["checksum"] in breakables:
            o["faces"] = [(BREAKABLE_FACE, terrain, a, b, c) for _, terrain, a, b, c in o["faces"]]
        if any(shift) and o["checksum"] not in local:
            # (not the LevelObjects': in their own space)
            b = o["bbox"]
            o["bbox"] = (b[0] + shift[0], b[1] + shift[1], b[2] + shift[2], b[3],
                         b[4] + shift[0], b[5] + shift[1], b[6] + shift[2], b[7])
            o["verts"] = [(v[0] + shift[0], v[1] + shift[1], v[2] + shift[2]) for v in o["verts"]]
            o["bsp"] = move_tree(o["bsp"], shift)
        objects.append(o)
    return {"objects": objects, "pad": col["pad"]}


# --- nodes ------------------------------------------------------------------------------------------------------

def move_positions(value, shift):
    # Every Pos / Position vector of the nodes moved by `shift`.
    for node in value:
        if not isinstance(node, qb.Struct):
            continue
        for i, (key, v) in enumerate(node.entries):
            if key is not None and key.value in (qb.checksum("Pos"), qb.checksum("Position")) and \
                    isinstance(v, tuple) and v[0] == "vector":
                node.entries[i] = (key, ("vector", tuple(v[1][k] + shift[k] for k in range(3))))


SHATTERS = (qb.checksum("ShatterAndDie"), qb.checksum("Shatter"), qb.checksum("ShatterFromSkater"))


def breakable_scripts(array, script_qbs, names, drawn=None):
    # Breakable pieces (The Mall's glass doors and panes...): a node whose TriggerScript - or a script it calls -
    # shatters the node (THAW: MA_BreakGlass name=<the node> = Kill + ShatterAndDie, a THAW command). As THUG2's own
    # breakables (its Airport's AP_Shatter): Shatter name=<the node> ..., then Kill. Returns {trigger script: the
    # commands}, kept with the nodes' trigger faces as the kill zones are (without them the glass was a wall: the
    # Mall's way in from the start was shut).
    scripts = {}
    for q in script_qbs:
        scripts.update(q.scripts)

    def shatters(script, depth=0):
        for line in scripts.get(script, []):
            for t, p in line:
                if t == 0x16:
                    value = struct.unpack("<I", p)[0]
                    if value in SHATTERS:
                        return True
                    if depth < 2 and value in scripts and value != script and shatters(value, depth + 1):
                        return True
        return False

    out = {}
    for node in array:
        if not isinstance(node, qb.Struct) or nodes.class_of(node, names) != "levelgeometry":
            continue
        trigger, name = node.get("TriggerScript"), node.get("Name")
        if drawn is not None and isinstance(name, qb.Name) and name.value not in drawn:
            # (collision-only: Shatter / Kill look the scene sector up and read through the null)
            continue
        if isinstance(trigger, qb.Name) and isinstance(name, qb.Name) and shatters(trigger.value):
            text = qb.name_text(name.value, names)
            out[trigger.value] = ("PlaySound HitGlassPane2x vol=100\n"
                                  "    Shatter name=%s area=4000 variance=4 vel_x=50 vel_y=50 vel_z=50 spread=1\n"
                                  "    Kill name=%s" % (text, text))
    return out


class Port:
    # What bringing a level's scripts, objects, peds and cars along needs (thaw_scripts.py): THUG2's commands
    # (script_port.Thug2) and assets (peds_cars.Thug2Assets), THAW's game script files brought along.
    def __init__(self, args, names):
        self.thug2 = script_port.Thug2(args.thug2)
        self.assets = peds_cars.Thug2Assets(args.thug2)
        self.globals = thaw_scripts.global_sources(find_path(args.thaw, "Game", "data", "pak", "qb.pak.wpc"), names)
        self.particles = thaw_scripts.particle_textures(args.thug2)


def write_level_qb(ns, text, path, scripts, names):
    # The node array's NeverScript text compiled, the brought scripts added (script_port.append_scripts).
    # (network games: path followers, peds and cars kept out - no path manager or ped animations there -, restarts for the players)
    text, marked = nodes.net_game_nodes(text, scripts)
    if marked:
        print("  %s: %d node(s) changed for network games" % (os.path.basename(path), marked))
    common.ns_compile(ns, text, path)
    if scripts:
        data = script_port.append_scripts(open(path, "rb").read(), scripts, names)
        open(path, "wb").write(data)


def convert_level_nodes(files, zone, name, ns, out_dir, report, names, shift, drop=frozenset(), port=None,
                        sectors=frozenset(), col_objects=frozenset()):
    # The node array: kept nodes and kill zones (THUG2's SK3_KillSkater, each naming a restart) as <name>.qb; with a
    # Port, the level's scripts, moving objects, peds and cars too (thaw_scripts.py; sectors / col_objects: the
    # checksums the scene and collision have). Returns (the checksums of the nodes whose collision keeps its trigger
    # faces, the LOD sectors to leave out, the breakables, the node array text, the brought scripts, the LevelObjects
    # in their own space, the moving objects left out).
    low = zone.lower()
    if low + ".qb" not in files:
        raise FormatError("%s: no %s.qb in its archive" % (zone, low))
    q = thaw.QbItems(files[low + ".qb"], low + ".qb", names)
    script_qbs = [q]
    if low + "_scripts.qb" in files:
        script_qbs.append(thaw.QbItems(files[low + "_scripts.qb"], low + "_scripts.qb", names))
    array = q.get(zone + "_NodeArray")
    if not isinstance(array, list):
        raise FormatError("%s: no %s_NodeArray" % (zone, zone))
    array = nodes.expand_includes(array, {key.value: value for key, value in q.globals})
    # LOD copies: a node's LOD_Name is the sector drawn instead of it far away; THUG2 would draw both.
    lods = set()
    for node in array:
        if isinstance(node, qb.Struct) and isinstance(node.get("LOD_Name"), qb.Name):
            lods.add(node.get("LOD_Name").value)
    restarts = {n.get("Name").value for n in array if isinstance(n, qb.Struct) and isinstance(n.get("Name"), qb.Name)
                and nodes.class_of(n, names) == "restart"}
    kills = nodes.kill_commands(script_qbs, restarts, names)
    breakables = breakable_scripts(array, script_qbs, names, (set(sectors) - lods - set(drop)) if sectors else None)
    # The nodes left out (LOD copies, `drop`) become placeholders of a class convert_nodes drops, before it renumbers
    # the Links - removed after, every node after one pointed its Links one place off (rails linked to the wrong
    # nodes: The Mall hung loading once its story blocker was left out).
    gone = lods | set(drop)
    placeholder = qb.Struct([(qb.Name(qb.checksum("Class")), qb.Name(qb.checksum("LeftOut")))])
    original = array
    array = [placeholder if isinstance(n, qb.Struct) and isinstance(n.get("Name"), qb.Name) and
             n.get("Name").value in gone else n for n in array]
    made = None
    wanted = set()
    if port is not None:
        # (in place: the Links stay indices into the array)
        made = thaw_scripts.PortedNodes(array, names, set(sectors) - gone, col_objects, script_qbs, port.thug2,
                                        port.assets, port.particles, shift)
        array = [(made.convert(n) or n) if isinstance(n, qb.Struct) else n for n in array]
        for node in array:
            script = node.get("TriggerScript") if isinstance(node, qb.Struct) else None
            if isinstance(script, qb.Name) and nodes.class_of(node, names) in ("levelgeometry", "railnode"):
                wanted.add(script.value)
    else:
        # (static: THAW's LevelObjects are in their own space - left out rather than drawn at the origin)
        made_gone = {n.get("Name").value for n in array if isinstance(n, qb.Struct) and
                     nodes.class_of(n, names) == "levelobject" and isinstance(n.get("Name"), qb.Name)}
        array = [placeholder if isinstance(n, qb.Struct) and isinstance(n.get("Name"), qb.Name) and
                 n.get("Name").value in made_gone else n for n in array]
    converted, stats = nodes.convert_nodes(array, names, {**kills, **breakables}, extra_keys=("pos",),
                                           as_is=made.ids if made else (), scripts=wanted)
    if any(shift):
        move_positions(converted, shift)
    kills = nodes.add_kill_restarts(converted, kills, names)
    scripts, extra, script_report, local, objects_gone = {}, "", "", set(), set()
    if made is not None:
        sources = [thaw_scripts.ThawSource(x, thaw_scripts.LEVEL, names) for x in script_qbs] + port.globals
        provided = set(kills) | set(breakables) | {qb.checksum(s) for s in (
            "LoadTerrain", thaw_scripts.PED_WALK, thaw_scripts.PED_STAND, thaw_scripts.CAR_DRIVE)}
        scripts, globals_text, script_report, _ = thaw_scripts.port_scripts(
            converted, original, made, port.globals + sources, port.thug2, names, provided,
            {n.get("Name").value for n in converted if isinstance(n.get("Name"), qb.Name) and
             nodes.class_of(n, names) == "levelgeometry" and n.get("Name").value not in made.sectors})
        extra = globals_text + thaw_scripts.MADE_SCRIPTS + thaw_scripts.load_particle_textures_ns(made.particle_textures)
        local, objects_gone = made.local, made.gone
    else:
        objects_gone = made_gone
    kills.update(breakables)
    breaking_names = nodes.kill_trigger_names(converted, breakables)
    # (a breakable keeps no Type: THAW's Type=Crate - its physics breakables - made the glass a solid object here)
    type_key = qb.checksum("Type")
    for node in converted:
        if isinstance(node.get("Name"), qb.Name) and node.get("Name").value in breaking_names:
            node.entries = [(k, v) for k, v in node.entries if k is None or k.value != type_key]
    triggers = nodes.kill_trigger_names(converted, kills)
    kill_zones = len(triggers) - len(breaking_names)
    if made is not None:
        # (the geometry whose trigger script runs keeps its trigger faces: gaps, sounds...)
        triggers |= {n.get("Name").value for n in converted if isinstance(n.get("TriggerScript"), qb.Name)
                     and isinstance(n.get("Name"), qb.Name) and nodes.class_of(n, names) == "levelgeometry"}
    text = nodes.node_array_ns(name + "_NodeArray", converted, names, kills) + extra
    if breaking_names:
        # The breakables' glass sound (THAW's MA_BreakGlass plays HitGlassPane2x; THUG2 has it too, loaded per level):
        # loaded by the level's LoadTerrain, which load_level runs while loading (the level-specific sounds script
        # goes by the career level's prefix, shared by every ported level).
        start = "script LoadTerrain {\n"
        if text.count(start) != 1:
            raise FormatError("%s: LoadTerrain not found once" % name)
        text = text.replace(start, start + '    LoadSound "Shared\\\\Hits\\\\HitGlassPane2x" vol=100 dropoff=0 \n')
    os.makedirs(out_dir, exist_ok=True)
    write_level_qb(ns, text, os.path.join(out_dir, name + ".qb"), scripts, names)
    report.append("  nodes: %d of %d kept %s; %d kill zones, %d breakables; %d LOD sectors left out"
                  % (len(converted), len(array), stats, kill_zones, len(breaking_names), len(lods)))
    if script_report:
        report.append("  " + script_report)
    return triggers, lods, breaking_names, text, scripts, local, objects_gone


# --- converting -------------------------------------------------------------------------------------------------

def recentre_shift(col, limit):
    # Areas far from the origin (Oil Rig reaches z 112,000) lose precision in THUG2's camera and physics: moved so the
    # middle of their collision is at the origin (x / z, in whole 16-unit steps), when it is further than `limit`.
    boxes = [o["bbox"] for o in col["objects"] if o["faces"]]
    if not boxes:
        return (0.0, 0.0, 0.0)
    lo = [min(b[k] for b in boxes) for k in range(3)]
    hi = [max(b[k + 4] for b in boxes) for k in range(3)]
    if max(abs(lo[k]) for k in (0, 2)) < limit and max(abs(hi[k]) for k in (0, 2)) < limit:
        return (0.0, 0.0, 0.0)
    return (-16.0 * round((lo[0] + hi[0]) / 32.0), 0.0, -16.0 * round((lo[2] + hi[2]) / 32.0))


def convert_sky(args, out, report):
    path = find_path(args.thaw, "Game", "data", "skies", SKY, SKY.lower() + ".pak.wpc")
    if not path:
        return None
    files = zone_files(path)
    warnings = []
    name = "thaw_sky"
    scene = convert_scene(thaw.read_geom(files[".geom"], SKY), warnings)
    textures = thaw.read_tex(files[".tex"], SKY)
    common.write_level(os.path.join(out, "levels", name), name, scene, textures, None)
    common.report_level(report, name, scene, textures, None, warnings)
    return name


def cmd_convert(args):
    out = args.out or os.path.join(args.thug2, STAGING)
    if not os.path.exists(args.ns):
        sys.exit("%s not found: build it with tools\\build_neverscript.ps1" % args.ns)
    if not find_path(args.thaw, "Game", "data", "pak", "dbg.pak.wpc"):
        sys.exit("%s: THAW's Game\\data\\pak not found" % args.thaw)
    names = thaw.load_names(args.thaw)
    structs = level_structs(args, names)
    wanted = args.levels or [zone for zone, _, _, _, _, default in LEVELS if default]
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    manifest = {"levels": []}
    report = []
    port = None if args.static else Port(args, names)
    sky = convert_sky(args, out, report)
    for requested in wanted:
        info = BY_ZONE.get(requested.lower()) or BY_ZONE.get("z_" + requested.lower())
        if not info:
            sys.exit("%s: not a THAW level this tool knows (%s)" % (requested, ", ".join(z for z, *_ in LEVELS)))
        zone, sign, location, icon, level_num, _ = info
        path = zone_pak(args, zone)
        if not path:
            print("%s: no %s_net.pak.wpc - skipped" % (zone, zone.lower()))
            continue
        print("converting", zone)
        files = zone_files(path)
        name = "thaw_" + zone.lower()[2:]
        level_dir = os.path.join(out, "levels", name)
        col_in = thaw.read_col(files[".col"], zone + ".col")
        shift = recentre_shift(col_in, args.recentre)
        if any(shift):
            report.append("%s: moved by (%g, %g, %g)" % (name, shift[0], shift[1], shift[2]))
        geom = thaw.read_geom(files[".geom"], zone + ".geom")
        night = night_sectors(geom, names) | left_out(zone)
        node_report = ["  %d night-time sectors left out" % len(night)] if night else []
        drawn = {s["checksum"] for s in geom["sectors"] if not s["flags"] & thug2_scene.SEC_BILLBOARD and
                 any(len(m["strip"]) >= 3 and m["vertex_count"] for m in s["meshes"])}
        triggers, lods, breakables, node_text, scripts, local, objects_gone = convert_level_nodes(
            files, zone, name, args.ns, level_dir, node_report, names, shift, night, port, drawn,
            {o["checksum"] for o in col_in["objects"]})
        warnings = []
        scene = convert_scene(geom, warnings, shift, lods | night | objects_gone, local)
        scene_sectors = {s["checksum"] for s in scene["sectors"]}
        if local - scene_sectors:
            raise FormatError("%s: LevelObjects without a sector: %s" % (zone, ", ".join(
                qb.name_text(c, names) for c in sorted(local - scene_sectors))))
        node_text, cleared = nodes.strip_missing_trick_objects(node_text, scene_sectors)
        if cleared:
            write_level_qb(args.ns, node_text, os.path.join(level_dir, name + ".qb"), scripts, names)
            node_report.append("  %d geometry nodes without a scene sector: TrickObject / Cluster dropped" % cleared)
        textures = thaw.read_tex(files[".tex"], zone + ".tex")
        col = convert_col(col_in, lambda checksum: checksum in triggers, shift, left_out(zone) | objects_gone,
                          breakables, local)
        # (restarts over nothing - the shops' interiors - or over a kill zone: no network spawns, no kill targets)
        node_text, ground_report = nodes.ground_restarts(node_text, col)
        if ground_report:
            write_level_qb(args.ns, node_text, os.path.join(level_dir, name + ".qb"), scripts, names)
            node_report += ground_report
        common.write_level(level_dir, name, scene, textures, col)
        common.report_level(report, name, scene, textures, col, warnings)
        report += node_report
        struct_ = structs.get(zone.lower())
        display = struct_.get("name") if struct_ and isinstance(struct_.get("name"), str) else sign.title()
        manifest["levels"].append({
            "folder": zone,
            "level": name,
            "sky": sky,
            "display": "THAW " + sign,
            "name": display,
            "game": "THAW",
            "location": location,
            "icon": icon,
            "level_num": level_num,
            "default_stats": struct_.get("default_stats") if struct_ and isinstance(struct_.get("default_stats"), int)
            else 10,
            "lighting": {},
        })
    ported_levels.write_manifest(os.path.join(out, ported_levels.MANIFEST), manifest)
    print("\n".join(report))
    print("wrote %s (%d levels); install with tools\\menu_level\\make_menu_level.py" % (out, len(manifest["levels"])))


# --- self-check -------------------------------------------------------------------------------------------------

def cmd_selfcheck(args):
    pre = os.path.join(args.thug2, "Data", "pre")
    checked, failures = common.thug2_round_trips(pre, ("LAscn.prx", "LAcol.prx"))
    names = thaw.load_names(args.thaw)
    print("%d checksum names from dbg.pak" % len(names))
    data = find_path(args.thaw, "Game", "data")
    paks = [find_path(data, "pak", "qb.pak.wpc")]
    for folder in ("worlds\\worldzones", "skies"):
        root = os.path.join(data, folder)
        for zone in sorted(os.listdir(root)):
            for entry in sorted(os.listdir(os.path.join(root, zone))):
                if entry.lower().endswith("_net.pak.wpc") or folder == "skies" and entry.lower().endswith(".pak.wpc"):
                    paks.append(os.path.join(root, zone, entry))
    for path in paks:
        counts = {}
        try:
            for entry, data_ in thaw.pak_files(path):
                ext = entry.extension
                if ext in (".qb", ".nqb", ".sqb"):
                    thaw.QbItems(data_, entry.path or ext, names)
                elif ext == ".tex":
                    thaw.read_tex(data_, ext)
                elif ext == ".col":
                    stripped = thaw.strip_col(data_)
                    if thug2_col.write_col(thug2_col.read_col(stripped, ext)) != stripped:
                        raise FormatError("collision not the same written again")
                elif ext == ".geom":
                    g = thaw.read_geom(data_, ext)
                    convert_scene(g, [])
                else:
                    continue
                counts[ext] = counts.get(ext, 0) + 1
        except (FormatError, ValueError, IndexError, struct.error) as e:
            print("FAIL %s: %s" % (path, e))
            failures += 1
            continue
        checked += sum(counts.values())
        print("ok   %s: %s" % (os.path.relpath(path, data), ", ".join("%d %s" % (n, e) for e, n in sorted(counts.items()))))
    print("%d files checked, %d failures" % (checked, failures))
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("convert", "selfcheck"))
    parser.add_argument("levels", nargs="*", help="THAW zones (Z_CH, CH, ...; default: all in LEVELS)")
    parser.add_argument("--thug2", default=r"C:\Games\THUG2")
    parser.add_argument("--thaw", default=r"C:\Games\THAW")
    parser.add_argument("--out", help="where convert writes (default: <thug2>\\%s)" % STAGING)
    parser.add_argument("--ns", default=common.NS)
    parser.add_argument("--recentre", type=float, default=20000.0,
                        help="move an area whose collision reaches further than this from the origin (x / z)")
    parser.add_argument("--static", action="store_true",
                        help="convert: the levels' scripts, moving objects, peds and cars left out")
    args = parser.parse_args()
    if args.command == "selfcheck":
        sys.exit(cmd_selfcheck(args))
    cmd_convert(args)


if __name__ == "__main__":
    main()
