// NOT ORIGINAL - collision flags for ramps of converted levels whose source game decides skatability by itself
// (THPS2: tools/thps2_levels).
//
// THUG2's skater (src/game/skater/core_physics_feelers.cpp CheckFeeler) takes a face as ground when it is flagged
// skatable (0x1) or vert (0x8); flagged not skatable (0x2) or wallridable (0x4) it is a wall; with none of those, a
// face steeper than Wall_Non_Skatable_Angle (25 degrees from vertical in physics.qb: its normal's y under sin 25 =
// 0.42, i.e. sloped more than ~65 degrees) is a wall too. THUG2's own levels flag the steep faces of their ramps;
// THPS2's data has only its vert bit, so the top faces of its non-vert quarter pipes (67, 73 degrees - Skate Heaven,
// Chopper Drop) came out as walls: the skater rode up the transition and stopped dead against the last face.
//
// Here such faces are found from the geometry: a face too steep to be ground by itself (but still facing up) whose
// lower neighbour - a face joined along an edge, of any object - is ground, the join concave (the steep face rises in
// front of it) and the bend between them no more than kMaxRampBend, continues that ramp: it is flagged skatable, and
// in turn the ground of the face above it. A wall standing on a floor bends 65 - 90 degrees from it and stays a wall.
// And a face lying on another (coplanar, within kOverlayTolerance of its plane, its centre inside it: a decal model
// over a ramp - NY's graffiti over a vert wall) takes the flags of the face under it when those make it more
// rideable (vert over skatable over none over a wall): the skater meets the decal first.

import type { SourceColObject } from "./geometry.ts";
import * as thug2Col from "./thug2_col.ts";

type Vec = [number, number, number];

export const kWallNonSkatableUp = Math.sin((25 * Math.PI) / 180); // physics.qb's Wall_Non_Skatable_Angle
export const kMaxRampBend = 40; // degrees between a ramp face and the steep face above it that still continue it
export const kMinRampUp = 0.1; // a steep face's normal y at least this (sloped no more than ~84 degrees)
export const kEdgeTolerance = 0.5; // inches off another edge's line that still count as joined along it
export const kMinJoin = 1.0; // inches two edges must share along their line
export const kOverlayTolerance = 1.0; // inches off the other face's plane that still count as lying on it
const kSamePlane = Math.cos(Math.PI / 180); // normals within a degree: the same surface

const SKATE_BITS =
    thug2Col.FACE_SKATABLE | thug2Col.FACE_NOT_SKATABLE | thug2Col.FACE_WALL_RIDABLE | thug2Col.FACE_VERT;
const IGNORED = thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_TRIGGER;

export interface RampTri {
    p: [Vec, Vec, Vec];
    flags: number;
}

export interface RampResult {
    flags: number[]; // per triangle, the flags it should have
    promoted: number[]; // triangles made skatable as ramp faces
    overlays: number[]; // triangles that took the flags of a face under them
}

function sub(a: Vec, b: Vec): Vec {
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}
function cross(a: Vec, b: Vec): Vec {
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function dot(a: Vec, b: Vec): number {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
function length(a: Vec): number {
    return Math.hypot(a[0], a[1], a[2]);
}

// How THUG2's skater takes a face: 3 vert, 2 skatable (flag or not steep), 1 nothing said and steep (a wall unless
// made a ramp), 0 a wall by its flags.
export function rideRank(flags: number, up: number): number {
    if (flags & thug2Col.FACE_VERT) return 3;
    if (flags & thug2Col.FACE_SKATABLE) return 2;
    if (flags & (thug2Col.FACE_NOT_SKATABLE | thug2Col.FACE_WALL_RIDABLE)) return 0;
    return up >= kWallNonSkatableUp ? 2 : 1;
}

interface Info {
    n: Vec;
    d: number;
    area: number;
    centre: Vec;
    axis: number;
}

function infoOf(t: RampTri): Info | null {
    const c = cross(sub(t.p[1], t.p[0]), sub(t.p[2], t.p[0]));
    const l = length(c);
    if (!(l > 1e-6)) return null;
    const n: Vec = [c[0] / l, c[1] / l, c[2] / l];
    const ax = Math.abs(n[0]);
    const ay = Math.abs(n[1]);
    const az = Math.abs(n[2]);
    const centre = [0, 1, 2].map((k) => (t.p[0][k]! + t.p[1][k]! + t.p[2][k]!) / 3) as Vec;
    return { n, d: dot(n, t.p[0]), area: l / 2, centre, axis: ax >= ay && ax >= az ? 0 : ay >= az ? 1 : 2 };
}

// A point inside a triangle, in the plane of the two axes other than the normal's main one.
function inside(t: RampTri, axis: number, q: Vec, slack = 0): boolean {
    const [i, j] = axis === 0 ? [1, 2] : axis === 1 ? [0, 2] : [0, 1];
    let side = 0;
    for (let e = 0; e < 3; e++) {
        const a = t.p[e]!;
        const b = t.p[(e + 1) % 3]!;
        const ei = b[i]! - a[i]!;
        const ej = b[j]! - a[j]!;
        const c = ei * (q[j]! - a[j]!) - ej * (q[i]! - a[i]!);
        const l = Math.hypot(ei, ej);
        if (Math.abs(c) <= slack * l + 1e-9) continue;
        const s = Math.sign(c);
        if (side && s !== side) return false;
        side = s;
    }
    return true;
}

// A uniform grid over boxes (edges, triangles); boxes over kMaxCells cells (a sky's panorama) kept in a list of
// their own, met by every query.
const kMaxCells = 4096;
class Grid {
    private cells = new Map<string, number[]>();
    private big: number[] = [];
    private size: number;
    constructor(size: number) {
        this.size = size;
    }
    private range(lo: Vec, hi: Vec): [number[], number[], number] {
        const a = lo.map((v) => Math.floor(v / this.size));
        const b = hi.map((v) => Math.floor(v / this.size));
        return [a, b, (b[0]! - a[0]! + 1) * (b[1]! - a[1]! + 1) * (b[2]! - a[2]! + 1)];
    }
    add(id: number, lo: Vec, hi: Vec): void {
        const [a, b, count] = this.range(lo, hi);
        if (count > kMaxCells) {
            this.big.push(id);
            return;
        }
        for (let x = a[0]!; x <= b[0]!; x++)
            for (let y = a[1]!; y <= b[1]!; y++)
                for (let z = a[2]!; z <= b[2]!; z++) {
                    const k = `${x},${y},${z}`;
                    const list = this.cells.get(k);
                    if (list) list.push(id);
                    else this.cells.set(k, [id]);
                }
    }
    query(lo: Vec, hi: Vec): Set<number> {
        const [a, b, count] = this.range(lo, hi);
        const out = new Set<number>(this.big);
        if (count > this.cells.size) {
            for (const list of this.cells.values()) for (const id of list) out.add(id);
            return out;
        }
        for (let x = a[0]!; x <= b[0]!; x++)
            for (let y = a[1]!; y <= b[1]!; y++)
                for (let z = a[2]!; z <= b[2]!; z++)
                    for (const id of this.cells.get(`${x},${y},${z}`) ?? []) out.add(id);
        return out;
    }
}

function boxOf(points: readonly Vec[], pad = 0): [Vec, Vec] {
    const lo = [0, 1, 2].map((k) => Math.min(...points.map((p) => p[k]!)) - pad) as Vec;
    const hi = [0, 1, 2].map((k) => Math.max(...points.map((p) => p[k]!)) + pad) as Vec;
    return [lo, hi];
}

// Whether two edges lie along one line and share at least kMinJoin of it.
export function edgesJoin(a0: Vec, a1: Vec, b0: Vec, b1: Vec): boolean {
    const dir = sub(a1, a0);
    const l = length(dir);
    if (!(l > 1e-6)) return false;
    const u: Vec = [dir[0] / l, dir[1] / l, dir[2] / l];
    const off = (q: Vec): number => length(cross(sub(q, a0), u));
    if (off(b0) > kEdgeTolerance || off(b1) > kEdgeTolerance) return false;
    const s0 = dot(sub(b0, a0), u);
    const s1 = dot(sub(b1, a0), u);
    const shared = Math.min(l, Math.max(s0, s1)) - Math.max(0, Math.min(s0, s1));
    return shared >= kMinJoin;
}

// `canRamp(i)`: whether triangle i may be made a ramp face (a converter's own reasons to keep a wall); default all.
export function rampFlags(
    tris: readonly RampTri[],
    canRamp: (index: number) => boolean = () => true,
    maxBend = kMaxRampBend,
): RampResult {
    const flags = tris.map((t) => t.flags);
    const infos = tris.map(infoOf);
    const live = (i: number): boolean => infos[i] !== null && !(flags[i]! & IGNORED);
    const promoted: number[] = [];
    const overlays: number[] = [];

    // 1. faces lying on others
    const triGrid = new Grid(64);
    tris.forEach((t, i) => {
        if (!live(i)) return;
        const [lo, hi] = boxOf(t.p, kOverlayTolerance);
        triGrid.add(i, lo, hi);
    });
    tris.forEach((_, i) => {
        const a = infos[i];
        if (!a || !live(i)) return;
        let best = -1;
        let bestRank = rideRank(flags[i]!, a.n[1]);
        for (const o of triGrid.query(a.centre, a.centre)) {
            const b = infos[o]!;
            if (o === i || dot(a.n, b.n) < 0.99) continue;
            if (Math.abs(dot(b.n, a.centre) - b.d) > kOverlayTolerance) continue;
            // (the face under: the larger one; of two the same size neither changes)
            if (!(b.area > a.area * 1.001) || !inside(tris[o]!, b.axis, a.centre)) continue;
            const rank = rideRank(tris[o]!.flags, b.n[1]);
            if (rank > bestRank) {
                best = o;
                bestRank = rank;
            }
        }
        if (best >= 0) {
            flags[i] = (flags[i]! & ~SKATE_BITS) | (tris[best]!.flags & SKATE_BITS);
            if (bestRank === 2 && !(flags[i]! & thug2Col.FACE_SKATABLE) && a.n[1] < kWallNonSkatableUp)
                flags[i]! |= thug2Col.FACE_SKATABLE;
            overlays.push(i);
        }
    });

    // 2. steep faces continuing ramps
    const edgeGrid = new Grid(48);
    const edgeOf = (id: number): [Vec, Vec] => {
        const t = tris[Math.floor(id / 3)]!;
        return [t.p[id % 3]!, t.p[(id + 1) % 3]!];
    };
    tris.forEach((_, i) => {
        if (!live(i)) return;
        for (let e = 0; e < 3; e++) {
            const [lo, hi] = boxOf(edgeOf(i * 3 + e), kEdgeTolerance);
            edgeGrid.add(i * 3 + e, lo, hi);
        }
    });
    const isCandidate = (i: number): boolean => {
        const a = infos[i];
        if (!a || !live(i) || !canRamp(i)) return false;
        if (flags[i]! & (thug2Col.FACE_SKATABLE | thug2Col.FACE_VERT | thug2Col.FACE_NOT_SKATABLE)) return false;
        return a.n[1] >= kMinRampUp && a.n[1] < kWallNonSkatableUp;
    };
    const ground = (i: number): boolean => live(i) && rideRank(flags[i]!, infos[i]!.n[1]) >= 2;
    const cosBend = Math.cos((maxBend * Math.PI) / 180);
    // whether steep face i continues ground face g along one of i's edges: the other half of its quad (the same
    // plane), or a flatter face below it, the bend concave and small enough
    const continues = (i: number, g: number): boolean => {
        const a = infos[i]!;
        const b = infos[g]!;
        const cos = dot(a.n, b.n);
        const flat = cos >= kSamePlane;
        if (!flat && (b.n[1] <= a.n[1] || cos < cosBend)) return false;
        for (let e = 0; e < 3; e++) {
            const [a0, a1] = edgeOf(i * 3 + e);
            for (let f = 0; f < 3; f++) {
                const [b0, b1] = edgeOf(g * 3 + f);
                if (!edgesJoin(a0, a1, b0, b1)) continue;
                if (flat) return true;
                // concave: g's vertex off the shared line lies in front of i's plane
                const far = tris[g]!.p[(f + 2) % 3]!;
                if (dot(a.n, sub(far, a0)) > 0.01) return true;
            }
        }
        return false;
    };
    const neighbours = (i: number): Set<number> => {
        const out = new Set<number>();
        for (let e = 0; e < 3; e++) {
            const [lo, hi] = boxOf(edgeOf(i * 3 + e), kEdgeTolerance);
            for (const id of edgeGrid.query(lo, hi)) {
                const o = Math.floor(id / 3);
                if (o !== i) out.add(o);
            }
        }
        return out;
    };
    const queue: number[] = [];
    tris.forEach((_, i) => {
        if (ground(i)) queue.push(i);
    });
    const done = new Set<number>();
    while (queue.length) {
        const g = queue.pop()!;
        if (done.has(g)) continue;
        done.add(g);
        for (const i of neighbours(g)) {
            if (!isCandidate(i) || !continues(i, g)) continue;
            flags[i] = (flags[i]! & ~thug2Col.FACE_WALL_RIDABLE) | thug2Col.FACE_SKATABLE;
            promoted.push(i);
            queue.push(i);
        }
    }
    return { flags, promoted, overlays };
}

// The same over collision objects (all in one space): their faces' flags changed in place. `canRamp(object, face)`.
export function fixRampObjects(
    objects: readonly SourceColObject[],
    canRamp: (object: number, face: number) => boolean = () => true,
): { promoted: number; overlays: number } {
    const tris: RampTri[] = [];
    const where: [number, number][] = [];
    objects.forEach((o, oi) => {
        o.faces.forEach((f, fi) => {
            tris.push({ p: [f[2], f[3], f[4]].map((v) => o.verts[v]! as Vec) as [Vec, Vec, Vec], flags: f[0] });
            where.push([oi, fi]);
        });
    });
    const result = rampFlags(tris, (i) => canRamp(where[i]![0], where[i]![1]));
    result.flags.forEach((flags, i) => {
        const [oi, fi] = where[i]!;
        objects[oi]!.faces[fi]![0] = flags;
    });
    return { promoted: result.promoted.length, overlays: result.overlays.length };
}
