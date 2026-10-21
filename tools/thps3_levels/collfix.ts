// NOT ORIGINAL - THUGPRO's invisible collision overlays in the THPS3 ports ("collfix": Suburbia's `*_collfix`,
// Foundry's `*_coll` - TPN-THPS3-1 / -2, tools/compat/thugpro/README.md; docs/thugpro-hacks-review.json
// "collfix-overlay-collision"), what the converter's --no-collfix leaves out, and an offline comparison of each
// overlay with our own converted collision at the same place.
//
//   node tools/thps3_levels/collfix.ts [--game <folder>] [--json]
//
// --game: a THUG2 folder with the converted THPS3 levels (default C:\Games\THUG2 - read only; its Game folder for a
// stock install). Each overlay's faces against every other collision object's faces near it:
//   same       - an original face on the same plane (within 0.5 units, normals within 10 degrees) under it: a
//                duplicate, nothing to fix there;
//   offset     - an original face facing the same way, 0.5 to 8 units behind or in front (the overlay is a skin over
//                it: a lip, a bump, a smoothed transition);
//   back only  - only original faces facing the other way there (the overlay is the missing front face);
//   none       - no original face there (the overlay closes a gap or is an invisible wall).
// --json: the spots as tools/game/spots.json's `collfix` list (try_spot.ts --list collfix reads that file).
//
// A standing spot in front of each overlay: from the largest face that faces sideways (a ramp's face, a wall), out
// along its level normal until there's skatable ground below it (the overlay and the level's faces between are
// checked), the skater facing back at it. Picked offline: the game snaps the skater to the ground below the point.

import * as fs from "node:fs";
import * as path from "node:path";
import * as compat from "../compat/compat.ts";
import * as qb from "../level_port/qb.ts";
import { QbFile } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { parseCli } from "../lib/cli.ts";
import { findInstall } from "../lib/install.ts";
import { isMain, print, runMain } from "../lib/py.ts";

// --- what --no-collfix leaves out (pure, tested) --------------------------------------------------------------------

// THUGPRO's overlay objects are named after what they patch: `<name>_collfix` (Suburbia) or `<name>_coll` (Foundry).
export function isCollfix(name: string): boolean {
    return /_coll(fix)?$/i.test(name);
}

// The TPN-THPS3 entries without the overlays' collision objects and geometry nodes.
export function withoutCollfix<T extends compat.Entry>(entries: readonly T[]): T[] {
    return entries.filter((e) => !isCollfix(String(e.target?.name ?? "")));
}

// The THPS3 nodes an overlay replaces (THUGPRO switched them off - TP-14's flags - and laid `<name>_collfix` there
// instead): the overlays' names without the suffix that are nodes of the level's TP-14 `flags`. --no-collfix leaves
// their flag edits out, so THPS3's own node is there again (Suburbia's Inviso_PicnicCollide01).
export function replacedNodes(entries: readonly compat.Entry[], flagged: Iterable<string>): string[] {
    const bases = new Set(
        entries
            .map((e) => String(e.target?.name ?? ""))
            .filter(isCollfix)
            .map((n) => n.replace(/_coll(fix)?$/i, "").toLowerCase()),
    );
    return [...flagged].filter((n) => bases.has(n.toLowerCase()));
}

// --- geometry -------------------------------------------------------------------------------------------------------

type V3 = [number, number, number];
const sub = (a: V3, b: V3): V3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const add = (a: V3, b: V3): V3 => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const mul = (a: V3, s: number): V3 => [a[0] * s, a[1] * s, a[2] * s];
const dot = (a: V3, b: V3): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a: V3, b: V3): V3 => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const len = (a: V3): number => Math.sqrt(dot(a, a));

export interface Tri {
    object: number; // the collision object's checksum
    flags: number;
    a: V3;
    b: V3;
    c: V3;
    n: V3; // unit normal (b - a) x (c - a) - THUG2's winding
    area: number;
    centre: V3;
}

export function tri(object: number, flags: number, a: V3, b: V3, c: V3): Tri {
    const x = cross(sub(b, a), sub(c, a));
    const l = len(x);
    return {
        object,
        flags,
        a,
        b,
        c,
        n: l > 0 ? mul(x, 1 / l) : [0, 0, 0],
        area: l / 2,
        centre: mul(add(add(a, b), c), 1 / 3),
    };
}

// Whether `p` (on or near t's plane) falls inside t, grown by `margin` units.
export function inside(t: Tri, p: V3, margin: number): boolean {
    for (const [u, v] of [
        [t.a, t.b],
        [t.b, t.c],
        [t.c, t.a],
    ] as [V3, V3][]) {
        const edge = sub(v, u);
        const out = cross(edge, t.n); // (points out of the triangle for THUG2's winding)
        const l = len(out);
        if (l > 0 && dot(sub(p, u), out) / l > margin) return false;
    }
    return true;
}

// The distance along the ray from `o` in direction `d` (unit) to t, or null (Moller-Trumbore, both sides).
export function hit(t: Tri, o: V3, d: V3): number | null {
    const e1 = sub(t.b, t.a);
    const e2 = sub(t.c, t.a);
    const p = cross(d, e2);
    const det = dot(e1, p);
    if (Math.abs(det) < 1e-9) return null;
    const s = sub(o, t.a);
    const u = dot(s, p) / det;
    if (u < 0 || u > 1) return null;
    const q = cross(s, e1);
    const v = dot(d, q) / det;
    if (v < 0 || u + v > 1) return null;
    const dist = dot(e2, q) / det;
    return dist >= 0 ? dist : null;
}

export type Cover = "same" | "offset" | "back only" | "none";

// What of the level's own faces is under an overlay face (its centre and its corners pulled 10% in, the best of them).
export function cover(face: Tri, level: readonly Tri[]): { kind: Cover; offset: number; objects: Set<number> } {
    const objects = new Set<number>();
    let best: Cover = "none";
    let offset = Number.POSITIVE_INFINITY;
    const rank: Record<Cover, number> = { none: 0, "back only": 1, offset: 2, same: 3 };
    const points = [face.centre, ...[face.a, face.b, face.c].map((v) => add(mul(v, 0.9), mul(face.centre, 0.1)))];
    for (const t of level) {
        const cos = dot(face.n, t.n);
        if (Math.abs(cos) < Math.cos((10 * Math.PI) / 180)) continue;
        for (const p of points) {
            const distance = dot(sub(p, t.a), t.n);
            if (Math.abs(distance) > 8) continue;
            if (!inside(t, sub(p, mul(t.n, distance)), 1)) continue;
            const kind: Cover = cos < 0 ? "back only" : Math.abs(distance) <= 0.5 ? "same" : "offset";
            objects.add(t.object);
            if (rank[kind] > rank[best]) best = kind;
            if (kind !== "back only") offset = Math.min(offset, Math.abs(distance));
        }
    }
    return { kind: best, offset: Number.isFinite(offset) ? offset : 0, objects };
}

// What a skater coming at an overlay face from its front meets first, at points spread over the face: rays from 48
// units in front of each point back along its normal through the level's collidable faces.
//   in front - the level's surface is met first, more than 0.5 units before the overlay (`depth`: how much);
//   same     - met within 0.5 units of the overlay;
//   enclosed - the overlay is met first, but the point is shut in by the level (enclosed: inside a thick wall, under
//              a deck) - reached only through the level's surface;
//   exposed  - the overlay is met first in the open (`behind`: the level's surface that far behind it along the
//              ray, or none within 200 units) - there the overlay changes what the skater touches.
export interface Exposure {
    inFront: number;
    same: number;
    enclosed: number;
    exposed: number;
    depth: number[];
    behind: (number | null)[];
}

export function samples(t: Tri): V3[] {
    const out: V3[] = [];
    const steps = 4;
    for (let i = 0; i <= steps; i++) {
        for (let j = 0; j <= steps - i; j++) {
            const u = (i + 0.5) / (steps + 1.5);
            const v = (j + 0.5) / (steps + 1.5);
            if (u + v >= 1) continue;
            out.push(add(add(mul(t.a, 1 - u - v), mul(t.b, u)), mul(t.c, v)));
        }
    }
    return out;
}

// Whether `p` is shut in by the level: a face within 1000 units along each of the six axis directions (inside a thick
// wall, under a deck - a skater gets there only through the level's surface).
export function enclosed(p: V3, level: readonly Tri[]): boolean {
    const directions: V3[] = [
        [1, 0, 0],
        [-1, 0, 0],
        [0, 1, 0],
        [0, -1, 0],
        [0, 0, 1],
        [0, 0, -1],
    ];
    return directions.every((d) =>
        level.some((t) => {
            const h = hit(t, p, d);
            return h !== null && h <= 1000;
        }),
    );
}

// `level`: the faces near the overlay; `wide`: those the enclosure test looks through (default `level`).
export function exposure(faces: readonly Tri[], level: readonly Tri[], wide: readonly Tri[] = level): Exposure {
    const e: Exposure = { inFront: 0, same: 0, enclosed: 0, exposed: 0, depth: [], behind: [] };
    const solid = level.filter((t) => !(t.flags & thug2Col.FACE_NON_COLLIDABLE));
    const wideSolid = wide.filter((t) => !(t.flags & thug2Col.FACE_NON_COLLIDABLE));
    for (const f of faces) {
        const back = mul(f.n, -1);
        for (const p of samples(f)) {
            const from = add(p, mul(f.n, 48));
            let first: number | null = null;
            for (const t of solid) {
                const d = hit(t, from, back);
                if (d !== null && d <= 248 && (first === null || d < first)) first = d;
            }
            if (first !== null && first < 47.5) {
                e.inFront += 1;
                e.depth.push(r1(48 - first));
            } else if (first !== null && first <= 48.5) {
                e.same += 1;
            } else if (enclosed(add(p, mul(f.n, 0.5)), wideSolid)) {
                e.enclosed += 1;
            } else {
                e.exposed += 1;
                e.behind.push(first === null ? null : r1(first - 48));
            }
        }
    }
    return e;
}

// --- the comparison -------------------------------------------------------------------------------------------------

export interface Spot {
    level: string;
    name: string; // the overlay
    at: [number, number, number, number]; // x, y, z, heading
    note: string;
}

interface Report {
    level: string;
    name: string;
    faces: number;
    flags: string;
    bbox: [V3, V3];
    counts: Record<Cover, number>;
    offsets: number[];
    under: string[];
    exposure: Exposure;
    spot: Spot | null;
}

function flagNames(flags: number): string {
    const names: [number, string][] = [
        [thug2Col.FACE_SKATABLE, "skatable"],
        [thug2Col.FACE_NOT_SKATABLE, "not skatable"],
        [thug2Col.FACE_WALL_RIDABLE, "wallridable"],
        [thug2Col.FACE_VERT, "vert"],
        [thug2Col.FACE_NON_COLLIDABLE, "non-collidable"],
        [thug2Col.FACE_TRIGGER, "trigger"],
        [thug2Col.FACE_CAMERA_PASSES, "camera passes"],
        [thug2Col.FACE_UNDER_OK, "under ok"],
        [thug2Col.FACE_INVISIBLE, "invisible"],
    ];
    const set = names.filter(([bit]) => flags & bit).map(([, n]) => n);
    return set.length ? set.join(" ") : "none";
}

const r1 = (v: number): number => Math.round(v * 10) / 10;

// The ground below `p`: the highest face below it that is skatable-ish (normal y >= 0.85) and collidable, or null.
function ground(p: V3, level: readonly Tri[]): number | null {
    let best: number | null = null;
    for (const t of level) {
        if (t.n[1] < 0.85 || t.flags & thug2Col.FACE_NON_COLLIDABLE) continue;
        const d = hit(t, p, [0, -1, 0]);
        if (d !== null && d < 600 && (best === null || p[1] - d > best)) best = p[1] - d;
    }
    return best;
}

// Whether a segment from `a` to `b` passes through a collidable face of `level` (not `skip`'s).
function blocked(a: V3, b: V3, level: readonly Tri[], skip: number): boolean {
    const d = sub(b, a);
    const l = len(d);
    const u = mul(d, 1 / l);
    for (const t of level) {
        if (t.object === skip || t.flags & thug2Col.FACE_NON_COLLIDABLE) continue;
        const h = hit(t, a, u);
        if (h !== null && h < l) return true;
    }
    return false;
}

function pickSpot(
    level: string,
    name: string,
    faces: readonly Tri[],
    all: readonly Tri[],
    checksum: number,
): Spot | null {
    const bottom = Math.min(...faces.flatMap((f) => [f.a[1], f.b[1], f.c[1]]));
    // sideways faces, largest first (both sides: a thin overlay's back may be the side one meets)
    const sideways = faces.filter((f) => Math.abs(f.n[1]) < 0.9).sort((x, y) => y.area - x.area);
    const tries = sideways.length ? sideways : [...faces].sort((x, y) => y.area - x.area);
    for (const face of tries.slice(0, 6)) {
        for (const sign of [1, -1]) {
            const flat: V3 = [face.n[0] * sign, 0, face.n[2] * sign];
            const fl = len(flat);
            if (fl < 1e-3) continue;
            const h = mul(flat, 1 / fl);
            for (const distance of [300, 240, 360, 180, 120]) {
                const p = add(face.centre, mul(h, distance));
                const top: V3 = [p[0], Math.max(face.centre[1], p[1]) + 120, p[2]];
                const g = ground(top, all);
                // (on the floor below the overlay - not on a deck above it, not a level far below)
                if (g === null || g > face.centre[1] + 30 || g < Math.min(bottom, face.centre[1]) - 200) continue;
                const stand: V3 = [p[0], g + 2, p[2]];
                // (a clear run-up: nothing of the level for the first half of the way, at knee height - the rest is
                // the level's own ramp or wall in front of the overlay, what the skater meets)
                const knee: V3 = [stand[0], g + 30, stand[2]];
                if (blocked(knee, add(knee, mul(h, -distance / 2)), all, checksum)) continue;
                const heading = (Math.atan2(-h[0], -h[2]) * 180) / Math.PI;
                return {
                    level,
                    name,
                    at: [r1(stand[0]), r1(stand[1]), r1(stand[2]), Math.round(heading)],
                    note: `${distance} units out from its largest ${face.n[1] > 0.3 ? "ramp" : "wall"} face, facing it`,
                };
            }
        }
    }
    return null;
}

export function compare(level: string, col: thug2Col.ColFile, names: ReadonlyMap<number, string>): Report[] {
    const all: Tri[] = [];
    const byObject = new Map<number, Tri[]>();
    for (const o of col.objects) {
        const list: Tri[] = [];
        for (const [flags, , a, b, c] of o.faces) {
            const t = tri(o.checksum, flags, o.verts[a] as V3, o.verts[b] as V3, o.verts[c] as V3);
            if (t.area > 0) list.push(t);
        }
        byObject.set(o.checksum, list);
        all.push(...list);
    }
    const overlays = col.objects.filter((o) => isCollfix(names.get(o.checksum) ?? ""));
    const overlaySet = new Set(overlays.map((o) => o.checksum));
    const reports: Report[] = [];
    for (const o of overlays) {
        const name = names.get(o.checksum)!;
        const faces = byObject.get(o.checksum)!;
        const lo: V3 = [0, 1, 2].map((k) => Math.min(...faces.flatMap((f) => [f.a[k]!, f.b[k]!, f.c[k]!]))) as V3;
        const hi: V3 = [0, 1, 2].map((k) => Math.max(...faces.flatMap((f) => [f.a[k]!, f.b[k]!, f.c[k]!]))) as V3;
        const near = all.filter(
            (t) =>
                !overlaySet.has(t.object) &&
                [0, 1, 2].every(
                    (k) =>
                        Math.max(t.a[k]!, t.b[k]!, t.c[k]!) >= lo[k]! - 16 &&
                        Math.min(t.a[k]!, t.b[k]!, t.c[k]!) <= hi[k]! + 16,
                ),
        );
        const around = all.filter(
            (t) =>
                !overlaySet.has(t.object) &&
                [0, 1, 2].every(
                    (k) =>
                        Math.max(t.a[k]!, t.b[k]!, t.c[k]!) >= lo[k]! - 64 &&
                        Math.min(t.a[k]!, t.b[k]!, t.c[k]!) <= hi[k]! + 64,
                ),
        );
        const wide = all.filter(
            (t) =>
                !overlaySet.has(t.object) &&
                [0, 1, 2].every(
                    (k) =>
                        Math.max(t.a[k]!, t.b[k]!, t.c[k]!) >= lo[k]! - 1000 &&
                        Math.min(t.a[k]!, t.b[k]!, t.c[k]!) <= hi[k]! + 1000,
                ),
        );
        const counts: Record<Cover, number> = { same: 0, offset: 0, "back only": 0, none: 0 };
        const offsets: number[] = [];
        const under = new Set<number>();
        let flags = 0;
        for (const f of faces) {
            flags |= f.flags;
            const c = cover(f, near);
            counts[c.kind] += 1;
            if (c.kind === "offset") offsets.push(r1(c.offset));
            for (const id of c.objects) under.add(id);
        }
        reports.push({
            level,
            name,
            faces: faces.length,
            flags: flagNames(flags),
            bbox: [lo.map(r1) as V3, hi.map(r1) as V3],
            counts,
            offsets,
            under: [...under].map((id) => names.get(id) ?? `0x${id.toString(16).padStart(8, "0")}`),
            exposure: exposure(faces, around, wide),
            spot: pickSpot(level, name, faces, all, o.checksum),
        });
    }
    return reports;
}

// The converted levels' collision and node names: <game>\thug2-decomp-thps3\levels\<level>\.
function load(game: string, level: string): { col: thug2Col.ColFile; names: Map<number, string> } {
    const dir = path.join(game, "thug2-decomp-thps3", "levels", level);
    const col = thug2Col.readCol(fs.readFileSync(path.join(dir, `${level}.col.xbx`)), `${level}.col.xbx`);
    const names = new Map(new QbFile(fs.readFileSync(path.join(dir, `${level}.qb`)), `${level}.qb`, true).names);
    // (the register's names too, in case the level's name table lacks one - a level converted with --no-collfix has
    // no overlay, and nothing is compared)
    for (const e of compat.thugproNew(compat.load("thugpro"), "thps3_levels")) {
        const n = String(e.target.name ?? "");
        if (n && !names.has(qb.checksum(n))) names.set(qb.checksum(n), n);
    }
    return { col, names };
}

const USAGE = `usage: collfix.ts [-h] [--game GAME] [--json]

Compare THUGPRO's collision overlays in the converted THPS3 levels with the level's own collision there.

options:
  -h, --help   show this help message and exit
  --game GAME  a THUG2 folder with the converted THPS3 levels (default C:\\Games\\THUG2, read only)
  --json       print the standing spots as tools/game/spots.json's "collfix" list`;

function main(): number {
    const { values } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2" },
            json: { type: "boolean", default: false },
            detail: { type: "string" },
            "no-faces": { type: "boolean", default: false },
        },
    });
    const game = findInstall(values.game, "thug2") ?? values.game;
    const reports: Report[] = [];
    for (const level of ["sub", "foun"]) {
        const { col, names } = load(game, level);
        reports.push(...compare(level, col, names));
    }
    if (values.detail) {
        // every overlay face and the level's faces within 16 units of the overlay's box, object by object
        for (const level of ["sub", "foun"]) {
            const { col, names } = load(game, level);
            const o = col.objects.find(
                (x) => (names.get(x.checksum) ?? "").toLowerCase() === values.detail!.toLowerCase(),
            );
            if (!o) continue;
            const fmt = (t: Tri): string =>
                `n(${t.n.map((v) => v.toFixed(2)).join(",")}) flags 0x${t.flags.toString(16)} ` +
                [t.a, t.b, t.c].map((v) => `(${v.map(r1).join(",")})`).join(" ");
            const tris = (x: thug2Col.ColObject): Tri[] =>
                x.faces.map(([f, , a, b, c]) =>
                    tri(x.checksum, f, x.verts[a] as V3, x.verts[b] as V3, x.verts[c] as V3),
                );
            const mine = tris(o);
            print(`${level} ${values.detail}:`);
            const others = col.objects.filter((x) => x !== o).flatMap(tris);
            for (const t of mine) {
                const e = exposure([t], others);
                print(
                    `  ${fmt(t)}  -> ${cover(t, others).kind}; level first ${e.inFront}, same ${e.same}, overlay first ${e.exposed}`,
                );
            }
            if (values["no-faces"]) continue;
            const lo = [0, 1, 2].map((k) => Math.min(...mine.flatMap((f) => [f.a[k]!, f.b[k]!, f.c[k]!])));
            const hi = [0, 1, 2].map((k) => Math.max(...mine.flatMap((f) => [f.a[k]!, f.b[k]!, f.c[k]!])));
            for (const x of col.objects) {
                if (x === o) continue;
                const near = tris(x).filter((t) =>
                    [0, 1, 2].every(
                        (k) =>
                            Math.max(t.a[k]!, t.b[k]!, t.c[k]!) >= lo[k]! - 16 &&
                            Math.min(t.a[k]!, t.b[k]!, t.c[k]!) <= hi[k]! + 16,
                    ),
                );
                if (!near.length) continue;
                print(`  -- ${names.get(x.checksum) ?? x.checksum.toString(16)}: ${near.length} faces near`);
                for (const t of near.slice(0, 80)) print(`     ${fmt(t)}`);
            }
        }
        return 0;
    }
    if (values.json) {
        print(
            JSON.stringify(
                reports.map((r) => r.spot).filter((s) => s !== null),
                null,
                2,
            ),
        );
        return 0;
    }
    for (const r of reports) {
        print(`${r.level} ${r.name}: ${r.faces} faces (${r.flags}), box ${JSON.stringify(r.bbox)}`);
        print(
            `  same plane ${r.counts.same}, offset ${r.counts.offset}${r.offsets.length ? ` (${[...new Set(r.offsets)].sort((a, b) => a - b).join(", ")} units)` : ""}, ` +
                `back faces only ${r.counts["back only"]}, nothing ${r.counts.none}`,
        );
        print(`  the level's objects under it: ${r.under.join(", ") || "none"}`);
        const e = r.exposure;
        const spread = (v: readonly (number | null)[]): string => {
            const n = v.filter((x): x is number => x !== null).sort((a, b) => a - b);
            const none = v.length - n.length;
            const missing = none ? `${none} with nothing within 200` : "";
            if (!n.length) return missing || "-";
            return `${n[0]}..${n[n.length - 1]}, median ${n[n.length >> 1]}${missing ? `; ${missing}` : ""}`;
        };
        print(
            `  met from the front: the level first ${e.inFront} (${spread(e.depth)}), same ${e.same}, ` +
                `the overlay first ${e.enclosed} shut in + ${e.exposed} in the open (the level behind it: ${spread(e.behind)})`,
        );
        print(`  spot: ${r.spot ? `${r.spot.at.join(",")} (${r.spot.note})` : "none found"}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
