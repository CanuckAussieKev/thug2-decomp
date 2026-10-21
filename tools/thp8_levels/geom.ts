// THP8's scenes (.geom in the zone archives, Xbox 360): read from the files (no code of the game used). Big-endian.
//
//   0x00  u32 0, 7 x 0xCABAAAFA (as THAW's)
//   0x20  materials: u16 0x0310, u16 count, u32 size of the block (from 0x20), u32 16 (offset of the first), u32 -1;
//         then 0x134 bytes per material (MAT_*), then their parameter blocks (offsets from 0x20)
//   then the scene at S0 = 0x20 + size: u32 0xBABEFACE, u32 n (8 or 0x18), n - 8 bytes of 0; from H = S0 + n (the
//   scene's pointers are from S = H + 8): +0x00 u32 0, u32 0xEA, +0x08 box (4f min, 4f max), +0x28 sphere (4f),
//   +0x54 mesh count, +0x74 sector count, +0x78 sectors, +0x7C sector nodes, +0x80 -1, +0x84 0x14 bytes per sector
//   (runtime), +0x88 meshes, +0x94 (filler), then two index tables (runtime: -1 / mesh numbers)
//   sectors     0x50 bytes: u32 0, u32 checksum (the node's name), u32 flags (SEC_*), ..., +0x30 sphere (4f)
//   nodes       0x60 bytes per sector: +0x10 box (4f min, 4f max), +0x48 first mesh, +0x4C mesh count
//   meshes      0x80 bytes (MESH_*): sphere, material checksum, vertex layout, index / vertex counts, the offsets
//               (from S) of the vertex and index blocks
//   a block:    a 0x20-byte header (+0x1C: the count, little-endian at +0 in the vertex block's), then the data
//
// Vertices (stride = vertex bytes / count): float3 position (big-endian), a packed normal, (layout & 0x400: one more
// word; & 0x800: two - tangent frames), the colour (ARGB bytes, 255 = full) at the colour offset, then 16-bit float
// uv pairs from the uv offset: one per 2-bit field of the layout's high half that is 2 (the lowest is the material's
// first texture's). Indices: big-endian u16 triangle strips, 0x7FFF between strips? (see readIndices).

import { FormatError } from "../level_port/binio.ts";

const SCENE = 0xbabeface;
export const MAT_SIZE = 0x134;
const MAT_TEXTURES = 0xa8; // u32 x up to 16: texture checksums (the first is the diffuse map)
const MAT_PARAMS = 0x12c; // u32: offset (from 0x20) of the material's parameter block
const MAT_FLAGS = 0x130;
// parameter block
const PARAM_TEMPLATE = 0x04; // the shader template's checksum (MaterialLibrary.bin's)
const PARAM_TEXTURES = 0x08; // the number of textures listed
const PARAM_TERRAIN = 0x1c; // a TERRAIN_ checksum or 0
const PARAM_STATE = 0x3c; // render state bits (blend / alpha test?)

export const SEC_SIZE = 0x50;
export const NODE_SIZE = 0x60;
export const MESH_SIZE = 0x80;
export const RESTART = 0x7fff;

export interface GeomMaterial {
    checksum: number;
    flags: number;
    template: number;
    textures: number[];
    terrain: number;
    state: number;
    params: Buffer; // the parameter block's first 0x80 bytes
}

export interface GeomMesh {
    sphere: number[];
    material: number;
    layout: number; // the +0x1C word: uv field kinds (high half), extra normal words (0x400 / 0x800)
    flags: number; // +0x10
    colorOffset: number;
    uvOffset: number;
    vertexCount: number;
    indexCount: number;
    stride: number;
    vertices: Buffer;
    indices: Buffer; // big-endian u16
}

export interface GeomSector {
    checksum: number;
    flags: number;
    bbox: number[];
    sphere: number[];
    meshes: GeomMesh[];
}

export interface Geom {
    materials: GeomMaterial[];
    sectors: GeomSector[];
    bbox: number[];
}

const floats = (d: Buffer, at: number, n: number): number[] =>
    Array.from({ length: n }, (_, k) => d.readFloatBE(at + 4 * k));

export function readGeom(data: Buffer, name = "geom"): Geom {
    const u32 = (at: number): number => data.readUInt32BE(at);
    const version = data.readUInt16BE(0x20);
    if (version !== 0x0310) throw new FormatError(`${name}: material block version ${version.toString(16)}`);
    const count = data.readUInt16BE(0x22);
    const size = u32(0x24);
    const first = 0x20 + u32(0x28);
    const materials: GeomMaterial[] = [];
    for (let i = 0; i < count; i++) {
        const at = first + MAT_SIZE * i;
        const params = 0x20 + u32(at + MAT_PARAMS);
        // (the last record of some scenes is a stub - checksum 0, no parameter block: no textures)
        const n = params + 0x40 <= 0x20 + size && u32(params + PARAM_TEXTURES) <= 16 ? u32(params + PARAM_TEXTURES) : 0;
        materials.push({
            checksum: u32(at),
            flags: u32(at + MAT_FLAGS),
            template: u32(params + PARAM_TEMPLATE),
            textures: Array.from({ length: n }, (_, k) => u32(at + MAT_TEXTURES + 4 * k)),
            terrain: u32(params + PARAM_TERRAIN),
            state: u32(params + PARAM_STATE),
            // (the block: 0x10 + its +0x0C word bytes)
            params: Buffer.from(
                data.subarray(
                    params,
                    Math.min(params + 0x10 + (params + 0x10 <= 0x20 + size ? u32(params + 0x0c) : 0), 0x20 + size),
                ),
            ),
        });
    }
    const s0 = 0x20 + size;
    if (u32(s0) !== SCENE) throw new FormatError(`${name}: no scene (0xBABEFACE) at 0x${s0.toString(16)}`);
    const h = s0 + u32(s0 + 4); // (the header's second word: 8 or 0x18 - the fields below move with it)
    const s = h + 8;
    const meshCount = u32(h + 0x54);
    const sectorCount = u32(h + 0x74);
    const sectorsAt = s + u32(h + 0x78);
    const nodesAt = s + u32(h + 0x7c);
    const meshesAt = s + u32(h + 0x88);
    const meshes: GeomMesh[] = [];
    for (let m = 0; m < meshCount; m++) {
        const r = meshesAt + MESH_SIZE * m;
        const vertexBytes = u32(r + 0x4c);
        const vertexCount = data.readUInt16BE(r + 0x26);
        const indexCount = data.readUInt16BE(r + 0x24);
        if (!vertexCount || vertexBytes % vertexCount) {
            throw new FormatError(`${name}: mesh ${m}: ${vertexBytes} vertex bytes for ${vertexCount} vertices`);
        }
        const vAt = s + u32(r + 0x40) + 0x20;
        const iAt = s + u32(r + 0x5c) + 0x20;
        if (vAt + vertexBytes > data.length || iAt + 2 * indexCount > data.length) {
            throw new FormatError(`${name}: mesh ${m}: data past the end`);
        }
        meshes.push({
            sphere: floats(data, r, 4),
            material: u32(r + 0x14),
            flags: u32(r + 0x10),
            colorOffset: data[r + 0x1b]!,
            uvOffset: data[r + 0x1a]!,
            layout: u32(r + 0x1c),
            vertexCount,
            indexCount,
            stride: vertexBytes / vertexCount,
            vertices: data.subarray(vAt, vAt + vertexBytes),
            indices: data.subarray(iAt, iAt + 2 * indexCount),
        });
    }
    const sectors: GeomSector[] = [];
    for (let i = 0; i < sectorCount; i++) {
        const sec = sectorsAt + SEC_SIZE * i;
        const node = nodesAt + NODE_SIZE * i;
        const firstMesh = u32(node + 0x48);
        const n = u32(node + 0x4c);
        if (firstMesh + n > meshCount)
            throw new FormatError(`${name}: sector ${i}: meshes ${firstMesh}+${n} of ${meshCount}`);
        const lo = floats(data, node + 0x10, 3);
        const hi = floats(data, node + 0x20, 3);
        sectors.push({
            checksum: u32(sec + 4),
            flags: u32(sec + 8),
            bbox: [...lo, ...hi],
            sphere: floats(data, sec + 0x30, 4),
            meshes: meshes.slice(firstMesh, firstMesh + n),
        });
    }
    return { materials, sectors, bbox: [...floats(data, h + 8, 3), ...floats(data, h + 0x18, 3)] };
}

// IEEE half float.
export function half(h: number): number {
    const sign = h & 0x8000 ? -1 : 1;
    const e = (h >> 10) & 0x1f;
    const f = h & 0x3ff;
    if (e === 0) return sign * f * 2 ** -24;
    if (e === 31) return f ? Number.NaN : sign * Number.POSITIVE_INFINITY;
    return sign * (1 + f / 1024) * 2 ** (e - 15);
}

// The uv fields of a layout: the slot numbers (0-7) whose 2-bit kind is 2 (two 16-bit floats).
export function uvSlots(layout: number): number[] {
    const out: number[] = [];
    for (let k = 0; k < 8; k++) {
        const kind = (layout >>> (16 + 2 * k)) & 3;
        if (kind === 2) out.push(k);
        else if (kind !== 0) throw new FormatError(`vertex layout ${layout.toString(16)}: uv field kind ${kind}`);
    }
    return out;
}

export interface Vertex {
    position: number[];
    color: number[]; // BGRA, 255 = full
    uvs: number[][]; // per uv field
}

export function readVertices(m: GeomMesh): Vertex[] {
    const slots = uvSlots(m.layout);
    const out: Vertex[] = [];
    const d = m.vertices;
    for (let i = 0; i < m.vertexCount; i++) {
        const v = i * m.stride;
        const c = v + m.colorOffset;
        const uvs: number[][] = [];
        slots.forEach((_, k) => {
            const at = v + m.uvOffset + 4 * k;
            uvs.push([half(d.readUInt16BE(at)), half(d.readUInt16BE(at + 2))]);
        });
        out.push({
            position: [d.readFloatBE(v), d.readFloatBE(v + 4), d.readFloatBE(v + 8)],
            // ARGB bytes -> BGRA
            color: [d[c + 3]!, d[c + 2]!, d[c + 1]!, d[c]!],
            uvs,
        });
    }
    return out;
}

// The mesh's triangles (vertex index triples): its index list as triangle strips, RESTART between them.
export function triangles(m: GeomMesh): number[][] {
    const out: number[][] = [];
    let strip: number[] = [];
    const flush = (): void => {
        for (let k = 2; k < strip.length; k++) {
            const [a, b, c] =
                k % 2 ? [strip[k - 1]!, strip[k - 2]!, strip[k]!] : [strip[k - 2]!, strip[k - 1]!, strip[k]!];
            if (a !== b && b !== c && a !== c) out.push([a, b, c]);
        }
        strip = [];
    };
    for (let k = 0; k < m.indexCount; k++) {
        const i = m.indices.readUInt16BE(2 * k);
        if (i === RESTART || i === 0xffff) flush();
        else {
            if (i >= m.vertexCount) throw new FormatError(`index ${i} of ${m.vertexCount} vertices`);
            strip.push(i);
        }
    }
    flush();
    return out;
}
