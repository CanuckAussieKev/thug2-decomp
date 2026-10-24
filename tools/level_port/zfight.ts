// NOT ORIGINAL - z-fighting finder for converted levels (any port): surfaces of different meshes that lie in one plane
// (or so close to it that THUG2's depth buffer can't tell them apart) and overlap - the camera sees them flicker
// through each other.
//
//     node tools/level_port/zfight.ts <levels folder> [<level> ...] [--top N] [--tolerance IN] [--view M] [--near IN]
//                                     [--min-area M2] [--csv FILE]
//
// e.g. node tools/level_port/zfight.ts C:\Games\THUG2-2\thug2-decomp-thp8\levels thp8_net_beavervalley
//
// How close is too close: THUG2 draws with a 24-bit depth buffer (src/game/gfx/device.cpp: D3DFMT_D24X8) and a near
// plane of 12 inches (src/game/gfx/camera.cpp). A perspective depth buffer's step at distance z is about
// z^2 / (near x 2^bits); two surfaces `s` inches apart fight once a few steps (DEPTH_STEPS) span s - from
// fightDistance(s) = sqrt(s x near x 2^bits / DEPTH_STEPS) on: 0.02 inch apart from ~40 m, 0.25 inch from ~140 m,
// 2 inches from ~400 m (stand-ins are seen that far). Exactly coplanar surfaces cut into different triangles fight at
// any distance (their depths are interpolated over different triangles). A material's z bias decides such a pair
// (overlay_bias.ts) - pairs whose materials' z biases differ are counted apart ("biased"), not as fights.
//
// Facing: two surfaces facing the same way fight; facing opposite ways only when the one seen from behind isn't
// culled (THUG2 culls back faces unless the material has noBackfaceCulling) - counted apart ("opposite").
//
// The scene is taken as THUG2's Scene (thug2_scene.ts): read from a .scn.xbx, or straight from a converter's
// output before it's written (thp8_levels' convertScenes(...).scene) - findZFights doesn't care which.

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import { isMain, print, runMain } from "../lib/py.ts";
import * as thug2Scene from "./thug2_scene.ts";

export const DEPTH_BITS = 24;
export const NEAR_PLANE = 12; // inches
export const DEPTH_STEPS = 4; // depth-buffer steps two surfaces must be apart not to fight (interpolation, rounding)
export const INCHES_PER_METRE = 39.37;
export const DEFAULT_TOLERANCE = 2; // inches: farther apart isn't looked at (fights only past ~400 m)
export const DEFAULT_VIEW = 100; // metres: the distance a fight counts as seen within (ranking)
export const DEFAULT_MIN_AREA = 0.05; // m2 of overlap below which a pair of meshes isn't reported
export const MIN_NORMAL_DOT = 0.98; // (|cos| of the angle between the planes: ~11 degrees)
const CELL = 256; // inches: the grid the triangles are bucketed in

// The camera distance (inches) from which two surfaces `separation` inches apart fight.
export function fightDistance(separation: number, near = NEAR_PLANE, bits = DEPTH_BITS): number {
    return Math.sqrt((Math.max(separation, 0) * near * 2 ** bits) / DEPTH_STEPS);
}

// The separation (inches) below which surfaces fight from `distance` inches.
export function fightSeparation(distance: number, near = NEAR_PLANE, bits = DEPTH_BITS): number {
    return (distance * distance * DEPTH_STEPS) / (near * 2 ** bits);
}

type Vec = [number, number, number];

export interface Tri {
    sector: number; // index in scene.sectors
    mesh: number; // index in the sector's meshes
    material: number; // index in scene.materials (-1: not defined)
    p: [Vec, Vec, Vec];
    n: Vec;
    d: number;
    lo: Vec;
    hi: Vec;
}

const sub = (a: Vec, b: Vec): Vec => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a: Vec, b: Vec): Vec => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
];
const dot = (a: Vec, b: Vec): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

// Every drawn triangle of a scene (each mesh's first strip; degenerate ones left out), wound as drawn.
export function sceneTriangles(scene: thug2Scene.Scene): Tri[] {
    const materialIndex = new Map(scene.materials.map((m, i) => [m.checksum, i]));
    const out: Tri[] = [];
    scene.sectors.forEach((sector, s) => {
        sector.meshes.forEach((mesh, k) => {
            const buffer = mesh.buffers[0]?.[1];
            const strip = mesh.strips[0] ?? [];
            if (buffer === undefined || mesh.stride < 12) return;
            const material = materialIndex.get(mesh.material) ?? -1;
            const position = (i: number): Vec | null => {
                const at = i * mesh.stride;
                if (at + 12 > buffer.length) return null;
                return [buffer.readFloatLE(at), buffer.readFloatLE(at + 4), buffer.readFloatLE(at + 8)];
            };
            for (let j = 2; j < strip.length; j++) {
                const a = strip[j - 2]!;
                const b = strip[j - 1]!;
                const c = strip[j]!;
                if (a === b || b === c || a === c) continue;
                const pa = position(a);
                const pb = position(b);
                const pc = position(c);
                if (!pa || !pb || !pc) continue;
                // (a strip's odd triangles are wound the other way)
                const p: [Vec, Vec, Vec] = j % 2 ? [pb, pa, pc] : [pa, pb, pc];
                const t = makeTri(s, k, material, p);
                if (t) out.push(t);
            }
        });
    });
    return out;
}

export function makeTri(sector: number, mesh: number, material: number, p: [Vec, Vec, Vec]): Tri | null {
    const n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
    const length = Math.hypot(n[0], n[1], n[2]);
    if (!(length > 1e-6)) return null;
    n[0] /= length;
    n[1] /= length;
    n[2] /= length;
    const lo: Vec = [0, 1, 2].map((k) => Math.min(p[0][k]!, p[1][k]!, p[2][k]!)) as Vec;
    const hi: Vec = [0, 1, 2].map((k) => Math.max(p[0][k]!, p[1][k]!, p[2][k]!)) as Vec;
    return { sector, mesh, material, p, n, d: dot(n, p[0]), lo, hi };
}

type P2 = [number, number];

// The triangle's corners on the plane across `axis` (the other two coordinates), counter-clockwise.
function project(t: Tri, axis: number): P2[] {
    const [i, j] = axis === 0 ? [1, 2] : axis === 1 ? [0, 2] : [0, 1];
    const q = t.p.map((p): P2 => [p[i]!, p[j]!]);
    const area = (q[1]![0] - q[0]![0]) * (q[2]![1] - q[0]![1]) - (q[2]![0] - q[0]![0]) * (q[1]![1] - q[0]![1]);
    return area < 0 ? [q[0]!, q[2]!, q[1]!] : q;
}

// A convex polygon clipped by a counter-clockwise convex one (Sutherland-Hodgman).
export function clipPolygon(subject: readonly P2[], clip: readonly P2[]): P2[] {
    let out: P2[] = [...subject];
    for (let e = 0; e < clip.length && out.length; e++) {
        const a = clip[e]!;
        const b = clip[(e + 1) % clip.length]!;
        const side = (p: P2): number => (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
        const input = out;
        out = [];
        for (let k = 0; k < input.length; k++) {
            const p = input[k]!;
            const q = input[(k + 1) % input.length]!;
            const sp = side(p);
            const sq = side(q);
            if (sp >= 0) out.push(p);
            if (sp >= 0 !== sq >= 0) {
                const f = sp / (sp - sq);
                out.push([p[0] + f * (q[0] - p[0]), p[1] + f * (q[1] - p[1])]);
            }
        }
    }
    return out;
}

export function polygonArea(poly: readonly P2[]): number {
    let a = 0;
    for (let k = 0; k < poly.length; k++) {
        const p = poly[k]!;
        const q = poly[(k + 1) % poly.length]!;
        a += p[0] * q[1] - q[0] * p[1];
    }
    return Math.abs(a) / 2;
}

export interface Overlap {
    area: number; // square inches, in the planes
    maxSeparation: number; // inches, over the overlap
    minSeparation: number;
    centre: Vec;
}

// How two near-coplanar triangles overlap, seen across the first's main axis: null when they don't (or less than
// `minArea` square inches) or lie more than `tolerance` apart anywhere over the overlap.
export function overlapOf(t: Tri, o: Tri, tolerance: number, minArea = 1e-3): Overlap | null {
    const axis = mainAxis(t.n);
    const na = t.n[axis]!;
    const oa = o.n[axis]!;
    if (Math.abs(oa) < 1e-6) return null;
    const poly = clipPolygon(project(t, axis), project(o, axis));
    if (poly.length < 3) return null;
    const flat = polygonArea(poly);
    const area = flat / Math.abs(na);
    if (area < minArea) return null;
    const [i, j] = axis === 0 ? [1, 2] : axis === 1 ? [0, 2] : [0, 1];
    // (each overlap corner's height along the axis on both planes; the gap measured across t's plane)
    let maxSep = 0;
    let minSep = Number.POSITIVE_INFINITY;
    const centre: Vec = [0, 0, 0];
    for (const [u, v] of poly) {
        const ht = (t.d - t.n[i]! * u - t.n[j]! * v) / na;
        const ho = (o.d - o.n[i]! * u - o.n[j]! * v) / oa;
        const sep = Math.abs(ht - ho) * Math.abs(na);
        maxSep = Math.max(maxSep, sep);
        minSep = Math.min(minSep, sep);
        const point: Vec = [0, 0, 0];
        point[i] = u;
        point[j] = v;
        point[axis] = ht;
        for (let k = 0; k < 3; k++) centre[k]! += point[k]! / poly.length;
    }
    if (maxSep > tolerance) return null;
    return { area, maxSeparation: maxSep, minSeparation: minSep, centre };
}

function mainAxis(n: Vec): number {
    const ax = Math.abs(n[0]);
    const ay = Math.abs(n[1]);
    const az = Math.abs(n[2]);
    return ax >= ay && ax >= az ? 0 : ay >= az ? 1 : 2;
}

export interface FightOptions {
    tolerance?: number; // inches
    view?: number; // inches
    near?: number;
    bits?: number;
    minArea?: number; // square inches per pair of meshes
}

// A pair of meshes that overlap in one plane.
export interface Fight {
    a: { sector: number; mesh: number; material: number };
    b: { sector: number; mesh: number; material: number };
    area: number; // square inches
    seenArea: number; // ... of it fighting within `view` (separation under fightSeparation(view))
    maxSeparation: number;
    minSeparation: number;
    fightsFrom: number; // inches: the camera distance from which the closest part fights
    centre: Vec; // of the largest overlapping piece
    normal: Vec; // a's there
    opposite: boolean; // facing opposite ways (and one of them two-sided)
    biased: boolean; // their materials' z biases differ: the bias decides (not a fight)
    triangles: number; // overlapping triangle pairs
    identicalArea: number; // ... of the area: triangles with the same corners (equal depths: with LESSEQUAL the one
    // drawn later wins - little or no shimmer; two see-through copies blend twice. Strips that order the corners
    // differently can still round apart)
}

// Whether two triangles have the same corners in the same turn (a rotation of each other), within `eps` inches: the
// rasteriser then works out the same depths for both. (Another corner order can round differently.)
export function sameCorners(t: Tri, o: Tri, eps = 0.01): boolean {
    const same = (p: Vec, q: Vec): boolean =>
        Math.abs(p[0] - q[0]) <= eps && Math.abs(p[1] - q[1]) <= eps && Math.abs(p[2] - q[2]) <= eps;
    for (let k = 0; k < 3; k++) {
        if (same(t.p[0], o.p[k]!) && same(t.p[1], o.p[(k + 1) % 3]!) && same(t.p[2], o.p[(k + 2) % 3]!)) return true;
    }
    return false;
}

// Every pair of meshes of different sectors or materials whose triangles overlap within `tolerance`, largest first.
// (Pairs within one mesh aren't looked at.)
export function findZFights(
    scene: thug2Scene.Scene,
    options: FightOptions = {},
    tris = sceneTriangles(scene),
): Fight[] {
    const tolerance = options.tolerance ?? DEFAULT_TOLERANCE;
    const view = options.view ?? DEFAULT_VIEW * INCHES_PER_METRE;
    const seenSep = fightSeparation(view, options.near ?? NEAR_PLANE, options.bits ?? DEPTH_BITS);
    const minArea = options.minArea ?? DEFAULT_MIN_AREA * INCHES_PER_METRE * INCHES_PER_METRE;
    const materials = scene.materials;
    const twoSided = (m: number): boolean => m >= 0 && Boolean(materials[m]!.noBackfaceCulling);
    // a grid per main axis; a triangle goes into each axis' grid whose normal component is big enough (a diagonal
    // wall's pieces can have different main axes), a pair is looked at in its first triangle's main axis' grid only
    const grids = [new Map<number, number[]>(), new Map<number, number[]>(), new Map<number, number[]>()];
    const cellKey = (u: number, v: number): number => (u + 32768) * 65536 + (v + 32768);
    tris.forEach((t, index) => {
        for (let axis = 0; axis < 3; axis++) {
            if (Math.abs(t.n[axis]!) < 0.55 && axis !== mainAxis(t.n)) continue;
            const [i, j] = axis === 0 ? [1, 2] : axis === 1 ? [0, 2] : [0, 1];
            const u0 = Math.floor(t.lo[i]! / CELL);
            const u1 = Math.floor(t.hi[i]! / CELL);
            const v0 = Math.floor(t.lo[j]! / CELL);
            const v1 = Math.floor(t.hi[j]! / CELL);
            for (let u = u0; u <= u1; u++) {
                for (let v = v0; v <= v1; v++) {
                    const key = cellKey(u, v);
                    const list = grids[axis]!.get(key);
                    if (list) list.push(index);
                    else grids[axis]!.set(key, [index]);
                }
            }
        }
    });
    const pairs = new Map<string, Fight & { best: number }>();
    for (let axis = 0; axis < 3; axis++) {
        const [i, j] = axis === 0 ? [1, 2] : axis === 1 ? [0, 2] : [0, 1];
        for (const [key, list] of grids[axis]!) {
            const cu = Math.floor(key / 65536) - 32768;
            const cv = (key % 65536) - 32768;
            for (let x = 0; x < list.length; x++) {
                const t = tris[list[x]!]!;
                for (let y = x + 1; y < list.length; y++) {
                    const o = tris[list[y]!]!;
                    if (t.sector === o.sector && t.mesh === o.mesh) continue;
                    // (the pair's grid: its first triangle's main axis)
                    const first = list[x]! < list[y]! ? t : o;
                    if (mainAxis(first.n) !== axis) continue;
                    const cos = dot(t.n, o.n);
                    if (Math.abs(cos) < MIN_NORMAL_DOT) continue;
                    if (
                        t.lo[0] > o.hi[0] + tolerance ||
                        o.lo[0] > t.hi[0] + tolerance ||
                        t.lo[1] > o.hi[1] + tolerance ||
                        o.lo[1] > t.hi[1] + tolerance ||
                        t.lo[2] > o.hi[2] + tolerance ||
                        o.lo[2] > t.hi[2] + tolerance
                    )
                        continue;
                    // (looked at once: in the cell holding the low corner of where their boxes meet)
                    if (
                        Math.floor(Math.max(t.lo[i]!, o.lo[i]!) / CELL) !== cu ||
                        Math.floor(Math.max(t.lo[j]!, o.lo[j]!) / CELL) !== cv
                    )
                        continue;
                    // (quick: o's corners near t's plane)
                    let near = false;
                    for (const p of o.p) if (Math.abs(dot(t.n, p) - t.d) <= tolerance * 4 + 1e-3) near = true;
                    if (!near) continue;
                    const opposite = cos < 0;
                    if (opposite && !twoSided(t.material) && !twoSided(o.material)) continue; // (one is culled)
                    const ov = overlapOf(first, first === t ? o : t, tolerance);
                    if (ov === null) continue;
                    const [p, q] = t.sector < o.sector || (t.sector === o.sector && t.mesh < o.mesh) ? [t, o] : [o, t];
                    const pairKey = `${p.sector}:${p.mesh}|${q.sector}:${q.mesh}`;
                    let f = pairs.get(pairKey);
                    if (f === undefined) {
                        const ma = materials[p.material];
                        const mb = materials[q.material];
                        f = {
                            a: { sector: p.sector, mesh: p.mesh, material: p.material },
                            b: { sector: q.sector, mesh: q.mesh, material: q.material },
                            area: 0,
                            seenArea: 0,
                            maxSeparation: 0,
                            minSeparation: Number.POSITIVE_INFINITY,
                            fightsFrom: 0,
                            centre: ov.centre,
                            normal: [...p.n] as Vec,
                            opposite,
                            biased: ma !== undefined && mb !== undefined && ma.zBias !== mb.zBias,
                            triangles: 0,
                            identicalArea: 0,
                            best: 0,
                        };
                        pairs.set(pairKey, f);
                    }
                    f.area += ov.area;
                    if (ov.maxSeparation <= seenSep) f.seenArea += ov.area;
                    f.maxSeparation = Math.max(f.maxSeparation, ov.maxSeparation);
                    f.minSeparation = Math.min(f.minSeparation, ov.minSeparation);
                    f.triangles++;
                    if (!opposite && sameCorners(t, o)) f.identicalArea += ov.area;
                    if (ov.area > f.best) {
                        f.best = ov.area;
                        f.centre = ov.centre;
                        f.normal = [...p.n] as Vec;
                    }
                }
            }
        }
    }
    const near = options.near ?? NEAR_PLANE;
    const bits = options.bits ?? DEPTH_BITS;
    const out: Fight[] = [];
    for (const f of pairs.values()) {
        if (f.area < minArea) continue;
        const { best: _best, ...fight } = f;
        fight.fightsFrom = fightDistance(f.minSeparation, near, bits);
        out.push(fight);
    }
    out.sort((x, y) => y.seenArea - x.seenArea || y.area - x.area);
    return out;
}

// A spot for tools/game/try_spot.ts looking at a fight: on ground (an up-facing plane) its centre; at a wall or a
// ceiling 3 m out from it, facing it (heading: degrees, 0 = +z, 90 = +x).
export function spotFor(centre: readonly number[], normal: readonly number[]): [number, number, number, number] {
    if (normal[1]! > 0.7) return [centre[0]!, centre[1]!, centre[2]!, 0];
    const h = Math.hypot(normal[0]!, normal[2]!) || 1;
    const nx = normal[0]! / h;
    const nz = normal[2]! / h;
    const heading = (Math.atan2(-nx, -nz) * 180) / Math.PI;
    return [centre[0]! + nx * 120, centre[1]!, centre[2]! + nz * 120, (heading + 360) % 360];
}

export const fmtSpot = (s: readonly number[]): string =>
    `${s[0]!.toFixed(0)},${s[1]!.toFixed(0)},${s[2]!.toFixed(0)},${s[3]!.toFixed(0)}`;

// --- report ---------------------------------------------------------------------------------------------------------

export interface Labels {
    sector?: (checksum: number) => string; // a sector's name
    material?: (checksum: number) => string; // a material's description
    cause?: (f: Fight, scene: thug2Scene.Scene) => string; // why the pair overlaps
}

const hex = (x: number): string => x.toString(16).toUpperCase().padStart(8, "0");
const m2 = (sqIn: number): number => sqIn / (INCHES_PER_METRE * INCHES_PER_METRE);

// The level's fights grouped by cause (Labels.cause, else genericCause) and the top `top` pairs.
export function report(
    level: string,
    scene: thug2Scene.Scene,
    fights: readonly Fight[],
    labels: Labels = {},
    top = 25,
): string[] {
    const lines: string[] = [];
    const cause = (f: Fight): string => causeOf(f, scene, labels);
    const byCause = new Map<string, { pairs: number; area: number; seen: number }>();
    for (const f of fights) {
        const k = cause(f);
        const c = byCause.get(k) ?? { pairs: 0, area: 0, seen: 0 };
        c.pairs++;
        c.area += f.area;
        c.seen += f.seenArea;
        byCause.set(k, c);
    }
    const total = fights.reduce((a, f) => a + f.area, 0);
    const seen = fights.reduce((a, f) => a + f.seenArea, 0);
    lines.push(
        `${level}: ${fights.length} overlapping mesh pairs, ${m2(total).toFixed(0)} m2 (${m2(seen).toFixed(0)} m2 fighting within the view distance)`,
    );
    for (const [k, c] of [...byCause].sort((x, y) => y[1].seen - x[1].seen || y[1].area - x[1].area)) {
        lines.push(
            `  ${k.padEnd(48)} ${String(c.pairs).padStart(6)} pairs ${m2(c.area).toFixed(0).padStart(8)} m2 (${m2(c.seen).toFixed(0)} m2 seen)`,
        );
    }
    const sectorName = (s: number): string => {
        const c = scene.sectors[s]!.checksum;
        return labels.sector?.(c) ?? hex(c);
    };
    const materialName = (m: number): string => {
        const mat = scene.materials[m];
        if (!mat) return "?";
        return labels.material?.(mat.checksum) ?? `${hex(mat.checksum)} tex ${hex(mat.passes[0]?.texture ?? 0)}`;
    };
    lines.push(`  top ${Math.min(top, fights.length)} (seen area first):`);
    for (const f of fights.slice(0, top)) {
        const spot = spotFor(f.centre, f.normal);
        lines.push(
            `  ${m2(f.seenArea).toFixed(1).padStart(7)} / ${m2(f.area).toFixed(1).padStart(7)} m2  gap ${f.minSeparation.toFixed(3)}-${f.maxSeparation.toFixed(3)} in` +
                ` (fights from ${(f.fightsFrom / INCHES_PER_METRE).toFixed(0)} m)  ${cause(f)}`,
        );
        lines.push(`      ${sectorName(f.a.sector)} [${materialName(f.a.material)}]`);
        lines.push(`      ${sectorName(f.b.sector)} [${materialName(f.b.material)}]`);
        lines.push(
            `      try_spot ${level} ${fmtSpot(spot)}${f.identicalArea ? `  (${((100 * f.identicalArea) / f.area).toFixed(0)}% of it the same triangles)` : ""}`,
        );
    }
    return lines;
}

// A pair's cause: decided by a z bias, opposite-facing, the same triangles (no shimmer), else Labels.cause /
// genericCause.
export const IDENTICAL_SHARE = 0.9;
export function causeOf(f: Fight, scene: thug2Scene.Scene, labels: Labels = {}): string {
    if (f.biased) return "decided by a z bias";
    if (f.opposite) return "opposite-facing, two-sided";
    const why = (labels.cause ?? genericCause)(f, scene);
    return f.identicalArea >= IDENTICAL_SHARE * f.area
        ? `${why} - same triangles (equal depths: little or no shimmer)`
        : why;
}

// Why two meshes overlap, from the scene alone.
export function genericCause(f: Fight, scene: thug2Scene.Scene): string {
    const ma = scene.materials[f.a.material];
    const mb = scene.materials[f.b.material];
    const layered = (m: thug2Scene.Material | undefined): boolean =>
        m !== undefined && Boolean((m.passes[0]?.flags ?? 0) & (thug2Scene.PASS_TRANSPARENT | thug2Scene.PASS_DECAL));
    const decal = layered(ma) || layered(mb);
    if (f.a.sector === f.b.sector) return decal ? "layer in one object, no z bias" : "two materials of one object";
    const sameTexture = ma !== undefined && mb !== undefined && ma.passes[0]?.texture === mb.passes[0]?.texture;
    if (decal) return "decal / layer over another object, no z bias";
    return sameTexture ? "duplicate surface (same texture)" : "overlapping objects";
}

// --- command line ---------------------------------------------------------------------------------------------------

export function checkLevel(folder: string, level: string, options: FightOptions, top: number): string[] {
    const file = path.join(folder, level, `${level}.scn.xbx`);
    const scene = thug2Scene.readScene(fs.readFileSync(file), level);
    return report(level, scene, findZFights(scene, options), {}, top);
}

export function csvRows(
    level: string,
    scene: thug2Scene.Scene,
    fights: readonly Fight[],
    labels: Labels = {},
): string[] {
    return fights.map((f) => {
        const spot = spotFor(f.centre, f.normal);
        const s = (k: number): string => labels.sector?.(scene.sectors[k]!.checksum) ?? hex(scene.sectors[k]!.checksum);
        const m = (k: number): string => hex(scene.materials[k]?.checksum ?? 0);
        const cause = causeOf(f, scene, labels);
        return [
            level,
            s(f.a.sector),
            m(f.a.material),
            s(f.b.sector),
            m(f.b.material),
            m2(f.area).toFixed(2),
            m2(f.seenArea).toFixed(2),
            f.minSeparation.toFixed(4),
            f.maxSeparation.toFixed(4),
            (f.fightsFrom / INCHES_PER_METRE).toFixed(0),
            cause,
            fmtSpot(spot),
        ]
            .map((x) => (/[",]/.test(x) ? `"${x.replace(/"/g, '""')}"` : x))
            .join(",");
    });
}

export const CSV_HEADER =
    "level,sector_a,material_a,sector_b,material_b,area_m2,seen_m2,gap_min_in,gap_max_in,fights_from_m,cause,spot";

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const { values, positionals } = parseArgs({
        args: [...argv],
        allowPositionals: true,
        options: {
            top: { type: "string", default: "25" },
            tolerance: { type: "string" },
            view: { type: "string" },
            near: { type: "string" },
            "min-area": { type: "string" },
            csv: { type: "string" },
        },
    });
    const [folder, ...levels] = positionals;
    if (!folder) {
        print(
            "usage: node tools/level_port/zfight.ts <levels folder> [<level> ...] [--top N] [--tolerance IN] [--view M] [--near IN] [--min-area M2] [--csv FILE]",
        );
        return 2;
    }
    const options = optionsFrom(values);
    const names = levels.length
        ? levels
        : fs.readdirSync(folder).filter((d) => fs.existsSync(path.join(folder, d, `${d}.scn.xbx`)));
    const csv: string[] = [CSV_HEADER];
    for (const level of names) {
        const scene = thug2Scene.readScene(fs.readFileSync(path.join(folder, level, `${level}.scn.xbx`)), level);
        const fights = findZFights(scene, options);
        print(report(level, scene, fights, {}, Number(values.top)).join("\n"));
        csv.push(...csvRows(level, scene, fights));
    }
    if (values.csv) fs.writeFileSync(values.csv, `${csv.join("\n")}\n`);
    return 0;
}

export function optionsFrom(values: {
    tolerance?: string | undefined;
    view?: string | undefined;
    near?: string | undefined;
    "min-area"?: string | undefined;
}): FightOptions {
    const options: FightOptions = {};
    if (values.tolerance !== undefined) options.tolerance = Number(values.tolerance);
    if (values.view !== undefined) options.view = Number(values.view) * INCHES_PER_METRE;
    if (values.near !== undefined) options.near = Number(values.near);
    if (values["min-area"] !== undefined)
        options.minArea = Number(values["min-area"]) * INCHES_PER_METRE * INCHES_PER_METRE;
    return options;
}

if (isMain(import.meta.url)) await runMain(() => main());
