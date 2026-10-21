// THUG2 PC scene files (levels\<x>\<x>.scn.xbx; models' .mdl.xbx have the same layout): materials, then
// sectors (one per object), each a list of meshes with their own index lists and vertex buffer.
//
//   u32 material version, mesh version, vertex version (1, 1, 1 in the shipped files)
//   u32 material count, materials
//   i32 sector count, sectors
//   i32 hierarchy object count, hierarchy objects (none in levels)
//
// Read and written by the same field list so that a file read and written again is byte-identical (selfcheck).

import { FormatError, Reader, Writer } from "./binio.ts";

// Sector flags.
export const SEC_TEXCOORDS = 0x01;
export const SEC_COLORS = 0x02;
export const SEC_NORMALS = 0x04;
export const SEC_WEIGHTS = 0x10;
export const SEC_SHADOW_VOLUME = 0x200000;
export const SEC_BILLBOARD = 0x00800000;
export const SEC_COLOR_WIBBLES = 0x800;

// Material pass flags.
export const PASS_UV_WIBBLE = 1 << 0;
export const PASS_VC_WIBBLE = 1 << 1;
export const PASS_TEXTURED = 1 << 2;
export const PASS_ENVIRONMENT = 1 << 3;
export const PASS_DECAL = 1 << 4;
export const PASS_SMOOTH = 1 << 5;
export const PASS_TRANSPARENT = 1 << 6;
export const PASS_ANIMATES = 1 << 11;
export const PASS_IGNORE_VERTEX_ALPHA = 1 << 12;

// Direct3D FVF bits (the mesh's vertex shader word when it isn't skinned).
export const FVF_XYZ = 0x002;
export const FVF_NORMAL = 0x010;
export const FVF_DIFFUSE = 0x040;
export const FVF_TEXCOUNT_SHIFT = 8;

export interface Pass {
    texture: number;
    flags: number;
    hasColor: number;
    color: number[]; // 3f
    blendMode: number;
    fixedAlpha: number;
    uAddress: number;
    vAddress: number;
    envmapTiling: number[]; // 2f
    filtering: number;
    uvWibble?: number[]; // 8f
    vcWibble?: [number, number[]][]; // (phase, keys as 2 ints each)
    animation?: [number, number, number, number[][]]; // period, iterations, phase, keys {u32, u32}
    mip: number[]; // I I f f
}

export interface Material {
    checksum: number;
    nameChecksum: number;
    alphaCutoff: number;
    sorted: number;
    drawOrder: number;
    singleSided: number;
    noBackfaceCulling: number;
    zBias: number;
    grassify: number;
    grass?: number[]; // f i
    specularPower: number;
    specularColor?: number[]; // 3f
    passes: Pass[];
}

export interface Mesh {
    sphere: number[]; // 4f
    bbox: number[]; // 6f
    flags: number;
    material: number;
    lods: number[][];
    strips: number[][];
    stride: number;
    vertexCount: number;
    buffers: [number, Buffer][]; // (convert flag, data)
    vertexShader: number[]; // 2I
    offsets: number[]; // 3B
    hasWibble: number;
    wibble?: Buffer;
    indexSets: number;
    indexSetValues?: number[];
    pixelShader: number;
    psValue?: number;
    psData?: Buffer;
}

export interface Sector {
    checksum: number;
    bone: number;
    flags: number;
    bbox: number[]; // 6f
    sphere: number[]; // 4f
    billboard?: [number, number[]]; // (kind, 9f)
    meshes: Mesh[];
}

export interface Scene {
    versions: number[]; // 3I
    materials: Material[];
    sectors: Sector[];
    hierarchyCount: number;
    tail: Buffer; // (hierarchy objects: only in models; kept raw)
}

export function readMaterial(r: Reader): Material {
    const checksum = r.u32();
    const nameChecksum = r.u32();
    const passes = r.u32();
    const m: Material = {
        checksum,
        nameChecksum,
        alphaCutoff: r.u32(),
        sorted: r.u8(),
        drawOrder: r.f32(),
        singleSided: r.u8(),
        noBackfaceCulling: r.u8(),
        zBias: r.i32(),
        grassify: r.u8(),
        specularPower: 0,
        passes: [],
    };
    if (m.grassify) m.grass = r.read("fi");
    m.specularPower = r.f32();
    if (m.specularPower > 0.0) m.specularColor = r.read("3f");
    for (let i = 0; i < passes; i++) m.passes.push(readPass(r, i));
    return m;
}

export function readPass(r: Reader, index: number): Pass {
    const p: Pass = {
        texture: r.u32(),
        flags: r.u32(),
        hasColor: r.u8(),
        color: r.read("3f"),
        blendMode: r.u32(),
        fixedAlpha: r.u32(),
        uAddress: r.u32(),
        vAddress: r.u32(),
        envmapTiling: r.read("2f"),
        filtering: r.u32(),
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
    return p;
}

export function writeMaterial(w: Writer, m: Material): void {
    w.u32(m.checksum);
    w.u32(m.nameChecksum);
    w.u32(m.passes.length);
    w.u32(m.alphaCutoff);
    w.u8(m.sorted);
    w.f32(m.drawOrder);
    w.u8(m.singleSided);
    w.u8(m.noBackfaceCulling);
    w.i32(m.zBias);
    w.u8(m.grassify);
    if (m.grassify) w.write("fi", ...m.grass!);
    w.f32(m.specularPower);
    if (m.specularPower > 0.0) w.write("3f", ...m.specularColor!);
    m.passes.forEach((p, index) => {
        w.u32(p.texture);
        w.u32(p.flags);
        w.u8(p.hasColor);
        w.write("3f", ...p.color);
        w.u32(p.blendMode);
        w.u32(p.fixedAlpha);
        w.u32(p.uAddress);
        w.u32(p.vAddress);
        w.write("2f", ...p.envmapTiling);
        w.u32(p.filtering);
        if (p.flags & PASS_UV_WIBBLE) w.write("8f", ...p.uvWibble!);
        if (index === 0 && p.flags & PASS_VC_WIBBLE) {
            w.u32(p.vcWibble!.length);
            for (const [phase, keys] of p.vcWibble!) {
                w.u32(Math.floor(keys.length / 2));
                w.i32(phase);
                w.write(`${keys.length}i`, ...keys);
            }
        }
        if (p.flags & PASS_ANIMATES) {
            const [period, iterations, phase, keys] = p.animation!;
            w.write("4i", keys.length, period, iterations, phase);
            for (const key of keys) w.write("2I", ...key);
        }
        w.write("IIff", ...p.mip);
    });
}

// A mesh, as the game's loader reads it (sector loop 0x004C6B10, then the mesh's own 0x004D6100):
//   4f sphere, 6f box, u32 flags (0x400 no skater shadow, 0x20000 ...), u32 material checksum,
//   u32 LOD count (<= 8), per LOD {u32 count, u16 indices}      - the triangle lists, for collision-free uses
//   8 x {u16 count, u16 indices}                               - the strips drawn (LOD 0 first, the rest empty)
//   u8 vertex stride, u16 vertex count, u8 buffer count, per buffer {u8 convert flag, u32 size, data}
//   2 x u32 vertex shader (0..6 = built-in shaders, else a Direct3D FVF), u8 x3 (normal / colour / uv
//   offsets in the vertex), u8 has colour wibble data (+ a byte per vertex), u32 index sets (> 1: a u32 each),
//   u32 pixel shader (1: {u32, u32 size, data} follows; 2..18 the built-in ones, 0x004D3ED0).
export const STRIP_SLOTS = 8;

export function readMesh(r: Reader): Mesh {
    const sphere = r.read("4f");
    const bbox = r.read("6f");
    const flags = r.u32();
    const material = r.u32();
    const lodCount = r.u32();
    if (lodCount > 8)
        throw new FormatError(`${r.name}: ${lodCount} index levels at 0x${r.offset.toString(16).toUpperCase()}`);
    const lods: number[][] = [];
    for (let k = 0; k < lodCount; k++) lods.push(r.read(`${r.u32()}H`));
    const strips: number[][] = [];
    for (let k = 0; k < STRIP_SLOTS; k++) strips.push(r.read(`${r.u16()}H`));
    const stride = r.u8();
    const vertices = r.u16();
    const buffers: [number, Buffer][] = [];
    const bufferCount = r.u8();
    for (let k = 0; k < bufferCount; k++) {
        const flag = r.u8();
        buffers.push([flag, r.bytes(r.u32())]);
    }
    const m: Mesh = {
        sphere,
        bbox,
        flags,
        material,
        lods,
        strips,
        stride,
        vertexCount: vertices,
        buffers,
        vertexShader: r.read("2I"),
        offsets: r.read("3B"),
        hasWibble: r.u8(),
        indexSets: 0,
        pixelShader: 0,
    };
    if (m.hasWibble) m.wibble = r.bytes(vertices);
    m.indexSets = r.u32();
    if (m.indexSets > 1) m.indexSetValues = r.read(`${m.indexSets}I`);
    m.pixelShader = r.u32();
    if (m.pixelShader === 1) {
        m.psValue = r.u32();
        m.psData = r.bytes(r.u32());
    }
    return m;
}

export function writeMesh(w: Writer, m: Mesh): void {
    w.write("4f", ...m.sphere);
    w.write("6f", ...m.bbox);
    w.u32(m.flags);
    w.u32(m.material);
    w.u32(m.lods.length);
    for (const indices of m.lods) {
        w.u32(indices.length);
        w.write(`${indices.length}H`, ...indices);
    }
    // (padded to the 8 slots; more are written as they are, as Python's list + [()] * negative)
    const strips = [...m.strips];
    while (strips.length < STRIP_SLOTS) strips.push([]);
    for (const strip of strips) {
        w.u16(strip.length);
        w.write(`${strip.length}H`, ...strip);
    }
    w.u8(m.stride);
    w.u16(m.vertexCount);
    w.u8(m.buffers.length);
    for (const [flag, data] of m.buffers) {
        w.u8(flag);
        w.u32(data.length);
        w.raw(data);
    }
    w.write("2I", ...m.vertexShader);
    w.write("3B", ...m.offsets);
    w.u8(m.hasWibble);
    if (m.hasWibble) w.raw(m.wibble!);
    w.u32(m.indexSets);
    if (m.indexSets > 1) w.write(`${m.indexSets}I`, ...m.indexSetValues!);
    w.u32(m.pixelShader);
    if (m.pixelShader === 1) {
        w.u32(m.psValue!);
        w.u32(m.psData!.length);
        w.raw(m.psData!);
    }
}

export function readSector(r: Reader): Sector {
    const checksum = r.u32();
    const bone = r.i32();
    const flags = r.u32();
    const meshes = r.u32();
    if (meshes > 100000)
        throw new FormatError(`${r.name}: ${meshes} meshes at 0x${r.offset.toString(16).toUpperCase()}`);
    const s: Sector = { checksum, bone, flags, bbox: r.read("6f"), sphere: r.read("4f"), meshes: [] };
    if (s.flags & SEC_BILLBOARD) {
        const kind = r.u32();
        s.billboard = [kind, r.read("9f")];
    }
    for (let k = 0; k < meshes; k++) s.meshes.push(readMesh(r));
    return s;
}

export function writeSector(w: Writer, s: Sector): void {
    w.u32(s.checksum);
    w.i32(s.bone);
    w.u32(s.flags);
    w.u32(s.meshes.length);
    w.write("6f", ...s.bbox);
    w.write("4f", ...s.sphere);
    if (s.flags & SEC_BILLBOARD) {
        const [kind, values] = s.billboard!;
        w.u32(kind);
        w.write("9f", ...values);
    }
    for (const m of s.meshes) writeMesh(w, m);
}

export function readScene(data: Uint8Array, name = "scene"): Scene {
    const r = new Reader(data, 0, name);
    const versions = r.read("3I");
    const materials: Material[] = [];
    const materialCount = r.u32();
    for (let k = 0; k < materialCount; k++) materials.push(readMaterial(r));
    const sectors: Sector[] = [];
    const sectorCount = r.i32();
    for (let k = 0; k < sectorCount; k++) sectors.push(readSector(r));
    const hierarchyCount = r.i32();
    // (hierarchy objects: only in models; kept raw)
    const tail = r.bytes(data.length - r.offset);
    if (hierarchyCount === 0 && tail.length) throw new FormatError(`${name}: ${tail.length} bytes after the sectors`);
    return { versions, materials, sectors, hierarchyCount, tail };
}

// --- skinned meshes (people and animals: models\...\<name>.skin.xbx) ----------------------------------------------
//
// A skinned sector (SEC_WEIGHTS) has its meshes in the same form as the rest, with a vertex of its own (measured on
// THUG2's Ped_M_AU_Head01.skin.xbx and the other shipped peds - 1,066 meshes, Data\models\Peds, Ped_Male, Ped_Female):
//   +0  3f position
//   +12 u32 weights: three 11 / 11 / 10-bit fields, each n / 1023 (a fourth bone's weight is what is left of 1)
//   +16 4 x u16 bone indices, each the bone's number x 3 (the rows of its matrix in the shader's constants)
//   +24 u32 normal: x 11 bits, y 11 bits (signed, / 1023), z 10 bits (signed, / 511)
//   +28 BGRA colour (128 = full)
//   +32 2f uv, one pair per uv set (stride 40, 48, 56)
// The mesh's built-in vertex shader is how many of the three weights any vertex uses (1 ... 3: the shipped files' 4 and
// 6 are rare variants), its offsets [24, 28, 32]; the pixel shader as a static mesh's (geometry.pixelShader). The
// shipped meshes' first index list (`lods`) is the strip again - in the numbering of the exporter's whole sector when
// a sector has several meshes; the strip (`strips[0]`) is in the mesh's own.
export const SKIN_OFFSETS = [24, 28, 32] as const;
const SKIN_BASE_STRIDE = 32; // (without the uvs)

export interface SkinVertex {
    position: number[]; // 3f
    weights: number[]; // 3 (the packed fields / 1023)
    bones: number[]; // 4 bone numbers (not x 3)
    normal: number[]; // 3
    color: number[]; // BGRA
    uvs: number[]; // 2 per uv set
}

export function unpackWeights(packed: number): number[] {
    return [(packed & 0x7ff) / 1023, ((packed >>> 11) & 0x7ff) / 1023, ((packed >>> 22) & 0x3ff) / 1023];
}

export function packWeights(weights: readonly number[]): number {
    const field = (w: number | undefined, max: number): number =>
        Math.min(max, Math.max(0, Math.round((w ?? 0) * 1023)));
    return (field(weights[0], 0x7ff) | (field(weights[1], 0x7ff) << 11) | (field(weights[2], 0x3ff) << 22)) >>> 0;
}

export function unpackNormal(packed: number): number[] {
    const signed = (v: number, bits: number): number => (v >= 1 << (bits - 1) ? v - (1 << bits) : v);
    return [
        signed(packed & 0x7ff, 11) / 1023,
        signed((packed >>> 11) & 0x7ff, 11) / 1023,
        signed((packed >>> 22) & 0x3ff, 10) / 511,
    ];
}

export function packNormal(normal: readonly number[]): number {
    const field = (v: number | undefined, scale: number, mask: number): number =>
        Math.round(Math.min(1, Math.max(-1, v ?? 0)) * scale) & mask;
    return (
        (field(normal[0], 1023, 0x7ff) |
            (field(normal[1], 1023, 0x7ff) << 11) |
            (field(normal[2], 511, 0x3ff) << 22)) >>>
        0
    );
}

// The vertices of a skinned mesh's buffer.
export function readSkinVertices(mesh: Mesh): SkinVertex[] {
    const sets = (mesh.stride - SKIN_BASE_STRIDE) / 8;
    if (mesh.buffers.length !== 1 || !Number.isInteger(sets) || sets < 1)
        throw new FormatError(`not a skinned mesh (stride ${mesh.stride}, ${mesh.buffers.length} buffers)`);
    const data = mesh.buffers[0]![1];
    if (data.length !== mesh.stride * mesh.vertexCount)
        throw new FormatError(`skinned mesh: ${data.length} bytes for ${mesh.vertexCount} vertices`);
    const r = new Reader(data, 0, "skin vertices");
    const out: SkinVertex[] = [];
    for (let k = 0; k < mesh.vertexCount; k++) {
        const position = r.read("3f");
        const weights = unpackWeights(r.u32());
        const bones = r.read("4H").map((b) => {
            if (b % 3) throw new FormatError(`skinned mesh: bone index ${b} not a multiple of 3`);
            return b / 3;
        });
        const normal = unpackNormal(r.u32());
        const color = r.read("4B");
        out.push({ position, weights, bones, normal, color, uvs: r.read(`${2 * sets}f`) });
    }
    return out;
}

export function skinVertexBuffer(vertices: readonly SkinVertex[], uvSets: number): Buffer {
    const w = new Writer();
    for (const v of vertices) {
        w.write("3f", ...v.position);
        w.u32(packWeights(v.weights));
        w.write("4H", ...v.bones.map((b) => b * 3));
        w.u32(packNormal(v.normal));
        w.write("4B", ...v.color);
        if (v.uvs.length < 2 * uvSets)
            throw new FormatError(`skinned vertex: ${v.uvs.length / 2} of ${uvSets} uv sets`);
        w.write(`${2 * uvSets}f`, ...v.uvs.slice(0, 2 * uvSets));
    }
    return w.getvalue();
}

// The built-in vertex shader a skinned mesh needs: how many of the three packed weights its vertices use (at least 1).
export function skinWeightCount(vertices: readonly SkinVertex[]): number {
    let count = 1;
    for (const v of vertices) {
        const packed = packWeights(v.weights);
        if ((packed >>> 22) & 0x3ff) count = 3;
        else if (count < 2 && (packed >>> 11) & 0x7ff) count = 2;
        if (count === 3) break;
    }
    return count;
}

export interface SkinMesh {
    sphere: number[]; // 4f
    bbox: number[]; // 6f
    flags: number;
    material: number;
    vertices: SkinVertex[];
    uvSets: number;
    strip: number[]; // into `vertices`
    lod?: number[]; // the first index list when not the strip (the shipped files' exporter numbering)
    pixelShader: number;
    psValue?: number;
    psData?: Buffer;
}

// A skinned mesh in THUG2's form (the writer: readSkinVertices' counterpart).
export function skinnedMesh(m: SkinMesh): Mesh {
    const mesh: Mesh = {
        sphere: m.sphere,
        bbox: m.bbox,
        flags: m.flags,
        material: m.material,
        lods: [m.lod ?? m.strip],
        strips: [m.strip],
        stride: SKIN_BASE_STRIDE + 8 * m.uvSets,
        vertexCount: m.vertices.length,
        buffers: [[0, skinVertexBuffer(m.vertices, m.uvSets)]],
        vertexShader: [skinWeightCount(m.vertices), 0],
        offsets: [...SKIN_OFFSETS],
        hasWibble: 0,
        indexSets: 1,
        pixelShader: m.pixelShader,
    };
    if (m.pixelShader === 1) {
        mesh.psValue = m.psValue ?? 0;
        mesh.psData = m.psData ?? Buffer.alloc(0);
    }
    return mesh;
}

// A skinned scene written again from what readSkinVertices makes of it (the self-check: THUG2's own skins must come
// out byte-identical). Throws on a mesh the writer can't make (more than one index list, a colour wibble...).
export function rewriteSkin(scene: Scene): Scene {
    const sectors = scene.sectors.map((s) => {
        if (!(s.flags & SEC_WEIGHTS)) throw new FormatError(`sector 0x${s.checksum.toString(16)}: not skinned`);
        const meshes = s.meshes.map((mesh) => {
            if (mesh.lods.length !== 1 || mesh.strips.slice(1).some((x) => x.length) || mesh.hasWibble)
                throw new FormatError(`sector 0x${s.checksum.toString(16)}: a mesh the skin writer doesn't make`);
            const parts: SkinMesh = {
                sphere: mesh.sphere,
                bbox: mesh.bbox,
                flags: mesh.flags,
                material: mesh.material,
                vertices: readSkinVertices(mesh),
                uvSets: (mesh.stride - SKIN_BASE_STRIDE) / 8,
                strip: mesh.strips[0]!,
                lod: mesh.lods[0]!,
                pixelShader: mesh.pixelShader,
            };
            if (mesh.psValue !== undefined) parts.psValue = mesh.psValue;
            if (mesh.psData !== undefined) parts.psData = mesh.psData;
            return skinnedMesh(parts);
        });
        return { ...s, meshes };
    });
    return { ...scene, sectors };
}

export function writeScene(scene: Scene): Buffer {
    const w = new Writer();
    w.write("3I", ...scene.versions);
    w.u32(scene.materials.length);
    for (const m of scene.materials) writeMaterial(w, m);
    w.i32(scene.sectors.length);
    for (const s of scene.sectors) writeSector(w, s);
    w.i32(scene.hierarchyCount);
    w.raw(scene.tail ?? Buffer.alloc(0));
    return w.getvalue();
}
