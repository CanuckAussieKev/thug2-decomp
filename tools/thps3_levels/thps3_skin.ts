// NOT ORIGINAL - THPS3's skinned models (.SKN) and animations (.ska), read and made THUG2's (docs/ped-models-plan.md,
// phase 2: THPS3's own people in the THPS3 port).
//
// .SKN - a RenderWare 3.1 clump (thps3.ts's chunks):
//   frame list: per frame a 3 x 3 rotation (rows right / up / at: a point p maps to p.x * right + p.y * up + p.z * at +
//     position), the position, the parent frame; each frame's extension holds Neversoft's bone chunk (0x108): i32 the
//     bone's number (-1: a frame that isn't a bone - the ends of the limbs), then data not used here. Frame 0 is the
//     model's root (rows (1,0,0), (0,0,-1), (0,1,0): the bones' z-up space turned y-up), frame 1 bone 0. Every bone
//     frame has an identity rotation: the rig is positions only.
//   one geometry (positions, normals, uvs, the material list, a bin mesh of strips per material) and one atomic whose
//     extension holds RenderWare's skin (0x116): u32 bones, u32 vertices, per vertex 4 x u8 bone numbers, per vertex
//     4f weights, per bone 76 bytes {u32 its number, u32 its number, u32 flags, 4 x 4f the inverse bind matrix (rows,
//     the fourth float of each not used: garbage in the files), the translation row last}.
//   The geometry's vertices are in bone 0's space (z up, the origin at the pelvis).
// .ska - Neversoft's RenderWare animation:
//   u32 (a number per file, not used), u32 0x80000000, f32 duration (seconds), u32 rotation keys, u32 translation keys,
//   the rotation keys {4f quaternion x y z w, f32 time, u32 the previous key's offset in the rotation keys (bytes)},
//   the translation keys {3f, f32 time, u32 the previous key's offset}. The first key of each track is the bone's at
//   time 0, in bone order (as RenderWare streams them); a later key's bone is its previous key's. Rotations and
//   translations are the bone's local transform (relative to its parent bone), as THUG2's.
//
// Quaternions: THPS3's and THUG2's are stored the same way - a stored (x, y, z, w) turns a point by its conjugate
// (measured: THUG2's THPS6_human rest pose places its bones where Ped_Construction's skin has them only that way, and
// THPS3's walk bends the knees backwards only that way). The maths below is in the usual (Hamilton) sense: conj() at the
// files' edges.

import { FormatError, Reader, Writer } from "../level_port/binio.ts";
import { checksum } from "../level_port/qb.ts";
import * as thps3 from "./thps3.ts";

const SKIN = 0x116;
const BONE = 0x108;

// --- maths ---------------------------------------------------------------------------------------------------------

export type Vec = [number, number, number];
export type Quat = [number, number, number, number]; // x y z w

export const IDENTITY: Quat = [0, 0, 0, 1];

export function qmul(a: Quat, b: Quat): Quat {
    return [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ];
}

export function conj(q: Quat): Quat {
    return [-q[0], -q[1], -q[2], q[3]];
}

export function rotate(q: Quat, v: readonly number[]): Vec {
    const r = qmul(qmul(q, [v[0]!, v[1]!, v[2]!, 0]), conj(q));
    return [r[0], r[1], r[2]];
}

export function normalize(q: Quat): Quat {
    const n = Math.hypot(...q) || 1;
    return [q[0] / n, q[1] / n, q[2] / n, q[3] / n];
}

// A rotation matrix's quaternion (rows right / up / at as RenderWare's frames: x maps to right...).
export function matrixQuat(rows: readonly number[]): Quat {
    // (columns of the usual matrix are the rows here)
    const m = (r: number, c: number): number => rows[c * 3 + r]!;
    const trace = m(0, 0) + m(1, 1) + m(2, 2);
    let q: Quat;
    if (trace > 0) {
        const s = Math.sqrt(trace + 1) * 2;
        q = [(m(2, 1) - m(1, 2)) / s, (m(0, 2) - m(2, 0)) / s, (m(1, 0) - m(0, 1)) / s, s / 4];
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const s = Math.sqrt(1 + m(0, 0) - m(1, 1) - m(2, 2)) * 2;
        q = [s / 4, (m(0, 1) + m(1, 0)) / s, (m(0, 2) + m(2, 0)) / s, (m(2, 1) - m(1, 2)) / s];
    } else if (m(1, 1) > m(2, 2)) {
        const s = Math.sqrt(1 + m(1, 1) - m(0, 0) - m(2, 2)) * 2;
        q = [(m(0, 1) + m(1, 0)) / s, s / 4, (m(1, 2) + m(2, 1)) / s, (m(0, 2) - m(2, 0)) / s];
    } else {
        const s = Math.sqrt(1 + m(2, 2) - m(0, 0) - m(1, 1)) * 2;
        q = [(m(0, 2) + m(2, 0)) / s, (m(1, 2) + m(2, 1)) / s, s / 4, (m(1, 0) - m(0, 1)) / s];
    }
    return normalize(q);
}

// --- .SKN ----------------------------------------------------------------------------------------------------------

export interface SknBone {
    frame: number; // its frame in the clump
    parent: number; // its parent bone (-1: none - bone 0)
    // the bind pose (the inverse bind matrix inverted), in the geometry's space
    position: Vec;
    rotation: Quat;
}

export interface SknPart {
    geometry: thps3.Geometry;
    // per vertex of the geometry
    vertexBones: number[][]; // 4 bone numbers
    vertexWeights: number[][]; // 4 weights
}

export interface Skn {
    clump: thps3.Clump;
    root: Quat; // the root frame's rotation (bone 0's parent: bone 0's space into the model's, y up)
    bones: SknBone[];
    parts: SknPart[]; // per atomic (the pros: the body, the board)
}

function findChunk(data: Buffer, start: number, end: number, kind: number, name: string): thps3.Chunk | null {
    for (const c of thps3.children(data, start, end, name)) {
        if (c[0] === kind) return c;
        if (c[0] === thps3.EXTENSION) {
            const inner = findChunk(data, c[1], c[1] + c[2], kind, name);
            if (inner !== null) return inner;
        }
    }
    return null;
}

export function readSkn(data: Buffer, name = "skn"): Skn {
    const [clump, end] = thps3.readClump(data, 0, name);
    // (the frames' bone numbers and the atomics' skins: walked again from the clump's chunks - to the end readClump
    // found: the clump's declared size leaves out its extension's Neversoft header, as the .dff's)
    const kids = thps3.children(data, 12, end, name);
    const frameList = kids.find((k) => k[0] === thps3.FRAMELIST);
    const atomics = kids.filter((k) => k[0] === thps3.ATOMIC);
    if (frameList === undefined || !atomics.length) throw new FormatError(`${name}: no frame list / atomic`);
    const boneOfFrame: number[] = [];
    for (const [kind, o, n] of thps3.children(data, frameList[1], frameList[1] + frameList[2], name)) {
        if (kind !== thps3.EXTENSION) continue;
        const bone = n ? findChunk(data, o, o + n, BONE, name) : null;
        boneOfFrame.push(bone !== null && bone[2] >= 4 ? data.readInt32LE(bone[1]) : -1);
    }
    if (boneOfFrame.length !== clump.frames.length)
        throw new FormatError(`${name}: ${boneOfFrame.length} frame extensions for ${clump.frames.length} frames`);
    let bones: SknBone[] | null = null;
    const parts: SknPart[] = [];
    for (const [k, atomic] of atomics.entries()) {
        const skin = findChunk(data, atomic[1], atomic[1] + atomic[2], SKIN, name);
        if (skin === null) throw new FormatError(`${name}: atomic ${k} has no skin`);
        const geometry = clump.geometries[clump.atomics[k]!.geometry];
        if (geometry === undefined) throw new FormatError(`${name}: atomic ${k}'s geometry`);
        const r = new Reader(data.subarray(skin[1], skin[1] + skin[2]), 0, name);
        const boneCount = r.u32();
        const vertexCount = r.u32();
        if (vertexCount !== geometry.positions.length)
            throw new FormatError(`${name}: skin of ${vertexCount} vertices, geometry of ${geometry.positions.length}`);
        const vertexBones: number[][] = [];
        for (let v = 0; v < vertexCount; v++) vertexBones.push(r.read("4B"));
        const vertexWeights: number[][] = [];
        for (let v = 0; v < vertexCount; v++) vertexWeights.push(r.read("4f"));
        const these: SknBone[] = [];
        for (let b = 0; b < boneCount; b++) {
            r.read("3I");
            const m = r.read("16f");
            // (the inverse of the bind pose: a point's p.x * row 0 + p.y * row 1 + p.z * row 2 + row 3)
            const inverse = matrixQuat([m[0]!, m[1]!, m[2]!, m[4]!, m[5]!, m[6]!, m[8]!, m[9]!, m[10]!]);
            const rotation = conj(inverse);
            const position = rotate(rotation, [m[12]!, m[13]!, m[14]!]).map((x) => -x) as Vec;
            const frame = boneOfFrame.indexOf(b);
            if (frame < 0) throw new FormatError(`${name}: no frame for bone ${b}`);
            these.push({ frame, parent: -1, position, rotation });
        }
        r.expectEnd();
        for (const v of vertexBones) for (const b of v) if (b >= boneCount) throw new FormatError(`${name}: bone ${b}`);
        if (bones === null) bones = these;
        else if (
            these.length !== bones.length ||
            these.some((b, i) => b.position.some((x, j) => Math.abs(x - bones![i]!.position[j]!) > 1e-3))
        )
            throw new FormatError(`${name}: atomic ${k}'s skin has other bones`);
        parts.push({ geometry, vertexBones, vertexWeights });
    }
    for (const bone of bones!) {
        let f = clump.frames[bone.frame]!.parent;
        while (f >= 0 && boneOfFrame[f]! < 0) f = clump.frames[f]!.parent;
        bone.parent = f >= 0 ? boneOfFrame[f]! : -1;
    }
    for (const [b, bone] of bones!.entries()) {
        if (bone.parent >= b) throw new FormatError(`${name}: bone ${b}'s parent ${bone.parent} comes after it`);
        if (bone.parent < 0 !== (b === 0)) throw new FormatError(`${name}: bone ${b} is a root`);
    }
    const rootFrame = clump.frames[bones![0]!.frame]!.parent;
    if (rootFrame < 0) throw new FormatError(`${name}: bone 0 has no root frame`);
    return { clump, root: matrixQuat(clump.frames[rootFrame]!.rotation), bones: bones!, parts };
}

// --- .ska ----------------------------------------------------------------------------------------------------------

export interface RotationKey {
    time: number; // seconds
    q: Quat; // Hamilton (the file's conjugated)
}
export interface TranslationKey {
    time: number;
    t: Vec;
}
export interface Ska {
    duration: number;
    rotations: RotationKey[][]; // per bone, in time order
    translations: TranslationKey[][];
}

export function readSka(data: Buffer, bones: number, name = "ska"): Ska {
    const r = new Reader(data, 0, name);
    const [, flags] = r.read("2I") as [number, number];
    const duration = r.f32();
    const [nq, nt] = r.read("2I") as [number, number];
    if (flags !== 0x80000000 || nq < bones || nt < bones || data.length !== 20 + 24 * nq + 20 * nt)
        throw new FormatError(`${name}: not an animation of ${bones} bones`);
    const rotations: RotationKey[][] = Array.from({ length: bones }, () => []);
    const owner: number[] = [];
    for (let i = 0; i < nq; i++) {
        const [x, y, z, w, time] = r.read("5f") as [number, number, number, number, number];
        const prev = r.i32();
        const b = i < bones ? i : owner[prev / 24];
        if (b === undefined || (i >= bones && (prev % 24 || prev / 24 >= i)))
            throw new FormatError(`${name}: rotation key ${i}'s previous key ${prev}`);
        owner.push(b);
        rotations[b]!.push({ time, q: conj(normalize([x, y, z, w])) });
    }
    const translations: TranslationKey[][] = Array.from({ length: bones }, () => []);
    const owner2: number[] = [];
    for (let i = 0; i < nt; i++) {
        const [x, y, z, time] = r.read("4f") as [number, number, number, number];
        const prev = r.i32();
        const b = i < bones ? i : owner2[prev / 20];
        if (b === undefined || (i >= bones && (prev % 20 || prev / 20 >= i)))
            throw new FormatError(`${name}: translation key ${i}'s previous key ${prev}`);
        owner2.push(b);
        translations[b]!.push({ time, t: [x, y, z] });
    }
    r.expectEnd();
    for (const track of [...rotations, ...translations]) {
        for (let k = 1; k < track.length; k++) {
            if (track[k]!.time < track[k - 1]!.time) throw new FormatError(`${name}: keys out of time order`);
        }
    }
    return { duration, rotations, translations };
}

// --- THUG2's skeleton (.ske.xbx) -----------------------------------------------------------------------------------
// u32 2, u32 0, u32 bones, the bones' name checksums, their parents' (0: none), their mirror bones' (0: none - the
// flipped animations' partner), then per bone the rest pose: 4f quaternion (THUG2's stored form), 4f translation (w 1)
// - local, relative to the parent.

export interface SkeletonBone {
    name: string;
    parent: number; // -1: none
    mirror: number; // -1: none
    q: Quat; // Hamilton
    t: Vec;
}

export function writeSkeleton(bones: readonly SkeletonBone[]): Buffer {
    const w = new Writer();
    w.write("3I", 2, 0, bones.length);
    const ids = bones.map((b) => checksum(b.name));
    for (const id of ids) w.u32(id);
    for (const b of bones) w.u32(b.parent < 0 ? 0 : ids[b.parent]!);
    for (const b of bones) w.u32(b.mirror < 0 ? 0 : ids[b.mirror]!);
    for (const b of bones) {
        w.write("4f", ...conj(b.q));
        w.write("4f", ...b.t, 1);
    }
    return w.getvalue();
}

// THUG2's skeleton read back: per bone its parent (-1: none) and rest pose (Hamilton).
export function readSkeleton(data: Buffer, name = "ske"): { parents: number[]; rest: { q: Quat; t: Vec }[] } {
    const r = new Reader(data, 0, name);
    const [version, , count] = r.read("3I") as [number, number, number];
    if (version !== 2) throw new FormatError(`${name}: version ${version}`);
    const ids = r.read(`${count}I`);
    const parents = r.read(`${count}I`).map((p) => (p === 0 ? -1 : ids.indexOf(p)));
    r.read(`${count}I`);
    const rest: { q: Quat; t: Vec }[] = [];
    for (let b = 0; b < count; b++) {
        const q = conj(r.read("4f") as Quat);
        const t = r.read("4f").slice(0, 3) as Vec;
        rest.push({ q, t });
    }
    r.expectEnd();
    return { parents, rest };
}

// --- THUG2's animation (.ska.xbx) ----------------------------------------------------------------------------------
// (the layout: tools/thug1_levels/thug1.ts untableAnim; the decoders 0x00482C20 / 0x00482E70, the player 0x00484760)
// Only the plain key forms are written: rotation {u16 time (frames at 60 a second) | 0x8000 when w < 0, 3 x i16 x y z
// (/ 16384)}, translation {u8 0, u16 time, 3 x i16 x y z (/ 32)}. A bone without rotation keys is held at no rotation;
// with these flags every bone's translation is read from its keys (absolute, as THPS3's).
export const ANIM_FLAGS = 0x06810000;
export const FPS = 60;
const Q_SCALE = 16384;
const T_SCALE = 32;

export interface Track {
    rotations: RotationKey[];
    translations: TranslationKey[];
}

const clampI16 = (v: number): number => Math.max(-32768, Math.min(32767, Math.round(v)));

export function writeAnim(duration: number, tracks: readonly Track[], name = "anim"): Buffer {
    const rot: Buffer[] = [];
    const tra: Buffer[] = [];
    let nq = 0;
    let nt = 0;
    for (const track of tracks) {
        const w = new Writer();
        let last = -1;
        for (const k of track.rotations) {
            const frame = Math.round(k.time * FPS);
            if (frame <= last) continue; // (two keys rounding to one frame: the first kept)
            if (frame >= 0x4000) throw new FormatError(`${name}: a key past frame 0x3FFF`);
            last = frame;
            const q = conj(normalize(k.q)); // (stored form)
            w.u16(frame | (q[3] < 0 ? 0x8000 : 0));
            w.write("3h", clampI16(q[0] * Q_SCALE), clampI16(q[1] * Q_SCALE), clampI16(q[2] * Q_SCALE));
            nq += 1;
        }
        rot.push(w.getvalue());
        const v = new Writer();
        last = -1;
        for (const k of track.translations) {
            const frame = Math.round(k.time * FPS);
            if (frame <= last) continue;
            if (frame > 0xffff) throw new FormatError(`${name}: a key past frame 0xFFFF`);
            last = frame;
            if (k.t.some((x) => Math.abs(x * T_SCALE) > 32767))
                throw new FormatError(`${name}: a translation past 1024 units`);
            v.u8(0);
            v.u16(frame);
            v.write("3h", ...k.t.map((x) => clampI16(x * T_SCALE)));
            nt += 1;
        }
        tra.push(v.getvalue());
    }
    for (const b of [...rot, ...tra]) if (b.length > 0xffff) throw new FormatError(`${name}: a bone's keys past 64 KB`);
    const q = Buffer.concat(rot);
    const t = Buffer.concat(tra);
    const w = new Writer();
    w.write("2I", 1, ANIM_FLAGS);
    w.f32(duration);
    w.write("6I", tracks.length, nq, nt, 0, q.length, t.length);
    for (const b of rot) w.u16(b.length);
    for (const b of tra) w.u16(b.length);
    w.padTo(4);
    w.raw(q);
    w.raw(t);
    w.padTo(4);
    return w.getvalue();
}

// THUG2's .ska.xbx read back (the plain forms writeAnim makes; the self-check's round trip).
export function readAnim(data: Buffer, name = "anim"): { duration: number; tracks: Track[] } {
    const r = new Reader(data, 0, name);
    const [version, flags] = r.read("2I") as [number, number];
    if (version !== 1 || flags !== ANIM_FLAGS) throw new FormatError(`${name}: flags ${flags.toString(16)}`);
    const duration = r.f32();
    const [bones, , , custom, qBytes, tBytes] = r.read("6I") as number[];
    if (custom) throw new FormatError(`${name}: custom keys`);
    const qSizes = r.read(`${bones}H`);
    const tSizes = r.read(`${bones}H`);
    r.offset = (r.offset + 3) & ~3;
    const tracks: Track[] = [];
    for (let b = 0; b < bones!; b++) tracks.push({ rotations: [], translations: [] });
    for (let b = 0; b < bones!; b++) {
        const end = r.offset + qSizes[b]!;
        while (r.offset < end) {
            const h = r.u16();
            if (h & 0x4000) throw new FormatError(`${name}: a packed rotation key`);
            const xyz = r.read("3h").map((x) => x / Q_SCALE);
            const w = Math.sqrt(Math.max(0, 1 - xyz[0]! ** 2 - xyz[1]! ** 2 - xyz[2]! ** 2)) * (h & 0x8000 ? -1 : 1);
            tracks[b]!.rotations.push({ time: (h & 0x3fff) / FPS, q: conj([xyz[0]!, xyz[1]!, xyz[2]!, w]) });
        }
    }
    for (let b = 0; b < bones!; b++) {
        const end = r.offset + tSizes[b]!;
        while (r.offset < end) {
            if (r.u8() !== 0) throw new FormatError(`${name}: a packed translation key`);
            const time = r.u16() / FPS;
            const t = r.read("3h").map((x) => x / T_SCALE) as Vec;
            tracks[b]!.translations.push({ time, t });
        }
    }
    if (tracks.reduce((s, t) => s + t.rotations.length, 0) * 8 !== qBytes) throw new FormatError(`${name}: sizes`);
    if (tracks.reduce((s, t) => s + t.translations.length, 0) * 9 !== tBytes) throw new FormatError(`${name}: sizes`);
    return { duration, tracks };
}
