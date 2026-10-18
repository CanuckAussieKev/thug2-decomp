# THPS4 PC level files (Game\data\levels\<Lvl>\): <lvl>scn.dat (scene) and <lvl>col.dat (collision, version 8).
# The texture dictionary <lvl>tex.dat is THUG2's format already (tex.py).

from binio import Reader, FormatError

SEC_TEXCOORDS = 0x01
SEC_COLORS = 0x02
SEC_NORMALS = 0x04
SEC_WEIGHTS = 0x10
SEC_COLOR_WIBBLES = 0x800
SEC_BILLBOARD = 0x00800000

PASS_UV_WIBBLE = 1 << 0
PASS_VC_WIBBLE = 1 << 1
PASS_ANIMATES = 1 << 11


# --- scene ------------------------------------------------------------------------------------------------------
#   u32 material version, mesh version, vertex version; u32 material count, materials; i32 sector count, sectors.
# A material: u32 checksum, u32 pass count, u32 alpha cutoff, u8 sorted, f32 draw order, u8 single sided,
#   u8 grassify (+ f32 height, i32 layers), passes: u32 texture, u32 flags, u8 has colour, 3f colour, u32 blend mode,
#   u32 fixed alpha, u32 u / v addressing, u32 filtering (THUG2's has the env-map tiling 2f before it), 9f lighting
#   colours (diffuse / specular / ambient; THUG2 has none), then as THUG2: uv wibble, vc wibble, animation, mip info.
# A sector: u32 checksum, i32 bone, u32 flags, u32 mesh count, 6f box, 4f sphere, (billboard), u32 vertex count,
#   u32 stride, then the vertex arrays one after another (positions; normals; weights + bone indices; uv sets
#   {u32 count, then count x 2f per vertex}; colours BGRA; colour wibble indices), then the meshes:
#   u32 flags, u32 material checksum, u32 index count, u16 strip indices (into the sector's vertices).

def read_material(r):
    m = {"checksum": r.u32()}
    passes = r.u32()
    m["alpha_cutoff"] = r.u32()
    m["sorted"] = r.u8()
    m["draw_order"] = r.f32()
    m["single_sided"] = r.u8()
    m["grassify"] = r.u8()
    if m["grassify"]:
        m["grass"] = r.read("fi")
    m["passes"] = []
    for index in range(passes):
        p = {"texture": r.u32(), "flags": r.u32(), "has_color": r.u8(), "color": r.read("3f"),
             "blend_mode": r.u32(), "fixed_alpha": r.u32(), "u_address": r.u32(), "v_address": r.u32(),
             "filtering": r.u32(), "lighting": r.read("9f")}
        if p["flags"] & PASS_UV_WIBBLE:
            p["uv_wibble"] = r.read("8f")
        if index == 0 and p["flags"] & PASS_VC_WIBBLE:
            sequences = []
            for _ in range(r.u32()):
                keys = r.u32()
                phase = r.i32()
                sequences.append((phase, r.read("%di" % (keys * 2))))
            p["vc_wibble"] = sequences
        if p["flags"] & PASS_ANIMATES:
            keys, period, iterations, phase = r.read("4i")
            p["animation"] = (period, iterations, phase, [r.read("2I") for _ in range(keys)])
        p["mip"] = r.read("IIff")
        m["passes"].append(p)
    return m


def read_sector(r):
    s = {"checksum": r.u32(), "bone": r.i32(), "flags": r.u32()}
    meshes = r.u32()
    if meshes > 100000:
        raise FormatError("%s: %d meshes at 0x%X" % (r.name, meshes, r.offset))
    s["bbox"] = r.read("6f")
    s["sphere"] = r.read("4f")
    flags = s["flags"]
    if flags & SEC_BILLBOARD:
        s["billboard"] = (r.u32(), r.read("9f"))
    count = r.u32()
    s["stride"] = r.u32()
    s["positions"] = [r.read("3f") for _ in range(count)]
    if flags & SEC_NORMALS:
        s["normals"] = [r.read("3f") for _ in range(count)]
    if flags & SEC_WEIGHTS:
        s["weights"] = [r.read("4f") for _ in range(count)]
        s["bones"] = [r.read("4H") for _ in range(count)]
    if flags & SEC_TEXCOORDS:
        sets = r.u32()
        s["uv_sets"] = sets
        s["uvs"] = [r.read("%df" % (2 * sets)) for _ in range(count)]
    if flags & SEC_COLORS:
        s["colors"] = [r.read("4B") for _ in range(count)]
    if flags & SEC_COLOR_WIBBLES:
        s["wibbles"] = r.bytes(count)
    s["meshes"] = []
    for _ in range(meshes):
        mesh = {"flags": r.u32(), "material": r.u32()}
        mesh["strip"] = r.read("%dH" % r.u32())
        if mesh["strip"] and max(mesh["strip"]) >= count:
            raise FormatError("%s: sector %08X strip index past its %d vertices" % (r.name, s["checksum"], count))
        s["meshes"].append(mesh)
    return s


def read_scene(data, name="scene"):
    r = Reader(data, name=name)
    scene = {"versions": r.read("3I")}
    scene["materials"] = [read_material(r) for _ in range(r.u32())]
    scene["sectors"] = [read_sector(r) for _ in range(r.i32())]
    scene["tail"] = r.bytes(len(data) - r.offset)
    return scene


# --- collision, version 8 ---------------------------------------------------------------------------------------
#   header as THUG2's (version 8, object count, total vertices, large faces, small faces, large / small vertices, 0)
#   objects     64 bytes as THUG2's, but the first vertex is an index (not a byte offset), the intensity field 0
#   (aligned to 16)
#   vertices    3f + 4 intensity bytes each (16 bytes; THPS4 has no fixed-point vertices or intensity list)
#   faces       u16 flags, u16 terrain, 3 x u16 vertex, u16 0 (12 bytes), or small: 8 bytes
#   u32 node bytes, the BSP nodes (20 bytes in THPS4's: not used here - the converter builds THUG2's), face indices.
# Face flag 0x80 (camera collidable in THUG2) means the opposite in THPS4: set when the camera goes through it.

def read_col(data, name="col"):
    r = Reader(data, name=name)
    version, count, total_verts, large_faces, small_faces, large_verts, small_verts, pad = r.read("8i")
    if version != 8:
        raise FormatError("%s: collision version %d, not 8" % (name, version))
    objects = []
    for _ in range(count):
        o = {}
        o["checksum"], o["flags"], nverts, nfaces, o["small_faces"], o["fixed_verts"], face_off = r.read("IHHHBBI")
        o["bbox"] = r.read("8f")
        vert_index, bsp_off, intensity_off, o["value"] = r.read("3I4s")
        o["_where"] = (nverts, nfaces, face_off, vert_index)
        objects.append(o)
    vert_base = (r.offset + 15) & ~15
    if small_verts or any(o["fixed_verts"] for o in objects):
        raise FormatError("%s: fixed-point vertices in a version 8 file" % name)
    face_base = vert_base + total_verts * 16
    for o in objects:
        nverts, nfaces, face_off, vert_index = o.pop("_where")
        v = Reader(data, vert_base + vert_index * 16, name)
        o["verts"], o["intensities"] = [], []
        for _ in range(nverts):
            x, y, z, intensity = v.read("3f4s")
            o["verts"].append((x, y, z))
            o["intensities"].append(intensity)
        f = Reader(data, face_base + face_off, name)
        faces = []
        for _ in range(nfaces):
            if o["small_faces"]:
                flags, terrain, a, b, c, _ = f.read("HH4B")
            else:
                flags, terrain, a, b, c, _ = f.read("HH4H")
            if max(a, b, c) >= nverts:
                raise FormatError("%s: object %08X face past its %d vertices" % (name, o["checksum"], nverts))
            faces.append((flags, terrain, a, b, c))
        o["faces"] = faces
        objects_end = f.offset
    face_end = face_base + large_faces * 12 + small_faces * 8
    r.offset = face_end
    node_size = r.u32()
    r.offset += node_size
    index_count = (len(data) - r.offset) // 2
    if (len(data) - r.offset) % 2 or index_count < large_faces + small_faces - 1:
        raise FormatError("%s: %d bytes of face indices after the nodes" % (name, len(data) - r.offset))
    return {"objects": objects, "node_size": node_size, "index_count": index_count}
