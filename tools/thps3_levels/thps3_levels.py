# NOT ORIGINAL - converts Tony Hawk's Pro Skater 3 (PC) levels to THUG2's formats so that THUG2 can load and play
# them (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
#
#   python -I tools/thps3_levels/thps3_levels.py selfcheck [--thug2 C:\Games\THUG2] [--thps3 C:\Games\THPS3]
#   python -I tools/thps3_levels/thps3_levels.py convert [levels...] [--thug2 ...] [--thps3 ...] [--out <folder>]
#   then: python -I tools/menu_level/make_menu_level.py   (installs them into the data overlay, with level select signs)
#
# convert writes <thug2>\thug2-decomp-thps3\ (not read by the game): levels\thps3_<lvl>\thps3_<lvl>.scn.xbx /
# .tex.xbx / .col.xbx / .qb (the node array), the skies (scene and textures) and levels.json (what
# make_menu_level.py adds to Levels.qb and the level select: tools/level_port/ported_levels.py).
#
# THPS3's own formats (RenderWare worlds, texture dictionaries, Neversoft's plugin data) are read by thps3.py; the
# THUG2 writers, the geometry conversion, the node arrays and the installing are shared with tools/thps4_levels and
# tools/thug1_levels (tools/level_port).

import argparse
import glob
import math
import os
import re
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
import tex  # noqa: E402
import thps3  # noqa: E402
import thug2_col  # noqa: E402
import thug2_scene  # noqa: E402
from binio import FormatError  # noqa: E402

STAGING = "thug2-decomp-thps3"

# The THPS3 levels: folder under Data\Levels, sign text, real-world place, THUG2 sign texture to use as an icon
# (the level select's own table in tools/menu_level/level_select.py takes precedence), level select number (each its
# own; no career records - the info panel skips them), converted by default.
LEVELS = [
    ("Foun",      "FOUNDRY",       "Foundry (no city named)",              "sign_philly",     60, True),
    ("Can",       "CANADA",        "British Columbia, Canada",             "sign_canada",     61, True),
    ("Rio",       "RIO",           "Rio de Janeiro, Brazil",               "sign_barcelona",  62, True),
    ("Sub",       "SUBURBIA",      "Suburbia, USA",                        "sign_neworleans", 63, True),
    ("Ap",        "AIRPORT",       "Airport",                              "sign_airport",    64, True),
    ("SI",        "SKATER ISLAND", "Skater Island, Southern California",  "sign_australia",  65, True),
    ("LA",        "LOS ANGELES",   "Los Angeles, USA",                     "sign_la",         66, True),
    ("Tok",       "TOKYO",         "Tokyo, Japan",                         "sign_airport",    67, True),
    ("Shp",       "CRUISE SHIP",   "Cruise ship",                          "sign_jam",        68, True),
    ("Ware",      "WAREHOUSE",     "Warehouse, Woodland Hills, California", "sign_training",  69, True),
    ("Burn",      "BURNSIDE",      "Burnside, Portland, Oregon",           "sign_skatopia",   70, True),
    ("Ros",       "ROSWELL",       "Roswell, New Mexico",                  "sign_airport",    71, True),
    ("Tut",       "TUTORIALS",     "Tutorials (no place)",                 "sign_training",   72, True),
    ("SkateShop", "SKATE SHOP",    "THPS3's skate shop (no place)",        "sign_training",   73, True),
    ("Sk3Ed_Bch", "PARK BEACH",    "Park editor: beach",                   "sign_training",   74, False),
    ("Sk3Ed_Indr", "PARK INDOOR",  "Park editor: indoor",                  "sign_training",   75, False),
    ("Sk3Ed_Schl", "PARK SCHOOL",  "Park editor: school",                  "sign_training",   76, False),
]


# --- files ---------------------------------------------------------------------------------------------------------

def find_folder(levels_dir, name):
    for entry in os.listdir(levels_dir):
        if entry.lower() == name.lower():
            return os.path.join(levels_dir, entry)
    return None


def level_files(folder, base):
    # <base>.bsp / .dff / .tdx / .qb, any case.
    found = {}
    for path in glob.glob(os.path.join(folder, "*")):
        name = os.path.basename(path).lower()
        for kind in ("bsp", "dff", "tdx", "qb"):
            if name == (base + "." + kind).lower():
                found[kind] = path
    return found


def read(path):
    return open(path, "rb").read()


# --- materials and textures ----------------------------------------------------------------------------------------

def drawn_sectors(world):
    # The atomic sectors the scene will have a sector for (scene_sectors: at least one triangle of a material that
    # isn't invisible) - the others are collision-only.
    out = set()
    for sec in world["sectors"]:
        for material, _, _, _ in sec["triangles"]:
            m = world["materials"][material + sec["material_base"]]
            if not (m["ns"] and m["ns"]["flags"] & thps3.MAT_INVISIBLE):
                out.add(sec["checksum"])
                break
    return out


def without_missing_trick_objects(text, scene_path):
    # (nodes.strip_missing_trick_objects with the converted scene's sectors: THPS3 Airport crashed loading)
    sectors = {s["checksum"] for s in thug2_scene.read_scene(open(scene_path, "rb").read())["sectors"]}
    return nodes.strip_missing_trick_objects(text, sectors)


def usable_normals(sec):
    # The sector's normals, or None: THPS3's PC files leave them (near) zero on almost every sector (Foundry: 533 of
    # 534) - lit by zero normals in THUG2, a sector drew black (Foundry's halfpipe). Without normals THUG2 draws a
    # sector by its vertex colours (THPS3's prelighting), as the rest of the level.
    normals = sec.get("normals")
    if not normals or any(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 0.25 for n in normals):
        return None
    return normals


def blend_mode(code):
    # THPS3's blend: the PS2's alpha register, (A - B) * C + D, two bits each from the bottom (A, B, D: 0 source
    # colour, 1 destination colour, 2 zero; C: 0 source alpha, 1 destination alpha, 2 the fixed alpha) - the
    # formulas THUG2's blend modes are named after: (mode, uses the fixed alpha).
    a, b, c, d = code & 3, (code >> 2) & 3, (code >> 4) & 3, (code >> 6) & 3
    fixed = c == 2
    if a == b:
        return (0, False) if d == 0 else (5, False)      # just D: source (opaque) - or the rare others as blend
    if (a, b, d) == (0, 1, 1):
        return (6 if fixed else 5), fixed               # blend
    if (a, b, d) == (0, 2, 1):
        return (2 if fixed else 1), fixed               # add
    if (a, b, d) == (0, 2, 0):
        # Cs * C with nothing of the destination: THPS3 uses it for its reflection / light overlays
        # (Foun_CG_QP_Reflect_01 on the quarter / half pipes, Lamp_02_reflect, the vats' ripples) - as opaque (the
        # fallback below) a dark gradient covered Foundry's halfpipe black. Added, as the overlays look.
        return (2 if fixed else 1), fixed
    if (a, b, d) == (2, 0, 1):
        return (4 if fixed else 3), fixed               # subtract
    if (a, b, d) == (1, 2, 2):
        return (8 if fixed else 7), fixed               # modulate
    if (a, b, d) == (1, 2, 1):
        return (10 if fixed else 9), fixed              # brighten
    return (0, False) if d == 0 else (5, False)


def texture_checksum(name):
    return qb.checksum(name) if name else 0


def address(mode):
    # RenderWare's addressing (1 wrap, 2 mirror, 3 clamp, 4 border) as THUG2's (0 wrap, 1 clamp).
    return 1 if mode in (3, 4) else 0


def to_thug2_material(checksum, m, textures):
    # A THPS3 material (thps3.read_material) as THUG2's: one pass.
    ns = m["ns"] or {"flags": 0, "terrain": 0, "colors": [(128, 128, 128, 255)], "blend": 0, "fixed_alpha": 0,
                     "texture": None, "uv_flags": 0}
    tex_info = ns["texture"] or m["texture"]
    texture = texture_checksum(tex_info["name"]) if tex_info else 0
    if texture and texture not in textures:
        texture = 0
    mode, _ = blend_mode(ns["blend"])
    flags = thug2_scene.PASS_SMOOTH
    if texture:
        flags |= thug2_scene.PASS_TEXTURED
    if mode == 0:
        flags |= thug2_scene.PASS_IGNORE_VERTEX_ALPHA
    else:
        flags |= thug2_scene.PASS_TRANSPARENT
    if ns["flags"] & thps3.MAT_DECAL:
        flags |= thug2_scene.PASS_DECAL
    p = {"texture": texture, "flags": flags, "blend_mode": mode, "fixed_alpha": ns["fixed_alpha"] & 0xFF,
         "u_address": address(tex_info["u_address"]) if tex_info else 0,
         "v_address": address(tex_info["v_address"]) if tex_info else 0,
         "envmap_tiling": (3.0, 3.0), "filtering": 0x10004, "mip": (1, 4, -8.0, -8.0)}
    r, g, b, _ = ns["colors"][0]
    if all(0x7E <= x <= 0x81 for x in (r, g, b)):
        p["has_color"], p["color"] = 0, (0.5, 0.5, 0.5)
    else:
        p["has_color"], p["color"] = 1, (r / 256.0, g / 256.0, b / 256.0)
    if ns.get("uv_flags", 0) & thps3.UV_WIBBLE and any(ns["uv_wibble"]):
        p["flags"] |= thug2_scene.PASS_UV_WIBBLE
        p["uv_wibble"] = ns["uv_wibble"]
    transparent = mode != 0 or ns["flags"] & (thps3.MAT_TRANSPARENT | thps3.MAT_DECAL)
    return {
        "checksum": checksum,
        "name_checksum": checksum,
        "alpha_cutoff": 1,
        "sorted": 0,
        "draw_order": 1500.0 if transparent else 0.0,
        "single_sided": 0,
        "no_backface_culling": 0,
        "z_bias": 0,
        "grassify": 0,
        "specular_power": 0.0,
        "passes": [p],
    }


def mip_down(level, w, h):
    # The next mip level of a 32-bit texture: each 2 x 2 block averaged (a 1-wide side halves the other only).
    nw, nh = max(1, w // 2), max(1, h // 2)
    sx, sy = (2 if w > 1 else 1), (2 if h > 1 else 1)
    out = bytearray(nw * nh * 4)
    row = w * 4
    for y in range(nh):
        r0 = (y * sy) * row
        r1 = r0 + (row if sy == 2 else 0)
        o = y * nw * 4
        for x in range(nw):
            c0 = r0 + x * sx * 4
            c1 = c0 + (4 if sx == 2 else 0)
            d0 = r1 + x * sx * 4
            d1 = d0 + (4 if sx == 2 else 0)
            for k in range(4):
                out[o + k] = (level[c0 + k] + level[c1 + k] + level[d0 + k] + level[d1 + k] + 2) >> 2
            o += 4
    return bytes(out)


def convert_textures(tdx, mips=True):
    # THPS3's 32-bit textures (BGRA, as Direct3D's A8R8G8B8) in THUG2's dictionary, checksum = the name's; mip levels
    # made down to 8 x 8 (THPS3's have one level: without them distant textures shimmer).
    out = []
    seen = set()
    for t in tdx:
        checksum = texture_checksum(t["name"])
        if checksum in seen:
            continue
        seen.add(checksum)
        levels = [t["levels"][0]]
        w, h = t["width"], t["height"]
        while mips and w > 8 and h > 8 and len(levels) < 4:
            levels.append(mip_down(levels[-1], w, h))
            w, h = max(1, w // 2), max(1, h // 2)
        out.append({"checksum": checksum, "width": t["width"], "height": t["height"], "texel_depth": 32,
                    "palette_depth": 32, "dxt": 0, "palette": b"", "levels": levels})
    return {"version": 1, "textures": out}


# --- scene and collision -------------------------------------------------------------------------------------------

def tris_to_strip(tris):
    # A triangle list as one strip, the triangles joined by degenerate ones (each starts at an even index, so its
    # winding is kept).
    s = []
    for a, b, c in tris:
        if s:
            s.append(s[-1])
            s.append(a)
            if (len(s) - 1) % 2:
                s.append(a)
            s += [b, c]
        else:
            s += [a, b, c]
    return s


SECTOR_FLAGS = 0x80002000 | thug2_scene.SEC_TEXCOORDS | thug2_scene.SEC_COLORS  # (as tools/thps4_levels' sectors)


def scene_sectors(sectors, materials, checksums, skip):
    # THPS3's atomic sectors as the shared scene converter takes them (tools/level_port/geometry.convert_scene):
    # per material a strip (the bin mesh's, or its triangle list joined), invisible materials left out, colours
    # made THUG2's (BGRA, 128 = full).
    out = []
    for sec in sectors:
        if sec["checksum"] in skip:
            continue
        meshes = []
        by_material = {}
        for material, a, b, c in sec["triangles"]:
            by_material.setdefault(material + sec["material_base"], []).append((a, b, c))
        bm = sec["binmesh"]
        if bm and bm["strips"]:
            parts = [(mesh["material"], list(mesh["indices"])) for mesh in bm["meshes"]]
        else:
            parts = [(material, tris_to_strip(tris)) for material, tris in sorted(by_material.items())]
        for material, strip in parts:
            m = materials[material]
            if m["ns"] and m["ns"]["flags"] & thps3.MAT_INVISIBLE:
                continue
            if len(strip) < 3:
                continue
            meshes.append({"flags": 0, "material": checksums[material], "strip": tuple(strip)})
        if not meshes:
            continue
        positions = sec["positions"]
        colors = sec.get("colors") or [(255, 255, 255, 255)] * len(positions)
        normals = usable_normals(sec)
        flags = SECTOR_FLAGS | (thug2_scene.SEC_NORMALS if normals else 0)
        lo, hi, center, radius = geometry.bounds(positions)
        s = {"checksum": sec["checksum"], "bone": -1, "flags": flags, "bbox": tuple(lo + hi),
             "sphere": (center[0], center[1], center[2], radius), "positions": positions,
             "colors": [(b >> 1, g >> 1, r >> 1, a >> 1) for r, g, b, a in colors],
             # (the first uv set only: THPS3's second one is never written - uninitialised memory, NaNs)
             "uv_sets": 1,
             "uvs": [uv[:2] for uv in sec["uvs"]] or [(0.0, 0.0)] * len(positions), "meshes": meshes}
        if normals:
            s["normals"] = normals
        out.append(s)
    return out


def frame_transform(frames, index):
    # A clump frame's matrix with its parents' as (rotation rows right / up / at, position): a point p maps to
    # p.x * right + p.y * up + p.z * at + position.
    def apply(frame, p):
        r = frame["rotation"]
        return tuple(p[0] * r[k] + p[1] * r[3 + k] + p[2] * r[6 + k] + frame["position"][k] for k in range(3))
    chain = []
    while 0 <= index < len(frames) and len(chain) <= len(frames):
        chain.append(frames[index])
        index = frames[index]["parent"]
    def transform(p):
        for frame in chain:
            p = apply(frame, p)
        return p
    return transform


def moving_sector(clump, level, first_material):
    # A game object's clump as one sector in the object's own space (THUG2's LevelObject: the model component clones
    # the sector of the node's name and draws it at the object's position and orientation - THUG2's own LevelObject
    # sectors are centred on their origin), its atomics' geometry through their frames (a Z-up model turned Y-up:
    # (x, z, -y)) put together. Returns (sector as thps3.read_bsp makes them, materials as object_sectors's) or None.
    positions, colors, uvs, triangles, strips, materials = [], [], [], [], [], []
    all_strips = True
    for n, atomic in enumerate(clump["atomics"]):
        if not 0 <= atomic["geometry"] < len(clump["geometries"]):
            continue
        g = clump["geometries"][atomic["geometry"]]
        if not g["positions"] or not g["triangles"]:
            continue
        to_object = frame_transform(clump["frames"], atomic["frame"])
        base_vertex = len(positions)
        base = first_material + len(materials)
        for i, m in enumerate(g["materials"]):
            materials.append((qb.checksum("%s_object_%08X_%d_%d" % (level, clump["checksum"], n, i)), m))
        positions += [to_object(p) for p in g["positions"]]
        colors += g["colors"] or [(255, 255, 255, 255)] * len(g["positions"])
        uvs += g["uvs"] or [(0.0, 0.0)] * len(g["positions"])
        triangles += [(base + m, a + base_vertex, b + base_vertex, c + base_vertex) for m, a, b, c in g["triangles"]]
        if g["binmesh"] and g["binmesh"]["strips"]:
            strips += [{"material": base + mesh["material"], "indices": [v + base_vertex for v in mesh["indices"]]}
                       for mesh in g["binmesh"]["meshes"]]
        else:
            all_strips = False
    if not triangles:
        return None
    sec = {"checksum": clump["checksum"], "material_base": 0, "positions": positions, "colors": colors,
           "uvs": [uv[:2] for uv in uvs], "uv_sets": 1, "triangles": triangles, "binmesh": None}
    if all_strips and strips:
        sec["binmesh"] = {"strips": True, "meshes": strips}
    return sec, materials


def object_sectors(clumps, node_array, names, level, first_material, moving=frozenset()):
    # The game objects there at the start (CreatedAtStart GameObjects / BouncyObjects without a model= of their own:
    # Foundry's fans, Tokyo's lamps, Skater Island's cannons...) as static scene sectors, no collision: each clump's
    # geometry through its frames (a Z-up model turned Y-up: (x, z, -y)), turned by the node's heading and moved to
    # its Position (z negated, as the world's geometry). The clumps of the nodes in `moving` (THUG2 LevelObjects:
    # moved by their scripts) are one sector each in their own space instead (moving_sector), there at the start or
    # not. Returns (sectors as thps3.read_bsp makes them, their materials as (checksum, THPS3 material), numbered from
    # first_material).
    by_name = {}
    for node in node_array:
        if isinstance(node, qb.Struct) and isinstance(node.get("Name"), qb.Name):
            by_name[node.get("Name").value] = node
    sectors, materials = [], []
    for clump in clumps:
        if clump["checksum"] in moving:
            made = moving_sector(clump, level, first_material + len(materials))
            if made:
                sectors.append(made[0])
                materials += made[1]
            continue
        node = by_name.get(clump["checksum"])
        if node is None or not node.has_flag("CreatedAtStart") or node.get("model") is not None or \
                nodes.class_of(node, names) not in ("gameobject", "bouncyobject"):
            continue
        position = node.get("Position")
        angles = node.get("Angles")
        if not (isinstance(position, tuple) and position[0] == "vector"):
            continue
        px, py, pz = position[1]
        heading = angles[1][1] if isinstance(angles, tuple) and angles[0] == "vector" else 0.0
        cos, sin = math.cos(heading), math.sin(heading)
        for n, atomic in enumerate(clump["atomics"]):
            if not 0 <= atomic["geometry"] < len(clump["geometries"]):
                continue
            g = clump["geometries"][atomic["geometry"]]
            if not g["positions"] or not g["triangles"]:
                continue
            to_object = frame_transform(clump["frames"], atomic["frame"])
            positions = []
            for p in g["positions"]:
                x, y, z = to_object(p)
                positions.append((x * cos + z * sin + px, y + py, -x * sin + z * cos - pz))
            base = first_material + len(materials)
            for i, m in enumerate(g["materials"]):
                materials.append((qb.checksum("%s_object_%08X_%d_%d" % (level, clump["checksum"], n, i)), m))
            sec = {"checksum": qb.checksum("%s_object_%08X_%d" % (level, clump["checksum"], n)),
                   "material_base": 0, "positions": positions, "colors": g["colors"] or None,
                   "uvs": g["uvs"], "uv_sets": g["uv_sets"],
                   "triangles": [(base + m, a, b, c) for m, a, b, c in g["triangles"]],
                   "binmesh": None}
            if g["binmesh"] and g["binmesh"]["strips"]:
                sec["binmesh"] = {"strips": True, "meshes": [{"material": base + mesh["material"],
                                                              "indices": mesh["indices"]}
                                                             for mesh in g["binmesh"]["meshes"]]}
            sectors.append(sec)
    return sectors, materials


FACE_KEEP = 0x1FFF  # THPS3's face flags have THUG2's meanings (0x80: the camera collides, as THUG2's)


def face_flags(flags):
    return flags & FACE_KEEP


def collision_objects(sectors, materials, skip):
    # Each atomic sector one collision object (its name the sector's): every face, its flags and its material's
    # terrain.
    out = []
    for sec in sectors:
        if sec["checksum"] in skip or not sec["triangles"]:
            continue
        faces = []
        for (material, a, b, c), flags in zip(sec["triangles"], sec["face_flags"]):
            m = materials[material + sec["material_base"]]
            terrain = m["ns"]["terrain"] if m["ns"] else 0
            faces.append((flags, terrain, a, b, c))
        colors = sec.get("colors") or [(255, 255, 255, 255)] * len(sec["positions"])
        lo = [min(v[k] for v in sec["positions"]) for k in range(3)]
        hi = [max(v[k] for v in sec["positions"]) for k in range(3)]
        out.append({"checksum": sec["checksum"], "flags": 0, "verts": sec["positions"],
                    "intensities": bytes(max(r, g, b) >> 1 for r, g, b, _ in colors), "faces": faces,
                    "bbox": (lo[0], lo[1], lo[2], 1.0, hi[0], hi[1], hi[2], 1.0)})
    return {"objects": out}


def convert_world(world, tdx, name, out_dir, report, with_col=True, keep_triggers=lambda checksum: False,
                  skip=frozenset(), mips=True, objects=None):
    # objects: object_sectors' (sectors, materials) - drawn, not collided with.
    warnings = []
    textures = convert_textures(tdx, mips)
    known = {t["checksum"] for t in textures["textures"]}
    raw = list(world["materials"])
    checksums = [qb.checksum("%s_material_%d" % (name, i)) for i in range(len(raw))]
    object_list = []
    if objects:
        object_list = objects[0]
        raw += [m for _, m in objects[1]]
        checksums += [c for c, _ in objects[1]]
    materials = [to_thug2_material(checksums[i], m, known) for i, m in enumerate(raw)]
    for m in raw:
        info = (m["ns"] or {}).get("texture") or m["texture"]
        if info and texture_checksum(info["name"]) not in known:
            warnings.append("texture %s not in the dictionary - untextured" % info["name"])
    scene_in = {"sectors": scene_sectors(world["sectors"] + object_list, raw, checksums, skip)}
    used = {mesh["material"] for s in scene_in["sectors"] for mesh in s["meshes"]}
    scene = geometry.convert_scene(scene_in, warnings, [m for m in materials if m["checksum"] in used])
    col = None
    if with_col:
        col = geometry.convert_col(collision_objects(world["sectors"], world["materials"], skip), keep_triggers,
                                   warnings, face_flags)
    common.write_level(out_dir, name, scene, textures, col)
    common.report_level(report, name, scene, textures, col, sorted(set(warnings)))


# --- nodes ---------------------------------------------------------------------------------------------------------

def script_names(thps3_dir):
    # The game's global scripts (Data\Scripts\*.qb): the level scripts that kill the skater are among them.
    out = []
    for path in sorted(glob.glob(os.path.join(thps3_dir, "Data", "Scripts", "*.qb"))):
        try:
            out.append(qb.QbFile(read(path), path))
        except FormatError:
            pass
    return out


def name_value(payload):
    return struct.unpack("<I", payload)[0]


# Per level (folder, lower case): sector name prefixes left out. Warehouse: its lights flicker in THPS3
# (JKU_Ware_RoomFlicker toggles SkWare_Light_* / SkWare_Dark_*, the same surfaces lit and dark); without that script
# both sets were drawn - z-fighting. Kept lit, as THPS3's observer mode (JKU_Ware_IsObserver kills SkWare_Dark).
LEFT_OUT_PREFIXES = {"ware": ("skware_dark",)}


def created_at_start(q, global_qbs, names_all, base, start_scripts):
    # What THPS3's scripts create when a level starts (Create name= / prefix=), followed statically from its start
    # scripts through the scripts they call or spawn (both branches of every if): Load_<base> (its <base>_Startup),
    # the level's once_on_startup, the Player1 restart's trigger script, and the start scripts of the goal /
    # competition parameters StartRunScript gives this level (Goal_Start_Script... - not the goal complete ones).
    # Airport's areas, the tutorials' rooms and the like are made there; a press's other position, an open door or
    # LA's earthquake rubble are not.
    scripts = {}
    structs = {}
    for g in global_qbs + [q]:
        scripts.update(g.scripts)
        for key, value in g.globals:
            if isinstance(value, qb.Struct):
                structs[key.value] = value
    c = qb.checksum
    entries = list(start_scripts) + [c("Load_" + base)]
    # StartRunScript's branch for this level: `if CareerLevelIs LevelNum_X` ... GameStart / CompIntroSequence <params>
    level_num = None
    for command, args in statements_of(scripts.get(c("Load_" + base), [])):
        if command == c("CareerStartLevel") and c("level") in args:
            level_num = args[c("level")]
    in_branch = False
    for line in scripts.get(c("StartRunScript"), []):
        tokens = [(t, p) for t, p in line]
        if any(t == 0x16 and name_value(p) == c("CareerLevelIs") for t, p in tokens):
            in_branch = any(t == 0x16 and name_value(p) == level_num for t, p in tokens)
            continue
        if in_branch:
            for t, p in tokens:
                if t == 0x16 and name_value(p) in structs:
                    for key, value in structs[name_value(p)].entries:
                        text = names_all.get(key.value, "") if key is not None else ""
                        if isinstance(value, qb.Name) and "start_script" in text.lower() and \
                                "complete" not in text.lower() and "scripted" not in text.lower():
                            entries.append(value.value)
    names, prefixes, seen = set(), [], set()
    todo = [e for e in entries if e is not None]
    while todo:
        script = todo.pop()
        if script in seen or script not in scripts:
            continue
        seen.add(script)
        for line in scripts[script]:
            if not line or line[0][0] != 0x16:
                continue
            command = name_value(line[0][1])
            if command in scripts:
                todo.append(command)
            if command in (c("SpawnScript"), c("RunScriptOnObject")) and len(line) > 1 and line[1][0] == 0x16:
                todo.append(name_value(line[1][1]))
            if command == c("Create"):
                for i in range(1, len(line) - 2):
                    if line[i][0] == 0x16 and line[i + 1][0] == 0x07:
                        key, (vt, vp) = name_value(line[i][1]), line[i + 2]
                        if key == c("Name") and vt == 0x16:
                            names.add(name_value(vp))
                        elif key == c("prefix") and vt in (0x1B, 0x1C):
                            prefixes.append(vp.rstrip(b"\0").decode("latin-1").lower())
    return names, prefixes


def statements_of(lines):
    out = []
    for line in lines:
        if not line or line[0][0] != 0x16:
            continue
        args = {}
        for i in range(1, len(line) - 2):
            if line[i][0] == 0x16 and line[i + 1][0] == 0x07 and line[i + 2][0] == 0x16:
                args[name_value(line[i][1])] = name_value(line[i + 2][1])
        out.append((name_value(line[0][1]), args))
    return out


class Port:
    # What bringing a level's scripts along needs: THUG2's commands (script_port.Thug2), THPS3's script files
    # (script_port.Source), levels.qb (the levels' load scripts), and per level its own .qb (Source) and the names of
    # its .dff's clumps.
    def __init__(self, thug2, sources, levels_q, thps3_dir):
        self.thug2, self.sources, self.levels_q = thug2, sources, levels_q
        self.models_dir = os.path.join(thps3_dir, "Data", "Models")
        self.level = None
        self.clumps = set()
        self.moving = set()
        self.vehicles = {}   # node name -> model name (the level's vehicles made LevelObjects of their models)
        self.drawn = set()   # the level's atomic sectors with a scene sector (drawn_sectors)
        self.copies = {}     # node name -> clump checksum (game objects drawn with another clump of the level: model=)
        self._models = {}
        self.assets = peds_cars.Thug2Assets(thug2.root)

    def model(self, name):
        # THPS3's Data\Models\<name>\<name>.dff (a rigid clump: cars, trains, planes) and its .tdx, or None (skinned
        # models - .skn: peds, birds - aren't converted).
        key = name.lower()
        if key not in self._models:
            found = None
            folder = find_folder(self.models_dir, name) if os.path.isdir(self.models_dir) else None
            files = level_files(folder, name) if folder else {}
            if "dff" in files and "tdx" in files:
                try:
                    clumps = thps3.read_dff(read(files["dff"]), files["dff"])
                except FormatError:  # (Barrier.dff: a chunk the reader doesn't take - left out)
                    clumps = None
                if clumps:
                    found = (clumps[0], thps3.read_tdx(read(files["tdx"]), files["tdx"]))
            self._models[key] = found
        return self._models[key]


def _key(names, text):
    value = qb.checksum(text)
    names.setdefault(value, text)
    return qb.Name(value)


class PortedNodes:
    # THPS3's game objects and waypoints made THUG2's nodes (kept whole by nodes.convert_nodes):
    #   a GameObject with a clump of its name in the .dff (fans, presses, valve wheels, buckets): a LevelObject - the
    #       clump is a sector of that name in the scene (moving_sector), moved by the object's scripts;
    #   a GameObject with neither a clump nor a model (a script's controller: Foundry's Obj_Bucket_Controller): a
    #       ghost (THUG2's Type=Ghost model="none" game objects) running its script;
    #   a GameObject with a model (goal letters, stat points, tapes: THPS3's goal models): left out;
    #   a Waypoint: THUG2's, its Links to waypoints kept (Obj_MoveToNode / Obj_FollowPathLinked targets).
    # Their trigger scripts are added once known to be brought along (port_scripts).
    def __init__(self, names, array, port=None):
        self.names = names
        self.array = array
        self.port = port
        # the waypoints the peds walk (from their Links): THUG2's ped AI waypoints too
        self.ped_paths = set()
        for node in array:
            if isinstance(node, qb.Struct) and nodes.class_of(node, names) == "pedestrian":
                for link in self._links_to_waypoints(node) or []:
                    self.ped_paths.update(peds_cars.follow(array, link, names))
        self.ids = set()
        self.triggers = []   # (node name, script, context)
        self.scripts = set()
        self.moving = set()  # the LevelObjects' names (their clumps become sectors in their own space)
        self.animals = set()  # THUG2 animal skeletons used (their animations loaded: load_level_anims)
        self.proxims = []    # (node name, THPS3 script, radius) - proxim_scripts_ns
        self.counts = {}

    def _count(self, what):
        self.counts[what] = self.counts.get(what, 0) + 1

    def _links_to_waypoints(self, node):
        links = node.get("Links")
        if not isinstance(links, list):
            return None
        out = [v for v in links if isinstance(v, int) and 0 <= v < len(self.array) and
               isinstance(self.array[v], qb.Struct) and nodes.class_of(self.array[v], self.names) == "waypoint"]
        return sorted(set(out), key=out.index) or None

    def _base(self, node, cls):
        n = self.names
        entries = [(_key(n, "Position"), node.get("Position")), (_key(n, "Angles"), node.get("Angles")
                                                                 or ("vector", (0.0, 0.0, 0.0))),
                   (_key(n, "Name"), node.get("Name")), (_key(n, "Class"), _key(n, cls))]
        return entries

    def _made(self, entries, node, context):
        s = qb.Struct(entries)
        self.ids.add(id(s))
        script = node.get("TriggerScript")
        if isinstance(script, qb.Name) and isinstance(node.get("Name"), qb.Name):
            self.triggers.append((node.get("Name").value, script.value, context))
            self.scripts.add(script.value)
        return s

    def _flags(self, node):
        return [(None, _key(self.names, f)) for f in ("CreatedAtStart", "AbsentInNetGames") if node.has_flag(f)]

    def _level_object(self, node, what):
        # A LevelObject (moved by its scripts; drawn with the sector of its name - moving_sector).
        n = self.names
        self.moving.add(node.get("Name").value)
        self._count(what)
        links = self._links_to_waypoints(node)
        return self._made(self._base(node, "LevelObject") + [(_key(n, "Type"), _key(n, "Normal"))] +
                          self._flags(node) + [(_key(n, "SuspendDistance"), 0), (_key(n, "lod_dist1"), 10000),
                                               (_key(n, "lod_dist2"), 10001)] +
                          ([(_key(n, "Links"), links)] if links else []), node, script_port.OBJECT)

    def _left_out(self, what):
        self._count(what)
        return qb.Struct([(_key(self.names, "Class"), _key(self.names, "LeftOut"))])

    def _model_object(self, node, clumps, what):
        # A game / bouncy object drawn with a model: a clump of the level's .dff (model=<name>: Cruise Ship's
        # sailboats, deck chairs...) or a rigid model of Data\Models (model="police_car") - a LevelObject with a copy
        # of the geometry; THPS3's goal models (letters, stat points, tapes, deck icons) are left out. None: no model.
        model = node.get("model")
        name = node.get("Name").value
        if isinstance(model, qb.Name) and model.value in clumps and self.port is not None:
            self.port.copies[name] = model.value
            return self._level_object(node, what)
        if isinstance(model, str):
            if model.lower().startswith(GOAL_MODELS):
                return self._left_out("goal objects left out (%s...)" % "/".join(GOAL_MODELS))
            if self.port is not None and self.port.model(model):
                self.port.vehicles[name] = model
                return self._level_object(node, what)
        return self._left_out("objects whose model isn't converted left out")

    def convert(self, node, clumps):
        n = self.names
        cls = nodes.class_of(node, n)
        name = node.get("Name")
        if not isinstance(name, qb.Name) or not isinstance(node.get("Position"), tuple):
            return None
        if cls in ("gameobject", "bouncyobject"):
            if node.get("model") is not None:
                return self._model_object(node, clumps, "moving objects (models)")
            if name.value in clumps:
                return self._level_object(node, "moving objects")
            if cls == "bouncyobject":
                # (no geometry: nothing to show)
                return None
            links = self._links_to_waypoints(node)
            tail = [(_key(n, "Links"), links)] if links else []
            self._count("script objects")
            return self._made(self._base(node, "GameObject") + [(_key(n, "Type"), _key(n, "Ghost"))] +
                              self._flags(node) + [
                (None, _key(n, "NeverSuspend")), (_key(n, "model"), "none"), (_key(n, "lod_dist1"), 400),
                (_key(n, "lod_dist2"), 401)] + tail, node, script_port.OBJECT)
        if cls == "vehicle":
            # THPS3's traffic (a trigger script calling Car_Generic01, as THUG2's): THUG2's own cars on the same
            # waypoints (car).
            if self.calls_car_generic(node):
                return self.car(node)
            # THPS3's other vehicles (Tokyo's bullet trains and planes, Cruise Ship's boats, the snowmobiles) are path
            # followers moved by their scripts (Obj_FollowPathLinked...): LevelObjects of their models' geometry.
            model = node.get("model")
            if not (isinstance(model, str) and self.port is not None and self.port.model(model)):
                return self._left_out("vehicles without a rigid model left out")
            self.port.vehicles[name.value] = model
            return self._level_object(node, "vehicles")
        if cls == "animatingobject":
            return self.animating(node)
        if cls == "proximnode":
            return self.proxim(node)
        if cls == "waypoint":
            links = self._links_to_waypoints(node)
            flags = [(None, _key(n, "CreatedAtStart"))] if node.has_flag("CreatedAtStart") else []
            self._count("waypoints")
            ped = []
            if self.index_of(node) in self.ped_paths:
                # (as THUG2's LA's ped paths; THPS3's objects moved along the same waypoints don't mind)
                ped = [(_key(n, "Type"), _key(n, "PedAI")), (None, _key(n, "Continue")),
                       (_key(n, "PedType"), _key(n, "Walk")), (_key(n, "Priority"), _key(n, "Normal")),
                       (_key(n, "ContinueWeight"), 1.0)]
            s = qb.Struct(self._base(node, "Waypoint") + flags + ped + ([(_key(n, "Links"), links)] if links else []))
            self.ids.add(id(s))
            return s
        if cls in ("pedestrian", "pedestrian_pro"):
            return self.pedestrian(node)
        return None

    def index_of(self, node):
        if not hasattr(self, "_index"):
            self._index = {id(x): k for k, x in enumerate(self.array)}
        return self._index.get(id(node))

    def profile(self, model):
        # A THUG2 ped profile like THPS3's model: its pro, a worker, a guard, a woman, else any man.
        assets = self.port.assets if self.port is not None else None
        text = model.lower() if isinstance(model, str) else ""
        if text.startswith("pedpro_") and assets:
            for candidate in ("PedPro_%s" % model[7:], "PedPro_%s_skateboard" % model[7:]):
                if assets.profile_exists(candidate):
                    return candidate
        for part, profile in (("constr", "Ped_Construction_Worker_01"), ("welder", "Ped_Construction_Worker_01"),
                              ("security", "Ped_Security_Guard_01"), ("fem", FEMALE), ("girl", FEMALE),
                              ("steamer", FEMALE)):
            if part in text and (assets is None or assets.profile_exists(profile)):
                return profile
        return "random_male_profile"

    def pedestrian(self, node, model=None, script=None, what=None):
        # THPS3's peds (skinned models, animations and scripts THUG2 can't run) as THUG2's own ped AI at the same
        # place: walking the waypoints they link to (THPS3's walkers), else standing (the workers at their posts,
        # the photographers, the crowds...). There at the start as in THPS3; the others when a script creates them.
        # Women as THUG2's levels write them (Ped_F_From_Profile, the Ped_Female skeleton and animations).
        n = self.names
        links = self._links_to_waypoints(node) if script is None else None
        walking = bool(links)
        profile = self.profile(model or node.get("model") or node.get("Model"))
        female = profile == FEMALE
        entries = self._base(node, "Pedestrian") + [
            (_key(n, "Type"), _key(n, "Ped_F_From_Profile" if female else "Ped_From_Profile"))]
        entries += self._flags(node)
        entries += [(_key(n, "PedAI_Type"), _key(n, "Stand")),
                    (_key(n, "skeletonName"), _key(n, "Ped_Female" if female else "THPS6_human")),
                    (_key(n, "AnimName"), _key(n, "animload_ped_female" if female else "animload_THPS6_human")),
                    (_key(n, "profile"), _key(n, profile)),
                    (_key(n, "SuspendDistance"), 0), (_key(n, "lod_dist1"), 200), (_key(n, "lod_dist2"), 201),
                    (_key(n, "TriggerScript"), _key(n, script or (PED_WALK if walking else PED_STAND)))]
        if links:
            entries.append((_key(n, "Links"), links))
        self._count(what or ("walking peds" if walking else "standing peds"))
        s = qb.Struct(entries)
        self.ids.add(id(s))
        return s

    def calls_car_generic(self, node):
        # Whether the node's trigger script calls Car_Generic01 (THPS3's traffic script; THUG2 has its own of the name,
        # for its Vehicle objects - run by a LevelObject it crashes: no vehicle components).
        script = node.get("TriggerScript")
        if not isinstance(script, qb.Name) or self.port is None:
            return False
        car = qb.checksum("Car_Generic01")
        for source in [self.port.level] + list(self.port.sources):
            body = source.scripts.get(script.value)
            if body is not None:
                return any(t.t == 0x16 and t.name() == car for t in body)
        return False

    def car(self, node):
        # A THPS3 car as THUG2's level cars (THUG2's LA: Class=Vehicle, a THUG2 car model, skeletonName=car, running
        # THUG2's Car_Generic01 along the linked waypoints) - a THUG2 model like THPS3's (THUG2 can't draw THPS3's
        # models as a vehicle: skeleton). Only the ones there at the start: THPS3's spare cars (not CreatedAtStart)
        # are made by its car scripts, not brought along.
        n = self.names
        links = self._links_to_waypoints(node)
        if not node.has_flag("CreatedAtStart") or not links:
            return self._left_out("cars not there at the start left out")
        model = node.get("model") if isinstance(node.get("model"), str) else ""
        text = model.lower()
        name = n.get(node.get("Name").value, "")
        choices = next((c for parts, c in CAR_MODELS if any(p in text for p in parts)), CAR_MODELS[-1][1])
        assets = self.port.assets
        choices = [c for c in choices if assets.model_exists(c)] or [peds_cars.FALLBACK_CAR]
        chosen = choices[qb.checksum(name) % len(choices)]
        self._count("cars (THUG2's)")
        s = qb.Struct(self._base(node, "Vehicle") + [(_key(n, "Type"), _key(n, "Car"))] + self._flags(node) + [
            (_key(n, "model"), chosen), (_key(n, "skeletonName"), _key(n, "car")),
            (_key(n, "SuspendDistance"), 10000), (_key(n, "lod_dist1"), 400), (_key(n, "lod_dist2"), 401),
            (_key(n, "TriggerScript"), _key(n, CAR_BUS if "bus" in text else CAR)), (_key(n, "Links"), links)])
        self.ids.add(id(s))
        return s

    def animating(self, node):
        # THPS3's AnimatingObjects (skinned, animated by its own scripts): the crowds as THUG2's standing peds (a mix
        # of men and women, watching), the resting birds as THUG2's pigeons / seagulls (flying off when the skater
        # comes near and back again: ANIMAL_SCRIPTS), the sharks as THUG2's swimming their waypoints. THPS3's flying
        # birds (made by the resting ones' scripts) are left out.
        n = self.names
        kind = nodes.type_of(node, n)
        if kind.startswith("crowd"):
            model = "pedfem" if not kind.startswith("crowd_judge") and qb.checksum(
                n.get(node.get("Name").value, "")) & 1 else "crowd"
            return self.pedestrian(node, model=model, script=PED_STAND, what="crowds as standing peds")
        animal = ANIMALS.get(kind)
        if animal is None:
            return self._left_out("animated objects left out (%s)" % (kind if kind.startswith("bird") else "other"))
        skeleton, script = animal
        self.animals.add(skeleton)
        links = self._links_to_waypoints(node) if script == ANIMAL_SHARK else None
        if script == ANIMAL_SHARK and not links:
            return self._left_out("animated objects left out (shark without a path)")
        entries = self._base(node, "Pedestrian") + [(_key(n, "Type"), _key(n, skeleton))] + self._flags(node) + (
            [(None, _key(n, "NoPedLogic"))] if script == ANIMAL_SHARK else []) + [
            (_key(n, "PedAI_Type"), _key(n, "Stand")),
            (_key(n, "SkeletonName"), _key(n, skeleton)), (_key(n, "AnimName"), _key(n, "animload_" + skeleton)),
            (_key(n, "model"), "animals\\%s\\%s.skin" % (skeleton.lower(), skeleton.lower())),
            (_key(n, "SuspendDistance"), 0), (_key(n, "lod_dist1"), 400), (_key(n, "lod_dist2"), 401),
            (_key(n, "TriggerScript"), _key(n, script))] + ([(_key(n, "Links"), links)] if links else [])
        self._count("animals (%s)" % skeleton)
        s = qb.Struct(entries)
        self.ids.add(id(s))
        return s

    def proxim(self, node):
        # A THPS3 proximity node (a sphere round the node: its script runs with `inside` when the camera comes within
        # `radius`, without when it leaves - Airport's escalators swap static and moving steps, Canada's chimneys
        # smoke): a ghost game object of the same name (Create / Kill of it still work) whose own script watches the
        # radius (THUG2's ObjectInRadius / ObjectOutOfRadius exceptions) and spawns the THPS3 script
        # (proxim_scripts_ns, once the script is known to be brought along).
        n = self.names
        script = node.get("TriggerScript")
        radius = node.get("radius")
        if not isinstance(script, qb.Name) or not isinstance(radius, (int, float)):
            return self._left_out("proximity nodes without a script / radius left out")
        name = node.get("Name").value
        self.proxims.append((name, script.value, float(radius)))
        self.scripts.add(script.value)
        self._count("proximity nodes")
        s = qb.Struct(self._base(node, "GameObject") + [(_key(n, "Type"), _key(n, "Ghost"))] + self._flags(node) + [
            (None, _key(n, "NeverSuspend")), (_key(n, "model"), "none"), (_key(n, "lod_dist1"), 400),
            (_key(n, "lod_dist2"), 401)])
        self.ids.add(id(s))
        return s


# The made peds' trigger scripts (THUG2's ped AI), in every level's .qb.
PED_WALK, PED_STAND = "thps3_ped_walk", "thps3_ped_stand"
PED_SCRIPTS = "script %s {\n    Ped_InitAI \n}\nscript %s {\n    Ped_InitAI stand \n}\n" % (PED_WALK, PED_STAND)
FEMALE = "random_female_profile"
# THPS3's cars as THUG2's (the models THUG2's own levels drive with Car_Generic01): (parts of THPS3's model name, THUG2
# models - one picked by the car's name), the last the default. Their trigger scripts: THUG2's Car_Generic01, silent
# (THPS3's car sounds aren't converted).
CAR_MODELS = [
    (("police",), ["veh\\veh_cop\\veh_cop.mdl"]),
    (("van", "microbus", "icecream", "bus"), ["veh\\veh_van\\veh_van.mdl"]),
    (("pickup",), ["veh\\veh_rust\\veh_rust.mdl", "veh\\veh_van\\veh_van.mdl"]),
    ((), ["veh\\veh_sedan\\veh_sedan.mdl", "veh\\veh_galaxie\\veh_galaxie.mdl",
          "veh\\veh_chevy_impala\\veh_chevy_impala.mdl", "veh\\veh_custom\\veh_custom.mdl"]),
]
CAR, CAR_BUS = "thps3_car", "thps3_car_bus"
CAR_SCRIPTS = "script %s {\n    Car_Generic01 NoSound \n}\nscript %s {\n    Car_Generic01 NoSound Bus \n}\n" % (CAR, CAR_BUS)
# THPS3's goal models (career pickups): left out.
GOAL_MODELS = ("letter_", "stat_point", "deck_icon", "secret_tape")

# THPS3's animated animals as THUG2's (Data\models\animals; THUG2's AU / SE2 place them the same way): type ->
# (THUG2 skeleton, trigger script). The scripts are THUG2 AU's pigeons' (AU_Pigeon: fly up when the skater comes
# within 15 feet, down again once he's 20 away) and SE2's sharks' (SE2_Shark: swim the linked path), without their
# sounds.
ANIMAL_BIRD, ANIMAL_SHARK = "thps3_animal_bird", "thps3_animal_shark"
ANIMALS = {"bird_a_resting": ("Anl_Pigeon", ANIMAL_BIRD), "bird_b_resting": ("Anl_Seagull", ANIMAL_BIRD),
           "shark_a": ("Anl_Shark", ANIMAL_SHARK)}
ANIMAL_SCRIPTS = """script %(bird)s {
    Obj_ShadowOff
    Obj_ClearExceptions
    Obj_SetException Ex=ObjectInRadius Scr=%(bird)s_fly
    Obj_SetInnerRadius 15
    Obj_CycleAnim Anim=Idle
}
script %(bird)s_fly {
    Obj_ClearExceptions
    Obj_PlayAnim Anim=Fly Cycle
    Obj_MoveToRelPos (0.0, 250.0, 0.0) Speed=12 Acceleration=2
    Obj_WaitMove
    Obj_SetOuterRadius 20
    Obj_SetException Ex=ObjectOutOfRadius Scr=%(bird)s_return
}
script %(bird)s_return {
    Obj_ClearExceptions
    Obj_PlayAnim Anim=Fly Cycle
    Obj_MoveToRelPos (0.0, -250.0, 0.0) Speed=8 Acceleration=2
    Obj_WaitMove
    GoTo %(bird)s
}
script %(shark)s {
    Obj_ShadowOff
    Obj_StickToGround Off
    Obj_SetPathVelocity 6 mph
    Obj_SetPathTurnDist 10
    Obj_PathHeading On
    Obj_FollowPathLinked Orient
    Obj_PlayAnim Anim=Swim Cycle
}
""" % {"bird": ANIMAL_BIRD, "shark": ANIMAL_SHARK}


def load_anims_ns(animals):
    # THUG2's load_level runs the level's load_level_anims (every THUG2 level has one): the peds' animations
    # (THPS6_human, Ped_Female) and the animals'.
    return "script load_level_anims {\n%s}\n" % "".join(
        "    animload_%s \n" % a for a in ["THPS6_human", "ped_female"] + sorted(animals))


def proxim_scripts_ns(name, script, radius, names):
    # A proximity node's ghost's scripts (PortedNodes.proxim): THPS3's radius is in inches, THUG2's Obj_Set...Radius
    # in feet; the way out 2 feet further (no flicker on the edge). THPS3's script is spawned (run by no object, as
    # THPS3 ran it), with `inside` on the way in.
    base = "thps3_proxim_%08X" % name
    inner = round(radius / 12.0, 2)
    script = qb.name_text(script, names)
    return """script %(b)s {
    Obj_ClearExceptions
    Obj_SetInnerRadius %(r)s
    Obj_SetException Ex=ObjectInRadius Scr=%(b)s_in
}
script %(b)s_in {
    Obj_ClearExceptions
    SpawnScript %(s)s params={inside}
    Obj_SetOuterRadius %(o)s
    Obj_SetException Ex=ObjectOutOfRadius Scr=%(b)s_out
}
script %(b)s_out {
    Obj_ClearExceptions
    SpawnScript %(s)s
    GoTo %(b)s
}
""" % {"b": base, "r": inner, "o": inner + 2.0, "s": script}, base


def port_scripts(q, port, converted, array, kills, names, level, base, player1, made, undrawn=()):
    # The level's scripts brought along (tools/level_port/script_port.py) from its nodes' trigger scripts - the
    # sectors' and rails' (run by the skater), the made objects' (run by them) - and its start scripts: the scripts
    # THPS3's Load_<base> calls (Foun_Startup...) and the Player1 restart's trigger script (THPS3 runs it when the
    # skater starts: Foundry's sets the press up, makes the bucket). Those run from a setup script THUG2's load_level
    # calls (the level structure's setup_script). Trigger scripts that aren't brought along are taken off their nodes
    # (THUG2 runs a TriggerScript without checking it exists). Returns ({name: bytes}, the setup script as
    # NeverScript text or None, the THPS3 globals the scripts use as NeverScript text, a report line).
    sources = list(port.sources) + [port.level]
    allowed = set(script_port.DEFAULT_SOURCES) | {"level"}
    all_names = {n.get("Name").value for n in array if isinstance(n, qb.Struct) and isinstance(n.get("Name"), qb.Name)}
    node_names = {n.get("Name").value for n in converted if isinstance(n.get("Name"), qb.Name)}
    # (the kill scripts and LoadTerrain are the node array text's own - nodes.node_array_ns)
    provided = set(kills) | {qb.checksum(s) for s in ("LoadTerrain", PED_WALK, PED_STAND, ANIMAL_BIRD, ANIMAL_SHARK, CAR, CAR_BUS)}
    tr = script_port.Translator(sources, port.thug2, lambda label: label in allowed, node_names, all_names, names,
                                provided=provided, inherit_params=True, undrawn=undrawn)
    roots = []
    for node in converted:
        script = node.get("TriggerScript")
        if isinstance(script, qb.Name) and script.value not in kills and \
                nodes.class_of(node, names) in ("levelgeometry", "railnode"):
            roots.append((script.value, script_port.SKATER))
    roots += [(script, context) for _, script, context in made.triggers]
    roots += [(script, script_port.NONE) for _, script, _ in made.proxims]
    starts = []
    for command, _ in statements(port.levels_q, qb.checksum("Load_" + base)):
        if tr.can_bring(command):
            starts.append(command)
    if player1 is not None and isinstance(player1.get("TriggerScript"), qb.Name):
        starts.append(player1.get("TriggerScript").value)
    roots += [(s, script_port.NONE) for s in starts]
    out = tr.run(roots)

    def available(script):
        return script in out or script in port.thug2.scripts or script in provided

    trigger = qb.checksum("TriggerScript")
    dropped = 0
    for node in converted:
        script = node.get("TriggerScript")
        if isinstance(script, qb.Name) and not available(script.value):
            node.remove("TriggerScript")
            dropped += 1
    by_name = {n.get("Name").value: n for n in converted if isinstance(n.get("Name"), qb.Name)}
    for node_name, script, _ in made.triggers:
        if node_name in by_name and available(script):
            by_name[node_name].entries.append((qb.Name(trigger), qb.Name(script)))
    # the proximity nodes' ghosts: watching their radius when their THPS3 script is brought along and does something
    proxim_text, proxims = "", 0
    for node_name, script, radius in made.proxims:
        if node_name in by_name and script in out and script_port.does_something(out[script]):
            text, wrapper = proxim_scripts_ns(node_name, script, radius, names)
            proxim_text += text
            by_name[node_name].entries.append((qb.Name(trigger), _key(names, wrapper)))
            proxims += 1
    setup = None
    run = [s for s in starts if s in out]
    if run:
        setup = "script %s_setup {\n%s}\n" % (level, "".join("    SpawnScript %s \n" % qb.name_text(s, names)
                                                             for s in run))
    worst = sorted(tr.left_out.items(), key=lambda kv: -kv[1])
    report = "scripts: %d brought along (%d bytes), %d trigger scripts not; %d proximity nodes watching; made %s; "         "left out %d statements: %s" % (
            len(out), sum(map(len, out.values())), dropped, proxims, made.counts, sum(tr.left_out.values()),
            ", ".join("%s x%d" % kv for kv in worst[:12]))
    return out, setup, tr.globals_ns() + proxim_text, report


def convert_node_array(q, global_qbs, name, base, start_scripts, port=None):
    # port: a Port (the level's scripts brought along, its game objects, waypoints, peds...) or None (static: only
    # the geometry's nodes, restarts, rails and kill zones). Returns (NeverScript text, sectors left out, sectors
    # keeping their trigger faces, report, the brought scripts {name: THUG2 bytes}, whether there's a setup script).
    later = set()
    array = q.get("NodeArray")
    if not isinstance(array, list):
        raise FormatError("%s: no NodeArray" % q.name)
    names = {}
    for g in global_qbs:
        names.update(g.names)
    names.update(q.names)
    environment = qb.checksum("EnvironmentObject")
    # Environment objects not there at the start and not made by the level's start scripts (a press's other
    # position, a door open, LA's earthquake rubble: made by goal scripts THUG2 doesn't have) are left out, sectors
    # and nodes.
    player1 = next((n for n in array if isinstance(n, qb.Struct) and nodes.class_of(n, names) == "restart"
                    and isinstance(n.get("Type"), qb.Name) and n.get("Type").value == qb.checksum("Player1")), None)
    starts = list(start_scripts)
    if player1 is not None and isinstance(player1.get("TriggerScript"), qb.Name):
        starts.append(player1.get("TriggerScript").value)
    # The ProximNodes' trigger scripts too: THPS3 swaps level-of-detail with them - Airport's escalators: outside, the
    # static escalator (Create name=Escalator01, not CreatedAtStart); inside, moving game objects (the steps, not
    # converted). Left out, the static escalators were holes.
    proxim = qb.checksum("ProximNode")
    starts += [n.get("TriggerScript").value for n in array if isinstance(n, qb.Struct) and
               isinstance(n.get("Class"), qb.Name) and n.get("Class").value == proxim and
               isinstance(n.get("TriggerScript"), qb.Name)]
    created, prefixes = created_at_start(q, global_qbs, names, base, starts)
    hidden = set()
    # Sector sets a level's scripts keep switching between, left out (LEFT_OUT_PREFIXES): with both drawn they
    # fight over the same surfaces (flicker).
    for node in array:
        if isinstance(node, qb.Struct) and isinstance(node.get("Name"), qb.Name):
            text = names.get(node.get("Name").value, "").lower()
            if any(text.startswith(p) for p in LEFT_OUT_PREFIXES.get(base.lower(), ())):
                hidden.add(node.get("Name").value)
    for node in array:
        if isinstance(node, qb.Struct) and isinstance(node.get("Class"), qb.Name) and \
                node.get("Class").value == environment and isinstance(node.get("Name"), qb.Name) and \
                not node.has_flag("CreatedAtStart"):
            checksum = node.get("Name").value
            text = names.get(checksum, "").lower()
            if checksum in created or (text and any(text.startswith(p) for p in prefixes)):
                continue
            if port is None:
                hidden.add(checksum)
            else:
                # (kept, not there at the start: the level's scripts create them - a press's other position)
                later.add(checksum)
    # (every node keeps its place: the Links are indices into the array - nodes.convert_nodes renumbers them; one
    # left out here is a placeholder of a class it drops)
    kept = []
    made = PortedNodes(names, array, port) if port is not None else None
    for node in array:
        if not isinstance(node, qb.Struct) or (isinstance(node.get("Name"), qb.Name) and
                                               node.get("Name").value in hidden):
            kept.append(qb.Struct([(qb.Name(qb.checksum("Class")), qb.Name(qb.checksum("LeftOut")))]))
            continue
        if made is not None:
            new = made.convert(node, port.clumps)
            if new is not None:
                kept.append(new)
                continue
        if isinstance(node.get("Class"), qb.Name) and node.get("Class").value == environment:
            # (THPS3's sector nodes: THUG2's LevelGeometry)
            node = qb.Struct(list(node.entries))
            node.set("Class", qb.Name(qb.checksum("LevelGeometry")))
            names.setdefault(qb.checksum("LevelGeometry"), "LevelGeometry")
            if not node.has_flag("CreatedAtStart") and node.get("Name").value not in later:
                # (made by the level's start scripts)
                node.entries.append((None, qb.Name(qb.checksum("CreatedAtStart"))))
                names.setdefault(qb.checksum("CreatedAtStart"), "CreatedAtStart")
        links = node.get("Links")
        if links is not None and nodes.class_of(node, names) != "railnode":
            # (sector nodes link to restarts and each other for THPS3's scripts: nothing for THUG2 - its node
            # loader follows non-rail links as paths)
            node = qb.Struct(list(node.entries))
            node.remove("Links")
            links = None
        if isinstance(links, list) and len(set(links)) != len(links):
            # (Foundry's TRG_117 has Links=[70, 70]: once is enough)
            node = qb.Struct(list(node.entries))
            node.set("Links", sorted(set(links), key=links.index))
        kept.append(node)
    restarts = {n.get("Name").value for n in kept if nodes.class_of(n, names) == "restart"
                and isinstance(n.get("Name"), qb.Name)}
    kills = nodes.kill_commands([q] + global_qbs, restarts, names)
    wanted = set()
    if port is not None:
        # the trigger scripts of the sectors and rails (run by the skater) and of the made nodes
        for node in kept:
            script = node.get("TriggerScript")
            if isinstance(script, qb.Name) and script.value not in kills and \
                    nodes.class_of(node, names) in ("levelgeometry", "railnode"):
                wanted.add(script.value)
        wanted |= set(made.scripts)
    converted, stats = nodes.convert_nodes(kept, names, kills, as_is=made.ids if made else (), scripts=wanted)
    kills = nodes.add_kill_restarts(converted, kills, names)
    scripts, setup, globals_text, script_report = {}, None, "", ""
    if port is not None:
        port.moving = made.moving
        # the geometry nodes the scene has no sector for (collision-only, or left out): their Visible / Kill...
        # crash THUG2 (script_port Translator.without_undrawn)
        undrawn = {n.get("Name").value for n in array if isinstance(n, qb.Struct) and
                   isinstance(n.get("Class"), qb.Name) and n.get("Class").value == environment and
                   isinstance(n.get("Name"), qb.Name) and (n.get("Name").value not in port.drawn or
                                                          n.get("Name").value in hidden)}
        scripts, setup, globals_text, script_report = port_scripts(q, port, converted, array, kills, names, name,
                                                                   base, player1, made, undrawn)
    triggers = nodes.kill_trigger_names(converted, kills)
    if port is not None:
        # (the sectors whose trigger script runs keep their trigger faces)
        triggers |= {n.get("Name").value for n in converted if isinstance(n.get("TriggerScript"), qb.Name)
                     and isinstance(n.get("Name"), qb.Name) and nodes.class_of(n, names) == "levelgeometry"}
    text = nodes.node_array_ns(name + "_NodeArray", converted, names, kills)
    if port is not None:
        text += globals_text + (setup or "") + PED_SCRIPTS + ANIMAL_SCRIPTS + CAR_SCRIPTS + load_anims_ns(made.animals)
    report = "nodes: %d of %d kept %s; %d kill zones, %d sectors not there at the start" % (
        len(converted), len(array), stats, len(triggers), len(hidden) + len(later))
    if script_report:
        report += "\n  " + script_report
    return text, hidden, triggers, report, scripts, setup is not None


def write_level_qb(ns, text, path, scripts, port):
    # The node array's NeverScript text compiled, the brought scripts added (script_port.append_scripts).
    common.ns_compile(ns, text, path)
    if scripts:
        names = {}
        for s in list(port.sources) + [port.level]:
            names.update(s.names)
        data = script_port.append_scripts(open(path, "rb").read(), scripts, names)
        open(path, "wb").write(data)


# --- level structures ----------------------------------------------------------------------------------------------

def statements(q, script):
    # A script's statements as (command name, {key: value}) with the simple values (numbers, strings, names).
    out = []
    for line in q.scripts.get(script, []):
        if not line or line[0][0] != 0x16:
            continue
        command = struct.unpack("<I", line[0][1])[0]
        args = {}
        i = 1
        while i < len(line):
            t, payload = line[i]
            if t == 0x16 and i + 2 < len(line) and line[i + 1][0] == 0x07:
                vt, vp = line[i + 2]
                key = struct.unpack("<I", payload)[0]
                if vt in (0x17, 0x18):
                    args[key] = struct.unpack("<i", vp)[0]
                elif vt == 0x1A:
                    args[key] = struct.unpack("<f", vp)[0]
                elif vt in (0x1B, 0x1C):
                    args[key] = vp.rstrip(b"\0").decode("latin-1")
                elif vt == 0x16:
                    args[key] = struct.unpack("<I", vp)[0]
                i += 3
                continue
            i += 1
        out.append((command, args))
    return out


def load_script_info(levels_q, base):
    # From THPS3's Load_<base> (levels.qb): the sky (the single-player branch's loadlevelgeometry Sky=) and the
    # lights script it calls (SetUp...Lights).
    sky, lights = None, None
    c = qb.checksum
    for command, args in statements(levels_q, c("Load_" + base)):
        if command == c("loadlevelgeometry"):
            value = args.get(c("Sky"))
            if isinstance(value, str) and value:
                sky = os.path.splitext(os.path.basename(value.replace("\\", "/")))[0]
        elif levels_q.names.get(command, "").lower().startswith("setup") and \
                levels_q.names.get(command, "").lower().endswith("lights"):
            lights = command
    return sky, lights


def wrap_degrees(value):
    return (value + 180.0) % 360.0 - 180.0


def lighting(lights_q, script):
    # THPS3's light set-up (SetDFFAmbientColor, SetDFFDirectColor / Rotation for lights 0 and 1,
    # SetDynamicLightModulationFactor) in THUG2's level structure keys. THPS3's pitch is down from the horizon
    # (90 = straight down); THUG2's the other way (THPS4's levels have -127 ...).
    out = {}
    if not lights_q or script is None:
        return out
    c = qb.checksum
    for command, args in statements(lights_q, script):
        index = args.get(c("lightIndex"))
        if command == c("SetDFFAmbientColor"):
            pass
        if command == c("SetDFFDirectColor") and index in (0, 1):
            for key in ("r", "g", "b"):
                if isinstance(args.get(c(key)), int):
                    out["%s_%d" % ({"r": "red", "g": "green", "b": "blue"}[key], index)] = args[c(key)]
        elif command == c("SetDFFDirectRotation") and index in (0, 1):
            if isinstance(args.get(c("pitch")), (int, float)):
                out["pitch_%d" % index] = int(round(wrap_degrees(-float(args[c("pitch")]))))
            if isinstance(args.get(c("heading")), (int, float)):
                out["heading_%d" % index] = int(round(wrap_degrees(float(args[c("heading")]))))
        elif command == c("SetDynamicLightModulationFactor"):
            value = args.get(c("value"))
            if not isinstance(value, (int, float)):
                continue
            if c("directional") in args and args[c("directional")] in (0, 1):
                key = "mod_factor_%d" % args[c("directional")]
            elif c("directional") in args:
                continue
            else:
                key = "ambient_mod_factor"
            out[key + "_lo"] = out[key + "_hi"] = round(float(value), 4)
    # SetDFFAmbientColor's values are in a structure ({r= g= b=}): read from the statement's tokens.
    for line in lights_q.scripts.get(script, []):
        if line and line[0][0] == 0x16 and struct.unpack("<I", line[0][1])[0] == c("SetDFFAmbientColor"):
            values = {}
            for i in range(len(line) - 2):
                if line[i][0] == 0x16 and line[i + 1][0] == 0x07 and line[i + 2][0] == 0x17:
                    values[struct.unpack("<I", line[i][1])[0]] = struct.unpack("<i", line[i + 2][1])[0]
            for key, out_key in (("r", "ambient_red"), ("g", "ambient_green"), ("b", "ambient_blue")):
                if c(key) in values:
                    out[out_key] = values[c(key)]
    return out


# --- convert -------------------------------------------------------------------------------------------------------

def cmd_convert(args):
    levels_dir = os.path.join(args.thps3, "Data", "Levels")
    out = args.out or os.path.join(args.thug2, STAGING)
    if not os.path.exists(args.ns):
        sys.exit("%s not found: build it with tools\\build_neverscript.ps1" % args.ns)
    table = {row[0].lower(): row for row in LEVELS}
    wanted = args.levels or [row[0] for row in LEVELS if row[5]]
    scripts = os.path.join(args.thps3, "Data", "Scripts")
    levels_q = qb.QbFile(read(os.path.join(scripts, "levels.qb")), "levels.qb")
    lights_q = qb.QbFile(read(os.path.join(scripts, "lights.qb")), "lights.qb")
    global_qbs = script_names(args.thps3)
    port = None
    if not args.no_scripts:
        port = Port(script_port.Thug2(args.thug2),
                    [script_port.Source(read(p), os.path.basename(p).lower())
                     for p in sorted(glob.glob(os.path.join(scripts, "*.qb")))], levels_q, args.thps3)
    master = {}
    for value in levels_q.get("master_level_list") or []:
        if isinstance(value, qb.Struct) and isinstance(value.get("load_script"), qb.Name):
            master[value.get("load_script").value] = value
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    manifest = {"levels": []}
    report = []
    skies_done = set()
    for folder_name in wanted:
        row = table.get(folder_name.lower())
        folder = find_folder(levels_dir, folder_name)
        if not folder or not row:
            report.append("%s: not a THPS3 level folder in %s" % (folder_name, levels_dir))
            continue
        base, sign, location, icon, level_num, _ = row
        name = "thps3_" + base.lower()
        files = level_files(folder, base)
        for kind in ("bsp", "tdx", "qb"):
            if kind not in files:
                raise FormatError("%s: no .%s file" % (folder, kind))
        level_dir = os.path.join(out, "levels", name)
        os.makedirs(level_dir, exist_ok=True)

        # The node array first: the collision keeps the trigger faces of the kill zones it keeps, and the sectors
        # not there at the start are left out of both.
        q = qb.QbFile(read(files["qb"]), files["qb"])
        entry_struct = master.get(qb.checksum("Load_" + base))
        once = entry_struct.get("once_on_startup") if entry_struct else None
        clumps = thps3.read_dff(read(files["dff"]), files["dff"]) if "dff" in files and not args.no_objects else []
        world = thps3.read_bsp(read(files["bsp"]), files["bsp"])
        if port is not None:
            port.drawn = drawn_sectors(world)
            port.level = script_port.Source(read(files["qb"]), "level")
            port.clumps = {c["checksum"] for c in clumps}
            port.moving = set()
            port.vehicles = {}
            port.copies = {}
        text, hidden, triggers, node_report, scripts, has_setup = convert_node_array(
            q, global_qbs, name, base, [once.value] if isinstance(once, qb.Name) else [], port=port)
        qb_path = os.path.join(level_dir, name + ".qb")
        write_level_qb(args.ns, text, qb_path, scripts, port)
        tdx = thps3.read_tdx(read(files["tdx"]), files["tdx"])
        objects = None
        # (each vehicle its own copy of its model: a LevelObject draws the sector of its node's name)
        vehicles = []
        models_added = set()
        if port is not None:
            for node_name, model in sorted(port.vehicles.items()):
                clump, model_tdx = port.model(model)
                vehicles.append(dict(clump, checksum=node_name))
                if model.lower() not in models_added:
                    models_added.add(model.lower())
                    tdx = tdx + model_tdx
            by_checksum = {c["checksum"]: c for c in clumps}
            for node_name, source in sorted(port.copies.items()):
                vehicles.append(dict(by_checksum[source], checksum=node_name))
        if clumps or vehicles:
            names = dict(q.names)
            for g in global_qbs:
                names.update(g.names)
            objects = object_sectors(clumps + vehicles, q.get("NodeArray"), names, name, len(world["materials"]),
                                     moving=port.moving if port is not None else frozenset())
            report.append("  game objects: %d sectors (%d moving; no collision)" % (
                len(objects[0]), len(port.moving) if port is not None else 0))
        convert_world(world, tdx, name, level_dir, report, keep_triggers=lambda checksum: checksum in triggers,
                      skip=hidden, mips=not args.no_mips, objects=objects)
        text, cleared = without_missing_trick_objects(text, os.path.join(level_dir, name + ".scn.xbx"))
        if cleared:
            write_level_qb(args.ns, text, qb_path, scripts, port)
            report.append("  %d geometry nodes without a scene sector: TrickObject / Cluster dropped" % cleared)
        report.append("  " + node_report)

        # The sky (scene and textures, no collision - as THUG2's own skies).
        sky_base, lights_script = load_script_info(levels_q, base)
        sky = None
        if sky_base:
            sky_folder = find_folder(levels_dir, sky_base)
            sky_files = level_files(sky_folder, sky_base) if sky_folder else {}
            if "bsp" in sky_files and "tdx" in sky_files:
                sky = "thps3_" + sky_base.lower()
                if sky not in skies_done:
                    convert_world(thps3.read_bsp(read(sky_files["bsp"]), sky_files["bsp"]),
                                  thps3.read_tdx(read(sky_files["tdx"]), sky_files["tdx"]), sky,
                                  os.path.join(out, "levels", sky), report, with_col=False, mips=not args.no_mips)
                    skies_done.add(sky)
        display_name = entry_struct.get("level_name") if entry_struct and \
            isinstance(entry_struct.get("level_name"), str) else sign.title()
        manifest["levels"].append({
            "folder": base,
            "level": name,
            "sky": sky,
            "display": "THPS3 " + sign,
            "name": display_name,
            "game": "THPS3",
            "location": location,
            "icon": icon,
            "level_num": level_num,
            "default_stats": 6,
            "lighting": lighting(lights_q, lights_script),
        })
        if has_setup:
            manifest["levels"][-1]["setup_script"] = name + "_setup"
    ported_levels.write_manifest(os.path.join(out, ported_levels.MANIFEST), manifest)
    print("\n".join(report))
    print("wrote %s (%d levels); install with tools\\menu_level\\make_menu_level.py" % (out, len(manifest["levels"])))


# --- self-check ----------------------------------------------------------------------------------------------------

def cmd_selfcheck(args):
    pre = os.path.join(args.thug2, "Data", "pre")
    checked, failures = common.thug2_round_trips(
        pre, ("LAscn.prx", "LAcol.prx", "TestLevelscn.prx", "TestLevelcol.prx") + tuple(args.extra))
    levels_dir = os.path.join(args.thps3, "Data", "Levels")
    paths = sorted(glob.glob(os.path.join(levels_dir, "*", "*"))) + \
        sorted(glob.glob(os.path.join(args.thps3, "Data", "Scripts", "*.qb")))
    for path in paths:
        low = path.lower()
        try:
            data = read(path)
            if low.endswith(".bsp"):
                w = thps3.read_bsp(data, path)
                what = "%d materials, %d sectors, %d triangles" % (len(w["materials"]), len(w["sectors"]),
                                                                  w["triangles"])
            elif low.endswith(".dff"):
                clumps = thps3.read_dff(data, path)
                what = "%d clumps" % len(clumps)
            elif low.endswith(".tdx"):
                t = thps3.read_tdx(data, path)
                what = "%d textures" % len(t)
            elif low.endswith(".qb"):
                q = qb.QbFile(data, path)
                what = "%d globals, %d scripts" % (len(q.globals), len(q.scripts))
            else:
                continue
        except (FormatError, ValueError, IndexError, struct.error) as e:
            print("FAIL %s: %s" % (path, e))
            failures += 1
            continue
        checked += 1
        print("ok   %s: %s" % (os.path.relpath(path, args.thps3), what))
    print("%d files checked, %d failures" % (checked, failures))
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("convert", "selfcheck"))
    parser.add_argument("levels", nargs="*", help="THPS3 level folders (default: all but the park editor's)")
    parser.add_argument("--thug2", default=r"C:\Games\THUG2")
    parser.add_argument("--thps3", default=r"C:\Games\THPS3")
    parser.add_argument("--out", help="where convert writes (default: <thug2>\\%s)" % STAGING)
    parser.add_argument("--ns", default=common.NS)
    parser.add_argument("--no-objects", action="store_true", help="convert: the game objects (.dff) left out")
    parser.add_argument("--no-scripts", action="store_true",
                        help="convert: the level's scripts, moving objects and waypoints left out (static levels)")
    parser.add_argument("--no-mips", action="store_true", help="convert: no mip levels made (faster)")
    parser.add_argument("--extra", nargs="*", default=[], help="selfcheck: more THUG2 archives (Data\\pre\\*.prx)")
    args = parser.parse_args()
    if args.command == "selfcheck":
        sys.exit(cmd_selfcheck(args))
    cmd_convert(args)


if __name__ == "__main__":
    main()
