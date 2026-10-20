// NOT ORIGINAL - bake a level editor layout into a copy of the level's files (phase 6 of the editor plan).
//
//   node tools/game/apply_layout.ts <layout.txt> [--level <name>] [--game <folder>] [--out <folder>] [--dry-run]
//   node tools/game/apply_layout.ts --level <name> ...            (the layout: <game>\thug2-decomp-layouts\<scene>.txt)
//   [--install]                                                    write into <game>\thug2-decomp-data\levels\<level>
//
// A layout (src/game/front/level_editor_layout.h - tools/game/layout_file.ts reads it) is what the editor changed in a
// level, put in place by the DLL only when the level's EDITED copy loads. Baked, the changes are in the level's own
// files instead - its scene (.scn.xbx), collision (.col.xbx) and node array (.qb) - written as a new copy:
//   - read from the game folder (--game, default C:\Games\THUG2\Game; a stock install's Game folder is stepped into):
//     the level's loose files, the data overlay's (thug2-decomp-data\levels\<level>) first, else Data\levels\<level>;
//   - written to --out (default out\baked\<level> in the repo): levels\<level>\<level>.scn.xbx / .col.xbx / .qb, and
//     bake_report.txt; never into a game folder unless --install is given, which writes into --game's overlay - a
//     workspace (C:\Games\THUG2-n). The main install C:\Games\THUG2 only with --allow-main (the user's own runs:
//     agents never pass it); the backups (THUG2_PM_BAK, THUG2-THPSX-ORIGINAL) and THUGPRO's folders never.
//   - --dry-run reads and bakes in memory, writes nothing, and prints the report.
//
// What each record becomes (the DLL's meaning, src/game/front/level_editor_*.h):
//   sector / clone / rail / wall: the sector's render vertices (and normals, turned) and its collision object moved -
//     QuarterTurn(x * scale, quarters) + offset of the place as loaded; boxes and spheres made again, the collision's
//     BSP tree and reaches built again (tools/level_port/thug2_col.ts); a hidden sector left out of both files; a clone
//     is a new sector and collision object (named ed_clone_<source>_<n>); a wall's `off`: its collision object left
//     out (the render sector stays); a rail point's pos= moved (hidden: its node hidden as below, links to it cut);
//   object / copy: the object's node's pos= and angles= (its heading - the editor only turns objects about up); a copy
//     is a new node, its source's fields with its own name and place;
//   set: pos= / angles= and the Inspector's field edits (level_editor_fields.h): the node's own component - over its
//     shared structure's (ncomp_...) as an override at its end, as the DLL does;
//   node / point / order: the nodes the editor added, appended to the node array (the level's indexes stay) with the
//     fields the DLL gives them (NodeCreate::Fields; a point copies its `like` node's), crowns in the layout's order;
//   hide / drop: the node's class and type become the editor's marker ed_hidden (ed_hidden_class / ed_hidden_type keep
//     its own) - exactly what the DLL does; it isn't removed (indexes stay);
//   links / terrain: links= (names turned back into indexes) / TerrainType=;
//   lock: ignored (an editor-only aid); level (name, start, time of day, ambient, fog): not in the level's files - they
//     are its Levels.qb structure's; listed in the report to set by hand;
//   trigger (phase 5): read, but not baked yet - an editor trigger is a run-time box, overlay node and script
//     (level_editor_triggers.h); baking needs a trigger collision box, a node and a script in the level's scripts .qb.
//     Each is listed as an "unsupported record" with what it does; a level trigger it replaces (switched off by its
//     `wall` record) is kept on, so the level doesn't lose it;
//   anything else (a later record kind): "unsupported record", listed.
// THUGPRO parity: a level shared with THUGPRO (a ported level in tools/compat/thugpro's register, or THUG2's own -
// THUGPRO players have those as shipped) gets a warning for every edit that changes spawns, crowns (King of the Hill),
// flags / CTF / team nodes, gaps or geometry: "this breaks online parity".

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { checksum, Local, Name, QbFile, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import { type Element, replaceGlobalArray } from "../level_port/qb_write.ts";
import { buildTree, type ColFile, type ColObject, readCol, setReaches, writeCol } from "../level_port/thug2_col.ts";
import { readScene, type Scene, type Sector, writeScene } from "../level_port/thug2_scene.ts";
import { parseCli } from "../lib/cli.ts";
import { Float, hex8, isMain, print, pyExit, runMain } from "../lib/py.ts";
import { resolveGameFolder } from "./edit_level.ts";
import { type FieldValue, type Layout, LayoutError, type Placement, readLayout, type V3 } from "./layout_file.ts";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO = path.dirname(path.dirname(HERE));
const OVERLAY = "thug2-decomp-data";
const MARKER = "ed_hidden";

// --- geometry -------------------------------------------------------------------------------------------------------

export function quarterTurn(v: V3, quarters: number): V3 {
    switch (quarters & 3) {
        case 1:
            return { x: v.z, y: v.y, z: -v.x };
        case 2:
            return { x: -v.x, y: v.y, z: -v.z };
        case 3:
            return { x: -v.z, y: v.y, z: v.x };
        default:
            return { x: v.x, y: v.y, z: v.z };
    }
}

export function applyPlacement(p: Placement, v: V3): V3 {
    const t = quarterTurn({ x: v.x * p.scale, y: v.y * p.scale, z: v.z * p.scale }, p.quarters);
    return { x: t.x + p.offset.x, y: t.y + p.offset.y, z: t.z + p.offset.z };
}

function isIdentity(p: Placement): boolean {
    return (p.quarters & 3) === 0 && p.scale === 1 && p.offset.x === 0 && p.offset.y === 0 && p.offset.z === 0;
}

const FVF_XYZ = 0x002;
const FVF_NORMAL = 0x010;

function bounds(points: V3[]): { min: V3; max: V3; centre: V3; radius: number } {
    const min = { x: Infinity, y: Infinity, z: Infinity };
    const max = { x: -Infinity, y: -Infinity, z: -Infinity };
    for (const p of points) {
        min.x = Math.min(min.x, p.x);
        min.y = Math.min(min.y, p.y);
        min.z = Math.min(min.z, p.z);
        max.x = Math.max(max.x, p.x);
        max.y = Math.max(max.y, p.y);
        max.z = Math.max(max.z, p.z);
    }
    const centre = { x: (min.x + max.x) / 2, y: (min.y + max.y) / 2, z: (min.z + max.z) / 2 };
    let radius = 0;
    for (const p of points) radius = Math.max(radius, Math.hypot(p.x - centre.x, p.y - centre.y, p.z - centre.z));
    return { min, max, centre, radius };
}

// A sector's render meshes moved: every vertex buffer of the mesh's size (positions; normals turned), the meshes' and
// the sector's boxes and spheres made again.
export function placeSector(sector: Sector, p: Placement): void {
    const all: V3[] = [];
    for (const mesh of sector.meshes) {
        const fvf = mesh.vertexShader[0] ?? 0;
        const stride = mesh.stride;
        const points: V3[] = [];
        mesh.buffers = mesh.buffers.map(([flag, data]) => {
            const copy = Buffer.from(data);
            if (!(fvf & FVF_XYZ) || stride < 12 || copy.length < stride * mesh.vertexCount) return [flag, copy];
            for (let v = 0; v < mesh.vertexCount; v++) {
                const at = v * stride;
                const q = applyPlacement(p, {
                    x: copy.readFloatLE(at),
                    y: copy.readFloatLE(at + 4),
                    z: copy.readFloatLE(at + 8),
                });
                copy.writeFloatLE(q.x, at);
                copy.writeFloatLE(q.y, at + 4);
                copy.writeFloatLE(q.z, at + 8);
                points.push(q);
                if (fvf & FVF_NORMAL && stride >= 24) {
                    const n = quarterTurn(
                        { x: copy.readFloatLE(at + 12), y: copy.readFloatLE(at + 16), z: copy.readFloatLE(at + 20) },
                        p.quarters,
                    );
                    copy.writeFloatLE(n.x, at + 12);
                    copy.writeFloatLE(n.y, at + 16);
                    copy.writeFloatLE(n.z, at + 20);
                }
            }
            return [flag, copy];
        });
        if (points.length) {
            const b = bounds(points);
            mesh.bbox = [b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z];
            mesh.sphere = [b.centre.x, b.centre.y, b.centre.z, b.radius];
            all.push(...points);
        }
    }
    if (all.length) {
        const b = bounds(all);
        sector.bbox = [b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z];
        sector.sphere = [b.centre.x, b.centre.y, b.centre.z, b.radius];
    } else {
        // (no vertices: its box moved by its corners)
        const corners = [0, 1, 2, 3, 4, 5, 6, 7].map((i) =>
            applyPlacement(p, {
                x: sector.bbox[i & 1 ? 3 : 0]!,
                y: sector.bbox[i & 2 ? 4 : 1]!,
                z: sector.bbox[i & 4 ? 5 : 2]!,
            }),
        );
        const b = bounds(corners);
        sector.bbox = [b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z];
        const c = applyPlacement(p, { x: sector.sphere[0]!, y: sector.sphere[1]!, z: sector.sphere[2]! });
        sector.sphere = [c.x, c.y, c.z, (sector.sphere[3] ?? 0) * p.scale];
    }
}

// A collision object moved: its vertices, box, tree and reaches made again (fixed-point vertices kept while the box
// still fits 16-bit sixteenths).
export function placeCollision(o: ColObject, p: Placement): void {
    o.verts = o.verts.map((v) => {
        const q = applyPlacement(p, { x: v[0]!, y: v[1]!, z: v[2]! });
        return [q.x, q.y, q.z];
    });
    if (!o.verts.length) return;
    const b = bounds(o.verts.map((v) => ({ x: v[0]!, y: v[1]!, z: v[2]! })));
    o.bbox = [b.min.x, b.min.y, b.min.z, 1, b.max.x, b.max.y, b.max.z, 1];
    if (o.fixedVerts && Math.max(b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z) * 16 > 65535)
        o.fixedVerts = 0;
    o.bsp = buildTree(o.verts, o.faces);
    setReaches(o);
}

function cloneSector(s: Sector, name: number): Sector {
    return {
        ...s,
        checksum: name,
        bbox: [...s.bbox],
        sphere: [...s.sphere],
        meshes: s.meshes.map((m) => ({
            ...m,
            buffers: m.buffers.map(([f, d]) => [f, Buffer.from(d)] as [number, Buffer]),
        })),
    };
}

function cloneCollision(o: ColObject, name: number): ColObject {
    return {
        ...o,
        checksum: name,
        bbox: [...o.bbox],
        verts: o.verts.map((v) => [...v]),
        faces: o.faces.map((f) => [...f] as typeof f),
        intensities: Buffer.from(o.intensities),
        reach: Buffer.from(o.reach),
    };
}

// --- shared collision (src/game/front/level_editor_shared_coll.h, ported) ---------------------------------------------
//
// A piece's collision isn't always its own object: collision-only objects (no render sector of their name) often hold
// the faces of pieces nearby. When the editor moves or hides a piece it takes the faces that belong to it into a
// collision object of its own (a companion) and switches the originals off (face flag 0x10). A face belongs when it
// is collidable and not a trigger (0x50 clear), its object is on (no flag 2 / 4) and all three corners are in the
// piece's box grown by 6 - and either its object is named after the piece (<name>_col / _ic / _col01 / _ic01: then
// within 100 of the box), or every corner is within 6 of the piece's triangles and the piece is strictly the nearest
// of the other solid pieces whose boxes hold it. At most 2048 faces. See-through pieces (every mesh's material's first
// pass transparent - the engine's material flag 0x40) take nothing and don't compete.

export interface FaceCopy {
    v: V3[];
    flags: number;
    terrain: number;
    bytes: number[];
}

const NEAR = 6;
const MOST_FACES = 2048;
const SUFFIXES = ["_col", "_ic", "_col01", "_ic01"];

function sub(a: V3, b: V3): V3 {
    return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z };
}
function dot(a: V3, b: V3): number {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
function cross(a: V3, b: V3): V3 {
    return { x: a.y * b.z - a.z * b.y, y: a.z * b.x - a.x * b.z, z: a.x * b.y - a.y * b.x };
}
function segmentDistance(p: V3, a: V3, b: V3): number {
    const ab = sub(b, a);
    const len = dot(ab, ab);
    const t = len > 0 ? Math.max(0, Math.min(1, dot(sub(p, a), ab) / len)) : 0;
    const q = { x: a.x + ab.x * t, y: a.y + ab.y * t, z: a.z + ab.z * t };
    return Math.sqrt(dot(sub(p, q), sub(p, q)));
}
export function pointTriangleDistance(p: V3, a: V3, b: V3, c: V3): number {
    const ab = sub(b, a);
    const ac = sub(c, a);
    const n = cross(ab, ac);
    const n2 = dot(n, n);
    const scale = Math.max(dot(ab, ab), dot(ac, ac), dot(sub(c, b), sub(c, b)));
    if (!(n2 > scale * scale * 1e-12))
        return Math.min(segmentDistance(p, a, b), segmentDistance(p, b, c), segmentDistance(p, a, c));
    const inside =
        dot(cross(ab, sub(p, a)), n) >= 0 &&
        dot(cross(sub(c, b), sub(p, b)), n) >= 0 &&
        dot(cross(sub(a, c), sub(p, c)), n) >= 0;
    if (inside) return Math.abs(dot(sub(p, a), n)) / Math.sqrt(n2);
    return Math.min(segmentDistance(p, a, b), segmentDistance(p, b, c), segmentDistance(p, c, a));
}

interface Piece {
    checksum: number;
    min: V3;
    max: V3;
    triangles: [V3, V3, V3][];
    seeThrough: boolean;
}

function inside(p: V3, min: V3, max: V3): boolean {
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
}

export class SharedCollision {
    readonly pieces = new Map<number, Piece>();
    readonly sources: ColObject[] = [];
    readonly namedObjects = new Map<number, number>(); // collision-only object -> the piece it's named after
    private readonly names: ReadonlyMap<number, string>;

    constructor(scene: Scene, col: ColFile, names: ReadonlyMap<number, string>) {
        this.names = names;
        const render = new Set(scene.sectors.map((s) => s.checksum));
        for (const o of col.objects)
            if (!render.has(o.checksum) && !(o.flags & 6) && o.faces.length) this.sources.push(o);
        const transparent = new Set<number>();
        for (const m of scene.materials) if ((m.passes[0]?.flags ?? 0) & 0x40) transparent.add(m.checksum);
        for (const s of scene.sectors) {
            const triangles: [V3, V3, V3][] = [];
            let seeThrough = s.meshes.length > 0;
            for (const mesh of s.meshes) {
                if (!transparent.has(mesh.material)) seeThrough = false;
                const data = mesh.buffers[0]?.[1];
                const indices = mesh.lods[0] ?? [];
                if (!data || mesh.flags & 0x10 || mesh.stride < 12) continue;
                const at = (i: number): V3 => ({
                    x: data.readFloatLE(i * mesh.stride),
                    y: data.readFloatLE(i * mesh.stride + 4),
                    z: data.readFloatLE(i * mesh.stride + 8),
                });
                for (let i = 2; i < indices.length; i++) {
                    const [a, b, c] = [indices[i - 2]!, indices[i - 1]!, indices[i]!];
                    if (a === b || b === c || a === c || Math.max(a, b, c) >= mesh.vertexCount) continue;
                    triangles.push([at(a), at(b), at(c)]);
                }
            }
            if (!s.meshes.length || s.flags & 0x00800000) continue; // (billboards left out, as picking does)
            this.pieces.set(s.checksum, {
                checksum: s.checksum,
                min: { x: s.bbox[0]! - NEAR, y: s.bbox[1]! - NEAR, z: s.bbox[2]! - NEAR },
                max: { x: s.bbox[3]! + NEAR, y: s.bbox[4]! + NEAR, z: s.bbox[5]! + NEAR },
                triangles,
                seeThrough,
            });
        }
        const sourceNames = new Set(this.sources.map((o) => o.checksum));
        for (const piece of this.pieces.keys())
            for (const n of this.namedFor(piece, sourceNames)) this.namedObjects.set(n, piece);
    }

    private namedFor(piece: number, sourceNames: ReadonlySet<number>): number[] {
        const name = this.names.get(piece);
        if (!name || checksum(name) !== piece) return [];
        return SUFFIXES.map((s) => checksum(name + s)).filter((c) => sourceNames.has(c));
    }

    private distance(piece: Piece, p: V3, best: number): number {
        for (const [a, b, c] of piece.triangles) {
            const dx = Math.max(Math.min(a.x, b.x, c.x) - p.x, p.x - Math.max(a.x, b.x, c.x), 0);
            const dy = Math.max(Math.min(a.y, b.y, c.y) - p.y, p.y - Math.max(a.y, b.y, c.y), 0);
            const dz = Math.max(Math.min(a.z, b.z, c.z) - p.z, p.z - Math.max(a.z, b.z, c.z), 0);
            if (dx * dx + dy * dy + dz * dz >= best * best) continue;
            best = Math.min(best, pointTriangleDistance(p, a, b, c));
        }
        return best;
    }

    private faceDistance(piece: Piece, v: V3[], limit: number): number {
        let worst = 0;
        const over = limit + 1e-3;
        for (const p of v) {
            const d = this.distance(piece, p, over);
            if (d > limit) return over;
            worst = Math.max(worst, d);
        }
        return worst;
    }

    // The faces that belong to `piece` (as copies); `take`: the originals switched off (flag 0x10).
    gather(pieceName: number, take: boolean): FaceCopy[] {
        const piece = this.pieces.get(pieceName);
        const out: FaceCopy[] = [];
        if (!piece || piece.seeThrough) return out;
        const sourceNames = new Set(this.sources.map((o) => o.checksum));
        const mine = new Set(this.namedFor(pieceName, sourceNames));
        const others = [...this.pieces.values()].filter(
            (o) =>
                o.checksum !== pieceName &&
                !o.seeThrough &&
                !(o.min.x > piece.max.x || o.max.x < piece.min.x || o.min.y > piece.max.y || o.max.y < piece.min.y) &&
                !(o.min.z > piece.max.z || o.max.z < piece.min.z),
        );
        const grow = (d: number): [V3, V3] => [
            { x: piece.min.x - d, y: piece.min.y - d, z: piece.min.z - d },
            { x: piece.max.x + d, y: piece.max.y + d, z: piece.max.z + d },
        ];
        const [namedMin, namedMax] = grow(100);
        for (const o of this.sources) {
            const named = mine.has(o.checksum);
            if (!named && this.namedObjects.has(o.checksum)) continue;
            o.faces.forEach((face, f) => {
                if (out.length >= MOST_FACES || face[0] & 0x50) return;
                const v = [face[2], face[3], face[4]].map((i) => ({
                    x: o.verts[i]![0]!,
                    y: o.verts[i]![1]!,
                    z: o.verts[i]![2]!,
                }));
                if (named) {
                    if (!v.every((p) => inside(p, namedMin, namedMax))) return;
                } else {
                    if (!v.every((p) => inside(p, piece.min, piece.max))) return;
                    const own = this.faceDistance(piece, v, NEAR);
                    if (!(own <= NEAR)) return;
                    let nearest = Infinity;
                    for (const other of others) {
                        if (!v.every((p) => inside(p, other.min, other.max))) continue;
                        nearest = Math.min(nearest, this.faceDistance(other, v, own));
                        if (!(own < nearest)) return;
                    }
                }
                out.push({
                    v,
                    flags: face[0],
                    terrain: face[1],
                    bytes: [face[2], face[3], face[4]].map((i) => o.intensities[i] ?? 0x80),
                });
                if (take) o.faces[f] = [face[0] | 0x10, face[1], face[2], face[3], face[4]];
            });
        }
        return out;
    }
}

// A companion collision object: the faces (corners shared where equal), placed.
export function companionObject(name: number, faces: readonly FaceCopy[], p: Placement): ColObject {
    const verts: number[][] = [];
    const bytes: number[] = [];
    const out: [number, number, number, number, number][] = [];
    for (const face of faces) {
        const idx = face.v.map((v, k) => {
            const q = applyPlacement(p, v);
            let i = verts.findIndex(
                (w, j) => w[0] === q.x && w[1] === q.y && w[2] === q.z && bytes[j] === face.bytes[k],
            );
            if (i < 0) {
                verts.push([q.x, q.y, q.z]);
                bytes.push(face.bytes[k]!);
                i = verts.length - 1;
            }
            return i;
        });
        out.push([face.flags, face.terrain, idx[0]!, idx[1]!, idx[2]!]);
    }
    const b = bounds(verts.map((v) => ({ x: v[0]!, y: v[1]!, z: v[2]! })));
    const o: ColObject = {
        checksum: name,
        flags: 0,
        smallFaces: verts.length < 256 ? 1 : 0,
        fixedVerts: 0,
        bbox: [b.min.x, b.min.y, b.min.z, 1, b.max.x, b.max.y, b.max.z, 1],
        verts,
        intensities: Buffer.from(bytes),
        faces: out,
        reach: Buffer.alloc(0),
        bsp: { kind: "leaf", faces: [], low: 3 },
    };
    o.bsp = buildTree(o.verts, o.faces);
    setReaches(o);
    return o;
}

// An object's rows (right, up, at) as node angles: its heading about up (the editor turns objects only about up -
// angles (0, a, 0) face (sin a, 0, cos a), level_editor_node_edit.h); false when it's tilted.
export function anglesOfRows(rows: readonly number[]): { angles: V3; tilted: boolean } {
    const up = { x: rows[3] ?? 0, y: rows[4] ?? 1, z: rows[5] ?? 0 };
    const at = { x: rows[6] ?? 0, z: rows[8] ?? 1 };
    const tilted = Math.abs(up.y - 1) > 1e-3 || Math.abs(up.x) > 1e-3 || Math.abs(up.z) > 1e-3;
    return { angles: { x: 0, y: Math.atan2(at.x, at.z), z: 0 }, tilted };
}

// --- nodes ----------------------------------------------------------------------------------------------------------

const K = {
    name: checksum("Name"),
    cls: checksum("Class"),
    type: checksum("Type"),
    pos: checksum("Pos"),
    angles: checksum("Angles"),
    links: checksum("Links"),
    terrain: checksum("TerrainType"),
    cluster: checksum("Cluster"),
    trickObject: checksum("TrickObject"),
    createdAtStart: checksum("CreatedAtStart"),
    hiddenClass: checksum("ed_hidden_class"),
    hiddenType: checksum("ed_hidden_type"),
    marker: checksum(MARKER),
};

function getName(s: Struct, key: number): number | null {
    const v = s.get(key);
    return v instanceof Name ? v.value : null;
}

function v3(v: V3): Vector {
    return new Vector("vector", [v.x, v.y, v.z]);
}

export function qbValueOf(v: FieldValue): QbValue {
    switch (v.kind) {
        case "integer":
            return v.integer ?? 0;
        case "float":
            return new Float(v.numbers?.[0] ?? 0);
        case "name":
            return new Name(v.checksum ?? 0);
        case "string":
            return v.text ?? "";
        case "local":
            return new Local(v.text ?? "");
        case "vector":
            return new Vector("vector", v.numbers ?? [0, 0, 0]);
        case "pair":
            return new Vector("pair", v.numbers ?? [0, 0]);
        case "names":
            return (v.names ?? []).map((n) => new Name(n));
        case "integers":
            return [...(v.names ?? [])];
        case "floats":
            return (v.numbers ?? []).map((f) => new Float(f));
        case "flag":
            return new Name(0); // (flags aren't values: setField handles them)
    }
}

// The level's node array and the file's other globals (the shared structures the nodes include).
export class NodeArray {
    readonly nodes: Struct[];
    readonly level: readonly Struct[]; // (the nodes as the level has them: a point copies its `like` node from these)
    readonly original: number; // (how many the level had)
    readonly changed = new Set<number>();
    readonly byName = new Map<number, number>();
    readonly includes: Map<number, Struct>;
    // links= set by name (index -> names): turned back into indexes at the end (appended nodes have indexes then).
    readonly linkNames = new Map<number, number[]>();
    readonly names = new Map<number, string>(); // (new names for the file's name table)

    constructor(nodes: QbValue[], globals: readonly [Name, QbValue][]) {
        this.nodes = nodes.map((n) => (n instanceof Struct ? n : new Struct()));
        this.level = [...this.nodes];
        this.original = this.nodes.length;
        this.includes = new Map();
        for (const [key, value] of globals) if (value instanceof Struct) this.includes.set(key.value, value);
        this.nodes.forEach((n, i) => {
            const name = getName(n, K.name);
            if (name !== null && !this.byName.has(name)) this.byName.set(name, i);
        });
    }

    index(name: number): number {
        return this.byName.get(name) ?? -1;
    }

    // Node `i`, copied the first time it changes (the file's other nodes keep their bytes).
    edit(i: number): Struct {
        if (!this.changed.has(i) && i < this.original) {
            this.nodes[i] = new Struct(this.nodes[i]!.entries.map(([k, v]) => [k, v] as [Name | null, QbValue]));
            this.changed.add(i);
        }
        return this.nodes[i]!;
    }

    append(node: Struct): number {
        this.nodes.push(node);
        const name = getName(node, K.name);
        if (name !== null && !this.byName.has(name)) this.byName.set(name, this.nodes.length - 1);
        return this.nodes.length - 1;
    }

    isInclude(entry: [Name | null, QbValue]): Struct | null {
        if (entry[0] !== null || !(entry[1] instanceof Name)) return null;
        return this.includes.get(entry[1].value) ?? null;
    }

    remember(text: string | null | undefined): void {
        if (text && /^[A-Za-z_][A-Za-z0-9_]*$/.test(text)) this.names.set(checksum(text), text);
    }
}

function matches(array: NodeArray, entry: [Name | null, QbValue], key: number, flag: boolean): boolean {
    if (flag) return entry[0] === null && entry[1] instanceof Name && entry[1].value === key && !array.isInclude(entry);
    return entry[0] !== null && entry[0].value === key;
}

function includeHas(s: Struct, key: number, flag: boolean): boolean {
    return s.entries.some(([k, v]) => (flag ? k === null && v instanceof Name && v.value === key : k?.value === key));
}

// The node's own field `key` set (or removed), as the DLL's Fields::Set does: in its place, unless a shared structure
// after it has the key (then at the end, where it counts); a flag added / taken out. A shared structure's flag can't
// be switched off for one node: false.
export function setField(array: NodeArray, node: Struct, key: number, value: FieldValue | null): boolean {
    const flag =
        value?.kind === "flag" ||
        (value === null && node.entries.some((e) => matches(array, e, key, true))) ||
        (value === null &&
            !node.entries.some((e) => matches(array, e, key, false)) &&
            node.entries.some((e) => {
                const inc = array.isInclude(e);
                return inc !== null && includeHas(inc, key, true);
            }));
    let own = -1;
    node.entries.forEach((e, i) => {
        if (matches(array, e, key, flag)) own = i;
    });
    const includedAfter = (from: number): boolean =>
        node.entries.some((e, i) => {
            const inc = i > from ? array.isInclude(e) : null;
            return inc !== null && includeHas(inc, key, flag);
        });
    if (flag) {
        const on = value?.kind === "flag" ? value.on === true : false;
        if (!on && own < 0 && includedAfter(-1)) return false;
        if (on && own >= 0) return true;
        node.entries = node.entries.filter((e) => !matches(array, e, key, true));
        if (on) node.entries.push([null, new Name(key)]);
        return true;
    }
    const keepPlace = own >= 0 && !includedAfter(own);
    const others = node.entries.slice(0, Math.max(own, 0)).filter((e) => !matches(array, e, key, false)).length;
    node.entries = node.entries.filter((e) => !matches(array, e, key, false));
    if (value === null) return true;
    const entry: [Name, QbValue] = [new Name(key), qbValueOf(value)];
    if (keepPlace) node.entries.splice(others, 0, entry);
    else node.entries.push(entry);
    return true;
}

function setVector(node: Struct, key: number, v: V3): void {
    const at = node.entries.findIndex(([k]) => k?.value === key);
    if (at >= 0) node.entries[at] = [node.entries[at]![0], v3(v)];
    else node.entries.push([new Name(key), v3(v)]);
}

function readVector(node: Struct, key: number): V3 | null {
    const v = node.get(key);
    return v instanceof Vector && v.kind === "vector" ? { x: v.values[0]!, y: v.values[1]!, z: v.values[2]! } : null;
}

// The editor's marker (NodeCreate::Hide): class and type ed_hidden, its own kept beside.
function hideNode(array: NodeArray, i: number): void {
    const node = array.edit(i);
    const cls = getName(node, K.cls);
    if (cls === K.marker) return;
    const type = getName(node, K.type) ?? 0;
    const set = (key: number, value: number): void => {
        node.entries = node.entries.filter(([k]) => k?.value !== key);
        node.entries.push([new Name(key), new Name(value)]);
    };
    set(K.hiddenClass, cls ?? 0);
    set(K.hiddenType, type);
    set(K.cls, K.marker);
    set(K.type, K.marker);
    array.names.set(K.marker, MARKER);
    array.names.set(K.hiddenClass, "ed_hidden_class");
    array.names.set(K.hiddenType, "ed_hidden_type");
}

function linksOf(array: NodeArray, i: number): number[] {
    const set = array.linkNames.get(i);
    if (set) return set;
    const v = array.nodes[i]!.get(K.links);
    if (!Array.isArray(v)) return [];
    return v.map((x) => {
        const index = typeof x === "number" ? x : -1;
        return getName(array.nodes[index] ?? new Struct(), K.name) ?? 0;
    });
}

// --- the added nodes (level_editor_node_create.cpp's Fields / Build) ------------------------------------------------

const TEAMS = ["Red", "Blue", "Green", "Yellow"];
function teamOf(text: string): string | null {
    const t = text.endsWith("_Base") ? text.slice(0, -5) : text;
    return TEAMS.find((c) => t.endsWith(`_${c}`)) ?? null;
}
function same(a: string, b: string): boolean {
    return a.toLowerCase() === b.toLowerCase();
}

export function addedNode(array: NodeArray, r: Layout["nodes"][number]): Struct {
    const s = new Struct();
    const name = (key: string, text: string): void => {
        s.entries.push([new Name(checksum(key)), new Name(checksum(text))]);
        array.remember(key);
        array.remember(text);
    };
    const flag = (text: string): void => {
        s.entries.push([null, new Name(checksum(text))]);
        array.remember(text);
    };
    s.entries.push([new Name(K.pos), v3(r.pos)], [new Name(K.angles), v3(r.angles)]);
    name("Name", r.name);
    name("Class", r.cls);
    if (r.type) name("Type", r.type);
    const team = teamOf(same(r.cls, "GameObject") ? r.type : r.name);
    if (same(r.cls, "Restart")) {
        flag("CreatedAtStart");
        s.entries.push([
            new Name(checksum("RestartName")),
            r.name.startsWith("TRG_CTF_Restart_") ? `Team: CTF ${team ?? ""}` : "Multi: Restart",
        ]);
        s.entries.push([new Name(checksum("restart_types")), r.types.map((t) => new Name(checksum(t)))]);
        for (const t of r.types) array.remember(t);
        array.remember("RestartName");
        array.remember("restart_types");
    } else if (same(r.cls, "GenericNode") && same(r.type, "Crown")) {
        flag("CreatedAtStart");
    } else if (same(r.cls, "GameObject") && r.type.startsWith("Flag_") && team) {
        const base = r.type.endsWith("_Base");
        const model = `flag_${team.toLowerCase()}${base ? "_base" : ""}`;
        flag("NeverSuspend");
        s.entries.push([new Name(checksum("Model")), `gameobjects\\flags\\${model}\\${model}.mdl`]);
        s.entries.push([new Name(checksum("lod_dist1")), 800], [new Name(checksum("lod_dist2")), 801]);
        if (!base) name("TriggerScript", `TRG_CTF_${team}Script`);
    } else if (same(r.cls, "LevelLight")) {
        flag("CreatedAtStart");
        s.entries.push(
            [new Name(checksum("Brightness")), new Float(r.brightness)],
            [new Name(checksum("InnerRadius")), new Float(r.inner)],
            [new Name(checksum("OuterRadius")), new Float(r.outer)],
            [new Name(checksum("Color")), [...r.colour]],
        );
    } else if (same(r.cls, "EmitterObject") || same(r.cls, "ProximNode")) {
        flag("CreatedAtStart");
        name("Shape", "Sphere");
        s.entries.push([new Name(checksum("Radius")), new Float(r.radius)]);
    }
    return s;
}

export function addedPoint(array: NodeArray, p: Layout["points"][number]): Struct {
    const s = new Struct();
    const likeIndex = p.like ? array.index(p.like) : -1;
    if (likeIndex >= 0) {
        const drop = new Set([K.name, K.pos, K.angles, K.links, K.cluster, K.hiddenClass, K.hiddenType]);
        for (const [k, v] of (array.level[likeIndex] ?? array.nodes[likeIndex]!).entries) {
            if (k && drop.has(k.value)) continue;
            if (!k && v instanceof Name && v.value === K.trickObject) continue;
            s.entries.push([k, v]);
        }
    }
    const put = (key: number, value: QbValue): void => {
        s.entries = s.entries.filter(([k]) => k?.value !== key);
        s.entries.push([new Name(key), value]);
    };
    put(K.pos, v3(p.pos));
    put(K.angles, v3({ x: 0, y: 0, z: 0 }));
    put(K.name, new Name(checksum(p.name)));
    put(K.cls, new Name(checksum(p.cls)));
    array.remember(p.name);
    array.remember(p.cls);
    if (!s.entries.some(([k, v]) => k === null && v instanceof Name && v.value === K.createdAtStart))
        s.entries.push([null, new Name(K.createdAtStart)]);
    if (p.cls === "RailNode" && !(likeIndex >= 0 && !p.terrain)) {
        const terrain = p.terrain || "TERRAIN_GRINDMETAL";
        put(K.terrain, new Name(checksum(terrain)));
        array.remember(terrain);
    }
    return s;
}

// The crowns among the added nodes put in `order` in the slots crowns hold (NodeCreate::ApplyCrownOrder).
export function applyCrownOrder<T extends { name: string; cls: string; type: string }>(
    list: T[],
    order: readonly string[],
): T[] {
    const isCrown = (n: T): boolean => same(n.cls, "GenericNode") && same(n.type, "Crown");
    const slots = list.map((n, i) => (isCrown(n) ? i : -1)).filter((i) => i >= 0);
    const crowns = slots.map((i) => list[i]!);
    const ordered: T[] = [];
    for (const name of order) {
        const c = crowns.find((n) => same(n.name, name) && !ordered.includes(n));
        if (c) ordered.push(c);
    }
    for (const c of crowns) if (!ordered.includes(c)) ordered.push(c);
    const out = [...list];
    slots.forEach((slot, k) => {
        out[slot] = ordered[k]!;
    });
    return out;
}

// --- THUGPRO parity ---------------------------------------------------------------------------------------------------

export interface Shared {
    level: string;
    why: string; // "a ported THPS3 level in THUGPRO's register" / "THUG2's own level"
}

// The ported levels the THUGPRO register names (tools/compat/thugpro/<game>_levels.json: every section's level keys).
export function registerLevels(folder: string = path.join(REPO, "tools", "compat", "thugpro")): Map<string, string> {
    const out = new Map<string, string>();
    if (!fs.existsSync(folder)) return out;
    for (const file of fs.readdirSync(folder)) {
        const m = /^(\w+)_levels\.json$/.exec(file);
        if (!m) continue;
        const data = JSON.parse(fs.readFileSync(path.join(folder, file), "utf8")) as Record<string, unknown>;
        for (const [section, value] of Object.entries(data)) {
            if (section.startsWith("_") || section === "thugpro_new" || section === "port_changes") continue;
            if (!value || typeof value !== "object" || Array.isArray(value)) continue;
            for (const level of Object.keys(value)) {
                if (!level.startsWith("_")) out.set(level.toLowerCase(), m[1]!.toUpperCase());
            }
        }
    }
    return out;
}

function nodeKind(node: Struct | undefined): string | null {
    if (!node) return null;
    const cls = getName(node, K.cls) === K.marker ? getName(node, K.hiddenClass) : getName(node, K.cls);
    const type = getName(node, K.cls) === K.marker ? getName(node, K.hiddenType) : getName(node, K.type);
    const name = getName(node, K.name);
    if (cls === checksum("Restart")) return "a spawn (restart)";
    if (cls === checksum("GenericNode") && type === checksum("Crown")) return "a crown (King of the Hill)";
    if (cls === checksum("GameObject") && type !== null) {
        for (const t of TEAMS) {
            if (type === checksum(`Flag_${t}`) || type === checksum(`Flag_${t}_Base`)) return "a CTF / team flag";
        }
    }
    for (const t of TEAMS)
        if (name === checksum(`TRG_CTF_${t}`) || name === checksum(`TRG_CTF_Restart_${t}`)) return "a CTF node";
    if (cls === checksum("RailNode") || cls === checksum("ClimbingNode")) return "a rail (geometry)";
    return null;
}

// --- the bake ---------------------------------------------------------------------------------------------------------

export interface LevelFiles {
    level: string;
    scene: Scene;
    col: ColFile | null;
    qb: Buffer;
}
export interface Baked {
    scene: Scene;
    col: ColFile | null;
    qb: Buffer;
    report: string[];
    warnings: string[];
    parity: string[];
    unsupported: string[];
}

export function nodeArrayName(file: QbFile, level: string): number {
    const wanted = checksum(`${level}_NodeArray`);
    if (file.globals.some(([k, v]) => k.value === wanted && Array.isArray(v))) return wanted;
    // (else the biggest top-level array of structures)
    let best: [number, number] = [0, -1];
    for (const [k, v] of file.globals) {
        if (Array.isArray(v) && v.length > best[1] && v.every((x) => x instanceof Struct)) best = [k.value, v.length];
    }
    return best[0];
}

export function bake(layout: Layout, files: LevelFiles, shared: Shared | null): Baked {
    const report: string[] = [];
    const warnings: string[] = [];
    const parity: string[] = [];
    const unsupported: string[] = [];
    const scene = files.scene;
    const col = files.col;
    const qbFile = new QbFile(files.qb, `${files.level}.qb`, true);
    const arrayName = nodeArrayName(qbFile, files.level);
    const nodesValue = qbFile.globals.find(([k]) => k.value === arrayName)?.[1];
    if (!Array.isArray(nodesValue)) throw new Error(`${files.level}.qb: no node array`);
    const array = new NodeArray(nodesValue, qbFile.globals);
    const label = (c: number): string => qbFile.names.get(c) ?? hex8(c);
    const breaks = (what: string): void => {
        if (shared) parity.push(`${what} - this breaks online parity`);
    };
    const nodeAt = (name: number, what: string): number => {
        const i = array.index(name);
        if (i < 0) warnings.push(`${what}: no node ${label(name)} in ${files.level}.qb - skipped`);
        return i;
    };

    // Scenery: sectors, clones, collision.
    const sectorAt = new Map(scene.sectors.map((s, i) => [s.checksum, i]));
    const colAt = new Map((col?.objects ?? []).map((o, i) => [o.checksum, i]));
    const dropSectors = new Set<number>();
    const dropCollision = new Set<number>();
    const loaded = new Map<number, Sector>(); // (the sources of clones as loaded)
    const loadedCol = new Map<number, ColObject>();
    for (const c of layout.clones) {
        const s = sectorAt.get(c.source);
        if (s !== undefined && !loaded.has(c.source)) loaded.set(c.source, cloneSector(scene.sectors[s]!, c.source));
        const o = colAt.get(c.source);
        if (o !== undefined && col && !loadedCol.has(c.source))
            loadedCol.set(c.source, cloneCollision(col.objects[o]!, c.source));
    }
    // Shared collision (level_editor_shared_coll.h): a moved / hidden piece takes the faces of collision-only objects
    // that belong to it - gathered on the level as loaded, in the records' order (a face taken is off for the next).
    const sharedColl = col ? new SharedCollision(scene, col, qbFile.names) : null;
    const companions: { piece: number; faces: FaceCopy[]; placement: Placement | null }[] = [];
    if (sharedColl) {
        for (const r of layout.sectors) {
            if (!r.hidden && isIdentity(r.placement)) continue;
            const taken = sharedColl.gather(r.checksum, true);
            if (taken.length)
                companions.push({ piece: r.checksum, faces: taken, placement: r.hidden ? null : r.placement });
        }
        for (const c of layout.clones) {
            const mine = companions.find((x) => x.piece === c.source);
            const faces = mine ? mine.faces : sharedColl.gather(c.source, false);
            if (faces.length)
                companions.push({
                    piece: c.source,
                    faces,
                    placement: { ...c.placement, offset: { ...c.placement.offset } },
                });
        }
    }
    let moved = 0;
    for (const r of layout.sectors) {
        const s = sectorAt.get(r.checksum);
        const o = colAt.get(r.checksum);
        if (s === undefined && o === undefined) {
            warnings.push(`sector ${label(r.checksum)}: not in the level's scene or collision - skipped`);
            continue;
        }
        if (r.hidden) {
            dropSectors.add(r.checksum);
            if (array.index(r.checksum) >= 0) {
                warnings.push(
                    `scenery ${label(r.checksum)} hidden: left out of the files - its node stays (a script that ` +
                        "creates / kills it finds no sector)",
                );
            }
            dropCollision.add(r.checksum);
            breaks(`scenery ${label(r.checksum)} hidden (left out of the scene and collision)`);
            continue;
        }
        if (isIdentity(r.placement)) continue;
        if (s !== undefined) placeSector(scene.sectors[s]!, r.placement);
        if (o !== undefined && col) placeCollision(col.objects[o]!, r.placement);
        moved++;
        breaks(`scenery ${label(r.checksum)} moved / turned / scaled`);
    }
    let cloneCount = 0;
    for (const c of layout.clones) {
        const source = loaded.get(c.source);
        const sourceCol = loadedCol.get(c.source);
        if (!source && !sourceCol) {
            warnings.push(`clone of ${label(c.source)}: no such sector - skipped`);
            continue;
        }
        let n = cloneCount + 1;
        let name = checksum(`ed_clone_${hex8(c.source)}_${n}`);
        while (sectorAt.has(name) || colAt.has(name)) name = checksum(`ed_clone_${hex8(c.source)}_${++n}`);
        cloneCount = n;
        if (source) {
            const copy = cloneSector(source, name);
            placeSector(copy, c.placement);
            scene.sectors.push(copy);
            sectorAt.set(name, scene.sectors.length - 1);
        }
        if (sourceCol && col) {
            const copy = cloneCollision(sourceCol, name);
            placeCollision(copy, c.placement);
            col.objects.push(copy);
            colAt.set(name, col.objects.length - 1);
        }
        array.names.set(name, `ed_clone_${hex8(c.source)}_${n}`);
        breaks(`a copy of scenery ${label(c.source)} added`);
    }
    if (col) {
        let faces = 0;
        companions.forEach((c, k) => {
            if (!c.placement) return;
            const name = `ed_coll_${hex8(c.piece)}_${k + 1}`;
            col.objects.push(companionObject(checksum(name), c.faces, c.placement));
            array.names.set(checksum(name), name);
            faces += c.faces.length;
        });
        if (companions.length) {
            report.push(
                `shared collision: ${companions.length} pieces took ${faces} faces of collision-only objects along ` +
                    "(the originals switched off with face flag 0x10, as the editor does)",
            );
        }
    }
    // (a level trigger an editor trigger stands in for is switched off by its wall record: the trigger isn't baked
    // yet, so the level's own stays on - else the level would lose it)
    const replaced = new Set(
        layout.triggers
            .filter((t) => /^[0-9A-Fa-f]{1,8}$/.test(t.fields.replaces ?? ""))
            .map((t) => Number.parseInt(t.fields.replaces!, 16) >>> 0),
    );
    for (const w of layout.walls) {
        const o = colAt.get(w.checksum);
        if (o === undefined || !col) {
            warnings.push(`wall ${label(w.checksum)}: no collision object of that name - skipped`);
            continue;
        }
        if (!isIdentity(w.placement)) {
            placeCollision(col.objects[o]!, w.placement);
            breaks(`collision ${label(w.checksum)} moved`);
        }
        if (w.off && replaced.has(w.checksum)) {
            warnings.push(`level trigger ${label(w.checksum)}: kept on - the editor trigger replacing it isn't baked`);
        } else if (w.off) {
            dropCollision.add(w.checksum);
            breaks(`collision ${label(w.checksum)} switched off (left out of the collision file)`);
        }
    }
    scene.sectors = scene.sectors.filter((s) => !dropSectors.has(s.checksum));
    if (col) col.objects = col.objects.filter((o) => !dropCollision.has(o.checksum));

    // Nodes: rails, objects, sets, links, terrains, hidden, dropped.
    for (const r of layout.rails) {
        const i = nodeAt(r.name, "rail");
        if (i < 0) continue;
        breaks(`rail point ${label(r.name)} ${r.hidden ? "hidden" : "moved"}`);
        if (r.hidden) {
            hideNode(array, i);
            array.linkNames.set(i, []);
            array.nodes.forEach((_, j) => {
                const links = linksOf(array, j);
                if (links.includes(r.name))
                    array.linkNames.set(
                        j,
                        links.filter((l) => l !== r.name),
                    );
            });
            warnings.push(
                `rail point ${label(r.name)} hidden: baked as the editor's hidden marker, the links to it cut`,
            );
            continue;
        }
        const node = array.edit(i);
        const pos = readVector(node, K.pos);
        if (!pos) {
            warnings.push(`rail point ${label(r.name)}: no pos= - skipped`);
            continue;
        }
        setVector(node, K.pos, applyPlacement(r.placement, pos));
    }
    for (const o of layout.objects) {
        const i = nodeAt(o.id, "object");
        if (i < 0) continue;
        const node = array.edit(i);
        setVector(node, K.pos, o.pos);
        const { angles, tilted } = anglesOfRows(o.rows);
        if (tilted) warnings.push(`object ${label(o.id)} is tilted: only its heading is baked`);
        setVector(node, K.angles, angles);
        const kind = nodeKind(node);
        if (kind) breaks(`${kind} ${label(o.id)} moved`);
    }
    for (const c of layout.copies) {
        const i = nodeAt(c.source, "copy");
        if (i < 0) continue;
        const copy = new Struct(array.nodes[i]!.entries.filter(([k]) => k?.value !== K.name).map(([k, v]) => [k, v]));
        copy.entries.unshift([new Name(K.name), new Name(c.name)]);
        setVector(copy, K.pos, c.pos);
        setVector(copy, K.angles, anglesOfRows(c.rows).angles);
        array.append(copy);
        const kind = nodeKind(copy);
        breaks(`a copy of ${kind ?? "object"} ${label(c.source)} added`);
    }
    let fields = 0;
    for (const s of layout.sets) {
        const i = nodeAt(s.name, "set");
        if (i < 0) continue;
        const node = array.edit(i);
        const kind = nodeKind(node);
        if (s.pos) setVector(node, K.pos, s.pos);
        if (s.angles) setVector(node, K.angles, s.angles);
        if ((s.pos || s.angles) && kind) breaks(`${kind} ${label(s.name)} moved / turned`);
        for (const f of s.fields) {
            array.remember(f.keyText);
            for (const t of f.value?.nameTexts ?? []) array.remember(t);
            if (f.key === K.links) {
                array.linkNames.set(i, f.value?.names ?? []);
                continue;
            }
            if (setField(array, node, f.key, f.removed ? null : (f.value ?? null))) fields++;
            else
                warnings.push(
                    `${label(s.name)}.${f.keyText ?? hex8(f.key)}: a shared structure's flag - not switched off`,
                );
            if (kind) breaks(`${kind} ${label(s.name)}: field ${f.keyText ?? hex8(f.key)} changed`);
        }
    }
    for (const t of layout.terrains) {
        const i = nodeAt(t.name, "terrain");
        if (i < 0) continue;
        const node = array.edit(i);
        node.entries = node.entries.filter(([k]) => k?.value !== K.terrain);
        node.entries.push([new Name(K.terrain), new Name(checksum(t.terrain))]);
        array.remember(t.terrain);
        breaks(`rail ${label(t.name)}'s terrain changed`);
    }
    for (const h of layout.hidden) {
        const i = nodeAt(h, "hide");
        if (i < 0) continue;
        const kind = nodeKind(array.nodes[i]);
        hideNode(array, i);
        if (kind) breaks(`${kind} ${label(h)} hidden (the crowns after a hidden crown each move up a number)`);
    }
    for (const d of layout.dropped) {
        const i = nodeAt(d, "drop");
        if (i < 0) continue;
        hideNode(array, i);
        breaks(`chain point ${label(d)} deleted`);
    }

    // Added nodes and points (after the level's: its indexes stay), crowns in the layout's order.
    const added = applyCrownOrder(layout.nodes, layout.crownOrder);
    for (const r of added) {
        if (array.index(checksum(r.name)) >= 0) {
            warnings.push(`node ${r.name}: the level has a node of that name already - not added`);
            continue;
        }
        const node = addedNode(array, r);
        array.append(node);
        const kind = nodeKind(node);
        if (kind) breaks(`${kind} ${r.name} added`);
        if (same(r.cls, "EmitterObject")) warnings.push(`sound emitter ${r.name}: silent until a script plays on it`);
    }
    for (const p of layout.points) {
        if (array.index(checksum(p.name)) >= 0) {
            warnings.push(`point ${p.name}: the level has a node of that name already - not added`);
            continue;
        }
        const i = array.append(addedPoint(array, p));
        array.linkNames.set(i, p.links);
        if (p.cls === "RailNode") breaks(`rail point ${p.name} added`);
    }
    for (const l of layout.links) {
        const i = nodeAt(l.name, "links");
        if (i < 0) continue;
        array.linkNames.set(i, l.links);
        const kind = nodeKind(array.nodes[i]);
        if (kind) breaks(`${kind} ${label(l.name)} relinked`);
    }
    // links= by name -> indexes.
    for (const [i, names] of array.linkNames) {
        const node = i < array.original ? array.edit(i) : array.nodes[i]!;
        const indexes: number[] = [];
        for (const n of names) {
            const at = array.index(n);
            if (at < 0)
                warnings.push(
                    `${label(getName(node, K.name) ?? 0)}: links to ${label(n)}, which isn't a node - dropped`,
                );
            else indexes.push(at);
        }
        node.entries = node.entries.filter(([k]) => k?.value !== K.links);
        if (indexes.length) node.entries.push([new Name(K.links), indexes]);
    }

    if (layout.crownOrder.length) breaks("the added crowns reordered");
    for (const lock of layout.locks) report.push(`lock ${lock.kind} ${label(lock.id)}: ignored (an editor-only aid)`);
    const level = layout.level;
    if (Object.keys(level).length) {
        const parts: string[] = [];
        if (level.name) parts.push(`name "${level.name}"`);
        if (level.start) parts.push(`start at restart ${label(level.start)}`);
        if (level.tod) parts.push(`tod_state=${level.tod}`);
        if (level.time !== undefined) parts.push(`tod_manager_active=${level.time}`);
        if (level.ambient) parts.push(`ambient_red/green/blue=${level.ambient.join(" ")}`);
        if (level.fog) parts.push(`fog_red/green/blue/alpha/dist=${level.fog.join(" ")}`);
        warnings.push(
            `level properties not baked (they're the level's Levels.qb structure, not its files): ${parts.join(", ")}`,
        );
        if (level.start) breaks("the level's start restart changed");
    }
    // (phase 5's trigger volumes: in the game they are the editor's own - an overlay node, a box registered as movable
    // collision, a script made at run time (level_editor_triggers.h). Baking one needs a trigger collision box, a node
    // and a script in the level's scripts .qb: not done yet - each listed with what it does.)
    for (const t of layout.triggers) {
        const f = t.fields;
        const what = [
            f.target ? `target ${f.target}` : "",
            f.script ? `script ${f.script}` : "",
            f.gap ? `gap ${f.gap}` : "",
            f.replaces ? `replaces ${label(parseInt(f.replaces, 16) >>> 0)}` : "",
        ]
            .filter((s) => s)
            .join(", ");
        unsupported.push(
            `unsupported record "trigger" (line ${t.line}): ${t.name}, a ${t.kind} trigger at ${f.pos}` +
                `${what ? ` (${what})` : ""} - not baked (an editor trigger is a run-time box, node and script; ` +
                "its baked form - a trigger collision box, a node and a script in the level's scripts .qb - isn't made yet)",
        );
        breaks(`trigger ${t.name} (${t.kind}${f.gap ? `, gap ${f.gap}` : ""}) added - kill zones, teleports and gaps`);
    }
    for (const u of layout.unknown) {
        unsupported.push(`unsupported record "${u.kind}" (line ${u.line}): not baked - ${u.text}`);
        if (shared)
            parity.push(
                `unsupported record "${u.kind}" (line ${u.line}) may change triggers / gaps - check online parity by hand`,
            );
    }

    // The node array written back: unchanged nodes keep their bytes.
    const elements: Element[] = array.nodes.map((n, i) =>
        i < array.original && !array.changed.has(i) ? { keep: i } : { value: n },
    );
    const qb = replaceGlobalArray(files.qb, arrayName, elements, array.names, true);
    report.push(
        `${files.level}: ${moved} sectors moved, ${dropSectors.size} hidden, ${cloneCount} copies, ${layout.walls.length} walls, ` +
            `${array.changed.size} level nodes changed (${fields} fields), ${array.nodes.length - array.original} nodes added`,
    );
    return { scene, col, qb, report, warnings, parity, unsupported };
}

// --- files and the command line ---------------------------------------------------------------------------------------

export function findLevelDir(game: string, level: string | null, scene: number): { dir: string; level: string } | null {
    for (const root of [path.join(game, OVERLAY, "levels"), path.join(game, "Data", "levels")]) {
        if (!fs.existsSync(root)) continue;
        for (const name of fs.readdirSync(root)) {
            const match = level ? same(name, level) : checksum(name) === scene;
            if (match && fs.existsSync(path.join(root, name, `${name}.qb`)))
                return { dir: path.join(root, name), level: name };
        }
    }
    return null;
}

// Folders nothing is ever written into, and the main install (only with --allow-main).
export function installRefusal(game: string, allowMain: boolean): string | null {
    const norm = path
        .resolve(game)
        .toLowerCase()
        .replace(/[\\/]+$/, "");
    for (const never of ["c:\\games\\thug2_pm_bak", "c:\\games\\thug2-thpsx-original", "c:\\games\\thugpro"]) {
        if (norm === never || norm.startsWith(`${never}\\`)) return `never written: ${game}`;
    }
    if (norm.includes("\\thug pro")) return `never written: ${game} (THUGPRO's)`;
    if ((norm === "c:\\games\\thug2" || norm === "c:\\games\\thug2\\game") && !allowMain) {
        return "C:\\Games\\THUG2 is the main install: --install there needs --allow-main (the user's own runs only)";
    }
    return null;
}

const USAGE = `usage: apply_layout.ts [-h] [--level LEVEL] [--game GAME] [--out OUT] [--dry-run] [--install] [--allow-main] [layout]

Bake a level editor layout (thug2-decomp-layouts\\<scene>.txt) into a copy of the level's .scn.xbx / .col.xbx / .qb.

positional arguments:
  layout        the layout file (default: <game>\\thug2-decomp-layouts\\<the level's scene>.txt)

options:
  -h, --help    show this help message and exit
  --level LEVEL the level's folder name (TR, z_ma, foun...; default: the one whose name's checksum is the layout's scene)
  --game GAME   the game folder read from (default C:\\Games\\THUG2; a stock install's Game folder is stepped into)
  --out OUT     the output folder (default out\\baked\\<level> in the repo)
  --dry-run     bake in memory and print the report; write nothing
  --install     write into <game>\\thug2-decomp-data\\levels\\<level> (a workspace) instead of --out
  --allow-main  let --install write into the main install C:\\Games\\THUG2 (the user only)`;

export function main(argv: string[] = process.argv.slice(2)): number {
    const { values, positionals } = parseCli(USAGE, {
        args: argv,
        options: {
            level: { type: "string" },
            game: { type: "string" },
            out: { type: "string" },
            "dry-run": { type: "boolean" },
            install: { type: "boolean" },
            "allow-main": { type: "boolean" },
        },
        allowPositionals: true,
    });
    const game = resolveGameFolder(values.game ?? "C:\\Games\\THUG2");
    let layoutPath = positionals[0] ?? null;
    let level = values.level ?? null;
    if (!layoutPath && !level) pyExit("give a layout file or --level");
    let scene = 0;
    if (!layoutPath) {
        const found = findLevelDir(game, level, 0);
        if (!found) pyExit(`no level ${level} in ${game}`);
        level = found!.level;
        scene = checksum(level);
        layoutPath = path.join(game, "thug2-decomp-layouts", `${hex8(scene)}.txt`);
    }
    if (!fs.existsSync(layoutPath)) pyExit(`no layout file ${layoutPath}`);
    let layout: Layout;
    try {
        layout = readLayout(fs.readFileSync(layoutPath!, "latin1"));
    } catch (e) {
        if (!(e instanceof LayoutError)) throw e;
        return pyExit(`${layoutPath}: ${e.message} - not a layout this reads (the DLL would refuse it too)`);
    }
    const found = findLevelDir(game, level, layout.scene);
    if (!found) pyExit(`no level for scene ${hex8(layout.scene)} in ${game} (give --level)`);
    const { dir, level: name } = found!;
    if (checksum(name) !== layout.scene) {
        print(`warning: the layout is for scene ${hex8(layout.scene)}, not ${name}'s ${hex8(checksum(name))}`);
    }
    const scnPath = path.join(dir, `${name}.scn.xbx`);
    const colPath = path.join(dir, `${name}.col.xbx`);
    const files: LevelFiles = {
        level: name,
        scene: readScene(fs.readFileSync(scnPath), scnPath),
        col: fs.existsSync(colPath) ? readCol(fs.readFileSync(colPath), colPath) : null,
        qb: fs.readFileSync(path.join(dir, `${name}.qb`)),
    };
    const ported = registerLevels();
    const portedGame = ported.get(name.toLowerCase());
    const own = dir.toLowerCase().includes(`${path.sep}data${path.sep}levels`.toLowerCase());
    const shared: Shared | null = portedGame
        ? { level: name, why: `a ported ${portedGame} level in THUGPRO's register (tools/compat/thugpro)` }
        : own
          ? { level: name, why: "THUG2's own level - THUGPRO players have it as shipped" }
          : null;
    const baked = bake(layout, files, shared);
    const lines = [
        `apply_layout: ${layoutPath} -> ${name} (read from ${dir})`,
        ...baked.report,
        ...baked.warnings.map((w) => `warning: ${w}`),
        ...baked.unsupported.map((u) => `warning: ${u}`),
        ...(baked.parity.length
            ? [`THUGPRO parity (${shared!.why}): ${baked.parity.length} edits`, ...baked.parity.map((p) => `  ${p}`)]
            : []),
    ];
    for (const line of lines) print(line);
    if (values["dry-run"]) {
        print("dry run: nothing written");
        return 0;
    }
    let outDir: string;
    if (values.install) {
        const refusal = installRefusal(game, values["allow-main"] === true);
        if (refusal) pyExit(refusal);
        outDir = path.join(game, OVERLAY, "levels", name);
        if (own) {
            print(
                "warning: THUG2's own level - whether the game reads the overlay's loose files over its pre archives " +
                    `(${name}scn.pre / ${name}col.pre) isn't checked`,
            );
        }
    } else {
        outDir = path.join(values.out ?? path.join(REPO, "out", "baked", name), "levels", name);
    }
    fs.mkdirSync(outDir, { recursive: true });
    const scn = writeScene(baked.scene);
    readScene(scn, name); // (read back first, as the converters do)
    fs.writeFileSync(path.join(outDir, `${name}.scn.xbx`), scn);
    if (baked.col) {
        const colData = writeCol(baked.col);
        readCol(colData, name);
        fs.writeFileSync(path.join(outDir, `${name}.col.xbx`), colData);
    }
    new QbFile(baked.qb, name, true);
    fs.writeFileSync(path.join(outDir, `${name}.qb`), baked.qb);
    fs.writeFileSync(
        path.join(values.install ? outDir : path.dirname(path.dirname(outDir)), "bake_report.txt"),
        `${lines.join("\n")}\n`,
    );
    print(`written: ${outDir}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
