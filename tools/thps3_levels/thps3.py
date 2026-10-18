# THPS3 PC level files (Data\Levels\<Lvl>\): RenderWare 3.1 binary streams (every chunk: u32 type, u32 size,
# u32 version 0x310) with Neversoft's own plugin chunks (0x0294AF01...).
#
#   <lvl>.bsp   an RpWorld: world struct, material list, the plane-section tree whose leaves are the atomic sectors
#               (each one of Neversoft's objects: its checksum name and per-face flags in a Neversoft extension),
#               then the world's extension.
#   <lvl>.dff   clumps one after another (a level's movable objects: fans, doors...; each its checksum name in the
#               clump's extension). Each clump's declared size leaves out the 12-byte header of the Neversoft chunk
#               in its extension: the clumps are walked by their children, not their sizes.
#   <lvl>.tdx   a texture dictionary: D3D8 texture natives (every one in THPS3 PC is 32-bit, one mip level).
#   <lvl>.qb    the node array (tools/level_port/qb.py reads it: THPS4's token format).
#
# Read field by field; anything not as expected raises FormatError (the self-check parses every file).

import struct

from binio import Reader, FormatError

VERSION = 0x310

STRUCT = 0x01
STRING = 0x02
EXTENSION = 0x03
TEXTURE = 0x06
MATERIAL = 0x07
MATLIST = 0x08
ATOMIC_SECTOR = 0x09
PLANE_SECTOR = 0x0A
WORLD = 0x0B
FRAMELIST = 0x0E
GEOMETRY = 0x0F
CLUMP = 0x10
ATOMIC = 0x14
TEXTURE_NATIVE = 0x15
TEXTURE_DICTIONARY = 0x16
GEOMETRY_LIST = 0x1A
BINMESH = 0x50E
NEVERSOFT = 0x0294AF01      # material, texture, sector, atomic, clump data
NEVERSOFT_GEOMETRY = 0x0294AF02
NEVERSOFT_WORLD = 0x0294AF04

# World / geometry flags.
GEO_TRISTRIP = 0x01
GEO_POSITIONS = 0x02
GEO_TEXTURED = 0x04
GEO_PRELIT = 0x08
GEO_NORMALS = 0x10
GEO_LIGHT = 0x20
GEO_MODULATE = 0x40
GEO_TEXTURED2 = 0x80

# Neversoft material pass flags (byte 19 of the material extension).
MAT_INVISIBLE = 0x02        # transparent.png, wire, the collision-only and trigger planes
MAT_TRANSPARENT = 0x08      # alpha-blended (leaves, signs, water)
MAT_DECAL = 0x10            # stickers, graffiti, stripes

# The material's uv flags (the byte after the texture).
UV_WIBBLE = 0x01
UV_ENVIRONMENT = 0x80


def chunk_header(data, offset, name):
    if offset + 12 > len(data):
        raise FormatError("%s: chunk header past the end at 0x%X" % (name, offset))
    return struct.unpack_from("<3I", data, offset)


def children(data, start, end, name):
    # The chunks inside [start, end): (type, data offset, size).
    out = []
    offset = start
    while offset < end:
        kind, size, version = chunk_header(data, offset, name)
        if version != VERSION:
            raise FormatError("%s: chunk %X at 0x%X has version %X" % (name, kind, offset, version))
        if offset + 12 + size > end:
            raise FormatError("%s: chunk %X at 0x%X runs past its parent" % (name, kind, offset))
        out.append((kind, offset + 12, size))
        offset += 12 + size
    if offset != end:
        raise FormatError("%s: chunks end at 0x%X, not 0x%X" % (name, offset, end))
    return out


def only(kids, kind, name, where):
    found = [k for k in kids if k[0] == kind]
    if len(found) != 1:
        raise FormatError("%s: %d chunks %X in %s" % (name, len(found), kind, where))
    return found[0]


def cstring(raw):
    return raw.split(b"\0", 1)[0].decode("latin-1")


# --- materials ---------------------------------------------------------------------------------------------------
# A material: struct {u32 flags, u8 RGBA colour, u32 unused, u32 textured, 3f ambient / specular / diffuse}, the
# texture (struct {u8 filter, u8 addressing (u nibble, v nibble), u16}, name, mask name, extension), then the
# extension with Neversoft's material:
#   u32 id, 12 bytes 0, u8 1, u8 pass count, u8 1, then per pass (the first's):
#     u8 flags (MAT_*), u8 terrain type (the first pass only), 4 x RGBA (colour, then three unused), u32 blend (the
#     PS2's alpha blend register: (A - B) * C + D, two bits each from the bottom: 0 source colour, 1 destination
#     colour, 2 zero / fixed alpha), u32 fixed alpha (0x80 = 1), u8 has texture, u16 0, the texture chunk (as the
#     material's), u8 uv flags (UV_*), 8f uv wibble (u / v velocity, frequency, amplitude, phase), 4f, then the
#     vertex colour wibble (u8, u8 has one, u32, u32 key count, keys {RGBA, u32 time}).
#   Further passes have no flags / terrain and no texture in THPS3's files (23 two-pass materials): not used.

def read_texture_chunk(data, offset, size, name):
    kids = children(data, offset, offset + size, name)
    s = only(kids, STRUCT, name, "texture")
    filt, addressing = struct.unpack_from("<BB", data, s[1])
    strings = [cstring(data[k[1]:k[1] + k[2]]) for k in kids if k[0] == STRING]
    if not strings:
        raise FormatError("%s: texture without a name at 0x%X" % (name, offset))
    return {"name": strings[0], "mask": strings[1] if len(strings) > 1 else "", "filter": filt,
            "u_address": addressing & 0xF, "v_address": addressing >> 4}


def read_neversoft_material(raw, name):
    r = Reader(raw, name=name)
    m = {"id": r.u32()}
    r.bytes(12)
    _, passes, _ = r.read("3B")
    m["pass_count"] = passes
    m["flags"], m["terrain"] = r.read("2B")
    m["colors"] = [r.read("4B") for _ in range(4)]
    m["blend"], m["fixed_alpha"] = r.read("2I")
    has_texture = r.u8()
    r.bytes(2)
    m["texture"] = None
    if has_texture:
        kind, size, version = struct.unpack_from("<3I", raw, r.offset)
        if kind != TEXTURE or version != VERSION:
            raise FormatError("%s: material texture chunk %X" % (name, kind))
        m["texture"] = read_texture_chunk(raw, r.offset + 12, size, name)
        r.offset += 12 + size
    if len(raw) - r.offset >= 49:
        # (untextured materials have 24 bytes of zeros here instead)
        m["uv_flags"] = r.u8()
        m["uv_wibble"] = r.read("8f")
        r.read("4f")
    else:
        m["uv_flags"] = 0
    return m


def read_material(data, offset, size, name):
    kids = children(data, offset, offset + size, name)
    s = only(kids, STRUCT, name, "material")
    flags, rgba, _, textured = struct.unpack_from("<I4sII", data, s[1])
    m = {"rw_color": tuple(rgba), "texture": None, "ns": None}
    for kind, o, n in kids:
        if kind == TEXTURE:
            m["texture"] = read_texture_chunk(data, o, n, name)
        elif kind == EXTENSION:
            for kind2, o2, n2 in children(data, o, o + n, name):
                if kind2 == NEVERSOFT and n2 >= 48:
                    m["ns"] = read_neversoft_material(data[o2:o2 + n2], name)
    return m


def read_material_list(data, offset, size, name):
    kids = children(data, offset, offset + size, name)
    s = only(kids, STRUCT, name, "material list")
    count = struct.unpack_from("<I", data, s[1])[0]
    refs = struct.unpack_from("<%di" % count, data, s[1] + 4)
    materials = [read_material(data, o, n, name) for kind, o, n in kids if kind == MATERIAL]
    out = []
    for ref in refs:
        # -1: the next material chunk; else a copy of an earlier one
        out.append(materials[len([x for x in out if x is not None])] if ref < 0 else out[ref])
    if len([r for r in refs if r < 0]) != len(materials):
        raise FormatError("%s: %d materials for %d new entries" % (name, len(materials), len(refs)))
    return out


# --- the world (.bsp) --------------------------------------------------------------------------------------------
# World struct: i32 root is a sector, 3f inverse origin, 3f ambient / specular / diffuse, u32 triangles, vertices,
# planes, sectors, collision sector size, flags (GEO_*).
# Atomic sector struct: u32 material base, u32 triangles, u32 vertices, 3f box min, 3f box max, 2 x u32, then the
# vertices (3f), normals (4 x i8, GEO_NORMALS), colours (RGBA, GEO_PRELIT; 255 = full), uvs (2f), second uvs (2f,
# GEO_TEXTURED2), triangles (u16 material, 3 x u16 vertex). Its extension: the bin mesh (strips or lists per
# material), RW's collision tree (not used), material effects, and Neversoft's:
#   u32 6, u16 face flags per triangle (THUG2's collision flag meanings), u32 0, u32 checksum (the object's name),
#   u8, u8, u8 has vertex data (+ 2 bytes per vertex: wibble data, not used).

def read_binmesh(data, offset, size, name):
    r = Reader(data[offset:offset + size], name=name)
    face_type, count, total = r.read("3I")
    meshes = []
    for _ in range(count):
        n, material = r.read("2I")
        meshes.append({"material": material, "indices": r.read("%dI" % n)})
    r.expect_end()
    return {"strips": face_type & 1 == 1, "meshes": meshes}


def read_sector(data, offset, size, flags, name):
    kids = children(data, offset, offset + size, name)
    s = only(kids, STRUCT, name, "atomic sector")
    r = Reader(data[s[1]:s[1] + s[2]], name=name)
    base, ntri, nvert = r.read("3I")
    sec = {"material_base": base, "bbox": r.read("6f")}
    r.read("2I")
    sec["positions"] = [r.read("3f") for _ in range(nvert)]
    if flags & GEO_NORMALS:
        sec["normals"] = [tuple(x / 127.0 for x in r.read("4b")[:3]) for _ in range(nvert)]
    if flags & GEO_PRELIT:
        sec["colors"] = [r.read("4B") for _ in range(nvert)]
    sets = 2 if flags & GEO_TEXTURED2 else (1 if flags & GEO_TEXTURED else 0)
    sec["uvs"] = [r.read("%df" % (2 * sets)) for _ in range(nvert)] if sets else []
    sec["uv_sets"] = sets
    sec["triangles"] = [r.read("4H") for _ in range(ntri)]  # (material, a, b, c)
    r.expect_end()
    for material, a, b, c in sec["triangles"]:
        if max(a, b, c) >= nvert:
            raise FormatError("%s: triangle past its sector's %d vertices" % (name, nvert))
    sec["binmesh"] = None
    sec["checksum"] = 0
    sec["face_flags"] = [0] * ntri
    sec["ns_flags"] = 0
    for kind, o, n in kids:
        if kind != EXTENSION:
            continue
        for kind2, o2, n2 in children(data, o, o + n, name):
            if kind2 == BINMESH:
                sec["binmesh"] = read_binmesh(data, o2, n2, name)
            elif kind2 == NEVERSOFT:
                e = Reader(data[o2:o2 + n2], name=name)
                if e.u32() != 6:
                    raise FormatError("%s: sector extension not version 6" % name)
                sec["face_flags"] = list(e.read("%dH" % ntri))
                e.u32()
                sec["checksum"] = e.u32()
                sec["ns_flags"], _, has_vertex_data = e.read("3B")
                if has_vertex_data:
                    e.bytes(2 * nvert)
                e.expect_end()
    return sec


def sectors_of(data, offset, size, flags, name, out):
    # The plane-section tree's leaves in order (a plane sector: struct {u32 type, f32 value, i32 left is a sector,
    # i32 right is a sector, 2f}, the left child, the right one).
    for kind, o, n in children(data, offset, offset + size, name):
        if kind == ATOMIC_SECTOR:
            out.append(read_sector(data, o, n, flags, name))
        elif kind == PLANE_SECTOR:
            sectors_of(data, o, n, flags, name, out)
        elif kind != STRUCT:
            raise FormatError("%s: chunk %X in the plane sectors" % (name, kind))


def read_bsp(data, name="bsp"):
    kind, size, version = chunk_header(data, 0, name)
    if kind != WORLD or 12 + size != len(data):
        raise FormatError("%s: not a world (chunk %X, %d bytes)" % (name, kind, size))
    kids = children(data, 12, 12 + size, name)
    s = only(kids, STRUCT, name, "world")
    fields = struct.unpack_from("<i3f3f6I", data, s[1])
    world = {"flags": fields[-1], "triangles": fields[7], "vertices": fields[8], "sector_count": fields[10]}
    m = only(kids, MATLIST, name, "world")
    world["materials"] = read_material_list(data, m[1], m[2], name)
    sectors = []
    for kind, o, n in kids:
        if kind == PLANE_SECTOR:
            sectors_of(data, o, n, world["flags"], name, sectors)
        elif kind == ATOMIC_SECTOR:
            sectors.append(read_sector(data, o, n, world["flags"], name))
    if len(sectors) != world["sector_count"]:
        raise FormatError("%s: %d sectors, the world says %d" % (name, len(sectors), world["sector_count"]))
    if sum(len(x["triangles"]) for x in sectors) != world["triangles"]:
        raise FormatError("%s: triangle count differs from the world's" % name)
    world["sectors"] = sectors
    return world


# --- the clumps (.dff) -------------------------------------------------------------------------------------------
# A clump: struct {u32 atomic count}, the frame list (struct {u32 count, per frame 3x3f rotation, 3f position,
# i32 parent, u32}, an extension each), the geometry list (struct {u32 count}, geometries), the atomics (struct
# {u32 frame, u32 geometry, u32 flags, u32}, extension), the extension (Neversoft: u32 1, u32 checksum).
# A geometry: struct {u16 flags, u16, u32 triangles, u32 vertices, u32 morph targets, 3f lighting (RW 3.1), the
# colours (GEO_PRELIT), uvs, triangles (u16 b, u16 a, u16 material, u16 c), per morph target {4f sphere, u32 has
# positions, u32 has normals, positions, normals}}, its material list, the extension (bin mesh...).

def read_geometry(data, offset, size, name):
    kids = children(data, offset, offset + size, name)
    s = only(kids, STRUCT, name, "geometry")
    r = Reader(data[s[1]:s[1] + s[2]], name=name)
    flags, _, ntri, nvert, targets = r.read("HHIII")
    r.read("3f")
    g = {"flags": flags}
    g["colors"] = [r.read("4B") for _ in range(nvert)] if flags & GEO_PRELIT else []
    sets = 2 if flags & GEO_TEXTURED2 else (1 if flags & GEO_TEXTURED else 0)
    g["uvs"] = [r.read("%df" % (2 * sets)) for _ in range(nvert)] if sets else []
    g["uv_sets"] = sets
    g["triangles"] = []
    for _ in range(ntri):
        b, a, material, c = r.read("4H")
        g["triangles"].append((material, a, b, c))
    if targets < 1:
        raise FormatError("%s: geometry without a morph target" % name)
    r.read("4f")
    has_positions, has_normals = r.read("2I")
    g["positions"] = [r.read("3f") for _ in range(nvert)] if has_positions else []
    g["normals"] = [r.read("3f") for _ in range(nvert)] if has_normals else []
    for _ in range(targets - 1):
        r.read("4f")
        p, n = r.read("2I")
        r.bytes(12 * nvert * ((1 if p else 0) + (1 if n else 0)))
    r.expect_end()
    m = only(kids, MATLIST, name, "geometry")
    g["materials"] = read_material_list(data, m[1], m[2], name)
    g["binmesh"] = None
    for kind, o, n in kids:
        if kind == EXTENSION:
            for kind2, o2, n2 in children(data, o, o + n, name):
                if kind2 == BINMESH:
                    g["binmesh"] = read_binmesh(data, o2, n2, name)
    return g


def read_clump(data, offset, name):
    # Returns (clump, the offset after it).
    kind, size, version = chunk_header(data, offset, name)
    if kind != CLUMP or version != VERSION:
        raise FormatError("%s: no clump at 0x%X" % (name, offset))
    o = offset + 12
    clump = {"frames": [], "geometries": [], "atomics": [], "checksum": 0}
    while True:
        kind, size, version = chunk_header(data, o, name)
        if version != VERSION:
            raise FormatError("%s: chunk %X at 0x%X has version %X" % (name, kind, o, version))
        body = o + 12
        if kind == STRUCT:
            clump["atomic_count"] = struct.unpack_from("<I", data, body)[0]
        elif kind == FRAMELIST:
            kids = children(data, body, body + size, name)
            s = only(kids, STRUCT, name, "frame list")
            count = struct.unpack_from("<I", data, s[1])[0]
            for i in range(count):
                values = struct.unpack_from("<12fiI", data, s[1] + 4 + 56 * i)
                clump["frames"].append({"rotation": values[0:9], "position": values[9:12], "parent": values[12]})
        elif kind == GEOMETRY_LIST:
            for kind2, o2, n2 in children(data, body, body + size, name):
                if kind2 == GEOMETRY:
                    clump["geometries"].append(read_geometry(data, o2, n2, name))
        elif kind == ATOMIC:
            kids = children(data, body, body + size, name)
            s = only(kids, STRUCT, name, "atomic")
            frame, geometry, flags, _ = struct.unpack_from("<4I", data, s[1])
            clump["atomics"].append({"frame": frame, "geometry": geometry, "flags": flags})
        elif kind == EXTENSION:
            for kind2, o2, n2 in children(data, body, body + size, name):
                if kind2 == NEVERSOFT and n2 >= 8:
                    clump["checksum"] = struct.unpack_from("<I", data, o2 + 4)[0]
            return clump, body + size
        else:
            raise FormatError("%s: chunk %X in a clump at 0x%X" % (name, kind, o))
        o = body + size


def read_dff(data, name="dff"):
    clumps = []
    offset = 0
    while offset < len(data):
        clump, offset = read_clump(data, offset, name)
        if len(clump["atomics"]) != clump["atomic_count"]:
            raise FormatError("%s: clump %08X has %d atomics, says %d" % (name, clump["checksum"],
                                                                         len(clump["atomics"]), clump["atomic_count"]))
        clumps.append(clump)
    return clumps


# --- textures (.tdx) ---------------------------------------------------------------------------------------------
# A texture dictionary: struct {u16 count, u16 0}, texture natives, an extension. A texture native (D3D8): struct
# {u32 platform 8, u8 filter, u8 addressing, u16, name[128], mask[128], u32 raster format, u32 has alpha, u16 width,
# u16 height, u8 depth, u8 levels, u8 raster type, u8 compression, (palette), per level {u32 size, data}}, an
# extension.

RASTER_8888 = 0x0500
RASTER_888 = 0x0600
RASTER_PAL8 = 0x2000
RASTER_PAL4 = 0x4000


def read_tdx(data, name="tdx"):
    kind, size, version = chunk_header(data, 0, name)
    if kind != TEXTURE_DICTIONARY or 12 + size != len(data):
        raise FormatError("%s: not a texture dictionary" % name)
    textures = []
    for kind, o, n in children(data, 12, 12 + size, name):
        if kind != TEXTURE_NATIVE:
            continue
        kids = children(data, o, o + n, name)
        s = only(kids, STRUCT, name, "texture native")
        r = Reader(data[s[1]:s[1] + s[2]], name=name)
        platform, filt, addressing, _ = r.read("IBBH")
        if platform != 8:
            raise FormatError("%s: texture platform %d" % (name, platform))
        t = {"name": cstring(r.bytes(128)), "mask": cstring(r.bytes(128)), "filter": filt,
             "addressing": addressing}
        t["format"], t["has_alpha"], t["width"], t["height"], t["depth"], levels, t["type"], t["compression"] = \
            r.read("IIHHBBBB")
        if t["format"] & (RASTER_PAL8 | RASTER_PAL4) or t["compression"] or t["depth"] != 32 or \
                (t["format"] & 0xF00) not in (RASTER_8888, RASTER_888):
            raise FormatError("%s: texture %s is format %X depth %d compression %d (only 32-bit ones are read)"
                              % (name, t["name"], t["format"], t["depth"], t["compression"]))
        t["levels"] = []
        for i in range(levels):
            level = r.bytes(r.u32())
            w, h = max(1, t["width"] >> i), max(1, t["height"] >> i)
            if len(level) != w * h * 4:
                raise FormatError("%s: texture %s level %d is %d bytes" % (name, t["name"], i, len(level)))
            t["levels"].append(level)
        r.expect_end()
        textures.append(t)
    return textures
