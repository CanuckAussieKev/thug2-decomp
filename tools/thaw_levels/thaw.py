# Tony Hawk's American Wasteland (PC, 1.01) level files, read into tools/level_port's forms. Nearly every file of the
# game ends in .wpc; the levels are in Game\data\worlds\worldzones\<zone>\<zone>[_net].pak.wpc.
#
# - Archives (.pak.wpc, + .pab.wpc): the newer Neversoft pak (the GH3 family), uncompressed. A table of 32-byte
#   entries {u32 extension checksum, u32 data offset (from the entry), u32 size, u32 pak name checksum, u32 full
#   name checksum, u32 name checksum, u32 0, u32 flags}; flags & 0x20: a 160-byte path follows the entry. The table
#   ends with a ".last" entry. With a .pab the .pak is only the table and the offsets run on into the .pab.
# - Textures (.tex): u32 0xABADD00D, u16 0x0401, u16 count, then per texture u32 0xABADD00D, u16 2, u16 20 (the
#   header size), u32 checksum, u16 width, height, width, height, u8 levels, u8 texel depth, u8 DXT, u8 0, then per
#   level {u32 size, data} (32-bit ones: {u16 pitch, u16 rows}, data): THUG2's .tex.xbx field for field.
# - Collision (.col): THUG2's version 10 with 48 more bytes after the 32-byte header: {u32 0, u32 0, i32 grid x,
#   i32 grid z, 4f box min, 4f box max} (a supersector grid THUG2 makes at load time). Without them THUG2's reader
#   (tools/level_port/thug2_col.py) reads it and writes it back byte-identical.
# - QB (.qb, .nqb node arrays, .sqb scripts): item-based (GH3 family), below (QbItems). Scripts are LZSS'd THUG2
#   token streams with one more token, 0x4A (an inline structure in the item format). The names of the checksums
#   are in data\pak\dbg.pak.wpc (one .dbg text file per qb: "0x1234abcd Name" lines).
# - Scene (.geom): read_geom below.

import os
import re
import struct

import prx
import qb
import thug2_col
from binio import Reader, FormatError
from qb import Name, Struct, checksum


# --- archives ---------------------------------------------------------------------------------------------------

EXTENSIONS = (".qb", ".nqb", ".sqb", ".mqb", ".tex", ".geom", ".col", ".mdl", ".skin", ".stex", ".img", ".pimg",
              ".ska", ".ske", ".rnb", ".shd", ".gap", ".pfx", ".dbg", ".last", "last")
EXTENSION_OF = {checksum(e): e for e in EXTENSIONS}
PAK_NAMED = 0x20      # a 160-byte path follows the entry
PAK_SFX = 0x02        # the zone's _sfx sub-zone (sounds, almost no geometry)


class PakEntry:
    __slots__ = ("extension", "offset", "size", "pak", "full", "name", "flags", "path")

    def __repr__(self):
        return "PakEntry(%s %s %d)" % (self.extension, self.path or "%08X" % self.full, self.size)


def pak_entries(path):
    # The entries of <x>.pak.wpc and the data they point into (the .pak followed by its .pab when there is one).
    data = open(path, "rb").read()
    if path.lower().endswith(".pak.wpc"):
        pab = path[:-len(".pak.wpc")] + ".pab.wpc"
        if os.path.exists(pab):
            data += open(pab, "rb").read()
    entries = []
    offset = 0
    while True:
        if offset + 32 > len(data):
            raise FormatError("%s: no .last entry" % path)
        ext, rel, size, pak, full, name, _, flags = struct.unpack_from("<8I", data, offset)
        e = PakEntry()
        e.extension = EXTENSION_OF.get(ext, "%08x" % ext)
        e.offset, e.size, e.pak, e.full, e.name, e.flags = offset + rel, size, pak, full, name, flags
        e.path = None
        header = 32
        if flags & PAK_NAMED:
            e.path = data[offset + 32:offset + 192].split(b"\0")[0].decode("latin-1")
            header += 160
        if e.extension in (".last", "last"):
            break
        if e.offset + size > len(data):
            raise FormatError("%s: entry at 0x%X past the end" % (path, offset))
        entries.append(e)
        offset += header
    return entries, data


def pak_files(path):
    # [(entry, bytes)] of a pak.
    entries, data = pak_entries(path)
    return [(e, data[e.offset:e.offset + e.size]) for e in entries]


# --- checksum names (dbg.pak) -----------------------------------------------------------------------------------

_NAME_LINE = re.compile(rb"0x([0-9a-fA-F]{8}) ([^\r\n]+)")


def load_names(thaw):
    # Every checksum name of every .dbg file in data\pak\dbg.pak.wpc: {checksum: text}.
    names = {}
    for _, data in pak_files(os.path.join(thaw, "Game", "data", "pak", "dbg.pak.wpc")):
        for match in _NAME_LINE.finditer(data):
            text = match.group(2).decode("latin-1").strip()
            value = int(match.group(1), 16)
            if value not in names and checksum(text) == value:
                names[value] = text
            else:
                names.setdefault(value, text)
    return names


# --- textures ---------------------------------------------------------------------------------------------------

TEX_MAGIC = 0xABADD00D


def level_size(value, dxt):
    # A level's size word: its bytes (DXT), or {u16 row pitch, u16 rows} (32-bit, uncompressed).
    return value if dxt else (value & 0xFFFF) * (value >> 16)


def read_tex(data, name="tex"):
    # A .tex (or a single .img) as tools/level_port/tex.py's THUG2 texture dictionary.
    r = Reader(data, name=name)
    magic, version, count = r.read("IHH")
    if magic != TEX_MAGIC:
        raise FormatError("%s: not a THAW texture file" % name)
    if version == 2:  # a single .img
        r.offset = 0
        count = 1
    textures = []
    for _ in range(count):
        magic, version, header = r.read("IHH")
        if magic != TEX_MAGIC or version != 2 or header != 20:
            raise FormatError("%s: texture header %08X %d %d at 0x%X" % (name, magic, version, header, r.offset))
        crc, w, h, w2, h2, levels, depth, dxt, pad = r.read("IHHHHBBBB")
        if (w, h) != (w2, h2) or dxt not in (0, 1, 2, 5) or depth != 32:
            raise FormatError("%s: texture %08X %dx%d (%dx%d) depth %d DXT %d" % (name, crc, w, h, w2, h2, depth, dxt))
        textures.append({"checksum": crc, "width": w, "height": h, "texel_depth": depth, "palette_depth": 32,
                         "dxt": dxt, "palette": b"", "levels": [r.bytes(level_size(r.u32(), dxt)) for _ in range(levels)]})
    r.expect_end()
    return {"version": 1, "textures": textures}


# --- collision --------------------------------------------------------------------------------------------------

COL_GRID = 48   # the bytes after the 32-byte header that THUG2's files don't have


def strip_col(data):
    return data[:32] + data[32 + COL_GRID:]


def read_col(data, name="col"):
    return thug2_col.read_col(strip_col(data), name)


# --- qb ---------------------------------------------------------------------------------------------------------
#
# u32 0, u32 file size, 20 fixed bytes (1C 08 02 04 10 04 08 0C 0C 08 02 04 14 02 04 0C 10 10 0C 00), then the
# items one after another, each {u8 0, u8 flags (4 / 0x20), u8 type, u8 0, u32 name, u32 file name checksum,
# u32 value, u32 0} with the value's data right after it (offsets are from the start of the file):
#   1 int, 2 float, 0x0D checksum, 0x1A a reference to a global ($name): the value itself
#   3 string, 4 local string ('text'): offset of the text (zero-terminated, padded to 4)
#   5 / 6 pair / vector: offset of {u32 0x00010000, 2f / 3f}
#   7 script: offset of {u32 checksum, u32 size, u32 packed size, the LZSS'd tokens padded to 4} (packed size ==
#     size: stored)
#   0x0A structure: offset of {u32 0x00010000, u32 offset of the first entry}; entries are 16 bytes {u8 0,
#     u8 type << 1 | 1, u16 0, u32 key (0: a flag / unnamed value), u32 value (as above by type), u32 next entry}
#   0x0C array: offset of {u8 0, u8 1, u8 element type, u8 0, u32 count, u32 x}: x = the single element's value
#     (one element, an inline type) or its offset (one element of a type with data), else the offset of count
#     u32 values / offsets.
T_INT, T_FLOAT, T_STRING, T_LOCAL, T_PAIR, T_VECTOR, T_SCRIPT = 1, 2, 3, 4, 5, 6, 7
T_STRUCT, T_ARRAY, T_NAME, T_REF = 0x0A, 0x0C, 0x0D, 0x1A
INLINE = (T_INT, T_FLOAT, T_NAME, T_REF)
QB_HEADER = 0x1C


class Ref:
    # $name: a reference to a global.
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value


class QbItems:
    # A THAW qb file read into tools/level_port/qb.py's values (globals, names, scripts as statements), so that
    # level_port's node code (nodes.py) takes it as a THUG1 QbFile.
    def __init__(self, data, name="qb", names=None):
        self.name = name
        self.data = data
        self.names = names if names is not None else {}
        self.globals = []
        self.scripts = {}
        self.script_data = {}
        size = struct.unpack_from("<I", data, 4)[0]
        if size != len(data):
            raise FormatError("%s: size %d in a file of %d" % (name, size, len(data)))
        offset = QB_HEADER
        while offset < len(data):
            if offset + 20 > len(data):
                raise FormatError("%s: item past the end at 0x%X" % (name, offset))
            zero, flags, kind, zero2 = data[offset:offset + 4]
            key, _, value, _ = struct.unpack_from("<4I", data, offset + 4)
            if zero or zero2:
                raise FormatError("%s: item header %s at 0x%X" % (name, data[offset:offset + 4].hex(), offset))
            self.end = offset + 20
            if kind == T_SCRIPT:
                _, usize, csize = struct.unpack_from("<3I", data, value)
                packed = data[value + 12:value + 12 + csize]
                body = prx.lzss_decode(packed, usize) if csize != usize else packed
                if len(body) != usize:
                    raise FormatError("%s: script %08X unpacks to %d of %d bytes" % (name, key, len(body), usize))
                self.script_data[key] = body
                self.scripts[key] = statements(body, name)
                self.end = value + 12 + csize
            else:
                self.globals.append((Name(key), self.value(kind, value)))
            offset = (self.end + 3) & ~3

    def _u32(self, at):
        return struct.unpack_from("<I", self.data, at)[0]

    def _reach(self, at):
        self.end = max(self.end, at)

    def value(self, kind, v):
        d = self.data
        if kind == T_INT:
            return struct.unpack("<i", struct.pack("<I", v))[0]
        if kind == T_FLOAT:
            return struct.unpack("<f", struct.pack("<I", v))[0]
        if kind == T_NAME:
            return Name(v)
        if kind == T_REF:
            return Ref(v)
        if kind in (T_STRING, T_LOCAL):
            end = d.index(b"\0", v)
            self._reach(end + 1)
            text = d[v:end].decode("latin-1")
            return text if kind == T_STRING else qb.Local(text)
        if kind in (T_PAIR, T_VECTOR):
            n = 2 if kind == T_PAIR else 3
            self._reach(v + 4 + 4 * n)
            return ("pair" if n == 2 else "vector", struct.unpack_from("<%df" % n, d, v + 4))
        if kind == T_STRUCT:
            return self.struct(v)
        if kind == T_ARRAY:
            return self.array(v)
        raise FormatError("%s: value type 0x%X" % (self.name, kind))

    def struct(self, at):
        self._reach(at + 8)
        entries = []
        item = self._u32(at + 4)
        while item:
            kind_byte = self.data[item + 1]
            key, v, nxt = struct.unpack_from("<3I", self.data, item + 4)
            self._reach(item + 16)
            if not kind_byte & 1:
                raise FormatError("%s: structure entry type byte %02X at 0x%X" % (self.name, kind_byte, item))
            entries.append((Name(key) if key else None, self.value(kind_byte >> 1, v)))
            item = nxt
        return Struct(entries)

    def array(self, at):
        kind, count, x = self.data[at + 2], self._u32(at + 4), self._u32(at + 8)
        self._reach(at + 12)
        if count == 0:
            return []
        if count == 1:
            return [self.value(kind, x)]
        values = struct.unpack_from("<%dI" % count, self.data, x)
        self._reach(x + 4 * count)
        return [self.value(kind, v) for v in values]

    def get(self, name):
        k = checksum(name)
        for key, value in self.globals:
            if key.value == k:
                return value
        return None

    def calls(self, script):
        out = []
        for line in self.scripts.get(script, []):
            if line and line[0][0] == 0x16:
                out.append((struct.unpack("<I", line[0][1])[0], line))
        return out


# Script tokens: THUG2's (src/game/script/qb.h) with weighted random tokens, plus 0x4A {u16 size, padding to 4 from
# the script's start, a structure in the item format (offsets from its own start)}.
FIXED = dict(qb.FIXED)


def tokens(data, name="script"):
    i, n = 0, len(data)
    while i < n:
        t = data[i]
        start = i
        i += 1
        if t in FIXED:
            payload = data[i:i + FIXED[t]]
            i += FIXED[t]
        elif t in (0x1B, 0x1C):
            length = struct.unpack_from("<I", data, i)[0]
            payload = data[i + 4:i + 4 + length]
            i += 4 + length
        elif t in (0x2F, 0x37, 0x40, 0x41):
            count = struct.unpack_from("<I", data, i)[0]
            size = 4 + 6 * count
            payload = data[i:i + size]
            i += size
        elif t == 0x4A:
            size = struct.unpack_from("<H", data, i)[0]
            i = (i + 2 + 3) & ~3
            payload = data[i:i + size]
            i += size
        else:
            payload = b""
        yield start, t, payload
        if t == 0x24 and i >= n:
            break


def statements(body, name="script"):
    # The script's statements (lines), each a list of (token, payload) - as level_port's QbFile.scripts.
    out, line = [], []
    for _, t, payload in tokens(body, name):
        if t in (0x01, 0x02):
            if line:
                out.append(line)
            line = []
        elif t == 0x24:
            break
        else:
            line.append((t, payload))
    if line:
        out.append(line)
    return out


# --- scene (.geom) ----------------------------------------------------------------------------------------------
#
# A memory image the loader (THAW.exe 0x005083B0) fixes up in place:
#   0x00  u32 0, 7 x 0xCABAAAFA
#   0x20  materials (0x00530470): u16 0x1002, u16 count, u32 size of the block (from 0x20), u32 16 (offset of the
#         first), u32 -1; then 0x120 bytes per material (MAT_*), then their extra data (uv wibbles, colour
#         wibbles, texture animations - offsets in the materials are from 0x20, -1 = none)
#   then the scene; S = its start + 8 + its second word (+0x10), offsets below are from S:
#   +0x00 u32 0xBABEFACE, u32 8, ...
#   S+0x80 sector count, S+0x84 sectors, S+0x88 sector nodes, S+0x90 0x14 bytes per sector (runtime),
#   S+0x94 meshes (the loader's pointers: offsets in the file)
#   sectors     0x30 bytes: u32 0 (vtable), u32 checksum, u32 flags (SEC_*), rest runtime
#   nodes       0x68 bytes per sector: +0x18 box (4f min, 4f max), +0x44 mesh count (the sector's meshes are the
#               next ones in the mesh list)
#   meshes      0xE0 bytes (MESH_*): sphere, material checksum, vertex stride, vertex / index counts, the FVF
#   0xAA filler (0-31 bytes), then per mesh: (sector flag 0x800: a colour wibble byte per vertex), the vertex buffers {u32 size,
#   data} (1, or 2 in the colour wibble sectors), aligned to 4, {u32 size, u16 indices: one triangle strip with
#   degenerate joins}, aligned to 4. The file ends there.
#
# Vertices are Direct3D FVF vertices (position, normal, D3DCOLOR (128 = full), uv sets): what THUG2's meshes take.

GEOM_SCENE = 0xBABEFACE
GEOM_MESH_TAG = 0x40            # set in every mesh record's first word (its flags: 0x20000 as THUG2's meshes, 0x100 ...)
MAT_SIZE = 0x120
SEC_COLOR_WIBBLE = 0x800

# Material record (0x120 bytes): offsets and per-pass arrays (4 passes at most).
MAT_CHECKSUM, MAT_NAME, MAT_PASSES = 0x00, 0x04, 0x08
MAT_DOUBLE_SIDED = 0x0D         # u8 (set on most of the transparent ones: fences, foliage)
MAT_ALPHA_CUTOFF = 0x10         # u8
MAT_Z_BIAS = 0x11               # u8
MAT_DRAW_ORDER = 0x2C           # f32
MAT_PASS_FLAGS = 0x30           # u32 x4: THUG2's pass flags (textured 4, smooth 0x20, transparent 0x40, ...)
MAT_TEXTURES = 0x40             # u32 x4
MAT_COLORS = 0x50               # 4f x4: rgb, fixed alpha / 128
MAT_BLEND = 0x90                # u32 x4: THUG2's blend mode | fixed alpha << 24
MAT_ADDRESS = 0xA0              # u16 u, u16 v x4
MAT_ENV_TILING = 0xB0           # 2f x4
MAT_FILTERING = 0xD0            # u32 x4 (as THUG2's: 0x00010004)
MAT_UV_WIBBLE = 0xE0            # u32 x4: offset of 12 floats (THUG2's 8 first), -1 none
MAT_MIP_BIAS = 0xF0             # f32 x4 (not used here)


def read_material(data, at, block):
    u32 = lambda off: struct.unpack_from("<I", data, at + off)[0]
    m = {"checksum": u32(MAT_CHECKSUM), "name_checksum": u32(MAT_NAME), "double_sided": data[at + MAT_DOUBLE_SIDED],
         "alpha_cutoff": data[at + MAT_ALPHA_CUTOFF], "z_bias": data[at + MAT_Z_BIAS],
         "draw_order": struct.unpack_from("<f", data, at + MAT_DRAW_ORDER)[0], "passes": []}
    count = u32(MAT_PASSES)
    if not 1 <= count <= 4:
        raise FormatError("material %08X: %d passes" % (m["checksum"], count))
    for i in range(count):
        p = {"flags": u32(MAT_PASS_FLAGS + 4 * i), "texture": u32(MAT_TEXTURES + 4 * i),
             "color": struct.unpack_from("<4f", data, at + MAT_COLORS + 16 * i),
             "blend": u32(MAT_BLEND + 4 * i),
             "address": struct.unpack_from("<2H", data, at + MAT_ADDRESS + 4 * i),
             "env_tiling": struct.unpack_from("<2f", data, at + MAT_ENV_TILING + 8 * i),
             "filtering": u32(MAT_FILTERING + 4 * i)}
        wibble = u32(MAT_UV_WIBBLE + 4 * i)
        if wibble != 0xFFFFFFFF:
            p["uv_wibble"] = struct.unpack_from("<12f", data, block + wibble)
        m["passes"].append(p)
    return m


# Mesh record (0xE0 bytes).
MESH_SPHERE = 0x04              # 4f
MESH_MATERIAL = 0x14            # u32 material checksum
MESH_STRIDE = 0x18              # u8
MESH_VERTICES = 0x26            # u16
MESH_INDICES = 0x28             # u32
MESH_FVF = 0x40                 # u32


def read_geom(data, name="geom"):
    # {"materials": [read_material], "sectors": [{checksum, flags, bbox (6f), meshes: [{sphere, material, fvf,
    # stride, vertex_count, vertices (bytes, the first buffer), buffers, strip (u16 tuple)}]}]}.
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]
    version, count, size = struct.unpack_from("<HHI", data, 0x20)
    if version != 0x1002:
        raise FormatError("%s: material block version %X" % (name, version))
    first = 0x20 + u32(0x28)
    materials = [read_material(data, first + i * MAT_SIZE, 0x20) for i in range(count)]
    base = 0x20 + size
    if u32(base) != GEOM_SCENE:
        raise FormatError("%s: no scene (0xBABEFACE) at 0x%X" % (name, base))
    s = base + 8 + u32(base + 4)
    sector_count, sectors_at, nodes_at, _, _, meshes_at = struct.unpack_from("<i5I", data, s + 0x80)
    sectors_at, nodes_at, meshes_at = s + sectors_at, s + nodes_at, s + meshes_at
    sectors = []
    for i in range(sector_count):
        _, checksum_, flags = struct.unpack_from("<3I", data, sectors_at + 0x30 * i)
        node = nodes_at + 0x68 * i
        lo, hi = struct.unpack_from("<3f", data, node + 0x18), struct.unpack_from("<3f", data, node + 0x28)
        sectors.append({"checksum": checksum_, "flags": flags, "bbox": lo + hi, "mesh_count": u32(node + 0x44),
                        "meshes": []})
    total = sum(sec["mesh_count"] for sec in sectors)
    at = meshes_at + 0xE0 * total
    while at < len(data) and data[at] == 0xAA:  # (filler up to the data's alignment in its pak)
        at += 1
    index = 0
    for sec in sectors:
        for _ in range(sec.pop("mesh_count")):
            rec = meshes_at + 0xE0 * index
            index += 1
            if not u32(rec) & GEOM_MESH_TAG or u32(rec) & 0xFFF0FE00:
                raise FormatError("%s: mesh %d tag %08X" % (name, index - 1, u32(rec)))
            m = {"sphere": struct.unpack_from("<4f", data, rec + MESH_SPHERE), "material": u32(rec + MESH_MATERIAL),
                 "stride": data[rec + MESH_STRIDE], "fvf": u32(rec + MESH_FVF),
                 "vertex_count": struct.unpack_from("<H", data, rec + MESH_VERTICES)[0]}
            indices = u32(rec + MESH_INDICES)
            vsize = m["vertex_count"] * m["stride"]
            if sec["flags"] & SEC_COLOR_WIBBLE:
                at += m["vertex_count"]
            buffers = []
            while True:
                if u32(at) != vsize:
                    raise FormatError("%s: mesh %d: vertex buffer of %d bytes at 0x%X, not %d"
                                      % (name, index - 1, u32(at), at, vsize))
                buffers.append(data[at + 4:at + 4 + vsize])
                at += 4 + vsize
                # (the colour wibble sectors' meshes have a second buffer of the same size)
                if not sec["flags"] & SEC_COLOR_WIBBLE or len(buffers) == 2:
                    break
            at = (at + 3) & ~3
            if u32(at) != 2 * indices:
                raise FormatError("%s: mesh %d: %d index bytes at 0x%X, not %d" % (name, index - 1, u32(at), at,
                                                                                  2 * indices))
            m["vertices"] = buffers[0]
            m["buffers"] = len(buffers)
            m["strip"] = struct.unpack_from("<%dH" % indices, data, at + 4)
            at = (at + 4 + 2 * indices + 3) & ~3
            sec["meshes"].append(m)
    if at != len(data) and (len(data) - at >= 32 or any(b != 0xAA for b in data[at:])):
        raise FormatError("%s: stopped at 0x%X of 0x%X" % (name, at, len(data)))
    return {"materials": materials, "sectors": sectors}
