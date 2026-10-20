# (Has a TypeScript twin, thug2_col.ts, which the TypeScript tools use: until this file is removed - once the Python
# tools importing it are ported, docs/typescript.md - change both, and run tools/parity's cases.)
# THUG2 PC collision files (levels\<x>\<x>.col.xbx), version 10. The layout, as the game's loader takes it apart
# (0x0049CBB0, the per-object fix-ups 0x004169C0 and 0x00410BC0):
#
#   header      i32 version (10), object count, total vertices, large faces, small faces, large (float) vertices,
#               small (fixed-point) vertices, 0
#   objects     64 bytes each: u32 checksum (the scene sector / node it belongs to), u16 flags, u16 vertex count,
#               u16 face count, u8 small faces, u8 fixed-point vertices, u32 first face (byte offset in the faces),
#               4f box min, 4f box max, u32 first vertex (byte offset), u32 BSP root (byte offset in the nodes),
#               u32 first intensity (index), f32 reach scale (below)
#   (aligned to 16)
#   vertices    float: 3f; fixed-point: 3 x u16 sixteenths above the object's box minimum
#   intensities a byte per vertex
#   (aligned to 4)
#   faces       large: u16 flags, u16 terrain, 3 x u16 vertex; small: u16 flags, u16 terrain, 3 x u8 vertex, u8 reach
#   reaches     a byte per large face (aligned to 4 after)
#   u32 size of the BSP nodes, nodes (8 bytes: a split {i32 axis | (sixteenths of the split point << 2),
#               u32 byte offset of the left child, the right one after it} or a leaf {u8 3, u8 0, u16 face count,
#               u32 first face index}), then the leaves' face indices (u16, per object)
#
# A face's reach byte is how far its farthest vertex is from the object's box minimum, times the object's reach
# scale (254 / the farthest vertex of the object), rounded up. A ray test (0x00413B50) skips the faces whose reach is
# below the ray's distance from the box minimum times the scale: 0 there makes a face all but uncollidable.

import math
import struct

from binio import Reader, Writer, FormatError

VERSION = 10

# Face flags.
FACE_SKATABLE = 0x0001
FACE_NOT_SKATABLE = 0x0002
FACE_WALL_RIDABLE = 0x0004
FACE_VERT = 0x0008
FACE_NON_COLLIDABLE = 0x0010
FACE_DECAL = 0x0020
FACE_TRIGGER = 0x0040
FACE_CAMERA_COLLIDABLE = 0x0080
FACE_NO_SKATER_SHADOW = 0x0100
FACE_SKATER_SHADOW = 0x0200
FACE_NO_SKATER_SHADOW_WALL = 0x0400
FACE_UNDER_OK = 0x0800
FACE_INVISIBLE = 0x1000


def read_col(data, name="col", versions=(VERSION,)):
    # versions: (9,) reads THUG1's collision, which is version 10 without the reaches: no byte array after the large
    # faces, the small faces' 4th byte 0 (padding), the objects' reach scale 0.
    r = Reader(data, name=name)
    version, count, total_verts, large_faces, small_faces, large_verts, small_verts, pad = r.read("8i")
    if version not in versions:
        raise FormatError("%s: collision version %d, not %s" % (name, version, " / ".join(map(str, versions))))
    has_reaches = version >= 10
    objects = []
    for _ in range(count):
        o = {}
        o["checksum"], o["flags"], nverts, nfaces, o["small_faces"], o["fixed_verts"], face_off = r.read("IHHHBBI")
        o["bbox"] = r.read("8f")
        vert_off, bsp_off, intensity_off = r.read("3I")
        o["value"] = r.bytes(4)
        o["_counts"] = (nverts, nfaces)
        o["_offsets"] = (face_off, vert_off, bsp_off, intensity_off)
        objects.append(o)
    vert_base = (r.offset + 15) & ~15
    intensity_base = vert_base + large_verts * 12 + small_verts * 6
    face_base = (intensity_base + total_verts + 3) & ~3
    face_size = large_faces * 10 + small_faces * 8
    face_bytes_base = face_base + face_size
    node_size_at = (face_bytes_base + (large_faces if has_reaches else 0) + 3) & ~3
    r.offset = node_size_at
    node_size = r.u32()
    node_base = r.offset
    index_base = node_base + node_size
    face_bytes_at = face_bytes_base
    for o in objects:
        nverts, nfaces = o.pop("_counts")
        face_off, vert_off, bsp_off, intensity_off = o.pop("_offsets")
        v = Reader(data, vert_base + vert_off, name)
        if o["fixed_verts"]:
            o["verts"] = [tuple(o["bbox"][k] + c / 16.0 for k, c in enumerate(v.read("3H"))) for _ in range(nverts)]
        else:
            o["verts"] = [v.read("3f") for _ in range(nverts)]
        o["intensities"] = data[intensity_base + intensity_off:intensity_base + intensity_off + nverts]
        f = Reader(data, face_base + face_off, name)
        faces, reaches = [], []
        for _ in range(nfaces):
            if o["small_faces"]:
                flags, terrain, a, b, c, reach = f.read("HH4B")
                reaches.append(reach)
            else:
                flags, terrain, a, b, c = f.read("HH3H")
            faces.append((flags, terrain, a, b, c))
        o["faces"] = faces
        if o["small_faces"]:
            o["reach"] = bytes(reaches)
        elif not has_reaches:
            o["reach"] = bytes(nfaces)
        else:
            o["reach"] = data[face_bytes_at:face_bytes_at + nfaces]
            face_bytes_at += nfaces
        o["bsp"] = read_tree(data, node_base, bsp_off, index_base, name)
        o["_bsp_off"] = bsp_off
    col = {"objects": objects, "pad": pad, "version": version}
    # (the nodes are read per object from its root; any not reached are a format surprise)
    col["_node_size"] = node_size
    col["_index_count"] = (len(data) - index_base) // 2
    return col


def read_tree(data, node_base, offset, index_base, name):
    r = Reader(data, node_base + offset, name)
    word, second = r.read("iI")
    if word & 0xFF == 3 and (word >> 8) & 0xFF == 0 and False:
        pass
    if (word & 3) == 3:
        count = (word >> 16) & 0xFFFF
        ir = Reader(data, index_base + second * 2, name)
        return ("leaf", ir.read("%dH" % count), word & 0xFFFF)
    axis = word & 3
    point = word >> 2
    left = read_tree(data, node_base, second, index_base, name)
    right = read_tree(data, node_base, second + 8, index_base, name)
    return ("split", axis, point, left, right)


def write_col(col):
    objects = col["objects"]
    w = Writer()
    total_verts = sum(len(o["verts"]) for o in objects)
    large_faces = sum(len(o["faces"]) for o in objects if not o["small_faces"])
    small_faces = sum(len(o["faces"]) for o in objects if o["small_faces"])
    large_verts = sum(len(o["verts"]) for o in objects if not o["fixed_verts"])
    small_verts = sum(len(o["verts"]) for o in objects if o["fixed_verts"])
    w.write("8i", VERSION, len(objects), total_verts, large_faces, small_faces, large_verts, small_verts,
            col.get("pad", 0))
    # (float vertices first, then the fixed-point ones, as the shipped files)
    large_verts_out, small_verts_out = Writer(), Writer()
    intensities, faces, face_bytes, nodes, indices = Writer(), Writer(), Writer(), Writer(), Writer()
    for o in objects:
        if len(o["verts"]) > 0xFFFF or len(o["faces"]) > 0xFFFF:
            raise FormatError("collision object %08X too big" % o["checksum"])
        root = nodes.tell()
        write_tree(o["bsp"], nodes, indices)
        w.write("IHHHBBI", o["checksum"], o["flags"], len(o["verts"]), len(o["faces"]), o["small_faces"],
                o["fixed_verts"], faces.tell())
        w.write("8f", *o["bbox"])
        verts = small_verts_out if o["fixed_verts"] else large_verts_out
        w.write("3I", verts.tell() + (large_verts * 12 if o["fixed_verts"] else 0), root, intensities.tell())
        w.raw(o.get("value", bytes(4)))
        for v in o["verts"]:
            if o["fixed_verts"]:
                verts.write("3H", *(int(round((v[k] - o["bbox"][k]) * 16.0)) for k in range(3)))
            else:
                verts.write("3f", *v)
        intensities.raw(o["intensities"])
        reach = o["reach"]
        if len(reach) != len(o["faces"]):
            raise FormatError("collision object %08X: %d reaches for %d faces" % (o["checksum"], len(reach),
                                                                                 len(o["faces"])))
        for i, (flags, terrain, a, b, c) in enumerate(o["faces"]):
            if o["small_faces"]:
                faces.write("HH4B", flags, terrain, a, b, c, reach[i])
            else:
                faces.write("HH3H", flags, terrain, a, b, c)
        if not o["small_faces"]:
            face_bytes.raw(reach)
    w.pad_to(16)
    w.raw(large_verts_out.getvalue())
    w.raw(small_verts_out.getvalue())
    w.raw(intensities.getvalue())
    w.pad_to(4)
    w.raw(faces.getvalue())
    w.raw(face_bytes.getvalue())
    w.pad_to(4)
    w.u32(nodes.tell())
    w.raw(nodes.getvalue())
    w.raw(indices.getvalue())
    return w.getvalue()


def write_tree(tree, nodes, indices):
    # Depth first, as the shipped files: a split's two children side by side, then the left child's subtree, then
    # the right's; the leaves' face indices in the order of the leaves' nodes.
    leaves = []

    def emit(node, at):
        if node[0] == "leaf":
            leaves.append((at, node))
        else:
            _, axis, point, left, right = node
            children = nodes.tell()
            nodes.raw(bytes(16))
            nodes.patch(at, "iI", (point << 2) | axis, children)
            emit(left, children)
            emit(right, children + 8)

    root = nodes.tell()
    nodes.raw(bytes(8))
    emit(tree, root)
    for at, node in sorted(leaves, key=lambda leaf: leaf[0]):
        faces = node[1]
        low = node[2] if len(node) > 2 else 3
        nodes.patch(at, "iI", (len(faces) << 16) | (low & 0xFFFF), indices.tell() // 2)
        indices.write("%dH" % len(faces), *faces)


# --- building collision for new geometry ----------------------------------------------------------------------

def set_reaches(o):
    # The reach scale and each face's reach (see the top): as the shipped files (to within one; one more is safe -
    # it only lets the ray test look at the face).
    origin = o["bbox"][:3]
    if not o["faces"]:
        o["value"] = struct.pack("<f", 1.0)
        o["reach"] = b""
        return
    farthest = max(math.dist(v, origin) for v in o["verts"]) or 1.0
    scale = struct.unpack("<f", struct.pack("<f", 254.0 / farthest))[0]
    o["value"] = struct.pack("<f", scale)
    o["reach"] = bytes(min(255, math.ceil(max(math.dist(o["verts"][i], origin) for i in face[2:]) * scale) + 1)
                       for face in o["faces"])


LEAF_FACES = 50  # faces in a leaf before it's split (as the shipped trees: leaves of up to ~50 faces)


def build_tree(verts, faces):
    # A BSP tree over the faces (indices into `faces`): split at the median of the vertex coordinates on the axis that
    # duplicates fewest faces, the split point in sixteenths of a unit (the file's fixed point).
    def build(face_ids, depth, stuck):
        if len(face_ids) <= LEAF_FACES or depth > 40:
            return ("leaf", tuple(face_ids), 3)
        best = None
        for axis in range(3):
            if axis in stuck:
                continue
            coords = sorted(verts[faces[f][i]][axis] for f in face_ids for i in (2, 3, 4))
            point = int(math.floor(coords[len(coords) // 2] * 16.0))
            split = point / 16.0
            left, right = [], []
            for f in face_ids:
                values = [verts[faces[f][i]][axis] for i in (2, 3, 4)]
                if min(values) < split:
                    left.append(f)
                if max(values) >= split:
                    right.append(f)
            duplicates = len(left) + len(right) - len(face_ids)
            if best is None or duplicates < best[0]:
                best = (duplicates, axis, point, left, right)
        if best is None:
            return ("leaf", tuple(face_ids), 3)
        duplicates, axis, point, left, right = best
        if duplicates >= len(face_ids) // 2 or not left or not right:
            return ("leaf", tuple(face_ids), 3)
        stuck_left = stuck | {axis} if len(left) == len(face_ids) else stuck
        stuck_right = stuck | {axis} if len(right) == len(face_ids) else stuck
        return ("split", axis, point, build(left, depth + 1, stuck_left), build(right, depth + 1, stuck_right))

    return build(list(range(len(faces))), 0, frozenset())
