// Level geometry in THUG2's form: scenes whose sectors keep one vertex array per sector (THPS4's scn.dat, THUG1's
// scn.xbx) become THUG2's per-mesh vertex buffers, and collision objects become THUG2's version 10 ones (in memory;
// tools/thps4_levels and tools/thug1_levels write them). (TypeScript twin of geometry.py.)

import { hex8, pyBytes, pyMax, pyMin, pySum, roundHalfEven } from "../lib/py.ts";
import { pack } from "../lib/struct.ts";
import * as thug2Col from "./thug2_col.ts";
import * as thug2Scene from "./thug2_scene.ts";

// --- scene ------------------------------------------------------------------------------------------------------

// THUG2's built-in pixel shaders (0x004DD7F0 compiles them, 0x004D3ED0 picks one by the mesh's number):
export const PS_TEXTURED = 2; // tex t0; r0.rgb = t0 * c0 * v0 * 4, r0.a = v0.a * t0.a * 2 (vertex alpha)
export const PS_TEXTURED_FIXED_A = 3; // the same with r0.a = c4.a * t0.a * 2 (the pass's own alpha: vertex alpha ignored)
export const PS_UNTEXTURED = 4; // r0.rgb = v0 * c0 * 2, r0.a = v0.a
export const PS_FROM_MATERIAL = 1; // {u32, u32 size, data}: the data is skipped and the shader made from the material
// (0x004DA6D0, then 0x004DDC00) - what the shipped files do for 2+ passes

export const MESH_THUG2 = 0x20000; // set on every mesh of the shipped levels (the loader copies it to the mesh)

export function pixelShader(material: thug2Scene.Material): number {
    const passes = material.passes;
    if (passes.length > 1) return PS_FROM_MATERIAL;
    if (!passes.length || !passes[0]!.texture || !(passes[0]!.flags & thug2Scene.PASS_TEXTURED)) return PS_UNTEXTURED;
    if (passes[0]!.flags & thug2Scene.PASS_IGNORE_VERTEX_ALPHA) return PS_TEXTURED_FIXED_A;
    return PS_TEXTURED;
}

export function bounds(points: readonly (readonly number[])[]): [number[], number[], number[], number] {
    const lo = [0, 1, 2].map((k) => pyMin(points.map((p) => p[k]!)));
    const hi = [0, 1, 2].map((k) => pyMax(points.map((p) => p[k]!)));
    const center = [0, 1, 2].map((k) => (lo[k]! + hi[k]!) / 2);
    // (Python: sum(... ** 2) - a compensated sum (pySum); x ** 2 there is the C library's pow, x * x here: the same
    // but for one in ~2000 values a last-bit difference, gone in the float32 the sphere is written as)
    const radius = Math.sqrt(
        pyMax(
            points.map((p) =>
                pySum(
                    [0, 1, 2].map((k) => {
                        const d = p[k]! - center[k]!;
                        return d * d;
                    }),
                ),
            ),
        ),
    );
    return [lo, hi, center, radius];
}

// A sector as tools/thps4_levels' / tools/thug1_levels' readers make it: one vertex array for all its meshes.
export interface SourceSector {
    checksum: number;
    bone: number;
    flags: number;
    bbox: number[];
    sphere: number[];
    billboard?: [number, number[]];
    positions: number[][];
    normals?: number[][];
    uvs?: number[][]; // per vertex: uv_sets x (u, v)
    uvSets?: number;
    colors?: number[][]; // BGRA
    meshes: { flags: number; material: number; strip: number[] }[];
}

export function convertScene(
    sceneIn: { sectors: SourceSector[] },
    warnings: string[],
    materials: thug2Scene.Material[],
    halveColors = false,
): thug2Scene.Scene {
    // sceneIn: {sectors} as thps4.readScene / thug1.readScene make them (a sector: checksum, bone, flags, box,
    // sphere, the vertex arrays positions / normals / uvs (uvSets per vertex) / colors (BGRA), meshes {flags,
    // material, strip}); materials: the scene's materials in THUG2's form. halveColors: vertex colours' rgb made
    // 128 = full from 255 = full (THPS4's levels and skies; their alpha is 128 = full already and kept).
    const byChecksum = new Map<number, thug2Scene.Material>();
    for (const m of materials) byChecksum.set(m.checksum, m);
    const sectors: thug2Scene.Sector[] = [];
    for (const s of sceneIn.sectors) {
        const flags = s.flags;
        if (flags & thug2Scene.SEC_WEIGHTS || s.bone !== -1) {
            warnings.push(`sector ${hex8(s.checksum)}: skinned - left out`);
            continue;
        }
        const hasNormals = Boolean(flags & thug2Scene.SEC_NORMALS);
        const hasColors = Boolean(flags & thug2Scene.SEC_COLORS);
        const hasUvs = Boolean(flags & thug2Scene.SEC_TEXCOORDS);
        const outFlags =
            ((flags & ~(thug2Scene.SEC_COLOR_WIBBLES | thug2Scene.SEC_WEIGHTS)) | thug2Scene.SEC_COLORS) >>> 0;
        const sector: thug2Scene.Sector = {
            checksum: s.checksum,
            bone: -1,
            flags: outFlags,
            bbox: s.bbox,
            sphere: s.sphere,
            meshes: [],
        };
        if (s.billboard !== undefined) sector.billboard = s.billboard;
        for (const mesh of s.meshes) {
            const strip = mesh.strip;
            if (strip.length < 3) continue;
            const material = byChecksum.get(mesh.material);
            if (material === undefined) {
                warnings.push(`sector ${hex8(s.checksum)}: material ${hex8(mesh.material)} missing - mesh left out`);
                continue;
            }
            const used = [...new Set(strip)].sort((a, b) => a - b);
            if (used.length > 0xffff) {
                warnings.push(`sector ${hex8(s.checksum)}: mesh of ${used.length} vertices left out`);
                continue;
            }
            const local = new Map<number, number>();
            used.forEach((v, i) => {
                local.set(v, i);
            });
            const newStrip = strip.map((v) => local.get(v)!);
            const sets = Math.max(1, Math.min(material.passes.length, s.uvSets || 1));
            const stride = 12 + (hasNormals ? 12 : 0) + 4 + 8 * sets;
            const parts: Buffer[] = [];
            for (const v of used) {
                parts.push(pack("<3f", ...s.positions[v]!));
                if (hasNormals) parts.push(pack("<3f", ...s.normals![v]!));
                if (hasColors) {
                    let [b, g, r, a] = s.colors![v]! as [number, number, number, number];
                    if (halveColors) {
                        // THPS4's PC files light rgb at 255 = full (median ~150 in its levels, alpha 128 = full);
                        // THUG2's shaders take 128 as full (x4 / x2 of 0.5): halved, or the level comes out
                        // twice as bright, washed out (light colours clipped to white).
                        b >>= 1;
                        g >>= 1;
                        r >>= 1;
                    }
                    parts.push(pack("<4B", b, g, r, a));
                } else {
                    parts.push(Buffer.from([0x80, 0x80, 0x80, 0x80]));
                }
                if (hasUvs) {
                    const uvs = s.uvs![v]!;
                    for (let k = 0; k < sets; k++) {
                        const k2 = 2 * k + 1 < uvs.length ? k : 0;
                        const u = uvs[2 * k2];
                        const w = uvs[2 * k2 + 1];
                        if (u === undefined || w === undefined) throw new RangeError("tuple index out of range");
                        parts.push(pack("<2f", u, w));
                    }
                } else {
                    parts.push(Buffer.alloc(8 * sets));
                }
            }
            let fvf = thug2Scene.FVF_XYZ | thug2Scene.FVF_DIFFUSE | (sets << thug2Scene.FVF_TEXCOUNT_SHIFT);
            if (hasNormals) fvf |= thug2Scene.FVF_NORMAL;
            const offsets = hasNormals ? [12, 24, 28] : [0, 12, 16];
            const [lo, hi, center, radius] = bounds(used.map((v) => s.positions[v]!));
            const out: thug2Scene.Mesh = {
                sphere: [center[0]!, center[1]!, center[2]!, radius],
                bbox: [lo[0]!, lo[1]!, lo[2]!, hi[0]!, hi[1]!, hi[2]!],
                flags: (mesh.flags | MESH_THUG2) >>> 0,
                material: mesh.material,
                lods: [newStrip],
                strips: [newStrip],
                stride,
                vertexCount: used.length,
                buffers: [[0, Buffer.concat(parts)]],
                vertexShader: [fvf >>> 0, 0],
                offsets,
                hasWibble: 0,
                indexSets: 1,
                pixelShader: pixelShader(material),
            };
            if (out.pixelShader === PS_FROM_MATERIAL) {
                out.psValue = 0;
                out.psData = Buffer.alloc(0);
            }
            if (!hasNormals) sector.flags = (sector.flags & ~thug2Scene.SEC_NORMALS) >>> 0;
            sector.meshes.push(out);
        }
        sectors.push(sector);
    }
    return { versions: [1, 1, 1], materials, sectors, hierarchyCount: 0, tail: Buffer.alloc(0) };
}

// --- collision --------------------------------------------------------------------------------------------------

// A collision object as tools/thps4_levels' reader makes it.
export interface SourceColObject {
    checksum: number;
    flags: number;
    bbox: number[];
    verts: number[][];
    intensities: Buffer | number[][]; // (bytes, or per vertex a tuple whose first value is the intensity)
    faces: thug2Col.Face[];
}

export function convertCol(
    colIn: { objects: SourceColObject[] },
    keepTriggers: (checksum: number) => boolean,
    warnings: string[],
    faceFlags: (flags: number) => number,
): thug2Col.ColFile {
    // colIn: {objects}: checksum, flags, box, verts (3f), intensities, faces (flags, terrain, a, b, c) - as
    // thps4.readCol makes them; faceFlags(flags): a face's flags in THUG2's meaning. The vertices are put on
    // THUG2's fixed-point grid where they fit, and the BSP trees and reaches made.
    void warnings;
    const objects: thug2Col.ColObject[] = [];
    for (const o of colIn.objects) {
        let verts = o.verts;
        if (!verts.length || !o.faces.length) {
            // (kept, as the shipped files keep theirs: an empty leaf, the box as it was)
            objects.push(emptyObject(o));
            continue;
        }
        const faces: thug2Col.Face[] = [];
        for (const [f, terrain, a, b, c] of o.faces) {
            let flags = faceFlags(f);
            if (!keepTriggers(o.checksum)) flags &= ~thug2Col.FACE_TRIGGER;
            faces.push([flags, terrain, a, b, c]);
        }
        let lo = [0, 1, 2].map((k) => pyMin(verts.map((v) => v[k]!)));
        let hi = [0, 1, 2].map((k) => pyMax(verts.map((v) => v[k]!)));
        // Fixed-point vertices (sixteenths, up to 4096 units from the box minimum) as the shipped files use for
        // most objects; small faces (u8 vertex indices) when they fit.
        const fixed = [0, 1, 2].every((k) => hi[k]! - lo[k]! < 4000.0);
        const small = verts.length <= 256;
        if (fixed) {
            lo = lo.map((x) => Math.floor(x * 16.0) / 16.0);
            const base = lo;
            verts = verts.map((v) => [0, 1, 2].map((k) => base[k]! + roundHalfEven((v[k]! - base[k]!) * 16.0) / 16.0));
            const fixedVerts = verts;
            hi = [0, 1, 2].map((k) => pyMax(fixedVerts.map((v) => v[k]!)));
        }
        const ints = o.intensities;
        const out: thug2Col.ColObject = {
            checksum: o.checksum,
            flags: o.flags,
            smallFaces: small ? 1 : 0,
            fixedVerts: fixed ? 1 : 0,
            bbox: [lo[0]!, lo[1]!, lo[2]!, 1.0, hi[0]!, hi[1]!, hi[2]!, 1.0],
            verts,
            intensities: Buffer.isBuffer(ints) ? ints : pyBytes(ints.map((i) => i[0]!)),
            faces,
            reach: Buffer.alloc(0),
            bsp: { kind: "leaf", faces: [], low: 3 },
        };
        thug2Col.setReaches(out);
        out.bsp = thug2Col.buildTree(verts, faces);
        objects.push(out);
    }
    return { objects, pad: 0 };
}

export function emptyObject(o: { checksum: number; flags: number; bbox: readonly number[] }): thug2Col.ColObject {
    return {
        checksum: o.checksum,
        flags: o.flags,
        smallFaces: 1,
        fixedVerts: 1,
        value: pack("<f", 1.0),
        bbox: [...o.bbox],
        verts: [],
        intensities: Buffer.alloc(0),
        faces: [],
        reach: Buffer.alloc(0),
        bsp: { kind: "leaf", faces: [], low: 3 },
    };
}
