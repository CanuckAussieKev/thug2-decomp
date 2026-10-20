# (Has a TypeScript twin, thug2_scene.ts, which the TypeScript tools use: until this file is removed - once the Python
# tools importing it are ported, docs/typescript.md - change both, and run tools/parity's cases.)
# THUG2 PC scene files (levels\<x>\<x>.scn.xbx; models' .mdl.xbx have the same layout): materials, then
# sectors (one per object), each a list of meshes with their own index lists and vertex buffer.
#
#   u32 material version, mesh version, vertex version (1, 1, 1 in the shipped files)
#   u32 material count, materials
#   i32 sector count, sectors
#   i32 hierarchy object count, hierarchy objects (none in levels)
#
# Read and written by the same field list so that a file read and written again is byte-identical (selfcheck).

from binio import Reader, Writer, FormatError

# Sector flags.
SEC_TEXCOORDS = 0x01
SEC_COLORS = 0x02
SEC_NORMALS = 0x04
SEC_WEIGHTS = 0x10
SEC_SHADOW_VOLUME = 0x200000
SEC_BILLBOARD = 0x00800000
SEC_COLOR_WIBBLES = 0x800

# Material pass flags.
PASS_UV_WIBBLE = 1 << 0
PASS_VC_WIBBLE = 1 << 1
PASS_TEXTURED = 1 << 2
PASS_ENVIRONMENT = 1 << 3
PASS_DECAL = 1 << 4
PASS_SMOOTH = 1 << 5
PASS_TRANSPARENT = 1 << 6
PASS_ANIMATES = 1 << 11
PASS_IGNORE_VERTEX_ALPHA = 1 << 12

# Direct3D FVF bits (the mesh's vertex shader word when it isn't skinned).
FVF_XYZ = 0x002
FVF_NORMAL = 0x010
FVF_DIFFUSE = 0x040
FVF_TEXCOUNT_SHIFT = 8


def read_material(r):
    m = {}
    m["checksum"] = r.u32()
    m["name_checksum"] = r.u32()
    passes = r.u32()
    m["alpha_cutoff"] = r.u32()
    m["sorted"] = r.u8()
    m["draw_order"] = r.f32()
    m["single_sided"] = r.u8()
    m["no_backface_culling"] = r.u8()
    m["z_bias"] = r.i32()
    m["grassify"] = r.u8()
    if m["grassify"]:
        m["grass"] = r.read("fi")
    m["specular_power"] = r.f32()
    if m["specular_power"] > 0.0:
        m["specular_color"] = r.read("3f")
    m["passes"] = [read_pass(r, i) for i in range(passes)]
    return m


def read_pass(r, index):
    p = {}
    p["texture"] = r.u32()
    p["flags"] = r.u32()
    p["has_color"] = r.u8()
    p["color"] = r.read("3f")
    p["blend_mode"] = r.u32()
    p["fixed_alpha"] = r.u32()
    p["u_address"] = r.u32()
    p["v_address"] = r.u32()
    p["envmap_tiling"] = r.read("2f")
    p["filtering"] = r.u32()
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
    return p


def write_material(w, m):
    w.u32(m["checksum"])
    w.u32(m["name_checksum"])
    w.u32(len(m["passes"]))
    w.u32(m["alpha_cutoff"])
    w.u8(m["sorted"])
    w.f32(m["draw_order"])
    w.u8(m["single_sided"])
    w.u8(m["no_backface_culling"])
    w.i32(m["z_bias"])
    w.u8(m["grassify"])
    if m["grassify"]:
        w.write("fi", *m["grass"])
    w.f32(m["specular_power"])
    if m["specular_power"] > 0.0:
        w.write("3f", *m["specular_color"])
    for index, p in enumerate(m["passes"]):
        w.u32(p["texture"])
        w.u32(p["flags"])
        w.u8(p["has_color"])
        w.write("3f", *p["color"])
        w.u32(p["blend_mode"])
        w.u32(p["fixed_alpha"])
        w.u32(p["u_address"])
        w.u32(p["v_address"])
        w.write("2f", *p["envmap_tiling"])
        w.u32(p["filtering"])
        if p["flags"] & PASS_UV_WIBBLE:
            w.write("8f", *p["uv_wibble"])
        if index == 0 and p["flags"] & PASS_VC_WIBBLE:
            w.u32(len(p["vc_wibble"]))
            for phase, keys in p["vc_wibble"]:
                w.u32(len(keys) // 2)
                w.i32(phase)
                w.write("%di" % len(keys), *keys)
        if p["flags"] & PASS_ANIMATES:
            period, iterations, phase, keys = p["animation"]
            w.write("4i", len(keys), period, iterations, phase)
            for key in keys:
                w.write("2I", *key)
        w.write("IIff", *p["mip"])


# A mesh, as the game's loader reads it (sector loop 0x004C6B10, then the mesh's own 0x004D6100):
#   4f sphere, 6f box, u32 flags (0x400 no skater shadow, 0x20000 ...), u32 material checksum,
#   u32 LOD count (<= 8), per LOD {u32 count, u16 indices}      - the triangle lists, for collision-free uses
#   8 x {u16 count, u16 indices}                               - the strips drawn (LOD 0 first, the rest empty)
#   u8 vertex stride, u16 vertex count, u8 buffer count, per buffer {u8 convert flag, u32 size, data}
#   2 x u32 vertex shader (0..6 = built-in shaders, else a Direct3D FVF), u8 x3 (normal / colour / uv
#   offsets in the vertex), u8 has colour wibble data (+ a byte per vertex), u32 index sets (> 1: a u32 each),
#   u32 pixel shader (1: {u32, u32 size, data} follows; 2..18 the built-in ones, 0x004D3ED0).
STRIP_SLOTS = 8


def read_mesh(r):
    m = {}
    m["sphere"] = r.read("4f")
    m["bbox"] = r.read("6f")
    m["flags"] = r.u32()
    m["material"] = r.u32()
    lods = r.u32()
    if lods > 8:
        raise FormatError("%s: %d index levels at 0x%X" % (r.name, lods, r.offset))
    m["lods"] = [r.read("%dH" % r.u32()) for _ in range(lods)]
    m["strips"] = [r.read("%dH" % r.u16()) for _ in range(STRIP_SLOTS)]
    m["stride"] = r.u8()
    vertices = r.u16()
    m["vertex_count"] = vertices
    m["buffers"] = []
    for _ in range(r.u8()):
        flag = r.u8()
        m["buffers"].append((flag, r.bytes(r.u32())))
    m["vertex_shader"] = r.read("2I")
    m["offsets"] = r.read("3B")
    m["has_wibble"] = r.u8()
    if m["has_wibble"]:
        m["wibble"] = r.bytes(vertices)
    m["index_sets"] = r.u32()
    if m["index_sets"] > 1:
        m["index_set_values"] = r.read("%dI" % m["index_sets"])
    m["pixel_shader"] = r.u32()
    if m["pixel_shader"] == 1:
        m["ps_value"] = r.u32()
        m["ps_data"] = r.bytes(r.u32())
    return m


def write_mesh(w, m):
    w.write("4f", *m["sphere"])
    w.write("6f", *m["bbox"])
    w.u32(m["flags"])
    w.u32(m["material"])
    w.u32(len(m["lods"]))
    for indices in m["lods"]:
        w.u32(len(indices))
        w.write("%dH" % len(indices), *indices)
    strips = list(m["strips"]) + [()] * (STRIP_SLOTS - len(m["strips"]))
    for strip in strips:
        w.u16(len(strip))
        w.write("%dH" % len(strip), *strip)
    w.u8(m["stride"])
    w.u16(m["vertex_count"])
    w.u8(len(m["buffers"]))
    for flag, data in m["buffers"]:
        w.u8(flag)
        w.u32(len(data))
        w.raw(data)
    w.write("2I", *m["vertex_shader"])
    w.write("3B", *m["offsets"])
    w.u8(m["has_wibble"])
    if m["has_wibble"]:
        w.raw(m["wibble"])
    w.u32(m["index_sets"])
    if m["index_sets"] > 1:
        w.write("%dI" % m["index_sets"], *m["index_set_values"])
    w.u32(m["pixel_shader"])
    if m["pixel_shader"] == 1:
        w.u32(m["ps_value"])
        w.u32(len(m["ps_data"]))
        w.raw(m["ps_data"])


def read_sector(r):
    s = {}
    s["checksum"] = r.u32()
    s["bone"] = r.i32()
    s["flags"] = r.u32()
    meshes = r.u32()
    if meshes > 100000:
        raise FormatError("%s: %d meshes at 0x%X" % (r.name, meshes, r.offset))
    s["bbox"] = r.read("6f")
    s["sphere"] = r.read("4f")
    if s["flags"] & SEC_BILLBOARD:
        s["billboard"] = (r.u32(), r.read("9f"))
    s["meshes"] = [read_mesh(r) for _ in range(meshes)]
    return s


def write_sector(w, s):
    w.u32(s["checksum"])
    w.i32(s["bone"])
    w.u32(s["flags"])
    w.u32(len(s["meshes"]))
    w.write("6f", *s["bbox"])
    w.write("4f", *s["sphere"])
    if s["flags"] & SEC_BILLBOARD:
        kind, values = s["billboard"]
        w.u32(kind)
        w.write("9f", *values)
    for m in s["meshes"]:
        write_mesh(w, m)


def read_scene(data, name="scene"):
    r = Reader(data, name=name)
    scene = {"versions": r.read("3I")}
    scene["materials"] = [read_material(r) for _ in range(r.u32())]
    scene["sectors"] = [read_sector(r) for _ in range(r.i32())]
    scene["hierarchy_count"] = r.i32()
    # (hierarchy objects: only in models; kept raw)
    scene["tail"] = r.bytes(len(data) - r.offset)
    if scene["hierarchy_count"] == 0 and scene["tail"]:
        raise FormatError("%s: %d bytes after the sectors" % (name, len(scene["tail"])))
    return scene


def write_scene(scene):
    w = Writer()
    w.write("3I", *scene["versions"])
    w.u32(len(scene["materials"]))
    for m in scene["materials"]:
        write_material(w, m)
    w.i32(len(scene["sectors"]))
    for s in scene["sectors"]:
        write_sector(w, s)
    w.i32(scene["hierarchy_count"])
    w.raw(scene.get("tail", b""))
    return w.getvalue()
