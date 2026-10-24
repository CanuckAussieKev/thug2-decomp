// NOT ORIGINAL - z bias for overlay surfaces of converted scenes whose source has none (THPS3, THPS4).
//
// THUG2 draws a material's meshes with its z bias as the depth bias (0x004D1F51 / 0x004D2420: the material's byte at
// +0x11 to render state 0x15, D3DRS_DEPTHBIAS = bias x -2e-6). Its own levels give one to the surfaces laid on top of
// others - decals, dirt and light layers, painted stripes, grates on a floor, vents on a ramp - so they win the depth
// test against the surface under them. THPS3's and THPS4's materials have no z bias: converted with 0, such a layer
// (exactly coplanar, or a fraction of a unit above) and the surface under it fight over the depth buffer, and the
// surface shimmers as the camera moves (docs/pending/ported-shimmer.md). Here the layers are found from the geometry:
// a triangle of one material whose centre lies on a triangle of another (same facing, within kPlaneTolerance of its
// plane, inside it) - and the material drawn later (a higher draw order; at the same draw order the decal /
// transparent one, else the one more of whose surface is covered) gets a z bias one more than the one under it.

import type * as thug2Scene from "./thug2_scene.ts";
import { PASS_DECAL, PASS_TRANSPARENT } from "./thug2_scene.ts";

export const kPlaneTolerance = 1.0; // units (inches) off the other triangle's plane that still count as "on" it
export const kMinOverlapArea = 1.0; // square units of a material lying on another's below which they're left alone
export const kMaxZBias = 6; // (THUG2's own levels go up to 11; a chain of layers rarely needs more)

type Vec = [number, number, number];

interface Tri {
    material: number;
    mesh: number;
    p: [Vec, Vec, Vec];
    n: Vec;
    d: number;
    area: number;
    axis: number; // the dominant axis of the normal: the triangle is tested in the plane of the other two
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

// A scene's triangles (the meshes' first strip; degenerate ones left out), with their material's index.
function triangles(scene: thug2Scene.Scene, materialIndex: Map<number, number>): Tri[] {
    const out: Tri[] = [];
    let meshNo = 0;
    for (const sector of scene.sectors) {
        for (const mesh of sector.meshes) {
            meshNo++;
            const material = materialIndex.get(mesh.material);
            const buffer = mesh.buffers[0]?.[1];
            const strip = mesh.strips[0] ?? [];
            if (material === undefined || buffer === undefined || mesh.stride < 12) continue;
            const position = (i: number): Vec | null => {
                const at = i * mesh.stride;
                if (at + 12 > buffer.length) return null;
                return [buffer.readFloatLE(at), buffer.readFloatLE(at + 4), buffer.readFloatLE(at + 8)];
            };
            for (let k = 2; k < strip.length; k++) {
                const a = strip[k - 2]!;
                const b = strip[k - 1]!;
                const c = strip[k]!;
                if (a === b || b === c || a === c) continue;
                const pa = position(a);
                const pb = position(b);
                const pc = position(c);
                if (!pa || !pb || !pc) continue;
                // (a strip's odd triangles are wound the other way)
                const p: [Vec, Vec, Vec] = k % 2 ? [pb, pa, pc] : [pa, pb, pc];
                const n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
                const length = Math.hypot(n[0], n[1], n[2]);
                if (!(length > 1e-6)) continue;
                n[0] /= length;
                n[1] /= length;
                n[2] /= length;
                const ax = Math.abs(n[0]);
                const ay = Math.abs(n[1]);
                const az = Math.abs(n[2]);
                const axis = ax >= ay && ax >= az ? 0 : ay >= az ? 1 : 2;
                out.push({ material, mesh: meshNo, p, n, d: dot(n, p[0]), area: length / 2, axis });
            }
        }
    }
    return out;
}

// Whether the point (on the triangle's plane, or near it) is inside the triangle, seen along its normal.
function contains(t: Tri, point: Vec): boolean {
    const [i, j] = t.axis === 0 ? [1, 2] : t.axis === 1 ? [0, 2] : [0, 1];
    let side = 0;
    for (let e = 0; e < 3; e++) {
        const a = t.p[e]!;
        const b = t.p[(e + 1) % 3]!;
        const c = (b[i]! - a[i]!) * (point[j]! - a[j]!) - (b[j]! - a[j]!) * (point[i]! - a[i]!);
        if (Math.abs(c) < 1e-9) continue;
        const s = Math.sign(c);
        if (side && s !== side) return false;
        side = s;
    }
    return side !== 0;
}

// The area of each material's triangles that lies on another material's: overlap.get(a)?.get(b) = how much of a's
// surface lies on b's. total[a] = a's whole area.
export function overlaps(scene: thug2Scene.Scene): {
    overlap: Map<number, Map<number, number>>;
    total: number[];
} {
    const materialIndex = new Map(scene.materials.map((m, i) => [m.checksum, i]));
    const tris = triangles(scene, materialIndex);
    const total = scene.materials.map(() => 0);
    const cell = 2 * kPlaneTolerance;
    const key = (t: Tri, plane: number): string =>
        `${Math.round(t.n[0] * 20)},${Math.round(t.n[1] * 20)},${Math.round(t.n[2] * 20)},${plane}`;
    const buckets = new Map<string, Tri[]>();
    for (const t of tris) {
        total[t.material]! += t.area;
        const k = key(t, Math.round(t.d / cell));
        const list = buckets.get(k);
        if (list) list.push(t);
        else buckets.set(k, [t]);
    }
    const overlap = new Map<number, Map<number, number>>();
    for (const t of tris) {
        const centre: Vec = [0, 1, 2].map((k) => (t.p[0][k]! + t.p[1][k]! + t.p[2][k]!) / 3) as Vec;
        const plane = Math.round(t.d / cell);
        const found = new Set<number>(); // (each other material once per triangle)
        for (const near of [plane - 1, plane, plane + 1]) {
            for (const o of buckets.get(key(t, near)) ?? []) {
                if (o.material === t.material || o.mesh === t.mesh || found.has(o.material)) continue;
                if (dot(t.n, o.n) < 0.99) continue;
                if (Math.abs(dot(o.n, centre) - o.d) > kPlaneTolerance) continue;
                if (!contains(o, centre)) continue;
                found.add(o.material);
                const row = overlap.get(t.material) ?? new Map<number, number>();
                overlap.set(t.material, row);
                row.set(o.material, (row.get(o.material) ?? 0) + t.area);
            }
        }
    }
    return { overlap, total };
}

// Whether a material is a layer by its look: a decal or transparent first pass.
function layered(m: thug2Scene.Material): boolean {
    const p = m.passes[0];
    return p !== undefined && Boolean(p.flags & (PASS_DECAL | PASS_TRANSPARENT));
}

// Two materials that look the same (the same passes: textures, flags, blending) - one lying on the other can't be seen.
function sameLook(a: thug2Scene.Material, b: thug2Scene.Material): boolean {
    return (
        a.passes.length === b.passes.length &&
        a.passes.every((p, i) => {
            const q = b.passes[i]!;
            return p.texture === q.texture && p.flags === q.flags && p.blendMode === q.blendMode;
        })
    );
}

// Which of two overlapping materials is the layer on top, and how sure that is (0 = by draw order, 1 = by look,
// 2 = by how much of each is covered), or null when it can't be told: two opaque surfaces of the same draw order
// covering each other alike (alternative geometry a script shows one of at a time, e.g. THPS3 LA's earthquake).
function topOf(
    scene: thug2Scene.Scene,
    a: number,
    b: number,
    coveredA: number,
    coveredB: number,
): [top: number, rank: number] | null {
    const ma = scene.materials[a]!;
    const mb = scene.materials[b]!;
    if (ma.drawOrder !== mb.drawOrder) return [ma.drawOrder > mb.drawOrder ? a : b, 0];
    if (layered(ma) !== layered(mb)) return [layered(ma) ? a : b, 1];
    if (Math.max(coveredA, coveredB) < 0.5 || Math.abs(coveredA - coveredB) < 0.1) return null;
    return [coveredA > coveredB ? a : b, 2];
}

// Gives the layers lying on other materials' surfaces a z bias above them (materials keep a bigger one they have).
// Returns the number of materials changed.
// `lookAlikes`: materials that look the same (same textures and flags), or that cover each other alike, are biased too
// - the one more of whose surface is covered over the other (else the second). Two copies of one surface with other
// lighting (THP8's, tessellated twice and shaded twice) flicker where they overlap and neither lies wholly on the other.
export function biasOverlays(scene: thug2Scene.Scene, lookAlikes = false): number {
    const { overlap, total } = overlaps(scene);
    const materials = scene.materials;
    const edges: { top: number; under: number; rank: number; area: number }[] = [];
    const decided = new Set<string>();
    for (const [a, row] of overlap) {
        for (const b of row.keys()) {
            const pair = a < b ? `${a},${b}` : `${b},${a}`;
            if (decided.has(pair)) continue;
            decided.add(pair);
            const ab = overlap.get(a)?.get(b) ?? 0; // a's area on b
            const ba = overlap.get(b)?.get(a) ?? 0;
            if (Math.max(ab, ba) < kMinOverlapArea) continue;
            const alike = sameLook(materials[a]!, materials[b]!);
            if (alike && !lookAlikes) continue;
            // (undecided, with lookAlikes: the one more of whose surface is covered, else the second)
            const guess = (): [number, number] => [ab / (total[a] || 1) > ba / (total[b] || 1) ? a : b, 3];
            const top = alike
                ? guess()
                : (topOf(scene, a, b, ab / (total[a] || 1), ba / (total[b] || 1)) ?? (lookAlikes ? guess() : null));
            if (top === null) continue;
            edges.push({ top: top[0], under: top[0] === a ? b : a, rank: top[1], area: Math.max(ab, ba) });
        }
    }
    // The surest first; one that would close a loop (a over b over ... over a) is left out.
    edges.sort((x, y) => x.rank - y.rank || y.area - x.area || x.top - y.top || x.under - y.under);
    const over: number[][] = materials.map(() => []);
    const reaches = (from: number, to: number): boolean => {
        const seen = new Set<number>([from]);
        const stack = [from];
        while (stack.length) {
            const at = stack.pop()!;
            if (at === to) return true;
            for (const next of over[at]!) {
                if (!seen.has(next)) {
                    seen.add(next);
                    stack.push(next);
                }
            }
        }
        return false;
    };
    for (const e of edges) if (!reaches(e.under, e.top)) over[e.top]!.push(e.under);
    // Each layer one above the highest one under it (the graph has no loops: depth-first, memoised).
    const before = materials.map((m) => m.zBias);
    const done = new Set<number>();
    const settle = (i: number): number => {
        if (done.has(i)) return materials[i]!.zBias;
        done.add(i);
        const m = materials[i]!;
        for (const under of over[i]!) m.zBias = Math.max(m.zBias, Math.min(kMaxZBias, settle(under) + 1));
        return m.zBias;
    };
    for (let i = 0; i < materials.length; i++) settle(i);
    return materials.filter((m, i) => m.zBias !== before[i]).length;
}
