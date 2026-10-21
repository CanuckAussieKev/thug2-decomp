// THUG1 PC level files (in Data\pre\<LV>scn.pre / <LV>col.pre: levels\<lv>\<lv>.scn.xbx / .tex.xbx / .col.xbx).
//
// - Textures (.tex.xbx): THUG2's format (tools/level_port/tex.ts).
// - Collision (.col.xbx, version 9): THUG2's version 10 without the faces' reaches (tools/level_port/thug2_col.ts
//   reads both); converted by adding them (toThug2Col).
// - Scene (.scn.xbx): THUG2's materials exactly; the sectors keep THPS4's layout - one vertex array per sector, the
//   meshes index into it:
//     u32 checksum, i32 bone, u32 flags, u32 mesh count, 6f box, 4f sphere, (billboard: u32 type, 9f),
//     u32 vertex count, u32 stride, then the arrays one after another: positions 3f; normals 3f; weights (u32 packed
//     11/11/10 bits) + 4 x u16 bone indices; uv sets {u32 count, then count x 2f per vertex}; colours BGRA (128 = full,
//     as THUG2's); colour wibble indices (a byte each),
//   then per mesh: 4f sphere, 6f box, u32 flags, u32 material checksum, u32 LOD count, per LOD {u32 count, u16 strip
//   indices into the sector's vertices} (every shipped mesh has one LOD).
//   The file ends with i32 0 (no hierarchy objects).
// - Skins (models\...\<name>.skin.xbx, the people in the <LV>ped.pre archives): the scene format with skinned sectors
//   (SEC_WEIGHTS): the weights packed as THUG2's, the bone indices the bones' own numbers (THUG2's are x 3), the
//   normals 3f (THUG2's packed) - toThug2Skin.

import { FormatError, Reader } from "../level_port/binio.ts";
import { pixelShader, type SourceSector } from "../level_port/geometry.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { hex8, pyMax } from "../lib/py.ts";

export const COL_VERSION = 9;

const hex = (n: number): string => n.toString(16).toUpperCase();

export interface Mesh {
    sphere: number[]; // 4f
    bbox: number[]; // 6f
    flags: number;
    material: number;
    lods: number[][];
    strip: number[]; // (the first LOD)
}

export interface Sector extends SourceSector {
    stride?: number;
    weights?: number[]; // a packed u32 per vertex
    bones?: number[][]; // 4H per vertex
    wibbles?: Buffer;
    meshes: Mesh[];
}

export interface Scene {
    versions: number[];
    materials: thug2Scene.Material[];
    sectors: Sector[];
    hierarchyCount: number;
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
    if (flags & thug2Scene.SEC_BILLBOARD) s.billboard = [r.u32(), r.read("9f")];
    const count = r.u32();
    s.stride = r.u32();
    s.positions = times(count, () => r.read("3f"));
    if (flags & thug2Scene.SEC_NORMALS) s.normals = times(count, () => r.read("3f"));
    if (flags & thug2Scene.SEC_WEIGHTS) {
        s.weights = r.read(`${count}I`);
        s.bones = times(count, () => r.read("4H"));
    }
    if (flags & thug2Scene.SEC_TEXCOORDS) {
        const sets = r.u32();
        if (sets > 8) throw new FormatError(`${r.name}: sector ${hex8(checksum)} has ${sets} uv sets`);
        s.uvSets = sets;
        s.uvs = times(count, () => r.read(`${2 * sets}f`));
    }
    if (flags & thug2Scene.SEC_COLORS) s.colors = times(count, () => r.read("4B"));
    if (flags & thug2Scene.SEC_COLOR_WIBBLES) s.wibbles = r.bytes(count);
    for (let m = 0; m < meshes; m++) {
        const sphere = r.read("4f");
        const bbox = r.read("6f");
        const meshFlags = r.u32();
        const material = r.u32();
        const lodCount = r.u32();
        if (lodCount > 8) throw new FormatError(`${r.name}: ${lodCount} LODs at 0x${hex(r.offset)}`);
        const lods = times(lodCount, () => r.read(`${r.u32()}H`));
        for (const indices of lods) {
            if (indices.length && pyMax(indices) >= count) {
                throw new FormatError(`${r.name}: sector ${hex8(checksum)} strip index past its ${count} vertices`);
            }
        }
        s.meshes.push({ sphere, bbox, flags: meshFlags, material, lods, strip: lods.length ? lods[0]! : [] });
    }
    return s;
}

export function readScene(data: Uint8Array, name = "scene"): Scene {
    const r = new Reader(data, 0, name);
    const versions = r.read("3I");
    const materials = times(r.u32(), () => thug2Scene.readMaterial(r));
    const sectors = times(r.i32(), () => readSector(r));
    const hierarchyCount = r.i32();
    if (hierarchyCount !== 0) throw new FormatError(`${name}: ${hierarchyCount} hierarchy objects`);
    r.expectEnd();
    return { versions, materials, sectors, hierarchyCount };
}

export function readCol(data: Uint8Array, name = "col"): thug2Col.ColFile {
    return thug2Col.readCol(data, name, [COL_VERSION]);
}

// --- to THUG2 ---------------------------------------------------------------------------------------------------

export function checkTree(o: thug2Col.ColObject, name: string): void {
    // THUG1's BSP trees are THUG2's (the same node words, sixteenths, leaves and face index lists): every face is in
    // a leaf. (Splits outside the object's box happen in both games' files: harmless.)
    const seen = new Set<number>();
    const walk = (node: thug2Col.BspNode): void => {
        if (node.kind === "leaf") {
            for (const f of node.faces) seen.add(f);
            return;
        }
        if (node.axis > 2) throw new FormatError(`${name}: object ${hex8(o.checksum)} split on axis ${node.axis}`);
        walk(node.left);
        walk(node.right);
    };
    walk(o.bsp);
    const all = o.faces.length;
    let same = seen.size === all;
    if (same) for (const f of seen) if (!(Number.isInteger(f) && f >= 0 && f < all)) same = false;
    if (!same) {
        throw new FormatError(`${name}: object ${hex8(o.checksum)}: ${seen.size} of ${all} faces in its tree`);
    }
}

export function toThug2Col(
    col: thug2Col.ColFile,
    keepTriggers: (checksum: number) => boolean,
    faceFlags: (flags: number) => number,
    name: string,
): thug2Col.ColFile {
    // Version 9 -> 10: the same objects, vertices and trees; the reaches computed (thug2_col.setReaches: THUG1 has
    // none, and zeros make the faces all but uncollidable). faceFlags(flags): a face's flags for THUG2. Trigger
    // faces lose their flag unless keepTriggers(object checksum) (THUG2 runs a trigger face's node's TriggerScript
    // without checking it exists).
    const objects: thug2Col.ColObject[] = [];
    for (const original of col.objects) {
        checkTree(original, name);
        const o = { ...original };
        const keep = keepTriggers(o.checksum);
        o.faces = o.faces.map(([flags, terrain, a, b, c]) => [
            keep ? faceFlags(flags) : faceFlags(flags) & ~thug2Col.FACE_TRIGGER,
            terrain,
            a,
            b,
            c,
        ]);
        thug2Col.setReaches(o);
        objects.push(o);
    }
    const out: thug2Col.ColFile = { objects };
    if (col.pad !== undefined) out.pad = col.pad;
    return out;
}

// --- skins to THUG2 -------------------------------------------------------------------------------------------------

// A THUG1 skin (a ped model or one of its CAS parts) in THUG2's form: each mesh's strip gets its own vertex buffer of
// the vertices it uses (thug2_scene.ts's skinned vertex), the materials, boxes, spheres and flags as they are (THUG1's
// skins have THUG2's sector flags, 0x80402057). `bones`: the skeleton's bone count - an index past it throws.
export function toThug2Skin(scene: Scene, name: string, bones: number): thug2Scene.Scene {
    const materials = new Map(scene.materials.map((m) => [m.checksum, m]));
    const sectors: thug2Scene.Sector[] = [];
    for (const s of scene.sectors) {
        if (!(s.flags & thug2Scene.SEC_WEIGHTS) || s.weights === undefined || s.bones === undefined)
            throw new FormatError(`${name}: sector ${hex8(s.checksum)} isn't skinned`);
        const sets = s.flags & thug2Scene.SEC_TEXCOORDS ? Math.max(1, s.uvSets ?? 1) : 1;
        const meshes: thug2Scene.Mesh[] = [];
        for (const mesh of s.meshes) {
            if (mesh.strip.length < 3) continue;
            const material = materials.get(mesh.material);
            if (material === undefined) throw new FormatError(`${name}: material ${hex8(mesh.material)} missing`);
            const uvSets = Math.max(1, Math.min(material.passes.length, sets));
            const used = [...new Set(mesh.strip)].sort((a, b) => a - b);
            const local = new Map(used.map((v, i) => [v, i]));
            const vertices = used.map((v): thug2Scene.SkinVertex => {
                const vertexBones = s.bones![v]!;
                for (const b of vertexBones) {
                    if (b >= bones) throw new FormatError(`${name}: bone ${b} past the skeleton's ${bones}`);
                }
                const uvs = s.uvs?.[v] ?? [];
                const out: number[] = [];
                for (let k = 0; k < uvSets; k++) {
                    const k2 = 2 * k + 1 < uvs.length ? k : 0;
                    out.push(uvs[2 * k2] ?? 0, uvs[2 * k2 + 1] ?? 0);
                }
                return {
                    position: s.positions[v]!,
                    weights: thug2Scene.unpackWeights(s.weights![v]!),
                    bones: vertexBones,
                    normal: s.normals?.[v] ?? [0, 1, 0],
                    color: s.colors?.[v] ?? [128, 128, 128, 128],
                    uvs: out,
                };
            });
            const strip = mesh.strip.map((v) => local.get(v)!);
            meshes.push(
                thug2Scene.skinnedMesh({
                    sphere: mesh.sphere,
                    bbox: mesh.bbox,
                    flags: mesh.flags,
                    material: mesh.material,
                    vertices,
                    uvSets,
                    strip,
                    pixelShader: pixelShader(material),
                }),
            );
        }
        const sector: thug2Scene.Sector = {
            checksum: s.checksum,
            bone: s.bone,
            flags: (s.flags & ~thug2Scene.SEC_COLOR_WIBBLES) >>> 0,
            bbox: s.bbox,
            sphere: s.sphere,
            meshes,
        };
        if (s.billboard !== undefined) sector.billboard = s.billboard;
        sectors.push(sector);
    }
    return { versions: [1, 1, 1], materials: scene.materials, sectors, hierarchyCount: 0, tail: Buffer.alloc(0) };
}

// --- animations to THUG2 ----------------------------------------------------------------------------------------

// THUG1's .ska.xbx is THUG2's format (THUG2's decoders 0x00482C20 / 0x00482E70 read both - THUG1's files lack the flags
// 0x4000 / 0x10000 / 0x20000), but a key may be an index into a "standard key" table the game loads at start-up
// (anims\standardkeyQ.bin / standardkeyT.bin in Bits.pre: 256 entries of 8 bytes, x / y / z u16 first) - and THUG2's
// tables are other values than THUG1's: a THUG1 animation played by THUG2 comes out crumpled. So each table key is
// written out with THUG1's table values (the key's explicit form), and the per-bone stream sizes made to match.
//   u32 1, u32 flags, f32 duration, u32 bones, u32 rotation keys, u32 translation keys, u32 custom keys,
//   u32 rotation bytes, u32 translation bytes, u16 rotation bytes per bone, u16 translation bytes per bone,
//   (aligned to 4; flag 0x80000, a partial animation: u32 count and its bits, aligned) the rotation keys, the translation keys, (aligned) the custom keys.
//   Rotation key: u16 h (time, 0x8000 the sign of w); h & 0x4000 clear: 3 x u16 (x, y, z); else h & 0x3800 clear: a
//   byte, the table index; else x, y, z each a byte (h bit 0x2000 / 0x1000 / 0x800) or a u16.
//   Translation key: byte b (b & 0x40: the time is b & 0x3f, else a u16 follows); b & 0x80: a byte, the table
//   index; else 3 x u16.
export const ANIM_FLAGS_UNSUPPORTED = 0x100 | 0x2000 | 0x8000 | 0x10000 | 0x1000000 | 0x8000000 | 0x10000000;

export function untableAnim(data: Buffer, qTable: Buffer, tTable: Buffer, name = "anim"): Buffer {
    const flags = data.readUInt32LE(4);
    if (data.readUInt32LE(0) !== 1 || !(flags & 0x800000) || flags & ANIM_FLAGS_UNSUPPORTED)
        throw new FormatError(`${name}: an animation of flags ${hex8(flags)}`);
    const bones = data.readUInt32LE(0xc);
    const qSize = data.readUInt32LE(0x1c);
    const tSize = data.readUInt32LE(0x20);
    const sizesAt = 0x24;
    let start = (sizesAt + 4 * bones + 3) & ~3;
    if (flags & 0x80000) {
        // (a partial animation: u32 bone count, then a bit per bone - kept as it is)
        const count = data.readInt32LE(start);
        start = (start + 4 * (((count - 1) >> 5) + 2) + 3) & ~3;
    }
    const qPer: Buffer[] = [];
    const tPer: Buffer[] = [];
    let p = start;
    const entry = (table: Buffer, index: number): Buffer => table.subarray(index * 8, index * 8 + 6);
    for (let b = 0; b < bones; b++) {
        const end = p + data.readUInt16LE(sizesAt + 2 * b);
        const parts: Buffer[] = [];
        while (p < end) {
            const h = data.readUInt16LE(p);
            if (h & 0x4000 && !(h & 0x3800)) {
                const out = Buffer.alloc(2);
                out.writeUInt16LE(h & ~0x4000);
                parts.push(out, entry(qTable, data[p + 2]!));
                p += 3;
                continue;
            }
            const size = !(h & 0x4000) ? 8 : 2 + (h & 0x2000 ? 1 : 2) + (h & 0x1000 ? 1 : 2) + (h & 0x800 ? 1 : 2);
            parts.push(data.subarray(p, p + size));
            p += size;
        }
        if (p !== end) throw new FormatError(`${name}: bone ${b}'s rotation keys overrun`);
        qPer.push(Buffer.concat(parts));
    }
    if (p !== start + qSize) throw new FormatError(`${name}: rotation keys don't add up`);
    for (let b = 0; b < bones; b++) {
        const end = p + data.readUInt16LE(sizesAt + 2 * bones + 2 * b);
        const parts: Buffer[] = [];
        while (p < end) {
            const k = data[p]!;
            const head = 1 + (k & 0x40 ? 0 : 2);
            if (k & 0x80) {
                const out = Buffer.from(data.subarray(p, p + head));
                out[0] = k & ~0x80;
                parts.push(out, entry(tTable, data[p + head]!));
                p += head + 1;
            } else {
                parts.push(data.subarray(p, p + head + 6));
                p += head + 6;
            }
        }
        if (p !== end) throw new FormatError(`${name}: bone ${b}'s translation keys overrun`);
        tPer.push(Buffer.concat(parts));
    }
    if (p !== start + qSize + tSize) throw new FormatError(`${name}: translation keys don't add up`);
    const rest = data.subarray((p + 3) & ~3);
    const q = Buffer.concat(qPer);
    const t = Buffer.concat(tPer);
    if (qPer.some((x) => x.length > 0xffff) || tPer.some((x) => x.length > 0xffff))
        throw new FormatError(`${name}: a bone's keys past 64 KB`);
    const head = Buffer.from(data.subarray(0, start));
    head.writeUInt32LE(q.length, 0x1c);
    head.writeUInt32LE(t.length, 0x20);
    for (let b = 0; b < bones; b++) {
        head.writeUInt16LE(qPer[b]!.length, sizesAt + 2 * b);
        head.writeUInt16LE(tPer[b]!.length, sizesAt + 2 * bones + 2 * b);
    }
    const body = Buffer.concat([q, t]);
    const pad = Buffer.alloc(((body.length + 3) & ~3) - body.length);
    return Buffer.concat([head, body, pad, rest]);
}
