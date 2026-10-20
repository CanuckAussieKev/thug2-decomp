// THPS3 PC level files (Data\Levels\<Lvl>\): RenderWare 3.1 binary streams (every chunk: u32 type, u32 size,
// u32 version 0x310) with Neversoft's own plugin chunks (0x0294AF01...).
//
//   <lvl>.bsp   an RpWorld: world struct, material list, the plane-section tree whose leaves are the atomic sectors
//               (each one of Neversoft's objects: its checksum name and per-face flags in a Neversoft extension),
//               then the world's extension.
//   <lvl>.dff   clumps one after another (a level's movable objects: fans, doors...; each its checksum name in the
//               clump's extension). Each clump's declared size leaves out the 12-byte header of the Neversoft chunk
//               in its extension: the clumps are walked by their children, not their sizes.
//   <lvl>.tdx   a texture dictionary: D3D8 texture natives (every one in THPS3 PC is 32-bit, one mip level).
//   <lvl>.qb    the node array (tools/level_port/qb.ts reads it: THPS4's token format).
//
// Read field by field; anything not as expected throws FormatError (the self-check parses every file).

import { FormatError, Reader } from "../level_port/binio.ts";
import { decodeLatin1 } from "../lib/py.ts";
import { unpackNumbers } from "../lib/struct.ts";

export const VERSION = 0x310;

export const STRUCT = 0x01;
export const STRING = 0x02;
export const EXTENSION = 0x03;
export const TEXTURE = 0x06;
export const MATERIAL = 0x07;
export const MATLIST = 0x08;
export const ATOMIC_SECTOR = 0x09;
export const PLANE_SECTOR = 0x0a;
export const WORLD = 0x0b;
export const FRAMELIST = 0x0e;
export const GEOMETRY = 0x0f;
export const CLUMP = 0x10;
export const ATOMIC = 0x14;
export const TEXTURE_NATIVE = 0x15;
export const TEXTURE_DICTIONARY = 0x16;
export const GEOMETRY_LIST = 0x1a;
export const BINMESH = 0x50e;
export const NEVERSOFT = 0x0294af01; // material, texture, sector, atomic, clump data
export const NEVERSOFT_GEOMETRY = 0x0294af02;
export const NEVERSOFT_WORLD = 0x0294af04;

// World / geometry flags.
export const GEO_TRISTRIP = 0x01;
export const GEO_POSITIONS = 0x02;
export const GEO_TEXTURED = 0x04;
export const GEO_PRELIT = 0x08;
export const GEO_NORMALS = 0x10;
export const GEO_LIGHT = 0x20;
export const GEO_MODULATE = 0x40;
export const GEO_TEXTURED2 = 0x80;

// Neversoft material pass flags (byte 19 of the material extension).
export const MAT_INVISIBLE = 0x02; // transparent.png, wire, the collision-only and trigger planes
export const MAT_TRANSPARENT = 0x08; // alpha-blended (leaves, signs, water)
export const MAT_DECAL = 0x10; // stickers, graffiti, stripes

// The material's uv flags (the byte after the texture).
export const UV_WIBBLE = 0x01;
export const UV_ENVIRONMENT = 0x80;

const hex = (n: number): string => n.toString(16).toUpperCase();

// A chunk inside its parent: [type, data offset, size].
export type Chunk = [number, number, number];

export function chunkHeader(data: Buffer, offset: number, name: string): [number, number, number] {
    if (offset + 12 > data.length) throw new FormatError(`${name}: chunk header past the end at 0x${hex(offset)}`);
    return unpackNumbers("<3I", data, offset) as [number, number, number];
}

export function children(data: Buffer, start: number, end: number, name: string): Chunk[] {
    // The chunks inside [start, end): [type, data offset, size].
    const out: Chunk[] = [];
    let offset = start;
    while (offset < end) {
        const [kind, size, version] = chunkHeader(data, offset, name);
        if (version !== VERSION)
            throw new FormatError(`${name}: chunk ${hex(kind)} at 0x${hex(offset)} has version ${hex(version)}`);
        if (offset + 12 + size > end)
            throw new FormatError(`${name}: chunk ${hex(kind)} at 0x${hex(offset)} runs past its parent`);
        out.push([kind, offset + 12, size]);
        offset += 12 + size;
    }
    if (offset !== end) throw new FormatError(`${name}: chunks end at 0x${hex(offset)}, not 0x${hex(end)}`);
    return out;
}

export function only(kids: readonly Chunk[], kind: number, name: string, where: string): Chunk {
    const found = kids.filter((k) => k[0] === kind);
    if (found.length !== 1) throw new FormatError(`${name}: ${found.length} chunks ${hex(kind)} in ${where}`);
    return found[0]!;
}

export function cstring(raw: Buffer): string {
    const end = raw.indexOf(0);
    return decodeLatin1(end < 0 ? raw : raw.subarray(0, end));
}

// --- materials ---------------------------------------------------------------------------------------------------
// A material: struct {u32 flags, u8 RGBA colour, u32 unused, u32 textured, 3f ambient / specular / diffuse}, the
// texture (struct {u8 filter, u8 addressing (u nibble, v nibble), u16}, name, mask name, extension), then the
// extension with Neversoft's material:
//   u32 id, 12 bytes 0, u8 1, u8 pass count, u8 1, then per pass (the first's):
//     u8 flags (MAT_*), u8 terrain type (the first pass only), 4 x RGBA (colour, then three unused), u32 blend (the
//     PS2's alpha blend register: (A - B) * C + D, two bits each from the bottom: 0 source colour, 1 destination
//     colour, 2 zero / fixed alpha), u32 fixed alpha (0x80 = 1), u8 has texture, u16 0, the texture chunk (as the
//     material's), u8 uv flags (UV_*), 8f uv wibble (u / v velocity, frequency, amplitude, phase), 4f, then the
//     vertex colour wibble (u8, u8 has one, u32, u32 key count, keys {RGBA, u32 time}).
//   Further passes have no flags / terrain and no texture in THPS3's files (23 two-pass materials): not used.

export interface RwTexture {
    name: string;
    mask: string;
    filter: number;
    uAddress: number;
    vAddress: number;
}

export interface NsMaterial {
    id: number;
    passCount: number;
    flags: number;
    terrain: number;
    colors: number[][]; // 4 x RGBA
    blend: number;
    fixedAlpha: number;
    texture: RwTexture | null;
    uvFlags: number;
    uvWibble?: number[]; // 8f
}

export interface RwMaterial {
    rwColor: number[];
    texture: RwTexture | null;
    ns: NsMaterial | null;
}

export function readTextureChunk(data: Buffer, offset: number, size: number, name: string): RwTexture {
    const kids = children(data, offset, offset + size, name);
    const s = only(kids, STRUCT, name, "texture");
    const [filter, addressing] = unpackNumbers("<BB", data, s[1]) as [number, number];
    const strings = kids.filter((k) => k[0] === STRING).map((k) => cstring(data.subarray(k[1], k[1] + k[2])));
    if (!strings.length) throw new FormatError(`${name}: texture without a name at 0x${hex(offset)}`);
    return {
        name: strings[0]!,
        mask: strings.length > 1 ? strings[1]! : "",
        filter,
        uAddress: addressing & 0xf,
        vAddress: addressing >> 4,
    };
}

export function readNeversoftMaterial(raw: Buffer, name: string): NsMaterial {
    const r = new Reader(raw, 0, name);
    const id = r.u32();
    r.bytes(12);
    const [, passCount] = r.read("3B") as [number, number, number];
    const [flags, terrain] = r.read("2B") as [number, number];
    const colors = [0, 1, 2, 3].map(() => r.read("4B"));
    const [blend, fixedAlpha] = r.read("2I") as [number, number];
    const hasTexture = r.u8();
    r.bytes(2);
    let texture: RwTexture | null = null;
    if (hasTexture) {
        if (r.offset + 12 > raw.length) throw new RangeError("unpack_from requires a buffer of at least 12 bytes"); // (struct.error)
        const [kind, size, version] = unpackNumbers("<3I", raw, r.offset) as [number, number, number];
        if (kind !== TEXTURE || version !== VERSION)
            throw new FormatError(`${name}: material texture chunk ${hex(kind)}`);
        texture = readTextureChunk(raw, r.offset + 12, size, name);
        r.offset += 12 + size;
    }
    const m: NsMaterial = { id, passCount, flags, terrain, colors, blend, fixedAlpha, texture, uvFlags: 0 };
    if (raw.length - r.offset >= 49) {
        // (untextured materials have 24 bytes of zeros here instead)
        m.uvFlags = r.u8();
        m.uvWibble = r.read("8f");
        r.read("4f");
    }
    return m;
}

export function readMaterial(data: Buffer, offset: number, size: number, name: string): RwMaterial {
    const kids = children(data, offset, offset + size, name);
    const s = only(kids, STRUCT, name, "material");
    const rwColor = [...data.subarray(s[1] + 4, s[1] + 8)];
    unpackNumbers("<I4BII", data, s[1]); // (the struct's size checked, as Python's unpack_from)
    const m: RwMaterial = { rwColor, texture: null, ns: null };
    for (const [kind, o, n] of kids) {
        if (kind === TEXTURE) {
            m.texture = readTextureChunk(data, o, n, name);
        } else if (kind === EXTENSION) {
            for (const [kind2, o2, n2] of children(data, o, o + n, name)) {
                if (kind2 === NEVERSOFT && n2 >= 48) m.ns = readNeversoftMaterial(data.subarray(o2, o2 + n2), name);
            }
        }
    }
    return m;
}

export function readMaterialList(data: Buffer, offset: number, size: number, name: string): RwMaterial[] {
    const kids = children(data, offset, offset + size, name);
    const s = only(kids, STRUCT, name, "material list");
    const count = unpackNumbers("<I", data, s[1])[0]!;
    const refs = unpackNumbers(`<${count}i`, data, s[1] + 4);
    const materials = kids.filter((k) => k[0] === MATERIAL).map(([, o, n]) => readMaterial(data, o, n, name));
    const out: RwMaterial[] = [];
    for (const ref of refs) {
        // -1: the next material chunk; else a copy of an earlier one
        const m = ref < 0 ? materials[out.length] : out.at(ref);
        if (m === undefined) throw new RangeError("list index out of range"); // (IndexError)
        out.push(m);
    }
    if (refs.filter((r) => r < 0).length !== materials.length)
        throw new FormatError(`${name}: ${materials.length} materials for ${refs.length} new entries`);
    return out;
}

// --- the world (.bsp) --------------------------------------------------------------------------------------------
// World struct: i32 root is a sector, 3f inverse origin, 3f ambient / specular / diffuse, u32 triangles, vertices,
// planes, sectors, collision sector size, flags (GEO_*).
// Atomic sector struct: u32 material base, u32 triangles, u32 vertices, 3f box min, 3f box max, 2 x u32, then the
// vertices (3f), normals (4 x i8, GEO_NORMALS), colours (RGBA, GEO_PRELIT; 255 = full), uvs (2f), second uvs (2f,
// GEO_TEXTURED2), triangles (u16 material, 3 x u16 vertex). Its extension: the bin mesh (strips or lists per
// material), RW's collision tree (not used), material effects, and Neversoft's:
//   u32 6, u16 face flags per triangle (THUG2's collision flag meanings), u32 0, u32 checksum (the object's name),
//   u8, u8, u8 has vertex data (+ 2 bytes per vertex: wibble data, not used).

export type Triangle = [number, number, number, number]; // (material, a, b, c)

export interface BinMesh {
    strips: boolean;
    meshes: { material: number; indices: number[] }[];
}

// An atomic sector (and, made by the converter, a game object's geometry in the same form).
export interface Sector {
    checksum: number;
    materialBase: number;
    bbox?: number[];
    positions: number[][];
    normals?: number[][];
    colors?: number[][] | null; // RGBA
    uvs: number[][]; // per vertex: (u0, v0, u1, v1...)
    uvSets: number;
    triangles: Triangle[];
    binmesh: BinMesh | null;
    faceFlags?: number[];
    nsFlags?: number;
}

export interface World {
    flags: number;
    triangles: number;
    vertices: number;
    sectorCount: number;
    materials: RwMaterial[];
    sectors: Sector[];
}

export function readUvSets(r: Reader, sets: number, count: number): number[][] {
    // RenderWare stores each uv set as its own array (all of set 0, then all of set 1): per vertex (u0, v0, u1, v1).
    const arrays: number[][][] = [];
    for (let k = 0; k < sets; k++) {
        const set: number[][] = [];
        for (let i = 0; i < count; i++) set.push(r.read("2f"));
        arrays.push(set);
    }
    if (!sets) return [];
    const out: number[][] = [];
    // (concat of the read arrays: a NaN's payload bits kept - struct.ts doubleArray)
    for (let i = 0; i < count; i++) out.push(arrays[0]![i]!.concat(...arrays.slice(1).map((a) => a[i]!)));
    return out;
}

export function readBinmesh(data: Buffer, offset: number, size: number, name: string): BinMesh {
    const r = new Reader(data.subarray(offset, offset + size), 0, name);
    const [faceType, count] = r.read("3I") as [number, number, number];
    const meshes: BinMesh["meshes"] = [];
    for (let k = 0; k < count; k++) {
        const [n, material] = r.read("2I") as [number, number];
        meshes.push({ material, indices: r.read(`${n}I`) });
    }
    r.expectEnd();
    return { strips: (faceType & 1) === 1, meshes };
}

export function readSector(data: Buffer, offset: number, size: number, flags: number, name: string): Sector {
    const kids = children(data, offset, offset + size, name);
    const s = only(kids, STRUCT, name, "atomic sector");
    const r = new Reader(data.subarray(s[1], s[1] + s[2]), 0, name);
    const [base, ntri, nvert] = r.read("3I") as [number, number, number];
    const bbox = r.read("6f");
    r.read("2I");
    const positions: number[][] = [];
    for (let i = 0; i < nvert; i++) positions.push(r.read("3f"));
    const sec: Sector = {
        checksum: 0,
        materialBase: base,
        bbox,
        positions,
        uvs: [],
        uvSets: 0,
        triangles: [],
        binmesh: null,
    };
    if (flags & GEO_NORMALS) {
        const normals: number[][] = [];
        for (let i = 0; i < nvert; i++)
            normals.push(
                r
                    .read("4b")
                    .slice(0, 3)
                    .map((x) => x / 127.0),
            );
        sec.normals = normals;
    }
    if (flags & GEO_PRELIT) {
        const colors: number[][] = [];
        for (let i = 0; i < nvert; i++) colors.push(r.read("4B"));
        sec.colors = colors;
    }
    const sets = flags & GEO_TEXTURED2 ? 2 : flags & GEO_TEXTURED ? 1 : 0;
    sec.uvs = readUvSets(r, sets, nvert);
    sec.uvSets = sets;
    for (let i = 0; i < ntri; i++) sec.triangles.push(r.read("4H") as Triangle); // (material, a, b, c)
    r.expectEnd();
    for (const [, a, b, c] of sec.triangles) {
        if (Math.max(a, b, c) >= nvert) throw new FormatError(`${name}: triangle past its sector's ${nvert} vertices`);
    }
    sec.faceFlags = new Array<number>(ntri).fill(0);
    sec.nsFlags = 0;
    for (const [kind, o, n] of kids) {
        if (kind !== EXTENSION) continue;
        for (const [kind2, o2, n2] of children(data, o, o + n, name)) {
            if (kind2 === BINMESH) {
                sec.binmesh = readBinmesh(data, o2, n2, name);
            } else if (kind2 === NEVERSOFT) {
                const e = new Reader(data.subarray(o2, o2 + n2), 0, name);
                if (e.u32() !== 6) throw new FormatError(`${name}: sector extension not version 6`);
                sec.faceFlags = e.read(`${ntri}H`);
                e.u32();
                sec.checksum = e.u32();
                const [nsFlags, , hasVertexData] = e.read("3B") as [number, number, number];
                sec.nsFlags = nsFlags;
                if (hasVertexData) e.bytes(2 * nvert);
                e.expectEnd();
            }
        }
    }
    return sec;
}

export function sectorsOf(
    data: Buffer,
    offset: number,
    size: number,
    flags: number,
    name: string,
    out: Sector[],
): void {
    // The plane-section tree's leaves in order (a plane sector: struct {u32 type, f32 value, i32 left is a sector,
    // i32 right is a sector, 2f}, the left child, the right one).
    for (const [kind, o, n] of children(data, offset, offset + size, name)) {
        if (kind === ATOMIC_SECTOR) out.push(readSector(data, o, n, flags, name));
        else if (kind === PLANE_SECTOR) sectorsOf(data, o, n, flags, name, out);
        else if (kind !== STRUCT) throw new FormatError(`${name}: chunk ${hex(kind)} in the plane sectors`);
    }
}

export function readBsp(data: Buffer, name = "bsp"): World {
    const [kind, size] = chunkHeader(data, 0, name);
    if (kind !== WORLD || 12 + size !== data.length)
        throw new FormatError(`${name}: not a world (chunk ${hex(kind)}, ${size} bytes)`);
    const kids = children(data, 12, 12 + size, name);
    const s = only(kids, STRUCT, name, "world");
    const fields = unpackNumbers("<i3f3f6I", data, s[1]);
    const m = only(kids, MATLIST, name, "world");
    const world: World = {
        flags: fields[12]!,
        triangles: fields[7]!,
        vertices: fields[8]!,
        sectorCount: fields[10]!,
        materials: readMaterialList(data, m[1], m[2], name),
        sectors: [],
    };
    const sectors: Sector[] = [];
    for (const [kind2, o, n] of kids) {
        if (kind2 === PLANE_SECTOR) sectorsOf(data, o, n, world.flags, name, sectors);
        else if (kind2 === ATOMIC_SECTOR) sectors.push(readSector(data, o, n, world.flags, name));
    }
    if (sectors.length !== world.sectorCount)
        throw new FormatError(`${name}: ${sectors.length} sectors, the world says ${world.sectorCount}`);
    if (sectors.reduce((t, x) => t + x.triangles.length, 0) !== world.triangles)
        throw new FormatError(`${name}: triangle count differs from the world's`);
    world.sectors = sectors;
    return world;
}

// --- the clumps (.dff) -------------------------------------------------------------------------------------------
// A clump: struct {u32 atomic count}, the frame list (struct {u32 count, per frame 3x3f rotation, 3f position,
// i32 parent, u32}, an extension each), the geometry list (struct {u32 count}, geometries), the atomics (struct
// {u32 frame, u32 geometry, u32 flags, u32}, extension), the extension (Neversoft: u32 1, u32 checksum).
// A geometry: struct {u16 flags, u16, u32 triangles, u32 vertices, u32 morph targets, 3f lighting (RW 3.1), the
// colours (GEO_PRELIT), uvs, triangles (u16 b, u16 a, u16 material, u16 c), per morph target {4f sphere, u32 has
// positions, u32 has normals, positions, normals}}, its material list, the extension (bin mesh...).

export interface Geometry {
    flags: number;
    colors: number[][];
    uvs: number[][];
    uvSets: number;
    triangles: Triangle[];
    positions: number[][];
    normals: number[][];
    materials: RwMaterial[];
    binmesh: BinMesh | null;
}

export interface Frame {
    rotation: number[];
    position: number[];
    parent: number;
}

export interface Atomic {
    frame: number;
    geometry: number;
    flags: number;
}

export interface Clump {
    frames: Frame[];
    geometries: Geometry[];
    atomics: Atomic[];
    checksum: number;
    atomicCount?: number;
}

export function readGeometry(data: Buffer, offset: number, size: number, name: string): Geometry {
    const kids = children(data, offset, offset + size, name);
    const s = only(kids, STRUCT, name, "geometry");
    const r = new Reader(data.subarray(s[1], s[1] + s[2]), 0, name);
    const [flags, , ntri, nvert, targets] = r.read("HHIII") as [number, number, number, number, number];
    r.read("3f");
    const colors: number[][] = [];
    if (flags & GEO_PRELIT) for (let i = 0; i < nvert; i++) colors.push(r.read("4B"));
    const sets = flags & GEO_TEXTURED2 ? 2 : flags & GEO_TEXTURED ? 1 : 0;
    const uvs = readUvSets(r, sets, nvert);
    const triangles: Triangle[] = [];
    for (let i = 0; i < ntri; i++) {
        const [b, a, material, c] = r.read("4H") as [number, number, number, number];
        triangles.push([material, a, b, c]);
    }
    if (targets < 1) throw new FormatError(`${name}: geometry without a morph target`);
    r.read("4f");
    const [hasPositions, hasNormals] = r.read("2I") as [number, number];
    const positions: number[][] = [];
    if (hasPositions) for (let i = 0; i < nvert; i++) positions.push(r.read("3f"));
    const normals: number[][] = [];
    if (hasNormals) for (let i = 0; i < nvert; i++) normals.push(r.read("3f"));
    for (let t = 0; t < targets - 1; t++) {
        r.read("4f");
        const [p, n] = r.read("2I") as [number, number];
        r.bytes(12 * nvert * ((p ? 1 : 0) + (n ? 1 : 0)));
    }
    r.expectEnd();
    const m = only(kids, MATLIST, name, "geometry");
    const g: Geometry = {
        flags,
        colors,
        uvs,
        uvSets: sets,
        triangles,
        positions,
        normals,
        materials: readMaterialList(data, m[1], m[2], name),
        binmesh: null,
    };
    for (const [kind, o, n] of kids) {
        if (kind === EXTENSION) {
            for (const [kind2, o2, n2] of children(data, o, o + n, name)) {
                if (kind2 === BINMESH) g.binmesh = readBinmesh(data, o2, n2, name);
            }
        }
    }
    return g;
}

export function readClump(data: Buffer, offset: number, name: string): [Clump, number] {
    // Returns [clump, the offset after it].
    const [kind0, , version0] = chunkHeader(data, offset, name);
    if (kind0 !== CLUMP || version0 !== VERSION) throw new FormatError(`${name}: no clump at 0x${hex(offset)}`);
    let o = offset + 12;
    const clump: Clump = { frames: [], geometries: [], atomics: [], checksum: 0 };
    for (;;) {
        const [kind, size, version] = chunkHeader(data, o, name);
        if (version !== VERSION)
            throw new FormatError(`${name}: chunk ${hex(kind)} at 0x${hex(o)} has version ${hex(version)}`);
        const body = o + 12;
        if (kind === STRUCT) {
            clump.atomicCount = unpackNumbers("<I", data, body)[0]!;
        } else if (kind === FRAMELIST) {
            const kids = children(data, body, body + size, name);
            const s = only(kids, STRUCT, name, "frame list");
            const count = unpackNumbers("<I", data, s[1])[0]!;
            for (let i = 0; i < count; i++) {
                const values = unpackNumbers("<12fiI", data, s[1] + 4 + 56 * i);
                clump.frames.push({
                    rotation: values.slice(0, 9),
                    position: values.slice(9, 12),
                    parent: values[12]!,
                });
            }
        } else if (kind === GEOMETRY_LIST) {
            for (const [kind2, o2, n2] of children(data, body, body + size, name)) {
                if (kind2 === GEOMETRY) clump.geometries.push(readGeometry(data, o2, n2, name));
            }
        } else if (kind === ATOMIC) {
            const kids = children(data, body, body + size, name);
            const s = only(kids, STRUCT, name, "atomic");
            const [frame, geometry, flags] = unpackNumbers("<4I", data, s[1]) as [number, number, number, number];
            clump.atomics.push({ frame, geometry, flags });
        } else if (kind === EXTENSION) {
            for (const [kind2, o2, n2] of children(data, body, body + size, name)) {
                if (kind2 === NEVERSOFT && n2 >= 8) clump.checksum = unpackNumbers("<I", data, o2 + 4)[0]!;
            }
            return [clump, body + size];
        } else {
            throw new FormatError(`${name}: chunk ${hex(kind)} in a clump at 0x${hex(o)}`);
        }
        o = body + size;
    }
}

export function readDff(data: Buffer, name = "dff"): Clump[] {
    const clumps: Clump[] = [];
    let offset = 0;
    while (offset < data.length) {
        const [clump, next] = readClump(data, offset, name);
        offset = next;
        if (clump.atomics.length !== clump.atomicCount) {
            throw new FormatError(
                `${name}: clump ${clump.checksum.toString(16).toUpperCase().padStart(8, "0")} has ` +
                    `${clump.atomics.length} atomics, says ${clump.atomicCount}`,
            );
        }
        clumps.push(clump);
    }
    return clumps;
}

// --- textures (.tdx) ---------------------------------------------------------------------------------------------
// A texture dictionary: struct {u16 count, u16 0}, texture natives, an extension. A texture native (D3D8): struct
// {u32 platform 8, u8 filter, u8 addressing, u16, name[128], mask[128], u32 raster format, u32 has alpha, u16 width,
// u16 height, u8 depth, u8 levels, u8 raster type, u8 compression, (palette), per level {u32 size, data}}, an
// extension.

export const RASTER_8888 = 0x0500;
export const RASTER_888 = 0x0600;
export const RASTER_PAL8 = 0x2000;
export const RASTER_PAL4 = 0x4000;

export interface TdxTexture {
    name: string;
    mask: string;
    filter: number;
    addressing: number;
    format: number;
    hasAlpha: number;
    width: number;
    height: number;
    depth: number;
    type: number;
    compression: number;
    levels: Buffer[];
}

export function readTdx(data: Buffer, name = "tdx"): TdxTexture[] {
    const [kind0, size0] = chunkHeader(data, 0, name);
    if (kind0 !== TEXTURE_DICTIONARY || 12 + size0 !== data.length)
        throw new FormatError(`${name}: not a texture dictionary`);
    const textures: TdxTexture[] = [];
    for (const [kind, o, n] of children(data, 12, 12 + size0, name)) {
        if (kind !== TEXTURE_NATIVE) continue;
        const kids = children(data, o, o + n, name);
        const s = only(kids, STRUCT, name, "texture native");
        const r = new Reader(data.subarray(s[1], s[1] + s[2]), 0, name);
        const [platform, filter, addressing] = r.read("IBBH") as [number, number, number, number];
        if (platform !== 8) throw new FormatError(`${name}: texture platform ${platform}`);
        const texName = cstring(r.bytes(128));
        const mask = cstring(r.bytes(128));
        const [format, hasAlpha, width, height, depth, levels, type, compression] = r.read("IIHHBBBB") as [
            number,
            number,
            number,
            number,
            number,
            number,
            number,
            number,
        ];
        const t: TdxTexture = {
            name: texName,
            mask,
            filter,
            addressing,
            format,
            hasAlpha,
            width,
            height,
            depth,
            type,
            compression,
            levels: [],
        };
        if (
            format & (RASTER_PAL8 | RASTER_PAL4) ||
            compression ||
            depth !== 32 ||
            ((format & 0xf00) !== RASTER_8888 && (format & 0xf00) !== RASTER_888)
        ) {
            throw new FormatError(
                `${name}: texture ${t.name} is format ${hex(format)} depth ${depth} compression ${compression} ` +
                    "(only 32-bit ones are read)",
            );
        }
        for (let i = 0; i < levels; i++) {
            const level = r.bytes(r.u32());
            const w = Math.max(1, width >> i);
            const h = Math.max(1, height >> i);
            if (level.length !== w * h * 4)
                throw new FormatError(`${name}: texture ${t.name} level ${i} is ${level.length} bytes`);
            t.levels.push(level);
        }
        r.expectEnd();
        textures.push(t);
    }
    return textures;
}
