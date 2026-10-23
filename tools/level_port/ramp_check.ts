// NOT ORIGINAL - how a converted level's ramps ride in THUG2, from its .col.xbx (tools/level_port/ramp_collision.ts
// has THUG2's rules).
//
//   node tools/level_port/ramp_check.ts <col.xbx | level folder | levels folder>...
//       per level: faces, concave ramp edges (both faces facing up) by bend in 10-degree steps, those over THUG2's
//       Ground_stick_angle (30), and what ramp_collision would still change - steep faces left walls inside a ramp
//       ("ramp walls") and faces lying on a more rideable one with other flags ("overlays"). 0 / 0 after the fix.
//   node tools/level_port/ramp_check.ts --spot <col.xbx> <x,y,z> [radius]
//       the collision faces near a point (THUG2 inches, a screenshot sidecar's position): object, face, slope, height,
//       normal, flags; the bends between joined ones.

import * as fs from "node:fs";
import * as path from "node:path";
import { type RampTri, rampFlags, rideRank } from "./ramp_collision.ts";
import * as col from "./thug2_col.ts";

type Vec = [number, number, number];
const sub = (a: Vec, b: Vec): Vec => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a: Vec, b: Vec): Vec => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
];
const dot = (a: Vec, b: Vec): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const unit = (a: Vec): Vec => {
    const l = Math.hypot(...a);
    return l ? [a[0] / l, a[1] / l, a[2] / l] : [0, 0, 0];
};
const degrees = (cos: number): number => (Math.acos(Math.max(-1, Math.min(1, cos))) * 180) / Math.PI;

interface Tri extends RampTri {
    object: number;
    face: number;
    n: Vec;
}

function trisOf(file: string): Tri[] {
    const c = col.readCol(fs.readFileSync(file), file);
    const out: Tri[] = [];
    c.objects.forEach((o, oi) => {
        o.faces.forEach(([flags, , a, b, cc], fi) => {
            const p = [o.verts[a]!, o.verts[b]!, o.verts[cc]!] as [Vec, Vec, Vec];
            out.push({ p, flags, object: oi, face: fi, n: unit(cross(sub(p[1], p[0]), sub(p[2], p[0]))) });
        });
    });
    return out;
}

const IGNORED = col.FACE_NON_COLLIDABLE | col.FACE_TRIGGER;

export interface RampStats {
    faces: number;
    concave: number;
    over30: number;
    over30Feet: number;
    buckets: number[];
    rampWalls: number;
    overlays: number;
}

export function rampStats(tris: readonly Tri[]): RampStats {
    const key = (p: Vec): string => p.map((x) => Math.round(x * 2)).join(",");
    const live = tris.filter((t) => !(t.flags & IGNORED));
    const edges = new Map<string, number[]>();
    live.forEach((t, i) => {
        for (let k = 0; k < 3; k++) {
            const a = key(t.p[k]!);
            const b = key(t.p[(k + 1) % 3]!);
            const e = a < b ? `${a}|${b}` : `${b}|${a}`;
            const list = edges.get(e) ?? [];
            list.push(i);
            edges.set(e, list);
        }
    });
    const stats: RampStats = {
        faces: live.length,
        concave: 0,
        over30: 0,
        over30Feet: 0,
        buckets: Array(9).fill(0),
        rampWalls: 0,
        overlays: 0,
    };
    for (const [e, list] of edges) {
        if (list.length !== 2) continue;
        const f = live[list[0]!]!;
        const g = live[list[1]!]!;
        if (f.n[1] < 0.1 || g.n[1] < 0.1) continue;
        // (only joins THUG2 rides across: both faces ground or vert)
        if (rideRank(f.flags, f.n[1]) < 2 || rideRank(g.flags, g.n[1]) < 2) continue;
        const angle = degrees(dot(f.n, g.n));
        if (angle < 1) continue;
        const ends = e.split("|");
        const far = g.p.find((p) => !ends.includes(key(p)));
        if (!far || dot(f.n, sub(far, f.p[0])) <= 0.01) continue;
        stats.concave++;
        stats.buckets[Math.min(8, Math.floor(angle / 10))]!++;
        if (angle > 30) {
            stats.over30++;
            const [a, b] = ends.map((s) => s.split(",").map((x) => Number(x) / 2) as Vec);
            stats.over30Feet += Math.hypot(...sub(a!, b!)) / 12;
        }
    }
    const fixed = rampFlags(tris);
    stats.rampWalls = fixed.promoted.length;
    stats.overlays = fixed.overlays.length;
    return stats;
}

function colFiles(arg: string): string[] {
    if (arg.endsWith(".col.xbx")) return [arg];
    const out: string[] = [];
    for (const entry of fs.readdirSync(arg, { withFileTypes: true })) {
        const full = path.join(arg, entry.name);
        if (entry.isDirectory()) out.push(...colFiles(full));
        else if (entry.name.endsWith(".col.xbx")) out.push(full);
    }
    return out.sort();
}

function flagText(flags: number): string {
    const names: [number, string][] = [
        [col.FACE_SKATABLE, "SKATABLE"],
        [col.FACE_NOT_SKATABLE, "NOT_SKATABLE"],
        [col.FACE_WALL_RIDABLE, "WALL_RIDABLE"],
        [col.FACE_VERT, "VERT"],
        [col.FACE_NON_COLLIDABLE, "NON_COLLIDABLE"],
        [col.FACE_TRIGGER, "TRIGGER"],
        [col.FACE_CAMERA_PASSES, "CAMERA_PASSES"],
    ];
    return (
        names
            .filter(([bit]) => flags & bit)
            .map(([, name]) => name)
            .join("|") || "0"
    );
}

function spot(file: string, at: string, radius: number): void {
    const p0 = at.split(",").map(Number) as Vec;
    const tris = trisOf(file).filter((t) => {
        if (t.flags & IGNORED) return false;
        const c = [0, 1, 2].map((k) => (t.p[0][k]! + t.p[1][k]! + t.p[2][k]!) / 3) as Vec;
        return Math.hypot(...sub(c, p0)) <= radius;
    });
    tris.sort((a, b) => Math.min(...a.p.map((q) => q[1])) - Math.min(...b.p.map((q) => q[1])));
    for (const t of tris) {
        const ys = t.p.map((q) => q[1]);
        console.log(
            `obj ${t.object} face ${t.face}: slope ${degrees(t.n[1]).toFixed(1)} y ${Math.min(...ys).toFixed(1)}..${Math.max(...ys).toFixed(1)} n ${t.n.map((x) => x.toFixed(2)).join(",")} ${flagText(t.flags)}`,
        );
    }
    const key = (p: Vec): string => p.map((x) => Math.round(x * 2)).join(",");
    const edges = new Map<string, Tri[]>();
    for (const t of tris)
        for (let i = 0; i < 3; i++) {
            const a = key(t.p[i]!);
            const b = key(t.p[(i + 1) % 3]!);
            const e = a < b ? `${a}|${b}` : `${b}|${a}`;
            edges.set(e, [...(edges.get(e) ?? []), t]);
        }
    console.log("bends between joined faces (> 1 degree):");
    for (const list of edges.values()) {
        if (list.length !== 2) continue;
        const [f, g] = list as [Tri, Tri];
        const a = degrees(dot(f.n, g.n));
        if (a >= 1) console.log(`  ${f.object}:${f.face} - ${g.object}:${g.face}: ${a.toFixed(1)}`);
    }
}

function main(argv: string[]): number {
    if (argv[0] === "--spot") {
        if (argv.length < 3) return usage();
        spot(argv[1]!, argv[2]!, Number(argv[3] ?? 200));
        return 0;
    }
    if (argv[0] === "--list") {
        // what ramp_collision would change, face by face
        for (const file of argv.slice(1).flatMap(colFiles)) {
            const tris = trisOf(file);
            const fixed = rampFlags(tris);
            for (const [what, list] of [
                ["ramp", fixed.promoted],
                ["overlay", fixed.overlays],
            ] as const)
                for (const i of list) {
                    const t = tris[i]!;
                    const c = [0, 1, 2].map((k) => ((t.p[0][k]! + t.p[1][k]! + t.p[2][k]!) / 3).toFixed(0));
                    console.log(
                        `${path.basename(file)} ${what} obj ${t.object} face ${t.face} at ${c.join(",")} slope ${degrees(t.n[1]).toFixed(1)}: ${flagText(t.flags)} -> ${flagText(fixed.flags[i]!)}`,
                    );
                }
        }
        return 0;
    }
    if (!argv.length) return usage();
    for (const file of argv.flatMap(colFiles)) {
        const s = rampStats(trisOf(file));
        const share = ((s.over30 / Math.max(1, s.concave)) * 100).toFixed(1);
        console.log(
            `${path.basename(file)}: ${s.faces} faces, concave ramp edges ${s.concave}, >30 deg ${s.over30} (${share}%, ${Math.round(s.over30Feet)} ft); ramp walls ${s.rampWalls}, overlays ${s.overlays}`,
        );
        console.log(`   ${s.buckets.map((n, b) => `${b * 10}-${b * 10 + 10}:${n}`).join(" ")}`);
    }
    return 0;
}

function usage(): number {
    console.error("usage: ramp_check.ts <col.xbx | folder>... | --spot <col.xbx> <x,y,z> [radius]");
    return 2;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename))
    process.exit(main(process.argv.slice(2)));
