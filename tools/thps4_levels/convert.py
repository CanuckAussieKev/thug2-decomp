# THPS4 -> THUG2 conversion of a level's scene, collision and textures (in memory; thps4_levels.py writes them).

import math
import struct

import thps4
import thug2_col
import thug2_scene

# --- scene ------------------------------------------------------------------------------------------------------

# THUG2's built-in pixel shaders (0x004DD7F0 compiles them, 0x004D3ED0 picks one by the mesh's number):
PS_TEXTURED = 2           # tex t0; r0.rgb = t0 * c0 * v0 * 4, r0.a = v0.a * t0.a * 2 (vertex alpha)
PS_TEXTURED_FIXED_A = 3   # the same with r0.a = c4.a * t0.a * 2 (the pass's own alpha: vertex alpha ignored)
PS_UNTEXTURED = 4         # r0.rgb = v0 * c0 * 2, r0.a = v0.a
PS_FROM_MATERIAL = 1      # {u32, u32 size, data}: the data is skipped and the shader made from the material
                          # (0x004DA6D0, then 0x004DDC00) - what the shipped files do for 2+ passes

MESH_THUG2 = 0x20000      # set on every mesh of the shipped levels (the loader copies it to the mesh)


def convert_material(m):
    out = {
        "checksum": m["checksum"],
        "name_checksum": m["checksum"],
        "alpha_cutoff": m["alpha_cutoff"],
        "sorted": m["sorted"],
        "draw_order": m["draw_order"],
        "single_sided": m["single_sided"],
        "no_backface_culling": 0,
        "z_bias": 0,
        "grassify": m["grassify"],
        "specular_power": 0.0,
        "passes": [],
    }
    if m["grassify"]:
        out["grass"] = m["grass"]
    for index, p in enumerate(m["passes"][:4]):
        q = {k: p[k] for k in ("texture", "flags", "has_color", "color", "blend_mode", "fixed_alpha", "u_address",
                                "v_address", "filtering", "mip")}
        q["envmap_tiling"] = (3.0, 3.0)
        for key in ("uv_wibble", "vc_wibble", "animation"):
            if key in p:
                q[key] = p[key]
        if index > 0:
            q["flags"] &= ~thps4.PASS_VC_WIBBLE
        out["passes"].append(q)
    return out


def pixel_shader(material):
    passes = material["passes"]
    if len(passes) > 1:
        return PS_FROM_MATERIAL
    if not passes or not passes[0]["texture"] or not passes[0]["flags"] & thug2_scene.PASS_TEXTURED:
        return PS_UNTEXTURED
    if passes[0]["flags"] & thug2_scene.PASS_IGNORE_VERTEX_ALPHA:
        return PS_TEXTURED_FIXED_A
    return PS_TEXTURED


def bounds(points):
    lo = [min(p[k] for p in points) for k in range(3)]
    hi = [max(p[k] for p in points) for k in range(3)]
    center = [(lo[k] + hi[k]) / 2 for k in range(3)]
    radius = math.sqrt(max(sum((p[k] - center[k]) ** 2 for k in range(3)) for p in points))
    return lo, hi, center, radius


def convert_scene(scene4, warnings, is_sky=False):
    materials = [convert_material(m) for m in scene4["materials"]]
    by_checksum = {m["checksum"]: m for m in materials}
    sectors = []
    for s in scene4["sectors"]:
        flags = s["flags"]
        if flags & thps4.SEC_WEIGHTS or s["bone"] != -1:
            warnings.append("sector %08X: skinned - left out" % s["checksum"])
            continue
        has_normals = bool(flags & thps4.SEC_NORMALS)
        has_colors = bool(flags & thps4.SEC_COLORS)
        has_uvs = bool(flags & thps4.SEC_TEXCOORDS)
        out_flags = (flags & ~(thps4.SEC_COLOR_WIBBLES | thps4.SEC_WEIGHTS)) | thug2_scene.SEC_COLORS
        sector = {"checksum": s["checksum"], "bone": -1, "flags": out_flags, "bbox": s["bbox"],
                  "sphere": s["sphere"], "meshes": []}
        if "billboard" in s:
            sector["billboard"] = s["billboard"]
        for mesh in s["meshes"]:
            strip = mesh["strip"]
            if len(strip) < 3:
                continue
            material = by_checksum.get(mesh["material"])
            if material is None:
                warnings.append("sector %08X: material %08X missing - mesh left out" % (s["checksum"], mesh["material"]))
                continue
            used = sorted(set(strip))
            if len(used) > 0xFFFF:
                warnings.append("sector %08X: mesh of %d vertices left out" % (s["checksum"], len(used)))
                continue
            local = {v: i for i, v in enumerate(used)}
            new_strip = tuple(local[v] for v in strip)
            sets = max(1, min(len(material["passes"]), s.get("uv_sets", 0) or 1))
            stride = 12 + (12 if has_normals else 0) + 4 + 8 * sets
            data = bytearray()
            for v in used:
                data += struct.pack("<3f", *s["positions"][v])
                if has_normals:
                    data += struct.pack("<3f", *s["normals"][v])
                if has_colors:
                    b, g, r, a = s["colors"][v]
                    if is_sky:
                        # THPS4's skies are lit 255 (full); THUG2's shaders take 128 as full (x4 / x2 of
                        # 0.5): halved, or the sky comes out twice as bright, washed out.
                        b, g, r = b >> 1, g >> 1, r >> 1
                    data += bytes((b, g, r, a))
                else:
                    data += b"\x80\x80\x80\x80"
                if has_uvs:
                    uvs = s["uvs"][v]
                    for k in range(sets):
                        k2 = k if 2 * k + 1 < len(uvs) else 0
                        data += struct.pack("<2f", uvs[2 * k2], uvs[2 * k2 + 1])
                else:
                    data += bytes(8 * sets)
            fvf = thug2_scene.FVF_XYZ | thug2_scene.FVF_DIFFUSE | (sets << thug2_scene.FVF_TEXCOUNT_SHIFT)
            if has_normals:
                fvf |= thug2_scene.FVF_NORMAL
            offsets = (12, 24, 28) if has_normals else (0, 12, 16)
            lo, hi, center, radius = bounds([s["positions"][v] for v in used])
            out = {
                "sphere": (center[0], center[1], center[2], radius),
                "bbox": (lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]),
                "flags": mesh["flags"] | MESH_THUG2,
                "material": mesh["material"],
                "lods": [new_strip],
                "strips": [new_strip],
                "stride": stride,
                "vertex_count": len(used),
                "buffers": [(0, bytes(data))],
                "vertex_shader": (fvf, 0),
                "offsets": offsets,
                "has_wibble": 0,
                "index_sets": 1,
                "pixel_shader": pixel_shader(material),
            }
            if out["pixel_shader"] == PS_FROM_MATERIAL:
                out["ps_value"] = 0
                out["ps_data"] = b""
            if not has_normals:
                sector["flags"] &= ~thug2_scene.SEC_NORMALS
            sector["meshes"].append(out)
        sectors.append(sector)
    return {"versions": (1, 1, 1), "materials": materials, "sectors": sectors, "hierarchy_count": 0, "tail": b""}


# --- collision --------------------------------------------------------------------------------------------------

# THPS4's face flag 0x80 means the camera goes through the face; THUG2's 0x80 means the camera collides with it.
FACE_CAMERA = 0x0080
# The face flags kept (the ones THUG2's own levels use: wall ride, vert, non-collidable, decal, trigger, camera,
# shadow, no-shadow wall, invisible). THPS4 sets 0x2000 on every face and 0x4000 / 0x8000 (render passes) on some -
# none of them in THUG2's collision files.
FACE_KEEP = 0x15FC


def convert_col(col4, keep_triggers, warnings):
    objects = []
    for o in col4["objects"]:
        verts = o["verts"]
        if not verts or not o["faces"]:
            # (kept, as the shipped files keep theirs: an empty leaf, the box as it was)
            objects.append({"checksum": o["checksum"], "flags": o["flags"], "small_faces": 1, "fixed_verts": 1,
                            "value": struct.pack("<f", 1.0), "bbox": tuple(o["bbox"]), "verts": [],
                            "intensities": b"", "faces": [], "reach": b"", "bsp": ("leaf", (), 3)})
            continue
        faces = []
        for flags, terrain, a, b, c in o["faces"]:
            flags = (flags ^ FACE_CAMERA) & FACE_KEEP
            if not keep_triggers(o["checksum"]):
                flags &= ~thug2_col.FACE_TRIGGER
            faces.append((flags, terrain, a, b, c))
        lo = [min(v[k] for v in verts) for k in range(3)]
        hi = [max(v[k] for v in verts) for k in range(3)]
        # Fixed-point vertices (sixteenths, up to 4096 units from the box minimum) as the shipped files use for
        # most objects; small faces (u8 vertex indices) when they fit.
        fixed = all(hi[k] - lo[k] < 4000.0 for k in range(3))
        small = len(verts) <= 256
        out = {"checksum": o["checksum"], "flags": o["flags"], "small_faces": int(small),
               "fixed_verts": int(fixed)}
        if fixed:
            lo = [math.floor(x * 16.0) / 16.0 for x in lo]
            verts = [tuple(lo[k] + round((v[k] - lo[k]) * 16.0) / 16.0 for k in range(3)) for v in verts]
            hi = [max(v[k] for v in verts) for k in range(3)]
        out["bbox"] = (lo[0], lo[1], lo[2], 1.0, hi[0], hi[1], hi[2], 1.0)
        out["verts"] = verts
        out["intensities"] = bytes(i[0] for i in o["intensities"])
        out["faces"] = faces
        thug2_col.set_reaches(out)
        out["bsp"] = thug2_col.build_tree(verts, faces)
        objects.append(out)
    return {"objects": objects, "pad": 0}
