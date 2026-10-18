# THUG1 PC level files (in Data\pre\<LV>scn.pre / <LV>col.pre: levels\<lv>\<lv>.scn.xbx / .tex.xbx / .col.xbx).
#
# - Textures (.tex.xbx): THUG2's format (tools/level_port/tex.py).
# - Collision (.col.xbx, version 9): THUG2's version 10 without the faces' reaches (tools/level_port/thug2_col.py
#   reads both); converted by adding them (to_thug2_col).
# - Scene (.scn.xbx): THUG2's materials exactly; the sectors keep THPS4's layout - one vertex array per sector, the
#   meshes index into it:
#     u32 checksum, i32 bone, u32 flags, u32 mesh count, 6f box, 4f sphere, (billboard: u32 type, 9f),
#     u32 vertex count, u32 stride, then the arrays one after another: positions 3f; normals 3f; weights (u32 packed
#     11/11/10 bits) + 4 x u16 bone indices; uv sets {u32 count, then count x 2f per vertex}; colours BGRA (128 = full,
#     as THUG2's); colour wibble indices (a byte each),
#   then per mesh: 4f sphere, 6f box, u32 flags, u32 material checksum, u32 LOD count, per LOD {u32 count, u16 strip
#   indices into the sector's vertices} (every shipped mesh has one LOD).
#   The file ends with i32 0 (no hierarchy objects).

import thug2_col
import thug2_scene
from binio import Reader, FormatError

COL_VERSION = 9


def read_sector(r):
    s = {"checksum": r.u32(), "bone": r.i32(), "flags": r.u32()}
    meshes = r.u32()
    if meshes > 100000:
        raise FormatError("%s: %d meshes at 0x%X" % (r.name, meshes, r.offset))
    s["bbox"] = r.read("6f")
    s["sphere"] = r.read("4f")
    flags = s["flags"]
    if flags & thug2_scene.SEC_BILLBOARD:
        s["billboard"] = (r.u32(), r.read("9f"))
    count = r.u32()
    s["stride"] = r.u32()
    s["positions"] = [r.read("3f") for _ in range(count)]
    if flags & thug2_scene.SEC_NORMALS:
        s["normals"] = [r.read("3f") for _ in range(count)]
    if flags & thug2_scene.SEC_WEIGHTS:
        s["weights"] = r.read("%dI" % count)
        s["bones"] = [r.read("4H") for _ in range(count)]
    if flags & thug2_scene.SEC_TEXCOORDS:
        sets = r.u32()
        if sets > 8:
            raise FormatError("%s: sector %08X has %d uv sets" % (r.name, s["checksum"], sets))
        s["uv_sets"] = sets
        s["uvs"] = [r.read("%df" % (2 * sets)) for _ in range(count)]
    if flags & thug2_scene.SEC_COLORS:
        s["colors"] = [r.read("4B") for _ in range(count)]
    if flags & thug2_scene.SEC_COLOR_WIBBLES:
        s["wibbles"] = r.bytes(count)
    s["meshes"] = []
    for _ in range(meshes):
        mesh = {"sphere": r.read("4f"), "bbox": r.read("6f"), "flags": r.u32(), "material": r.u32()}
        lods = r.u32()
        if lods > 8:
            raise FormatError("%s: %d LODs at 0x%X" % (r.name, lods, r.offset))
        mesh["lods"] = [r.read("%dH" % r.u32()) for _ in range(lods)]
        for indices in mesh["lods"]:
            if indices and max(indices) >= count:
                raise FormatError("%s: sector %08X strip index past its %d vertices" % (r.name, s["checksum"], count))
        mesh["strip"] = mesh["lods"][0] if mesh["lods"] else ()
        s["meshes"].append(mesh)
    return s


def read_scene(data, name="scene"):
    r = Reader(data, name=name)
    scene = {"versions": r.read("3I")}
    scene["materials"] = [thug2_scene.read_material(r) for _ in range(r.u32())]
    scene["sectors"] = [read_sector(r) for _ in range(r.i32())]
    scene["hierarchy_count"] = r.i32()
    if scene["hierarchy_count"] != 0:
        raise FormatError("%s: %d hierarchy objects" % (name, scene["hierarchy_count"]))
    r.expect_end()
    return scene


def read_col(data, name="col"):
    return thug2_col.read_col(data, name, versions=(COL_VERSION,))


# --- to THUG2 ---------------------------------------------------------------------------------------------------

def check_tree(o, name):
    # THUG1's BSP trees are THUG2's (the same node words, sixteenths, leaves and face index lists): every face is in
    # a leaf. (Splits outside the object's box happen in both games' files: harmless.)
    seen = set()

    def walk(node):
        if node[0] == "leaf":
            seen.update(node[1])
            return
        if node[1] > 2:
            raise FormatError("%s: object %08X split on axis %d" % (name, o["checksum"], node[1]))
        walk(node[3])
        walk(node[4])

    walk(o["bsp"])
    if seen != set(range(len(o["faces"]))):
        raise FormatError("%s: object %08X: %d of %d faces in its tree" % (name, o["checksum"], len(seen),
                                                                          len(o["faces"])))


def to_thug2_col(col, keep_triggers, face_flags, name):
    # Version 9 -> 10: the same objects, vertices and trees; the reaches computed (thug2_col.set_reaches: THUG1 has
    # none, and zeros make the faces all but uncollidable). face_flags(flags): a face's flags for THUG2. Trigger
    # faces lose their flag unless keep_triggers(object checksum) (THUG2 runs a trigger face's node's TriggerScript
    # without checking it exists).
    objects = []
    for o in col["objects"]:
        check_tree(o, name)
        o = dict(o)
        keep = keep_triggers(o["checksum"])
        o["faces"] = [((face_flags(flags) if keep else face_flags(flags) & ~thug2_col.FACE_TRIGGER), terrain, a, b, c)
                      for flags, terrain, a, b, c in o["faces"]]
        thug2_col.set_reaches(o)
        objects.append(o)
    return {"objects": objects, "pad": col["pad"]}
