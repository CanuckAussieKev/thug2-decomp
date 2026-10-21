// NOT ORIGINAL - THPS3's own people in the THPS3 level port (docs/ped-models-plan.md, phase 2): the ped models of the
// peds the port places, on THPS3's own skeletons, with THPS3's own animations.
//
// - Rig: THPS3's people are skinned to RenderWare skeletons of their own (thps3_skin.ts): 14 bones for the men's set
//   (pedestrian_a: root, pelvis, the legs, a torso, a head, the arms - no hands, no neck), 15 for the women's (Pedfem_a:
//   one more bone on the torso). THUG2's THPS6_human (50 bones) isn't retargeted to: THPS3's own skeleton is written as
//   a THUG2 .ske.xbx and THPS3's own animations as .ska.xbx, so THPS3's people move as they did in THPS3. (THUGPRO
//   re-rigged THPS3's models to THPS6_human by hand in its toolchain; read only, as a reference that the models work
//   in THUG2 - nothing taken.)
// - Skeleton per set: THPS3's models of one set share its animations but each has its own bind pose (RenderWare skins
//   with the bones' inverse bind matrices); THUG2 skins with the inverse of the skeleton's rest pose. One skeleton per
//   set (THPS3_<set>_skeleton: the set's base model's bind pose) and each model's vertices moved into that pose, bone by
//   bone (refit: within 0.05 units of where THPS3 poses them - the self-check's poseError). THUG2 finds an animation by
//   its name + the node's ReferenceChecksum, else its skeleton's name: the set's animations load under the skeleton's
//   name (SetReferenceChecksum), so the nodes need no reference.
// - Faces: THPS3 draws its people double-sided and its skins' strips wind both ways; THUG2 culls a skin's back faces,
//   so every triangle is turned to face along its normals (outwardStrip).
// - Space: THPS3's bones are z-up with the model's origin at the pelvis; the root frame turns them y-up (as THUG2's
//   own skeletons' root bone does). THUG2's bone 0 is THPS3's bone 0 with that turn and the height of the pelvis
//   above the feet (ground: the set's idle, frame 0, puts the lowest vertex of its base model at 0) - in the skeleton,
//   the animations' bone 0 keys and the vertices alike.
// - Models: models\THPS3_Peds\<model>\<model>.skin.xbx (the RenderWare geometry: per material a mesh of the
//   strip's vertices, THUG2's skinned vertex - thug2_scene.ts), .tex.xbx (the model's .tdx), an empty .col.xbx, the
//   12-byte .cas.xbx. Profiles THPS3_<model> = {ped_body={desc_id=THPS3_<model>}}, the part no_random: added to THUG2's
//   lists through the overlay (tools/level_port/ped_models.ts).
// - Animations: THUG2's ped AI drives them, so each set's script loads, under the set's reference, every animation the
//   ped AI may play (thug1_peds.pedAiAnims) from THPS3's file of the same role (ROLES; else the set's idle) - an
//   animation the set hasn't crashes the game (0x00482BA0) - and `default`. Files: anims\THPS3\<set>\<THPS3's name>.
// - Not yet: THPS3's pros (PedPro_*: built from the skater's CAS parts on the skater's 29-bone rig) and the crowds /
//   animals (the user's decision: later) - THUG2's stand-ins as before (the pros THUG2 has: its own PedPro_ models).
// - Positions (for tests): a ped stands at (x, y, -z) of its node's Position in the level's .qb (measured in Foundry,
//   Rio, Suburbia and Airport) - the automation channel's teleport / camera near a ped take (x, y, -z).

import * as fs from "node:fs";
import * as path from "node:path";
import { FormatError } from "../level_port/binio.ts";
import * as geometry from "../level_port/geometry.ts";
import * as pedModels from "../level_port/ped_models.ts";
import * as pedRigs from "../level_port/ped_rigs.ts";
import * as qb from "../level_port/qb.ts";
import { checksum } from "../level_port/qb.ts";
import * as tex from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { decodeLatin1, hex8 } from "../lib/py.ts";
import { pedAiAnims, Thug2Globals } from "../thug1_levels/thug1_peds.ts";
import * as thps3 from "./thps3.ts";
import * as levels from "./thps3_levels.ts";
import * as skin from "./thps3_skin.ts";

export const MODELS = "THPS3_Peds"; // models\THPS3_Peds\...
export const ANIMS = "THPS3"; // anims\THPS3\...
export const PREFIX = "THPS3_";
export const SECTOR_FLAGS = 0x80402057; // (THUG2's ped skins': uvs, colours, normals, weights)
// The root frame's turn: z-up bones into THUG2's y-up model (every THPS3 ped's root frame but a few, which have none
// stored and are posed the same way by the game).
export const ROOT: skin.Quat = [-Math.SQRT1_2, 0, 0, Math.SQRT1_2];

export interface SetInfo {
    female: boolean;
    base: string; // the model the ground height is measured on
    idle: string; // its idle animation (THPS3's name)
}

// THPS3's people's animation sets brought (its animload_<set> scripts: the men's, the women's).
export const SETS: Readonly<Record<string, SetInfo>> = {
    pedestrian_a: { female: false, base: "Pedestrian_A", idle: "Idle" },
    pedfem_a: { female: true, base: "Pedfem_a", idle: "Idle1" },
};

// An animation THUG2's ped AI may play: THPS3's of the same role - [pattern, the man's, the woman's]; the first that
// matches; else the set's idle.
export const ROLES: readonly (readonly [RegExp, string, string])[] = [
    [/getup/i, "GetUp", "GetUp"],
    [/falldown|knockdown|fall|layidle|lyingidle/i, "Fall", "KnockDown"],
    [/jump/i, "JumpBack", "Jump"],
    [/push|punch|slap|attack|shove/i, "Push", "Push"],
    [/clap/i, "Clap", "Clap"],
    [/cheer/i, "Cheering", "Clap"],
    [/wave|thumb/i, "Wave", "Wave"],
    [/disgust|angry|fist|pissed/i, "Disgust", "Disgust"],
    [/talk/i, "Talk", "Talk1"],
    [/rotate|turn/i, "RotateLeft", "Idle1Rotate"],
    [/idle\d*toidle/i, "IdletoIdle2", "Idle1toIdle2"],
    [/walk\w*toidle|toidle|tostand/i, "WalktoIdle", "WalktoIdle1"],
    [/fromidle|idletowalk|towalk/i, "IdletoWalk", "Idle1toWalk"],
    [/run/i, "Run", "Run"],
    [/walk/i, "Walk1", "Walk"],
];

export interface OwnPed {
    profile: string;
    rig: pedRigs.Rig;
    set: string;
    model: string; // THPS3's model folder's name
}

interface ModelInfo {
    folder: string; // Data\Models\<folder>
    skn: string;
    tdx: string | null;
    set: string; // (lower case, a SETS key)
}

interface Converted {
    skin: Buffer;
    tex: Buffer;
    folder: string; // THPS3's model folder
}

export interface Thps3PedsOptions {
    thug2: string; // THUG2's game folder (Data\anims: the ped AI's animation names)
    thug2Anims: ReadonlySet<number>; // every animation THUG2 loads (script_port.Thug2.anims)
}

// THPS3's animload scripts: animload_<name> -> the set it calls (animload_ped_Constr_A { animload_pedestrian_a }) or,
// for a set's own, its LoadAnims [descChecksum's name, file].
interface AnimScripts {
    calls: Map<string, string>; // lower name -> lower set
    loads: Map<string, [string, string][]>; // lower set -> [desc name, file]
}

export function readAnimScripts(scriptsDir: string): AnimScripts {
    const calls = new Map<string, string>();
    const loads = new Map<string, [string, string][]>();
    const nameKey = checksum("name");
    const descKey = checksum("descChecksum");
    for (const file of fs.readdirSync(scriptsDir).sort()) {
        if (!file.toLowerCase().endsWith(".qb")) continue;
        let q: qb.QbFile;
        try {
            q = new qb.QbFile(fs.readFileSync(path.join(scriptsDir, file)), file);
        } catch (e) {
            if (e instanceof FormatError || e instanceof RangeError) continue;
            throw e;
        }
        for (const [script, lines] of q.scripts) {
            const text = q.names.get(script) ?? "";
            if (!/^animload_/i.test(text)) continue;
            const own = text.slice("animload_".length).toLowerCase();
            const list: [string, string][] = [];
            for (const line of lines) {
                const first = line[0];
                if (first?.[0] === 0x16 && line.length === 1) {
                    const called = q.names.get(first[1].readUInt32LE(0)) ?? "";
                    if (/^animload_/i.test(called)) calls.set(own, called.slice("animload_".length).toLowerCase());
                }
                let desc = "";
                let name = "";
                for (let i = 0; i + 2 < line.length; i++) {
                    const [t, p] = line[i]!;
                    if (t !== 0x16 || line[i + 1]![0] !== 0x07) continue;
                    const [vt, vp] = line[i + 2]!;
                    const key = p.readUInt32LE(0);
                    if (key === descKey && vt === 0x16) desc = q.names.get(vp.readUInt32LE(0)) ?? "";
                    if (key === nameKey && vt === 0x1b) name = decodeLatin1(qb.rstripNul(vp));
                }
                if (desc && name) list.push([desc, name]);
            }
            if (list.length) loads.set(own, list);
        }
    }
    return { calls, loads };
}

// THUG2's animation names (its Data\anims files' names: a LoadAnim's descChecksum is its file's name), for ROLES.
export function thug2AnimNames(thug2: string): Map<number, string> {
    const out = new Map<number, string>();
    const walk = (dir: string): void => {
        if (!fs.existsSync(dir)) return;
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            if (e.isDirectory()) walk(path.join(dir, e.name));
            else if (/\.ska\.xbx$/i.test(e.name)) {
                const name = e.name.slice(0, -".ska.xbx".length);
                out.set(checksum(name), name);
            }
        }
    };
    walk(path.join(thug2, "Data", "anims"));
    return out;
}

// --- conversion ---------------------------------------------------------------------------------------------------

const add = (a: readonly number[], b: readonly number[]): skin.Vec => [a[0]! + b[0]!, a[1]! + b[1]!, a[2]! + b[2]!];
const sub = (a: readonly number[], b: readonly number[]): skin.Vec => [a[0]! - b[0]!, a[1]! - b[1]!, a[2]! - b[2]!];

// The bones' world poses for local ones (Hamilton quaternions, parents first).
export function worldPoses(
    parents: readonly number[],
    locals: readonly { q: skin.Quat; t: skin.Vec }[],
): { q: skin.Quat; t: skin.Vec }[] {
    const out: { q: skin.Quat; t: skin.Vec }[] = [];
    for (const [b, local] of locals.entries()) {
        const p = parents[b]!;
        if (p < 0) out.push({ q: local.q, t: local.t });
        else {
            const parent = out[p]!;
            out.push({ q: skin.qmul(parent.q, local.q), t: add(parent.t, skin.rotate(parent.q, local.t)) });
        }
    }
    return out;
}

// A track's value at a time (THPS3's keys: the previous key's, slerped / lerped to the next).
export function sampleRotation(keys: readonly skin.RotationKey[], time: number): skin.Quat {
    let k = 0;
    while (k + 1 < keys.length && keys[k + 1]!.time <= time) k += 1;
    const a = keys[k]!;
    const b = keys[k + 1];
    if (b === undefined || b.time <= a.time) return a.q;
    const f = (time - a.time) / (b.time - a.time);
    const dot = a.q.reduce((s, x, i) => s + x * b.q[i]!, 0);
    const sign = dot < 0 ? -1 : 1;
    return skin.normalize(a.q.map((x, i) => x * (1 - f) + sign * b.q[i]! * f) as skin.Quat);
}

export function sampleTranslation(keys: readonly skin.TranslationKey[], time: number): skin.Vec {
    let k = 0;
    while (k + 1 < keys.length && keys[k + 1]!.time <= time) k += 1;
    const a = keys[k]!;
    const b = keys[k + 1];
    if (b === undefined || b.time <= a.time) return a.t;
    const f = (time - a.time) / (b.time - a.time);
    return a.t.map((x, i) => x * (1 - f) + b.t[i]! * f) as skin.Vec;
}

// The lowest point of a model posed by an animation's first frame, in its bones' (z-up) space: -height.
export function lowestPoint(model: skin.Skn, anim: skin.Ska): number {
    const parents = model.bones.map((b) => b.parent);
    const posed = worldPoses(
        parents,
        model.bones.map((_, b) => ({
            q: sampleRotation(anim.rotations[b]!, 0),
            t: sampleTranslation(anim.translations[b]!, 0),
        })),
    );
    let low = Infinity;
    for (const part of model.parts) {
        part.geometry.positions.forEach((p, v) => {
            let z = 0;
            part.vertexBones[v]!.forEach((b, k) => {
                const w = part.vertexWeights[v]![k]!;
                if (!w) return;
                const bind = model.bones[b]!;
                const local = skin.rotate(skin.conj(bind.rotation), sub(p, bind.position));
                z += w * add(posed[b]!.t, skin.rotate(posed[b]!.q, local))[2];
            });
            low = Math.min(low, z);
        });
    }
    return low;
}

// A bone's mirror (the flipped animations' partner): the bone of the same parent's mirror whose bind position is its
// own mirrored in x (THPS3's people are x-symmetric: right at -x); -1 for a bone on the middle.
export function mirrors(bones: readonly skin.SknBone[]): number[] {
    return bones.map((bone, b) => {
        if (Math.abs(bone.position[0]) < 0.5) return -1;
        let best = -1;
        let bestD = 1.0;
        bones.forEach((other, c) => {
            if (c === b) return;
            const d = Math.hypot(
                other.position[0] + bone.position[0],
                other.position[1] - bone.position[1],
                other.position[2] - bone.position[2],
            );
            if (d < bestD) {
                best = c;
                bestD = d;
            }
        });
        return best;
    });
}

// The model's skeleton: its bind pose as the rest pose, bone 0 turned y-up and raised by `height`.
export function skeletonOf(model: skin.Skn, height: number, name: string): Buffer {
    const mirror = mirrors(model.bones);
    const bones: skin.SkeletonBone[] = model.bones.map((bone, b) => {
        let q: skin.Quat;
        let t: skin.Vec;
        if (bone.parent < 0) {
            q = skin.qmul(ROOT, bone.rotation);
            t = add(skin.rotate(ROOT, bone.position), [0, height, 0]);
        } else {
            const parent = model.bones[bone.parent]!;
            const inverse = skin.conj(parent.rotation);
            q = skin.qmul(inverse, bone.rotation);
            t = skin.rotate(inverse, sub(bone.position, parent.position));
        }
        return { name: `${name}_Bone${b}`, parent: bone.parent, mirror: mirror[b]!, q, t };
    });
    return skin.writeSkeleton(bones);
}

// THPS3's animation as THUG2's, bone 0 turned and raised as the skeleton's.
export function convertAnim(anim: skin.Ska, height: number, name: string): Buffer {
    const tracks: skin.Track[] = anim.rotations.map((rotations, b) => {
        const translations = anim.translations[b]!;
        if (b) return { rotations, translations };
        return {
            rotations: rotations.map((k) => ({ time: k.time, q: skin.qmul(ROOT, k.q) })),
            translations: translations.map((k) => ({
                time: k.time,
                t: add(skin.rotate(ROOT, k.t), [0, height, 0]),
            })),
        };
    });
    return skin.writeAnim(anim.duration, tracks, name);
}

// A vertex of `model` moved into `base`'s bind pose (the set's skeleton's rest pose): each of its bones carries it
// from its own bind pose to the base's, blended by its weights - what THPS3 draws, bar the blend of bones whose two
// bind poses differ (poseError measures it).
export function refit(
    model: skin.Skn,
    base: skin.Skn,
    p: readonly number[],
    bones: readonly number[],
    weights: readonly number[],
): skin.Vec {
    let out: skin.Vec = [0, 0, 0];
    let total = 0;
    bones.forEach((b, k) => {
        const w = weights[k]!;
        if (!(w > 0)) return;
        const own = model.bones[b]!;
        const to = base.bones[b]!;
        const local = skin.rotate(skin.conj(own.rotation), sub(p, own.position));
        out = add(
            out,
            add(to.position, skin.rotate(to.rotation, local)).map((x) => x * w),
        );
        total += w;
    });
    return total > 0 ? (out.map((x) => x / total) as skin.Vec) : [p[0]!, p[1]!, p[2]!];
}

// The model's geometry as a THUG2 skin: a sector per atomic, a mesh per material (its strip's vertices), the
// vertices refitted to the set's base model's bind pose (refit), then turned and raised as the skeleton's bone 0.
export function convertModel(
    model: skin.Skn,
    base: skin.Skn,
    height: number,
    name: string,
    textures: ReadonlySet<number>,
): thug2Scene.Scene {
    const materials: thug2Scene.Material[] = [];
    const sectors: thug2Scene.Sector[] = [];
    const place = (p: readonly number[]): number[] => add(skin.rotate(ROOT, p), [0, height, 0]);
    for (const [k, part] of model.parts.entries()) {
        const g = part.geometry;
        const checksums = g.materials.map((m, i) => {
            const c = checksum(`${name}_${k}_${i}`);
            materials.push(levels.toThug2Material(c, m, textures));
            return c;
        });
        const strips: [number, number[]][] = g.binmesh?.strips
            ? g.binmesh.meshes.map((m) => [m.material, outwardStrip(m.indices, g.positions, g.normals)])
            : [...groupTriangles(g.triangles)].map(([m, tris]) => [
                  m,
                  outwardStrip(levels.trisToStrip(tris), g.positions, g.normals),
              ]);
        const meshes: thug2Scene.Mesh[] = [];
        const all: number[][] = [];
        for (const [material, strip] of strips) {
            if (strip.length < 3) continue;
            const used = [...new Set(strip)].sort((a, b) => a - b);
            const local = new Map(used.map((v, i) => [v, i]));
            const vertices = used.map((v): thug2Scene.SkinVertex => {
                const order = [0, 1, 2, 3]
                    .filter((i) => part.vertexWeights[v]![i]! > 0)
                    .sort((a, b) => part.vertexWeights[v]![b]! - part.vertexWeights[v]![a]!);
                const total = order.reduce((s, i) => s + part.vertexWeights[v]![i]!, 0) || 1;
                const weights = order.map((i) => part.vertexWeights[v]![i]! / total);
                const bones = order.map((i) => part.vertexBones[v]![i]!);
                while (bones.length < 4) bones.push(bones[0] ?? 0);
                const uv = g.uvs[v] ?? [0, 0];
                return {
                    position: place(refit(model, base, g.positions[v]!, part.vertexBones[v]!, part.vertexWeights[v]!)),
                    weights: weights.slice(0, 3),
                    bones,
                    normal: g.normals[v] ? skin.rotate(ROOT, g.normals[v]!) : [0, 1, 0],
                    color: [128, 128, 128, 128],
                    uvs: [uv[0]!, uv[1]!],
                };
            });
            const positions = vertices.map((v) => v.position);
            all.push(...positions);
            const [lo, hi, center, radius] = geometry.bounds(positions);
            const m = materials[materials.length - g.materials.length + material];
            if (m === undefined) throw new FormatError(`${name}: material ${material}`);
            meshes.push(
                thug2Scene.skinnedMesh({
                    sphere: [...center, radius],
                    bbox: [...lo, ...hi],
                    flags: 0,
                    material: checksums[material]!,
                    vertices,
                    uvSets: 1,
                    strip: strip.map((v) => local.get(v)!),
                    pixelShader: geometry.pixelShader(m),
                }),
            );
        }
        if (!meshes.length) continue;
        const [lo, hi, center, radius] = geometry.bounds(all);
        sectors.push({
            checksum: checksum(`${name}_${k}`),
            bone: -1,
            flags: SECTOR_FLAGS,
            bbox: [...lo, ...hi],
            sphere: [...center, radius],
            meshes,
        });
    }
    return { versions: [1, 1, 1], materials, sectors, hierarchyCount: 0, tail: Buffer.alloc(0) };
}

// A triangle's face (its winding's normal, unnormalised) against its vertices' normals: > 0 along them.
function facing(
    positions: readonly (readonly number[])[],
    normals: readonly (readonly number[])[],
    a: number,
    b: number,
    c: number,
): number {
    const [p, q, r] = [positions[a]!, positions[b]!, positions[c]!];
    const e1 = sub(q, p);
    const e2 = sub(r, p);
    const face = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    let along = 0;
    for (const v of [a, b, c]) {
        const n = normals[v];
        if (n !== undefined) along += face[0]! * n[0]! + face[1]! * n[1]! + face[2]! * n[2]!;
    }
    return along;
}

// A strip's triangles in drawing order (the odd ones turned back), degenerate ones (the strip's joins) left out.
function stripTriangles(strip: readonly number[]): [number, number, number][] {
    const tris: [number, number, number][] = [];
    for (let i = 0; i + 2 < strip.length; i++) {
        const [a, b, c] = [strip[i]!, strip[i + 1]!, strip[i + 2]!];
        if (a === b || b === c || a === c) continue;
        tris.push(i % 2 ? [a, c, b] : [a, b, c]);
    }
    return tris;
}

// Triangles as one strip, each joined to the last by degenerate ones only: ..., c, c, a, a, (a,) b, c' - the first
// vertex doubled so no triangle spans the join, and once more when needed to start it at an even place (its winding
// kept) - as thps3_levels.trisToStrip does since 2026-10-22 (before, it doubled only the last vertex: its joins drew
// a stray triangle (c, a, b) between pairs).
export function joinTriangles(tris: readonly (readonly number[])[]): number[] {
    const s: number[] = [];
    for (const [a, b, c] of tris as [number, number, number][]) {
        if (s.length) {
            s.push(s[s.length - 1]!, a, a);
            if ((s.length - 1) % 2) s.push(a);
        } else s.push(a);
        s.push(b, c);
    }
    return s;
}

// A strip's triangles each wound to face along its vertices' normals, joined again (joinTriangles). THPS3 draws its
// people without back-face culling and their strips' windings are mixed (about half of the triangles face inwards -
// measured on Ped_Security_B, Pedestrian_A, Pedfem_a...); THUG2 culls a skin's back faces, and its own skins are wound
// along their normals (all but a handful of the triangles of its peds), so as they were the people had holes.
// A triangle without normals keeps its winding.
export function outwardStrip(
    strip: readonly number[],
    positions: readonly (readonly number[])[],
    normals: readonly (readonly number[])[],
): number[] {
    return joinTriangles(
        stripTriangles(strip).map(([a, b, c]) => (facing(positions, normals, a, b, c) < 0 ? [a, c, b] : [a, b, c])),
    );
}

// How many of a strip's triangles face against their vertices' normals (none after outwardStrip).
export function inwardTriangles(
    strip: readonly number[],
    positions: readonly (readonly number[])[],
    normals: readonly (readonly number[])[],
): number {
    return stripTriangles(strip).filter(([a, b, c]) => facing(positions, normals, a, b, c) < 0).length;
}

function groupTriangles(triangles: readonly thps3.Triangle[]): Map<number, number[][]> {
    const out = new Map<number, number[][]>();
    for (const [material, a, b, c] of triangles) {
        const list = out.get(material) ?? [];
        list.push([a, b, c]);
        out.set(material, list);
    }
    return new Map([...out].sort((x, y) => x[0] - y[0]));
}

// The check of the rig: a model posed at `time` the way THUG2 will (the written skeleton's rest pose inverted, the
// written animation read back) against the way THPS3 does (RenderWare: the animation's bones times the inverse bind
// matrices; then turned and raised as the conversion does) - the largest distance between a vertex's two places.
export function poseError(model: skin.Skn, base: skin.Skn, height: number, anim: skin.Ska, time: number): number {
    const parents = model.bones.map((b) => b.parent);
    const ske = skin.readSkeleton(skeletonOf(base, height, "check"));
    const restWorld = worldPoses(ske.parents, ske.rest);
    const back = skin.readAnim(convertAnim(anim, height, "check"));
    const pose = (tracks: readonly skin.Track[]) =>
        worldPoses(
            parents,
            tracks.map((t) => ({
                q: sampleRotation(t.rotations, time),
                t: sampleTranslation(t.translations, time),
            })),
        );
    const thug2 = pose(back.tracks);
    const thps3 = pose(anim.rotations.map((rotations, b) => ({ rotations, translations: anim.translations[b]! })));
    const skinned = (
        p: readonly number[],
        bones: readonly number[],
        weights: readonly number[],
        bind: (b: number) => { q: skin.Quat; t: skin.Vec },
        posed: readonly { q: skin.Quat; t: skin.Vec }[],
    ): skin.Vec => {
        let out: skin.Vec = [0, 0, 0];
        bones.forEach((b, k) => {
            const w = weights[k]!;
            if (!w) return;
            const local = skin.rotate(skin.conj(bind(b).q), sub(p, bind(b).t));
            const at = add(posed[b]!.t, skin.rotate(posed[b]!.q, local));
            out = add(
                out,
                at.map((x) => x * w),
            );
        });
        return out;
    };
    let worst = 0;
    for (const part of model.parts) {
        part.geometry.positions.forEach((p, v) => {
            const bones = part.vertexBones[v]!;
            const weights = part.vertexWeights[v]!;
            const original = skinned(
                p,
                bones,
                weights,
                (b) => ({ q: model.bones[b]!.rotation, t: model.bones[b]!.position }),
                thps3,
            );
            const expected = add(skin.rotate(ROOT, original), [0, height, 0]);
            const placed = add(skin.rotate(ROOT, refit(model, base, p, bones, weights)), [0, height, 0]);
            const got = skinned(placed, bones, weights, (b) => restWorld[b]!, thug2);
            worst = Math.max(worst, Math.hypot(...sub(got, expected)));
        });
    }
    return worst;
}

// --- the people of the levels -------------------------------------------------------------------------------------

export class Thps3Peds {
    readonly modelsDir: string;
    readonly animsDir: string;
    private readonly anims: AnimScripts;
    private readonly models = new Map<string, ModelInfo | null>(); // lower model name -> info
    private readonly converted = new Map<string, Converted>(); // our model name -> files
    private readonly heights = new Map<string, number>(); // set -> its ground height
    private readonly pedAi: Set<number>;
    private readonly thug2Names: Map<number, string>;
    readonly failures = new Map<string, string>();
    readonly outProfiles = new Map<string, string>();
    readonly outParts = new Map<string, string>(); // our desc -> NeverScript entry (ped_body)
    readonly animFiles = new Map<string, [string, string, string]>(); // our file (lower) -> [set, THPS3's file, our file]
    private readonly readCache = new Map<string, skin.Skn>();
    private readonly skeletons = new Set<string>(); // the sets whose skeleton is written

    constructor(thps3Dir: string, options: Thps3PedsOptions) {
        this.modelsDir = path.join(thps3Dir, "Data", "Models");
        this.animsDir = path.join(thps3Dir, "Data", "Anims");
        this.anims = readAnimScripts(path.join(thps3Dir, "Data", "Scripts"));
        const globals = new Thug2Globals(path.join(options.thug2, "Data", "pre", "qb_scripts.prx"));
        this.pedAi = pedAiAnims(globals, options.thug2Anims).walker;
        this.thug2Names = thug2AnimNames(options.thug2);
    }

    static ours(text: string): string {
        return PREFIX + text.replace(/[^A-Za-z0-9_]/g, "_");
    }

    private info(model: string): ModelInfo | null {
        const key = model.toLowerCase();
        if (this.models.has(key)) return this.models.get(key)!;
        let found: ModelInfo | null = null;
        const set = this.anims.calls.get(key) ?? key;
        const folders = fs.existsSync(this.modelsDir) ? fs.readdirSync(this.modelsDir) : [];
        const folder = folders.find((f) => f.toLowerCase() === key);
        if (SETS[set] === undefined) {
            this.failures.set(model, `its animation set (${set}) isn't brought`);
        } else if (folder === undefined) {
            this.failures.set(model, "no model folder");
        } else {
            const files = fs.readdirSync(path.join(this.modelsDir, folder));
            const skn = files.find((f) => f.toLowerCase() === `${key}.skn`);
            const tdx = files.find((f) => f.toLowerCase() === `${key}.tdx`);
            if (skn === undefined) this.failures.set(model, "no .SKN");
            else found = { folder, skn, tdx: tdx ?? null, set };
        }
        this.models.set(key, found);
        return found;
    }

    private skn(info: ModelInfo): skin.Skn {
        const file = path.join(this.modelsDir, info.folder, info.skn);
        let s = this.readCache.get(file);
        if (s === undefined) {
            s = skin.readSkn(fs.readFileSync(file), file);
            this.readCache.set(file, s);
        }
        return s;
    }

    private animFile(set: string, anim: string): string | null {
        const list = this.anims.loads.get(set) ?? [];
        const found = list.find(([desc]) => desc.toLowerCase() === anim.toLowerCase());
        if (found === undefined) return null;
        const rel = found[1].replaceAll("/", "\\").replace(/^anims\\/i, "");
        return fs.existsSync(path.join(this.animsDir, rel)) ? rel : null;
    }

    private readAnim(set: string, rel: string): skin.Ska {
        const base = this.info(SETS[set]!.base)!;
        return skin.readSka(fs.readFileSync(path.join(this.animsDir, rel)), this.skn(base).bones.length, rel);
    }

    // The set's ground height: its idle's frame 0 puts the base model's lowest vertex there.
    height(set: string): number {
        let h = this.heights.get(set);
        if (h === undefined) {
            const s = SETS[set]!;
            const base = this.info(s.base);
            const idle = this.animFile(set, s.idle);
            if (base === null || idle === null) throw new Error(`THPS3's ${set}: no base model / idle`);
            h = -lowestPoint(this.skn(base), this.readAnim(set, idle));
            this.heights.set(set, h);
        }
        return h;
    }

    // A set's rig: its skeleton (the base model's bind pose), the set's animations loaded under the skeleton's name.
    // (Not THPS3_<set>: the set's base model has that name as its profile - a global structure, and a script's
    // `SetReferenceChecksum THPS3_pedestrian_a` then passes the profile's contents instead of the name: the
    // animations went in under another reference and the first ped crashed on a missing `default`.)
    static rig(set: string): pedRigs.Rig {
        const skeleton = `${Thps3Peds.ours(set)}_skeleton`;
        return {
            type: SETS[set]!.female ? pedRigs.FEMALE.type : pedRigs.MALE.type,
            skeleton,
            anims: `animload_${Thps3Peds.ours(set)}`,
        };
    }

    // THPS3's model (a ped node's model=) as our person: profile and rig, or null (the reason in failures).
    resolve(model: string): OwnPed | null {
        const info = this.info(model);
        if (info === null) return null;
        const name = Thps3Peds.ours(info.folder);
        const rig = Thps3Peds.rig(info.set);
        if (!this.converted.has(name)) {
            try {
                const s = this.skn(info);
                const base = this.skn(this.info(SETS[info.set]!.base)!);
                if (s.bones.length !== base.bones.length)
                    throw new FormatError(`${s.bones.length} bones, its set's ${base.bones.length}`);
                const h = this.height(info.set);
                const tdx =
                    info.tdx === null
                        ? []
                        : thps3.readTdx(fs.readFileSync(path.join(this.modelsDir, info.folder, info.tdx)));
                const texFile = levels.convertTextures(tdx);
                const textures = new Set(texFile.textures.map((t) => t.checksum));
                const scene = convertModel(s, base, h, name, textures);
                for (const m of scene.materials) {
                    const t = m.passes[0]?.texture ?? 0;
                    if (t && !textures.has(t)) throw new FormatError(`texture ${hex8(t)} not in its .tdx`);
                }
                this.converted.set(name, {
                    skin: thug2Scene.writeScene(scene),
                    tex: tex.writeTex(texFile),
                    folder: info.folder,
                });
            } catch (e) {
                if (!(e instanceof Error)) throw e;
                this.failures.set(model, e.message);
                this.models.set(model.toLowerCase(), null);
                return null;
            }
        }
        const mesh = `models/${MODELS}/${info.folder}/${info.folder}.skin`;
        this.outParts.set(name, `{\n    desc_id=${name}\n    mesh="${mesh}"\n    no_random\n}`);
        this.outProfiles.set(name, `${name} = {\n    ped_body={desc_id=${name}}\n}`);
        return { profile: name, rig, set: info.set, model: info.folder };
    }

    // The animation names the set's script loads: [THUG2's name (checksum), THPS3's file].
    setAnims(set: string): [number, string][] {
        const s = SETS[set]!;
        const idle = this.animFile(set, s.idle)!;
        const out = new Map<number, string>();
        out.set(checksum("default"), idle);
        for (const d of this.pedAi) {
            const text = this.thug2Names.get(d) ?? "";
            // THPS3's animation of the same name (Ped_M_LookAtWatch: LookAtWatch), else one of the same role
            const same = this.animFile(set, text.replace(/^ped_[mf]_/i, ""));
            const role = ROLES.find(([re]) => re.test(text));
            const file = same ?? (role === undefined ? null : this.animFile(set, role[s.female ? 2 : 1]));
            out.set(d, file ?? idle);
        }
        return [...out];
    }

    // A level's load script of one set: the skeletons of its models the level has, then the set's animations under
    // its reference. [script name, NeverScript]
    setScript(set: string): [string, string] {
        const rig = Thps3Peds.rig(set);
        const name = rig.anims;
        this.skeletons.add(set);
        const lines = [`    LoadSkeleton name="skeletons/${rig.skeleton}.ske" ${rig.skeleton}`];
        lines.push(`    SetReferenceChecksum ${rig.skeleton}`);
        for (const [desc, file] of this.setAnims(set)) {
            const ours = `anims\\${ANIMS}\\${file}`;
            this.animFiles.set(ours.toLowerCase(), [set, file, ours]);
            // (a NeverScript keyword - `default` - or a name without a known text: by its checksum)
            const text = this.thug2Names.get(desc);
            const descText = qb.nameText(desc, new Map(text === undefined ? [] : [[desc, text]]));
            lines.push(`    LoadAnim name="${ours.replaceAll("\\", "\\\\")}" descChecksum=${descText}`);
        }
        lines.push("    SetReferenceChecksum 0");
        return [name, `script ${name} {\n${lines.join("\n")}\n}\n`];
    }

    // The self-check: every model of the brought sets converts (its skin read back), every animation of the sets
    // converts and reads back, and each model posed by its set's idle (start) and first walk (half way) lands where
    // THPS3 has it (poseError under 0.1 units: the keys' rounding). [lines, failures]
    selfcheck(): [string[], number] {
        const lines: string[] = [];
        let failures = 0;
        for (const [set, info] of Object.entries(SETS)) {
            // (the set's own model - animload_Pedfem_a is the set - and those calling it, not their LOD models)
            const models = [
                info.base,
                ...[...this.anims.calls]
                    .filter(([m, s]) => s === set && !/_lod\d+$/.test(m) && m !== info.base.toLowerCase())
                    .map(([m]) => m),
            ];
            const anims = (this.anims.loads.get(set) ?? []).map(([desc]) => this.animFile(set, desc));
            let animsOk = 0;
            for (const rel of anims) {
                if (rel === null) continue;
                try {
                    const a = this.readAnim(set, rel);
                    const back = skin.readAnim(convertAnim(a, this.height(set), rel), rel);
                    if (back.tracks.length !== a.rotations.length) throw new FormatError(`${rel}: tracks`);
                    animsOk += 1;
                } catch (e) {
                    if (!(e instanceof Error)) throw e;
                    failures += 1;
                    lines.push(`FAIL ${rel}: ${e.message}`);
                }
            }
            const walk = this.animFile(set, info.female ? "Walk" : "Walk1");
            const idle = this.readAnim(set, this.animFile(set, info.idle)!);
            const walking = walk === null ? null : this.readAnim(set, walk);
            let worst = 0;
            let made = 0;
            for (const m of models) {
                const own = this.resolve(m);
                if (own === null) {
                    // (THPS3's scripts name models it doesn't ship: terrorist_a's is another format)
                    lines.push(`skip ${m}: ${this.failures.get(m) ?? "?"}`);
                    continue;
                }
                const files = this.converted.get(own.profile)!;
                const scene = thug2Scene.readScene(files.skin, m);
                for (const s of scene.sectors) {
                    for (const mesh of s.meshes) {
                        const v = thug2Scene.readSkinVertices(mesh);
                        const inward = inwardTriangles(
                            mesh.lods[0] ?? [],
                            v.map((x) => x.position),
                            v.map((x) => x.normal),
                        );
                        if (inward) {
                            failures += 1;
                            lines.push(`FAIL ${m}: ${inward} triangles wound against their normals (culled in THUG2)`);
                        }
                    }
                }
                const s = this.skn(this.info(m)!);
                const h = this.height(set);
                const base = this.skn(this.info(info.base)!);
                worst = Math.max(worst, poseError(s, base, h, idle, 0));
                if (walking !== null) worst = Math.max(worst, poseError(s, base, h, walking, walking.duration / 2));
                made += 1;
            }
            if (worst > 0.1) {
                failures += 1;
                lines.push(`FAIL ${set}: a vertex ${worst.toFixed(3)} units from THPS3's pose`);
            }
            lines.push(
                `ok   ${set}: ${made} models, ${animsOk} animations, ground ${this.height(set).toFixed(2)}, ` +
                    `pose within ${worst.toFixed(3)}`,
            );
        }
        return [lines, failures];
    }

    // The models, skeletons and animations used so far into the staging folder, and its peds.json. A report line.
    write(staging: string): string {
        let bytes = 0;
        const put = (rel: string, data: Buffer): void => {
            const target = path.join(staging, rel);
            fs.mkdirSync(path.dirname(target), { recursive: true });
            fs.writeFileSync(target, data);
            bytes += data.length;
        };
        const col = thug2Col.writeCol({ objects: [], pad: 0 });
        for (const files of this.converted.values()) {
            const base = path.join("models", MODELS, files.folder, files.folder);
            put(`${base}.skin.xbx`, files.skin);
            put(`${base}.tex.xbx`, files.tex);
            put(`${base}.col.xbx`, col);
            put(`${base}.cas.xbx`, Buffer.from([2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]));
        }
        for (const set of this.skeletons) {
            const rig = Thps3Peds.rig(set);
            const base = this.skn(this.info(SETS[set]!.base)!);
            put(path.join("skeletons", `${rig.skeleton}.ske.xbx`), skeletonOf(base, this.height(set), rig.skeleton));
        }
        for (const [set, file, ours] of this.animFiles.values()) {
            put(`${ours}.xbx`, convertAnim(this.readAnim(set, file), this.height(set), file));
        }
        pedModels.writeAdditions(staging, {
            cas: { ped_body: [...this.outParts.values()] },
            profiles: [...this.outProfiles.values()],
        });
        return (
            `THPS3's people: ${this.outProfiles.size} profiles, ${this.converted.size} models, ` +
            `${this.animFiles.size} animations (${(bytes / 1048576).toFixed(1)} MB); not converted: ` +
            `${[...this.failures.keys()].join(", ") || "none"}`
        );
    }
}
