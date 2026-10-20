// THPS4 PC level files (Game\data\levels\<Lvl>\): <lvl>scn.dat (scene) and <lvl>col.dat (collision, version 8).
// The texture dictionary <lvl>tex.dat is THUG2's format already (tools/level_port/tex.ts).

import { FormatError, Reader } from "../level_port/binio.ts";
import type { SourceColObject, SourceSector } from "../level_port/geometry.ts";
import type * as thug2Scene from "../level_port/thug2_scene.ts";
import { floorDiv, hex8, pyMax, pyMod } from "../lib/py.ts";
import { doubleArray } from "../lib/struct.ts";

export const SEC_TEXCOORDS = 0x01;
export const SEC_COLORS = 0x02;
export const SEC_NORMALS = 0x04;
export const SEC_WEIGHTS = 0x10;
export const SEC_COLOR_WIBBLES = 0x800;
export const SEC_BILLBOARD = 0x00800000;

export const PASS_UV_WIBBLE = 1 << 0;
export const PASS_VC_WIBBLE = 1 << 1;
export const PASS_ANIMATES = 1 << 11;

// A new array of float values (doubleArray: a float32 NaN's payload kept - docs/typescript.md, the NaN payload trap).
export function floats(...values: number[]): number[] {
    const out = doubleArray();
    for (const v of values) out.push(v);
    return out;
}

const hex = (n: number): string => n.toString(16).toUpperCase();

// --- scene ------------------------------------------------------------------------------------------------------
//   u32 material version, mesh version, vertex version; u32 material count, materials; i32 sector count, sectors.
// A material: u32 checksum, u32 pass count, u32 alpha cutoff, u8 sorted, f32 draw order, u8 single sided,
//   u8 grassify (+ f32 height, i32 layers), passes: u32 texture, u32 flags, u8 has colour, 3f colour, u32 blend mode,
//   u32 fixed alpha, u32 u / v addressing, u32 filtering (THUG2's has the env-map tiling 2f before it), 9f lighting
//   colours (diffuse / specular / ambient; THUG2 has none), then as THUG2: uv wibble, vc wibble, animation, mip info.
// A sector: u32 checksum, i32 bone, u32 flags, u32 mesh count, 6f box, 4f sphere, (billboard), u32 vertex count,
//   u32 stride, then the vertex arrays one after another (positions; normals; weights + bone indices; uv sets
//   {u32 count, then count x 2f per vertex}; colours BGRA; colour wibble indices), then the meshes:
//   u32 flags, u32 material checksum, u32 index count, u16 strip indices (into the sector's vertices).

export interface Pass {
    texture: number;
    flags: number;
    hasColor: number;
    color: number[]; // 3f
    blendMode: number;
    fixedAlpha: number;
    uAddress: number;
    vAddress: number;
    filtering: number;
    lighting: number[]; // 9f
    uvWibble?: number[]; // 8f
    vcWibble?: [number, number[]][]; // (phase, keys as 2 ints each)
    animation?: [number, number, number, number[][]]; // period, iterations, phase, keys {u32, u32}
    mip: number[]; // I I f f
}

export interface Material {
    checksum: number;
    alphaCutoff: number;
    sorted: number;
    drawOrder: number;
    singleSided: number;
    grassify: number;
    grass?: number[]; // f i
    passes: Pass[];
}

export interface Mesh {
    flags: number;
    material: number;
    strip: number[];
}

export interface Sector extends SourceSector {
    stride?: number;
    weights?: number[][]; // 4f per vertex
    bones?: number[][]; // 4H per vertex
    wibbles?: Buffer;
    meshes: Mesh[];
}

export interface Scene {
    versions: number[];
    materials: Material[];
    sectors: Sector[];
    tail: Buffer;
}

export function readMaterial(r: Reader): Material {
    const checksum = r.u32();
    const passes = r.u32();
    const m: Material = {
        checksum,
        alphaCutoff: r.u32(),
        sorted: r.u8(),
        drawOrder: r.f32(),
        singleSided: r.u8(),
        grassify: r.u8(),
        passes: [],
    };
    if (m.grassify) m.grass = r.read("fi");
    for (let index = 0; index < passes; index++) {
        const p: Pass = {
            texture: r.u32(),
            flags: r.u32(),
            hasColor: r.u8(),
            color: r.read("3f"),
            blendMode: r.u32(),
            fixedAlpha: r.u32(),
            uAddress: r.u32(),
            vAddress: r.u32(),
            filtering: r.u32(),
            lighting: r.read("9f"),
            mip: [],
        };
        if (p.flags & PASS_UV_WIBBLE) p.uvWibble = r.read("8f");
        if (index === 0 && p.flags & PASS_VC_WIBBLE) {
            const sequences: [number, number[]][] = [];
            const count = r.u32();
            for (let k = 0; k < count; k++) {
                const keys = r.u32();
                const phase = r.i32();
                sequences.push([phase, r.read(`${keys * 2}i`)]);
            }
            p.vcWibble = sequences;
        }
        if (p.flags & PASS_ANIMATES) {
            const [keys, period, iterations, phase] = r.read("4i") as [number, number, number, number];
            const frames: number[][] = [];
            for (let k = 0; k < keys; k++) frames.push(r.read("2I"));
            p.animation = [period, iterations, phase, frames];
        }
        p.mip = r.read("IIff");
        m.passes.push(p);
    }
    return m;
}

function times<T>(count: number, make: () => T): T[] {
    const out: T[] = [];
    for (let k = 0; k < count; k++) out.push(make());
    return out;
}

export function readSector(r: Reader): Sector {
    const checksum = r.u32();
    const bone = r.i32();
    const flags = r.u32();
    const meshes = r.u32();
    if (meshes > 100000) throw new FormatError(`${r.name}: ${meshes} meshes at 0x${hex(r.offset)}`);
    const s: Sector = { checksum, bone, flags, bbox: r.read("6f"), sphere: r.read("4f"), positions: [], meshes: [] };
    if (flags & SEC_BILLBOARD) s.billboard = [r.u32(), r.read("9f")];
    const count = r.u32();
    s.stride = r.u32();
    s.positions = times(count, () => r.read("3f"));
    if (flags & SEC_NORMALS) s.normals = times(count, () => r.read("3f"));
    if (flags & SEC_WEIGHTS) {
        s.weights = times(count, () => r.read("4f"));
        s.bones = times(count, () => r.read("4H"));
    }
    if (flags & SEC_TEXCOORDS) {
        const sets = r.u32();
        s.uvSets = sets;
        s.uvs = times(count, () => r.read(`${2 * sets}f`));
    }
    if (flags & SEC_COLORS) s.colors = times(count, () => r.read("4B"));
    if (flags & SEC_COLOR_WIBBLES) s.wibbles = r.bytes(count);
    for (let k = 0; k < meshes; k++) {
        const meshFlags = r.u32();
        const material = r.u32();
        const strip = r.read(`${r.u32()}H`);
        if (strip.length && pyMax(strip) >= count) {
            throw new FormatError(`${r.name}: sector ${hex8(checksum)} strip index past its ${count} vertices`);
        }
        s.meshes.push({ flags: meshFlags, material, strip });
    }
    return s;
}

export function readScene(data: Uint8Array, name = "scene"): Scene {
    const r = new Reader(data, 0, name);
    const versions = r.read("3I");
    const materials = times(r.u32(), () => readMaterial(r));
    const sectors = times(r.i32(), () => readSector(r));
    return { versions, materials, sectors, tail: r.bytes(data.length - r.offset) };
}

// --- collision, version 8 ---------------------------------------------------------------------------------------
//   header as THUG2's (version 8, object count, total vertices, large faces, small faces, large / small vertices, 0)
//   objects     64 bytes as THUG2's, but the first vertex is an index (not a byte offset), the intensity field 0
//   (aligned to 16)
//   vertices    3f + 4 intensity bytes each (16 bytes; THPS4 has no fixed-point vertices or intensity list)
//   faces       u16 flags, u16 terrain, 3 x u16 vertex, u16 0 (12 bytes), or small: 8 bytes
//   u32 node bytes, the BSP nodes (20 bytes in THPS4's: not used here - the converter builds THUG2's), face indices.
// Face flag 0x80 (camera collidable in THUG2) means the opposite in THPS4: set when the camera goes through it.

export interface ColObject extends SourceColObject {
    smallFaces: number;
    fixedVerts: number;
    value: Buffer; // 4 bytes
    intensities: number[][]; // per vertex its 4 intensity bytes
}

export interface ColFile {
    objects: ColObject[];
    nodeSize: number;
    indexCount: number;
}

export function readCol(data: Uint8Array, name = "col"): ColFile {
    const r = new Reader(data, 0, name);
    const [version, count, totalVerts, largeFaces, smallFaces, , smallVerts] = r.read("8i") as number[];
    if (version !== 8) throw new FormatError(`${name}: collision version ${version}, not 8`);
    const objects: ColObject[] = [];
    const where: [number, number, number, number][] = [];
    for (let k = 0; k < count!; k++) {
        const [checksum, flags, nverts, nfaces, small, fixedVerts, faceOff] = r.read("IHHHBBI") as number[];
        const bbox = r.read("8f");
        const [vertIndex, , , value] = r.readAny("3I4s") as [number, number, number, Buffer];
        where.push([nverts!, nfaces!, faceOff!, vertIndex]);
        objects.push({
            checksum: checksum!,
            flags: flags!,
            smallFaces: small!,
            fixedVerts: fixedVerts!,
            bbox,
            value,
            verts: [],
            intensities: [],
            faces: [],
        });
    }
    const vertBase = (r.offset + 15) & ~15;
    if (smallVerts || objects.some((o) => o.fixedVerts)) {
        throw new FormatError(`${name}: fixed-point vertices in a version 8 file`);
    }
    const faceBase = vertBase + totalVerts! * 16;
    objects.forEach((o, k) => {
        const [nverts, nfaces, faceOff, vertIndex] = where[k]!;
        const v = new Reader(data, vertBase + vertIndex * 16, name);
        for (let n = 0; n < nverts; n++) {
            const [x, y, z, intensity] = v.readAny("3f4s") as [number, number, number, Buffer];
            o.verts.push(floats(x, y, z));
            o.intensities.push([...intensity]);
        }
        const f = new Reader(data, faceBase + faceOff, name);
        for (let n = 0; n < nfaces; n++) {
            const [flags, terrain, a, b, c] = f.read(o.smallFaces ? "HH4B" : "HH4H") as number[];
            if (Math.max(a!, b!, c!) >= nverts) {
                throw new FormatError(`${name}: object ${hex8(o.checksum)} face past its ${nverts} vertices`);
            }
            o.faces.push([flags!, terrain!, a!, b!, c!]);
        }
    });
    r.offset = faceBase + largeFaces! * 12 + smallFaces! * 8;
    const nodeSize = r.u32();
    r.offset += nodeSize;
    const left = data.length - r.offset;
    const indexCount = floorDiv(left, 2);
    if (pyMod(left, 2) || indexCount < largeFaces! + smallFaces! - 1) {
        throw new FormatError(`${name}: ${left} bytes of face indices after the nodes`);
    }
    return { objects, nodeSize, indexCount };
}

// --- to THUG2 ---------------------------------------------------------------------------------------------------

export function toThug2Material(m: Material): thug2Scene.Material {
    // THUG2's material: THPS4's without the passes' lighting colours, with THUG2's name checksum, z bias, no
    // specular, env-map tiling 3 x 3; up to 4 passes, the vertex colour wibble only on the first.
    const out: thug2Scene.Material = {
        checksum: m.checksum,
        nameChecksum: m.checksum,
        alphaCutoff: m.alphaCutoff,
        sorted: m.sorted,
        drawOrder: m.drawOrder,
        singleSided: m.singleSided,
        noBackfaceCulling: 0,
        zBias: 0,
        grassify: m.grassify,
        specularPower: 0.0,
        passes: [],
    };
    if (m.grassify) out.grass = m.grass!;
    m.passes.slice(0, 4).forEach((p, index) => {
        const q: thug2Scene.Pass = {
            texture: p.texture,
            flags: p.flags,
            hasColor: p.hasColor,
            color: p.color,
            blendMode: p.blendMode,
            fixedAlpha: p.fixedAlpha,
            uAddress: p.uAddress,
            vAddress: p.vAddress,
            filtering: p.filtering,
            mip: p.mip,
            envmapTiling: [3.0, 3.0],
        };
        if (p.uvWibble !== undefined) q.uvWibble = p.uvWibble;
        if (p.vcWibble !== undefined) q.vcWibble = p.vcWibble;
        if (p.animation !== undefined) q.animation = p.animation;
        if (index > 0) q.flags = (q.flags & ~PASS_VC_WIBBLE) >>> 0;
        out.passes.push(q);
    });
    return out;
}

// THPS4's face flag 0x80 means the camera goes through the face; THUG2's 0x80 means the camera collides with it.
export const FACE_CAMERA = 0x0080;
// The face flags kept: wall ride, vert, non-collidable, decal, trigger, camera, shadow, no-shadow wall, invisible
// (0x4 ... 0x1000 but 0x200 / 0x800). Left out although THUG2's own levels use them too: 0x01 / 0x02 / 0x200 / 0x800
// (1,292 / 1,513 / 1,076 / 1,532 of THUG2's faces) - their THPS4 meanings aren't checked (THPS4 sets them on 15,806 /
// 13,651 / 17,191 / 2,986 of its 488,023 faces). Above 0x1000: THPS4 sets 0x2000 on every face (487,923 of 488,023) -
// THUG2's "not taggable", which would make all of a THPS4 level untaggable: dropped; 0x4000 (2,387) and 0x8000
// (72,165) do nothing in THUG2 (below): dropped.
// THUG2's own bits above 0x1000 (docs/level-port-compromises.md): 0x2000 is "not taggable" - the graffiti feelers
// ignore 0x1000 | 0x2000 (SetIgnore(0x3000, 0) at 0x0051DF71 / 0x0051E74B); 0x4000 and 0x8000 (on 4,535 / 40,513 of
// THUG2's own faces - the park editor's shells, most levels) have no reader in THUG2's code: no feeler's ignore masks
// (SetIgnore's callers: 0x10, 0x810, 0x90, 0x3000, require 0x20 / 0x50) and no test of those bits in the face flags
// anywhere in the exe (every test / and with 0x4000 / 0x8000 is of other fields: CCollObj +0x08, pads, piece flags).
export const FACE_KEEP = 0x15fc;

export function faceFlags(flags: number): number {
    return (flags ^ FACE_CAMERA) & FACE_KEEP;
}
